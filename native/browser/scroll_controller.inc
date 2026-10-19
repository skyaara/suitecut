// SPDX-License-Identifier: MIT
// Include inside Source. Every temporary node belongs to the command group.
void ScrollRetry(int id,uint64_t generation,Dict config,const std::string& reason) {
  if(!operations_.IsCurrent(id,generation))return;
  locator_waits_[id]=reason;
  auto params=CefDictionaryValue::Create();
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  // Wait for actual release before another selector pass, including timeout:0.
  DevTools(id,"Runtime.releaseObjectGroup",params,[this,id,generation,config](bool ok,const std::string&){
    if(!ok){Complete(id,false,"Native scroll temporary node release failed");return;}
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,config]{
      self->ScrollAction(id,generation,config);
    }),20);
  });
}

void ScrollCheck(int id,uint64_t generation,Dict config,const std::string& object) {
  auto params=CefDictionaryValue::Create();params->SetString("objectId",object);
  params->SetString("functionDeclaration",kSuiteCutScrollReadiness);
  params->SetBool("awaitPromise",true);params->SetBool("returnByValue",true);
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  DevTools(id,"Runtime.callFunctionOn",params,[this,id,generation,config,object](bool ok,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")){
      const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
      const auto exception=details?details->GetDictionary("exception"):nullptr;
      auto error=exception?exception->GetString("description").ToString():"Native scroll readiness failed";
      error=error.substr(0,error.find('\n'));
      if(!config->HasKey("nativeHandle")&&error.find("Element is not attached")!=std::string::npos){ScrollRetry(id,generation,config,"element to be attached; retrying scroll into view action");return;}
      Complete(id,false,"scrollIntoViewIfNeeded: "+error);return;
    }
    const auto remote=result->GetDictionary("result");const auto state=remote?remote->GetDictionary("value"):nullptr;
    if(!state){Complete(id,false,"Native scroll readiness result is unavailable");return;}
    if(!state->GetBool("ready")){ScrollRetry(id,generation,config,state->GetString("reason").ToString());return;}
    auto scroll=CefDictionaryValue::Create();scroll->SetString("objectId",object);
    context_retries_.erase(id);
    DevTools(id,"DOM.scrollIntoViewIfNeeded",scroll,[this,id](bool success,const std::string& response){
      auto parsed=CefParseJSON(response,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      if(success&&result&&!result->HasKey("error")){Complete(id,true);return;}
      const auto error=result?result->GetDictionary("error"):nullptr;
      Complete(id,false,"scrollIntoViewIfNeeded: "+(error?error->GetString("message").ToString():"Native DOM scroll failed"));
    });
  });
}

void ScrollAction(int id,uint64_t generation,Dict config) {
  if(!operations_.IsCurrent(id,generation)||!browser_)return;
  const auto path=config->GetList("framePath");
  if(path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){ScrollAction(id,generation,resolved);});return;
  }
  if(!locator_handlers_.empty()&&LocatorHandlerBefore(id,generation,config,[this,id,generation,config]{ScrollAction(id,generation,config);}))return;
  if(config->HasKey("nativeHandle")){
    const auto handle=handles_.find(config->GetInt("nativeHandle"));
    if(handle==handles_.end()){Complete(id,false,MissingHandle(config->GetInt("nativeHandle")));return;}
    if(handle->second.node_id.empty()){Complete(id,false,"Native handle is not a DOM node");return;}
    const auto frame_id=handle->second.frame_id,node=handle->second.node_id;
    WithFrame(id,frame_id,[this,id,generation,config,frame_id,node](int context){
      const auto frame=frame_states_.find(frame_id);
      if(frame==frame_states_.end()){Complete(id,false,"Native scroll frame detached");return;}
      const auto session=frame->second.session,loader=frame->second.loader;
      evaluation_groups_[id].insert(session);
      SuiteCutQueryNodeTransfer::AdoptNode(
        [this,id](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result result){DevTools(id,method,params,std::move(result));},
        [this,id,generation,frame_id,session,loader,context]{const auto frame=frame_states_.find(frame_id);return operations_.IsCurrent(id,generation)&&frame!=frame_states_.end()&&frame->second.session==session&&frame->second.loader==loader&&frame->second.utility_context==context;},
        [this,id](const std::string& error){Complete(id,false,error);},node,context,"suitecut-automation-"+std::to_string(id),
        [this,id,generation,config](Dict object){ScrollCheck(id,generation,config,object->GetString("objectId").ToString());});
    },true);return;
  }
  QueryInspect(id,config,"if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")throw new Error('Strict locator matched multiple elements');return nodes[0];",true,false,
    [this,id,generation,config](bool ok,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote){FormError(id,"scrollIntoViewIfNeeded",result);return;}
      if(remote->GetType("objectId")!=VTYPE_STRING){ScrollRetry(id,generation,config,"element to be attached; retrying scroll into view action");return;}
      ScrollCheck(id,generation,config,remote->GetString("objectId").ToString());
    });
}
