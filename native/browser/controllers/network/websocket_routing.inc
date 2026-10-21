// SPDX-License-Identifier: MIT
// Exact root-target adapter for the versioned native Blink transport extension.
// No JavaScript socket replacement and no claim that stock CDP supports routing.
  struct NativeRoutedSocketEvent { Dict data; size_t bytes=0; double deadline=0; };
  std::deque<NativeRoutedSocketEvent> native_routed_events_;
  std::set<std::string> native_routed_sockets_;
  size_t native_routed_bytes_=0;
  uint64_t native_routed_epoch_=0;
  bool native_routed_active_=false,native_routed_configuring_=false;
  bool native_routed_enable_pending_=false,native_routed_cleanup_pending_=false;
  bool native_routed_cleanup_requested_=false,native_routed_cleanup_confirmed_=false;
  bool native_routed_retiring_=false;
  std::optional<std::pair<int,uint64_t>> native_routed_waiter_;
  std::string native_routed_failure_;
  void NativeWebSocketRouteClear() {
    native_routed_active_=false;
    native_routed_events_.clear();native_routed_sockets_.clear();native_routed_bytes_=0;
  }
  static bool NativeWebSocketRouteRevision(Dict result) {
    return result&&result->GetType("revision")==VTYPE_INT&&result->GetInt("revision")==1&&
      result->GetType("maxSockets")==VTYPE_INT&&result->GetInt("maxSockets")==128&&
      result->GetType("maxPendingMessages")==VTYPE_INT&&result->GetInt("maxPendingMessages")==64&&
      result->GetType("maxPendingBytes")==VTYPE_INT&&result->GetInt("maxPendingBytes")==2097152;
  }
  void NativeWebSocketRouteSettle(uint64_t epoch) {
    if(epoch!=native_routed_epoch_||native_routed_retiring_)return;
    if(!native_routed_enable_pending_&&!native_routed_cleanup_pending_&&native_routed_cleanup_requested_&&native_routed_cleanup_confirmed_)
      native_routed_configuring_=false;
  }
  void NativeWebSocketRouteCleanup(uint64_t epoch,std::function<void(bool)> done={}) {
    if(epoch!=native_routed_epoch_||native_routed_cleanup_requested_){if(done)done(false);return;}
    if(closing_page_){
      // OnBeforeClose is the actual channel-death receipt. Do not dispatch
      // replacement work while its moved callback registry is being drained.
      native_routed_retiring_=true;native_routed_configuring_=true;
      native_routed_cleanup_confirmed_=true;NativeWebSocketRouteClear();if(done)done(false);return;
    }
    native_routed_cleanup_requested_=true;native_routed_cleanup_pending_=true;native_routed_configuring_=true;
    NativeWebSocketRouteClear();NativeWebSocketRoutePump();
    auto params=CefDictionaryValue::Create();params->SetBool("enabled",false);
    // NativeNetworkSend owns the actual reply independently of any canceled
    // command. Its physical watchdog retains the call and retires the source
    // when the real reply fails to arrive; dispatch failure also calls back.
    NativeNetworkSend("","Network.suiteCutWebSocketConfigureV1",params,[this,epoch,done=std::move(done)](bool ok,const std::string&json){
      if(epoch!=native_routed_epoch_){if(done)done(false);return;}
      native_routed_cleanup_pending_=false;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      native_routed_cleanup_confirmed_=ok&&NativeWebSocketRouteRevision(result);
      if(!native_routed_cleanup_confirmed_){
        native_routed_retiring_=true;native_routed_configuring_=true;
        native_routed_failure_="Native WebSocket routing cleanup could not be confirmed; retiring the page";
        if(done)done(false);
        NativeWebSocketRoutePump();NetworkFatal(native_routed_failure_);return;
      }
      NativeWebSocketRouteSettle(epoch);if(done)done(true);
    },Now()+30000);
  }
  void NativeWebSocketRouteFail(const std::string& reason) {
    native_routed_failure_=reason;NativeWebSocketRouteClear();
    NativeWebSocketRouteCleanup(native_routed_epoch_);NativeWebSocketRoutePump();
  }
  void NativeWebSocketRoutePump() {
    if(!native_routed_events_.empty()&&Now()>=native_routed_events_.front().deadline){
      NativeWebSocketRouteFail("Native WebSocket route event original deadline exceeded");return;
    }
    if(!native_routed_waiter_)return;
    const auto [id,generation]=*native_routed_waiter_;
    if(!operations_.IsCurrent(id,generation)){native_routed_waiter_.reset();return;}
    if(operations_.IsExpired(id,generation,Now())){native_routed_waiter_.reset();Complete(id,false,"Native WebSocket route event wait Timeout exceeded");return;}
    if(!native_routed_failure_.empty()||!native_routed_active_){native_routed_waiter_.reset();Complete(id,false,native_routed_failure_.empty()?"Native WebSocket routing is disabled":native_routed_failure_);return;}
    if(native_routed_events_.empty())return;
    auto event=std::move(native_routed_events_.front());native_routed_events_.pop_front();
    native_routed_bytes_-=event.bytes;native_routed_waiter_.reset();
    Complete(id,true,"",Json(event.data));
  }
  void NativeWebSocketRouteEvent(Dict event,const std::string& session) {
    if(!session.empty()||native_routed_cleanup_requested_||native_routed_retiring_||(!native_routed_active_&&!native_routed_enable_pending_))return;
    if(!event||event->GetType("requestId")!=VTYPE_STRING||event->GetType("stage")!=VTYPE_STRING||
       event->GetType("url")!=VTYPE_STRING||event->GetType("messageId")!=VTYPE_STRING||
       event->GetType("outgoing")!=VTYPE_BOOL||event->GetType("opcode")!=VTYPE_INT||event->GetType("payloadBase64")!=VTYPE_STRING){NativeWebSocketRouteFail("Invalid native WebSocket route event");return;}
    const auto request=event->GetString("requestId").ToString(),stage=event->GetString("stage").ToString();
    const auto message=event->GetString("messageId").ToString(),payload=event->GetString("payloadBase64").ToString();
    const auto url=event->GetString("url").ToString();const auto opcode=event->GetInt("opcode");
    if(request.empty()||request.size()>128||url.size()>8192||message.size()>32||payload.size()>2796204||
       (stage!="created"&&stage!="message"&&stage!="closed")){
      NativeWebSocketRouteFail("Native WebSocket route event identity or payload exceeds limits");return;
    }
    if(stage=="created"){
      if(native_routed_sockets_.size()>=128||native_routed_sockets_.contains(request)||!message.empty()||!payload.empty()||opcode||
         !(url.starts_with("ws://")||url.starts_with("wss://"))){NativeWebSocketRouteFail("Invalid native WebSocket route creation");return;}
      native_routed_sockets_.insert(request);
    }else{
      if(!native_routed_sockets_.contains(request)){NativeWebSocketRouteFail("Native WebSocket route event has no owned socket");return;}
      if(stage=="message"){
        const auto decoded=payload.empty()?nullptr:CefBase64Decode(payload);
        std::vector<uint8_t> decoded_bytes(decoded?decoded->GetSize():0);
        if(decoded&&!decoded_bytes.empty())decoded->GetData(decoded_bytes.data(),decoded_bytes.size(),0);
        if(message.empty()||(opcode!=1&&opcode!=2)||(!payload.empty()&&(!decoded||decoded->GetSize()>2097152||CefBase64Encode(decoded_bytes.data(),decoded_bytes.size())!=payload))){NativeWebSocketRouteFail("Invalid native WebSocket route message");return;}
      }else{
        if(!message.empty()||!payload.empty()||opcode){NativeWebSocketRouteFail("Invalid native WebSocket route closure");return;}
        native_routed_sockets_.erase(request);
      }
    }
    const size_t bytes=payload.size()+request.size()+message.size()+url.size()+256;
    if(native_routed_events_.size()>=128||bytes>8*1048576-native_routed_bytes_){NativeWebSocketRouteFail("Native WebSocket route queue capacity exceeded");return;}
    auto copy=event->Copy(false);copy->SetString("routingToken",std::to_string(native_routed_epoch_));
    native_routed_bytes_+=bytes;native_routed_events_.push_back({copy,bytes,Now()+30000});
    NativeWebSocketRoutePump();WakeDeadlines();
  }
  void NativeWebSocketRouteCommand(int id,uint64_t generation,Dict message) {
    const auto action=message->GetString("action").ToString();
    // A frame facade cannot accidentally enable root-target policy.
    if(message->HasKey("frameId")||message->HasKey("frame")){Complete(id,false,"Native WebSocket routing currently requires the root page target");return;}
    if(action=="routeEnable"){
      if(NativeWebSocketPolicyRootOwned()){Complete(id,false,"Native WebSocket URL policy already owns this root target");return;}
      if(native_routed_active_||native_routed_configuring_||native_routed_enable_pending_||native_routed_cleanup_pending_||native_routed_retiring_){Complete(id,false,"Native WebSocket routing is already active, configuring or retiring");return;}
      if(native_routed_epoch_>=9007199254740991ULL){Complete(id,false,"Native WebSocket routing generation exhausted");return;}
      const auto epoch=++native_routed_epoch_;
      native_routed_configuring_=true;native_routed_enable_pending_=true;
      native_routed_cleanup_requested_=native_routed_cleanup_confirmed_=false;native_routed_failure_.clear();
      auto params=CefDictionaryValue::Create();params->SetBool("enabled",true);
      NativeNetworkSend("","Network.suiteCutWebSocketConfigureV1",params,[this,id,generation,epoch](bool ok,const std::string&json){
        if(epoch!=native_routed_epoch_)return;
        native_routed_enable_pending_=false;
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        const bool supported=ok&&NativeWebSocketRouteRevision(result);
        const bool live=operations_.IsCurrent(id,generation)&&!operations_.IsExpired(id,generation,Now())&&!closing_page_;
        const bool unsupported=!ok&&result&&result->GetType("code")==VTYPE_INT&&result->GetInt("code")==-32601;
        const bool not_dispatched=!ok&&(json=="Native network command capacity exceeded"||json=="Native network original command deadline exceeded"||json=="Native network command dispatch failed");
        if(!supported||!live||native_routed_cleanup_requested_||!native_routed_failure_.empty()){
          NativeWebSocketRouteClear();
          if((unsupported||not_dispatched)&&!native_routed_cleanup_requested_){native_routed_configuring_=false;}
          else NativeWebSocketRouteCleanup(epoch);
          NativeWebSocketRouteSettle(epoch);
          if(operations_.IsCurrent(id,generation))Complete(id,false,unsupported?"Native WebSocket routing requires the verified SuiteCut Network extension revision 1 in a custom CEF build":!native_routed_failure_.empty()?native_routed_failure_:"Native WebSocket routing activation failed or its original deadline exceeded");
          return;
        }
        native_routed_configuring_=false;native_routed_active_=true;
        auto receipt=CefDictionaryValue::Create();receipt->SetString("routingToken",std::to_string(epoch));receipt->SetInt("revision",1);
        Complete(id,true,"",Json(receipt));NativeWebSocketRoutePump();
      },Now()+30000);return;
    }
    if(message->GetType("routingToken")!=VTYPE_STRING||message->GetString("routingToken").ToString()!=std::to_string(native_routed_epoch_)||!native_routed_active_){Complete(id,false,"Native WebSocket routing token is stale or disabled");return;}
    if(action=="routeWait"){
      NativeWebSocketRoutePump();
      if(native_routed_waiter_){Complete(id,false,"Native WebSocket routing permits one pending event consumer");return;}
      native_routed_waiter_=std::make_pair(id,generation);NativeWebSocketRoutePump();return;
    }
    if(action=="routeDisable"){
      const auto epoch=native_routed_epoch_;
      NativeWebSocketRouteCleanup(epoch,[this,id,generation,epoch](bool ok){
        if(epoch!=native_routed_epoch_||!operations_.IsCurrent(id,generation))return;
        if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native WebSocket routing disable original command deadline exceeded");return;}
        Complete(id,ok,ok?"":"Native WebSocket routing disable could not be confirmed",ok?"{}":"");
      });return;
    }
    if(action!="routeCommand"){Complete(id,false,"Invalid native WebSocket routing action");return;}
    const auto request=message->GetString("requestId").ToString(),operation=message->GetString("operation").ToString();
    const auto pending=message->GetString("messageId").ToString(),payload=message->GetString("payloadBase64").ToString(),reason=message->GetString("reason").ToString();
    const int opcode=message->GetInt("opcode"),code=message->GetInt("code");
    if(!native_routed_sockets_.contains(request)||
       (operation!="continue"&&operation!="connect"&&operation!="mock"&&operation!="forward"&&operation!="replace"&&operation!="drop"&&operation!="client"&&operation!="server"&&operation!="close"&&operation!="abort")||
       pending.size()>32||payload.size()>2796204||reason.size()>123){Complete(id,false,"Invalid native WebSocket route command or ownership");return;}
    auto params=CefDictionaryValue::Create();params->SetString("requestId",request);params->SetString("action",operation);params->SetString("messageId",pending);params->SetString("payloadBase64",payload);params->SetInt("opcode",opcode);params->SetInt("code",code);params->SetString("reason",reason);
    const auto epoch=native_routed_epoch_;
    DevTools(id,"Network.suiteCutWebSocketCommandV1",params,[this,id,generation,epoch](bool ok,const std::string&json){
      if(epoch!=native_routed_epoch_){if(operations_.IsCurrent(id,generation))Complete(id,false,"Native WebSocket route generation changed before acknowledgement");return;}
      if(!operations_.IsCurrent(id,generation))return;
      if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native WebSocket route decision original command deadline exceeded");return;}
      if(ok)Complete(id,true,"","{}");
      else{auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;Complete(id,false,result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():"Native WebSocket route command failed");}
    },std::string(""),true,4096);
  }
