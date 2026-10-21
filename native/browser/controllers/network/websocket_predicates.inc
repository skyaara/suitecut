// SPDX-License-Identifier: MIT
// Include inside Source. Shared native candidate/ACK ownership and host physical
// callback accounting are the same as other event predicates. Only the journal
// cursor belongs to the genuine WebSocket controller rather than page events.
  struct WebSocketPredicateWait {
    std::uint64_t generation;
    std::string socket,event,public_event;
    bool waiting=false,emitted=false;
  };
  std::map<int,WebSocketPredicateWait> websocket_predicates_;
  void EmitWebSocketPredicate(int id,std::uint64_t generation) {
    auto found=websocket_predicates_.find(id);
    if(found==websocket_predicates_.end()||found->second.generation!=generation||found->second.emitted)return;
    if(!operations_.IsCurrent(id,generation))return;
    if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native WebSocket predicate Timeout exceeded");return;}
    const auto candidate=event_predicates_.Pending(id,generation);if(!candidate)return;
    auto header=Header("eventpredicate");header->SetInt("pageId",page_id_);header->SetInt("id",id);header->SetDouble("generation",static_cast<double>(generation));
    header->SetDouble("token",static_cast<double>(candidate->token));header->SetDouble("sequence",static_cast<double>(candidate->sequence));
    header->SetString("clientToken",candidate->client_token);header->SetString("event",candidate->event);
    if(writer_.TryEvent(header,std::vector<uint8_t>(candidate->json.begin(),candidate->json.end())))found->second.emitted=true;
    else WakeDeadlines();
  }
  void PollWebSocketPredicate(int id,std::uint64_t generation) {
    auto found=websocket_predicates_.find(id);
    if(found==websocket_predicates_.end()||found->second.generation!=generation)return;
    if(event_predicates_.Pending(id,generation)){EmitWebSocketPredicate(id,generation);return;}
    if(found->second.waiting)return;
    const auto cursor=event_predicates_.Cursor(id,generation);const auto operation=operations_.Get(id);
    if(!cursor||!operation||operation->generation!=generation)return;
    found->second.waiting=true;found->second.emitted=false;
    const auto state=found->second;
    if(!sockets_||!sockets_->Wait(id,state.socket,state.event,static_cast<double>(*cursor),operation->deadline_ms,
      [this,id,generation](bool ok,Dict info,const std::vector<uint8_t>& bytes,const std::string& error){
        auto current=websocket_predicates_.find(id);
        if(current==websocket_predicates_.end()||current->second.generation!=generation||!operations_.IsCurrent(id,generation))return;
        current->second.waiting=false;
        if(!ok){Complete(id,false,error);return;}
        if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native WebSocket predicate Timeout exceeded");return;}
        auto result=CefDictionaryValue::Create();result->SetDictionary("info",info->Copy(false));result->SetString("payloadBase64",CefBase64Encode(bytes.data(),bytes.size()));
        const auto status=event_predicates_.Offer(id,generation,static_cast<std::uint64_t>(info->GetDouble("sequence")),current->second.public_event,Json(result));
        if(status!=SuiteCutEventPredicateController::Status::CandidateReady){Complete(id,false,"Native WebSocket predicate candidate ownership or payload capacity exceeded");return;}
        EmitWebSocketPredicate(id,generation);
      }))Complete(id,false,"Invalid native WebSocket predicate wait or capacity exceeded");
  }
  void PumpWebSocketPredicates() {
    std::vector<std::pair<int,std::uint64_t>> scopes;
    for(const auto& [id,wait]:websocket_predicates_)scopes.emplace_back(id,wait.generation);
    for(const auto& [id,generation]:scopes)if(operations_.IsCurrent(id,generation))PollWebSocketPredicate(id,generation);
  }
  void StartWebSocketPredicate(int id,std::uint64_t generation,Dict config) {
    const auto socket=config->GetString("socketId").ToString(),event=config->GetString("event").ToString();
    if(!sockets_||config->GetType("socketId")!=VTYPE_STRING||(socket.empty()?event!="websocket":event!="framesent"&&event!="framereceived"&&event!="socketerror"&&event!="close")){Complete(id,false,"Invalid native WebSocket predicate event");return;}
    const double cursor=suitecut::DictionaryNumber(config,"afterSequence");
    if(!std::isfinite(cursor)||cursor<0||std::floor(cursor)!=cursor||cursor>static_cast<double>(sockets_->sequence())||config->GetType("clientToken")!=VTYPE_STRING||
       !event_predicates_.Start(id,generation,config->GetString("clientToken"),event,static_cast<std::uint64_t>(cursor),SuiteCutEventPredicateController::kWebSocketPayload)){Complete(id,false,"Invalid native WebSocket predicate cursor, identity or capacity");return;}
    websocket_predicates_.emplace(id,WebSocketPredicateWait{generation,socket,event=="websocket"?"created":event,event});
    PollWebSocketPredicate(id,generation);
  }
  void WebSocketPredicatePublished(Dict info) {
    if(info->GetString("event")!="close")return;
    // This hook runs only after the real close packet entered the Writer FIFO.
    // A host predicate may remain physically live after native cancellation;
    // shared SDK accounting retains that callback until its actual settlement.
    std::vector<std::pair<int,std::uint64_t>> closed;
    for(const auto& [id,wait]:websocket_predicates_)if(wait.event!="close"&&info->GetString("socketId")==wait.socket)closed.emplace_back(id,wait.generation);
    for(const auto& [id,generation]:closed)if(operations_.IsCurrent(id,generation))Complete(id,false,"WebSocket closed");
  }
