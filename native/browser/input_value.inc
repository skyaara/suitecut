// SPDX-License-Identifier: MIT
// Include inside Source. Primitive result admission is explicitly lossless;
// arbitrary author by-value objects are never interpreted as scalar sidecars.
bool InputValueCurrent(int id,uint64_t generation) {
  if(!operations_.IsCurrent(id,generation))return false;
  if(operations_.IsExpired(id,generation,Now())){
    Complete(id,false,"inputValue: Native operation timed out");return false;
  }
  return true;
}

void InputValueResult(int id,uint64_t generation,bool ok,const std::string& json) {
  if(!InputValueCurrent(id,generation))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
  if(!ok||!result||result->HasKey("exceptionDetails")){
    auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
    auto exception=details?details->GetDictionary("exception"):nullptr;
    auto error=exception?exception->GetString("description").ToString():result?result->GetString("message").ToString():std::string();
    if(error.empty())error="Native input value could not be read";
    error=error.substr(0,error.find('\n'));Complete(id,false,"inputValue: "+error.substr(0,1024));return;
  }
  auto remote=result->GetDictionary("result");auto value=remote?remote->GetValue("value"):nullptr;
  std::u16string units;
  if(!remote||remote->GetString("type")!="string"||!value||
     (value->GetType()!=VTYPE_STRING&&!suitecut::DevToolsReadTag(value,units))){
    Complete(id,false,"Native input value primitive is unavailable");return;
  }
  std::string encoded;
  if(!suitecut::DevToolsLosslessJSON(value,encoded,kMaxResponseBytes)){
    Complete(id,false,"Native input value exceeds the response limit");return;
  }
  if(InputValueCurrent(id,generation))Complete(id,true,"",encoded);
}

void InputValueInvoke(int id,uint64_t generation,const std::string& node) {
  if(!InputValueCurrent(id,generation))return;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",node);
  params->SetString("functionDeclaration","function(){return ("+std::string(kSuiteCutInputValue)+")(this);}");
  params->SetBool("returnByValue",true);params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  if(!InputValueCurrent(id,generation))return;
  DevTools(id,"Runtime.callFunctionOn",params,[this,id,generation](bool ok,const std::string& json){
    InputValueResult(id,generation,ok,json);
  },std::nullopt,false,kMaxResponseBytes,true);
}

void InputValueFixed(int id,uint64_t generation,Dict config) {
  if(!InputValueCurrent(id,generation))return;
  const auto handle=handles_.find(config->GetInt("nativeHandle"));
  if(handle==handles_.end()){Complete(id,false,MissingHandle(config->GetInt("nativeHandle")));return;}
  if(handle->second.node_id.empty()){Complete(id,false,"Native handle is not a DOM node");return;}
  const auto frame_id=handle->second.frame_id,node=handle->second.node_id;
  context_retries_.erase(id); // A fixed reference never reacquires a new node.
  WithFrame(id,frame_id,[this,id,generation,frame_id,node](int utility){
    if(!InputValueCurrent(id,generation))return;
    const auto frame=frame_states_.find(frame_id);
    if(frame==frame_states_.end()){Complete(id,false,"Native input value frame is detached");return;}
    const auto session=frame->second.session,loader=frame->second.loader;const auto main=frame->second.context;
    evaluation_groups_[id].insert(session);
    SuiteCutQueryNodeTransfer::AdoptNode(
      [this,id,generation,session](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){if(InputValueCurrent(id,generation))DevTools(id,method,params,std::move(done),session);},
      [this,id,generation,frame_id,session,loader,main,utility]{const auto frame=frame_states_.find(frame_id);return InputValueCurrent(id,generation)&&frame!=frame_states_.end()&&frame->second.context==main&&frame->second.utility_context==utility&&frame->second.loader==loader&&frame->second.session==session;},
      [this,id,generation](const std::string& error){if(InputValueCurrent(id,generation))Complete(id,false,"inputValue: "+error);},
      node,utility,"suitecut-automation-"+std::to_string(id),
      [this,id,generation](Dict node){InputValueInvoke(id,generation,node->GetString("objectId").ToString());});
  },true);
}

void InputValueAction(int id,uint64_t generation,Dict config) {
  if(!InputValueCurrent(id,generation)||!browser_)return;
  if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native input value physical capacity exceeded");return;}
  const auto path=config->GetList("framePath");
  if(path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){InputValueAction(id,generation,resolved);});return;
  }
  if(config->HasKey("nativeHandle")){InputValueFixed(id,generation,config);return;}
  QueryInspect(id,config,"if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")throw new Error('Strict locator matched multiple elements');return nodes[0];",true,false,
    [this,id,generation](bool ok,const std::string& json){
      if(!InputValueCurrent(id,generation))return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote){InputValueResult(id,generation,ok,json);return;}
      if(remote->GetType("objectId")!=VTYPE_STRING){locator_waits_[id]="element to be attached before inputValue";RetryContext(id);return;}
      InputValueInvoke(id,generation,remote->GetString("objectId").ToString());
    });
}
