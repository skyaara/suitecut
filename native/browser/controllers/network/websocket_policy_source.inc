// SPDX-License-Identifier: MIT
// Exact flattened target attachments owned by this actual Source.
struct NativeWebSocketPolicyScope {bool ready=false,failed=false;std::string error;uint64_t serial=0;std::string native_frame;std::vector<suitecut::NativeWebSocketRoutes::Result>waiting;};
std::map<std::string,NativeWebSocketPolicyScope>native_websocket_policy_scopes_;
bool native_websocket_policy_root_ready_=false,native_websocket_policy_recovery_blocked_=false;
uint64_t native_websocket_policy_serial_=0;
std::string NativeWebSocketPolicyFrame()const{const auto frame=browser_?browser_->GetMainFrame():nullptr;return frame?frame->GetIdentifier().ToString():std::string{};}
void NativeWebSocketPolicyClearScopes(const std::string&error){
  std::vector<suitecut::NativeWebSocketRoutes::Result>pending;
  for(auto&[session,scope]:native_websocket_policy_scopes_)for(auto&done:scope.waiting)pending.push_back(std::move(done));
  native_websocket_policy_scopes_.clear();for(auto&done:pending)done(false,error);
}
void NativeWebSocketPolicyLoseRoot(const std::string&error){
  native_websocket_policy_recovery_blocked_=true;native_websocket_policy_root_ready_=false;
  engine_.websocket_routes().DetachPrefix("page:"+std::to_string(page_id_)+"/ws:");NativeWebSocketPolicyClearScopes(error);NetworkFatal(error);
}
bool NativeWebSocketPolicyRootCurrent(){
  if(native_websocket_policy_recovery_blocked_)return false;
  const auto root=native_websocket_policy_scopes_.find("");if(root==native_websocket_policy_scopes_.end())return true;
  if(root->second.native_frame==NativeWebSocketPolicyFrame())return true;
  if(engine_.websocket_routes().HasPolicy(engine_.contexts()->ContextId(page_id_),page_id_)||engine_.websocket_routes().Owns(NativeWebSocketPolicyOwner(""))||native_routed_active_||native_routed_configuring_){NativeWebSocketPolicyLoseRoot("Native WebSocket renderer ownership changed; recreate the page because backend V1 has no authenticated agent handoff");return false;}
  engine_.websocket_routes().Detach(NativeWebSocketPolicyOwner(""));native_websocket_policy_scopes_.erase(root);return true;
}
std::string NativeWebSocketPolicyOwner(const std::string&session)const{return "page:"+std::to_string(page_id_)+"/ws:"+session;}
void NativeWebSocketPolicyAttach(const std::string&session,suitecut::NativeWebSocketRoutes::Result ready){
  if(session.empty()&&!NativeWebSocketPolicyRootCurrent()){ready(false,"Native WebSocket renderer ownership is retired; recreate the page");return;}
  if(closing_page_||!browser_){ready(false,"Actual WebSocket target channel is closed");return;}
  const auto found=native_websocket_policy_scopes_.find(session);
  if(found!=native_websocket_policy_scopes_.end()){
    if(found->second.ready){ready(true,"");return;}
    if(found->second.failed){ready(false,found->second.error);return;}
    if(found->second.waiting.size()>=64){ready(false,"Native WebSocket target readiness capacity exceeded");return;}
    found->second.waiting.push_back(std::move(ready));return;
  }
  auto&scope=native_websocket_policy_scopes_[session];scope.serial=++native_websocket_policy_serial_;scope.native_frame=session.empty()?NativeWebSocketPolicyFrame():std::string{};const auto serial=scope.serial;scope.waiting.push_back(std::move(ready));
  const auto owner=NativeWebSocketPolicyOwner(session);
  const bool accepted=engine_.websocket_routes().Attach(engine_.contexts()->ContextId(page_id_),page_id_,owner,
    [self=CefRefPtr<Source>(this),session](const std::string&method,Dict params,double deadline,suitecut::NativeWebSocketRoutes::Result done){
      if(self->closing_page_||!self->browser_){done(false,"Actual WebSocket target closed before dispatch");return;}
      if(session.empty()&&(self->native_routed_active_||self->native_routed_configuring_)){done(false,"Explicit WebSocket control and URL policy cannot own the same root target");return;}
      self->NativeNetworkSend(session,method,params,std::move(done),deadline);
    },[self=CefRefPtr<Source>(this)](const std::string&error){self->NetworkFatal(error);},
    [self=CefRefPtr<Source>(this),session,serial](bool ok,const std::string&error){
      const auto actual=self->native_websocket_policy_scopes_.find(session);if(actual==self->native_websocket_policy_scopes_.end()||actual->second.serial!=serial)return;
      auto waiting=std::move(actual->second.waiting);actual->second.ready=ok;actual->second.failed=!ok;actual->second.error=error.substr(0,1024);
      if(session.empty())self->native_websocket_policy_root_ready_=ok;
      for(auto&done:waiting)done(ok,error);
      if(session.empty()&&ok){self->SettingsCommit();self->MaybeReady();}
    });
  if(!accepted){auto waiting=std::move(scope.waiting);native_websocket_policy_scopes_.erase(session);for(auto&done:waiting)done(false,"Native WebSocket target attachment capacity exceeded");}
}
void NativeWebSocketPolicyDetach(const std::string&session){
  engine_.websocket_routes().Detach(NativeWebSocketPolicyOwner(session));
  const auto found=native_websocket_policy_scopes_.find(session);if(found==native_websocket_policy_scopes_.end())return;
  auto waiting=std::move(found->second.waiting);native_websocket_policy_scopes_.erase(found);
  for(auto&done:waiting)done(false,"Actual native WebSocket target attachment closed");
}
bool NativeWebSocketPolicyEvent(Dict event,const std::string&session){if(session.empty()&&!NativeWebSocketPolicyRootCurrent())return true;return engine_.websocket_routes().Event(NativeWebSocketPolicyOwner(session),event);}
void NativeWebSocketPolicyCommand(int id,uint64_t generation,Dict message){
  if(!NativeWebSocketPolicyRootCurrent()){Complete(id,false,"Native WebSocket renderer ownership is retired; recreate the page");return;}
  if(message->HasKey("frame")||message->HasKey("frameId")){Complete(id,false,"Native WebSocket registration belongs to an actual page or context");return;}
  const auto operation=operations_.Get(id);if(!operation)return;
  engine_.websocket_routes().Command(id,engine_.contexts()->ContextId(page_id_),page_id_,message,operation->deadline_ms,
    [self=CefRefPtr<Source>(this),id,generation](bool ok,Dict result,const std::string&error){
      if(!self->operations_.IsCurrent(id,generation))return;
      if(ok&&self->operations_.IsExpired(id,generation,Now())){self->Complete(id,false,"Native WebSocket policy original command deadline exceeded");return;}
      self->Complete(id,ok,error,result?Json(result):"{}");
    });
}
// Legacy explicit controller shares the same bounded wire staging mechanism.
suitecut::NativeWebSocketPayloads native_websocket_explicit_payloads_{[]{return Now();}};
bool NativeWebSocketPolicyRootOwned()const{return engine_.websocket_routes().Owns(NativeWebSocketPolicyOwner(""));}
void NativeWebSocketExplicitPayloadCommand(int id,uint64_t generation,Dict message){
  if(message->HasKey("frame")||message->HasKey("frameId")){Complete(id,false,"Explicit native WebSocket staging requires the root page target");return;}
  const auto owner=operations_.Get(id);if(!owner)return;
  native_websocket_explicit_payloads_.Command(engine_.contexts()->ContextId(page_id_),page_id_,message,owner->deadline_ms,
    [this](Dict metadata){return native_routed_active_&&!native_routed_configuring_&&metadata->GetString("routingToken").ToString()==std::to_string(native_routed_epoch_)&&native_routed_sockets_.contains(metadata->GetString("requestId").ToString());},
    [this,id,generation](Dict metadata,double deadline,suitecut::NativeWebSocketPayloads::Reply done){
      const auto epoch=native_routed_epoch_;auto params=CefDictionaryValue::Create();
      for(const char*key:{"requestId","messageId","payloadBase64"})params->SetString(key,metadata->GetString(key));
      params->SetString("action",metadata->GetString("operation"));params->SetInt("opcode",metadata->GetInt("opcode"));params->SetInt("code",0);params->SetString("reason","");
      NativeNetworkSend("","Network.suiteCutWebSocketCommandV1",params,[this,id,generation,epoch,deadline,done=std::move(done)](bool ok,const std::string&json){
        if(epoch!=native_routed_epoch_||!operations_.IsCurrent(id,generation)||Now()>=deadline){done(false,nullptr,"Native WebSocket staged decision original owner or deadline expired");return;}
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        done(ok,nullptr,ok?"":result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():json);
      },deadline);
    },[self=CefRefPtr<Source>(this),id,generation](bool ok,Dict result,const std::string&error){if(self->operations_.IsCurrent(id,generation))self->Complete(id,ok,error,result?Json(result):"{}");});
  WakeDeadlines();
}
