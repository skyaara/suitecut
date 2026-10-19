// SPDX-License-Identifier: MIT
// Include inside Source; all work is owned by the existing selector physical
// ledger, including node adoption, event admission, and object-group release.
bool DispatchEventCurrent(int id,uint64_t generation) {
  if(!operations_.IsCurrent(id,generation))return false;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"dispatchEvent: Native operation timed out");return false;}
  return true;
}

struct DispatchEventWork {
  int id=0,utility=0,main=0;uint64_t generation=0;size_t index=0;
  std::string frame,session,loader,node;
  Dict config;std::function<void()> retry;
  std::vector<ObjectHandle> handles;
  std::string packet;
  std::vector<std::string> arguments;
};

bool DispatchEventDocument(const std::shared_ptr<DispatchEventWork>& work) {
  if(!DispatchEventCurrent(work->id,work->generation))return false;
  const auto frame=frame_states_.find(work->frame);
  if(frame!=frame_states_.end()&&frame->second.context==work->main&&frame->second.utility_context==work->utility&&frame->second.session==work->session&&frame->second.loader==work->loader)return true;
  Complete(work->id,false,"dispatchEvent: Native target document changed");return false;
}

void DispatchEventResult(int id,uint64_t generation,bool ok,const std::string& json,Dict config=nullptr,std::function<void()> retry={}) {
  if(!DispatchEventCurrent(id,generation))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
  if(!ok||!result||result->HasKey("exceptionDetails")){
    auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
    auto exception=details?details->GetDictionary("exception"):nullptr;
    auto error=exception?exception->GetString("description").ToString():result?result->GetString("message").ToString():std::string();
    if(error.empty())error="Native event dispatch failed";
    error=error.substr(0,error.find('\n'));
    if(config&&!config->HasKey("nativeHandle")&&retry&&error=="Error: Element is not attached to the DOM"){
      context_retries_[id]=std::move(retry);locator_waits_[id]="element to be attached before dispatchEvent";RetryContext(id);return;
    }
    Complete(id,false,"dispatchEvent: "+error.substr(0,1024));return;
  }
  auto remote=result->GetDictionary("result");auto state=remote?remote->GetDictionary("value"):nullptr;
  if(!state||!state->GetBool("ready")||state->GetType("dispatched")!=VTYPE_BOOL){Complete(id,false,"Native event dispatch result unavailable");return;}
  Complete(id,true);
}

void DispatchEventArguments(const std::shared_ptr<DispatchEventWork>& work) {
  if(!DispatchEventDocument(work))return;
  if(work->index<work->handles.size()){
    const auto handle=work->handles[work->index];const auto source=frame_states_.find(handle.frame_id);
    if(source==frame_states_.end()||source->second.context!=handle.context||source->second.session!=work->session){Complete(work->id,false,"dispatchEvent: DOM handle belongs to an incompatible or destroyed execution context");return;}
    const auto destination=frame_states_.find(work->frame);
    if(handle.frame_id!=work->frame&&(source->second.origin.empty()||source->second.origin!=destination->second.origin)){Complete(work->id,false,"dispatchEvent: Cross-origin DOM handle transfer is unsupported");return;}
    const auto source_loader=source->second.loader;
    auto current=[this,work,handle,source_loader]{
      if(!DispatchEventDocument(work))return false;
      const auto source=frame_states_.find(handle.frame_id);
      if(source!=frame_states_.end()&&source->second.context==handle.context&&source->second.session==work->session&&source->second.loader==source_loader)return true;
      Complete(work->id,false,"dispatchEvent: Argument owner document changed");return false;
    };
    SuiteCutQueryNodeTransfer::AdoptNode(
      [this,work](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){if(DispatchEventDocument(work))DevTools(work->id,method,params,std::move(done),work->session);},
      current,
      [this,work](const std::string& error){if(DispatchEventCurrent(work->id,work->generation))Complete(work->id,false,"dispatchEvent: "+error);},
      handle.node_id,work->utility,"suitecut-automation-"+std::to_string(work->id),
      [this,work,current](Dict node){if(!current())return;work->arguments.push_back(node->GetString("objectId").ToString());++work->index;DispatchEventArguments(work);});
    return;
  }
  // An event can remove/navigate its document. Never replay this side effect
  // following context loss, caller cancellation, or a delayed browser ACK.
  const auto retry=context_retries_.find(work->id);if(retry!=context_retries_.end())work->retry=retry->second;
  context_retries_.erase(work->id);
  // Retain only native strings across callbacks. Construct one independent
  // CEF graph at final admission, without moving a child/list between owners.
  const auto function="function(packet,...handles){return ("+std::string(kSuiteCutDispatchEvent)+")(this,packet,handles);}";
  std::string json="{\"objectId\":"+Literal(work->node)+",\"functionDeclaration\":"+Literal(function)+",\"arguments\":[{\"value\":"+work->packet+"}";
  for(const auto& object:work->arguments)json+=",{\"objectId\":"+Literal(object)+"}";
  json+="],\"returnByValue\":true,\"objectGroup\":"+Literal("suitecut-automation-"+std::to_string(work->id))+"}";
  const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);
  const auto params=parsed?parsed->GetDictionary():nullptr;
  if(!params){Complete(work->id,false,"dispatchEvent: Native argument packet could not be constructed");return;}
  if(!DispatchEventDocument(work))return;
  DevTools(work->id,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){DispatchEventResult(work->id,work->generation,ok,json,work->config,std::move(work->retry));},work->session);
}

void DispatchEventInvoke(int id,uint64_t generation,Dict config,const std::string& node) {
  if(!DispatchEventCurrent(id,generation))return;
  auto packet=config->GetType("eventInitJSON")==VTYPE_STRING?CefParseJSON(config->GetString("eventInitJSON"),JSON_PARSER_RFC):nullptr;
  auto ids=config->GetList("handles");
  if(!packet||packet->GetType()!=VTYPE_LIST||!ids||ids->GetSize()>32){Complete(id,false,"Invalid native dispatchEvent argument");return;}
  auto work=std::make_shared<DispatchEventWork>();work->id=id;work->generation=generation;work->node=node;work->frame=FrameId(config);work->config=config;
  work->packet=config->GetString("eventInitJSON").ToString();
  for(size_t i=0;i<ids->GetSize();++i){
    const auto found=ids->GetType(i)==VTYPE_INT?handles_.find(ids->GetInt(i)):handles_.end();
    if(found==handles_.end()){Complete(id,false,"dispatchEvent: Native argument handle is disposed or unavailable");return;}
    if(found->second.node_id.empty()){Complete(id,false,"dispatchEvent: Opaque JSHandle arguments cannot be transferred into the native DOM world; pass serialized data or a DOM node handle");return;}
    work->handles.push_back(found->second);
  }
  WithFrame(id,work->frame,[this,work](int utility){
    if(!DispatchEventCurrent(work->id,work->generation))return;
    const auto frame=frame_states_.find(work->frame);if(frame==frame_states_.end()){Complete(work->id,false,"dispatchEvent: Native frame detached");return;}
    work->utility=utility;work->main=frame->second.context;work->session=frame->second.session;work->loader=frame->second.loader;
    evaluation_groups_[work->id].insert(work->session);DispatchEventArguments(work);
  },true);
}

// Only the native selection body creates this plain metadata wrapper. Read an
// own data property in its existing utility world; a real selected Node always
// takes the normal subtype=node path and is never decoded as author metadata.
void DispatchEventStrictResult(int id,uint64_t generation,const std::string& object) {
  if(!DispatchEventCurrent(id,generation))return;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",object);
  params->SetString("functionDeclaration","function(){const field=Object.getOwnPropertyDescriptor(this,'strictError');if(!field||typeof field.value!=='string'||field.value.length>16384)throw new Error('Invalid native dispatch strict diagnostic');return field.value;}");
  params->SetBool("returnByValue",true);params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  DevTools(id,"Runtime.callFunctionOn",params,[this,id,generation](bool ok,const std::string& json){
    if(!DispatchEventCurrent(id,generation))return;
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    auto remote=result?result->GetDictionary("result"):nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetString("type")!="string"){
      Complete(id,false,"Native dispatch strict diagnostic is unavailable");return;
    }
    const auto text=suitecut::DevToolsString(remote->GetValue("value"));
    if(text.empty()||text.size()>65536){Complete(id,false,"Native dispatch strict diagnostic is invalid or exceeds its bound");return;}
    if(DispatchEventCurrent(id,generation))Complete(id,false,"dispatchEvent: "+text);
  },std::nullopt,false,kMaxResponseBytes,true);
}

void DispatchEventAction(int id,uint64_t generation,Dict config) {
  if(!DispatchEventCurrent(id,generation)||!browser_)return;
  if(config->GetType("eventInitJSON")!=VTYPE_STRING||config->GetString("eventInitJSON").ToString().size()>16384){Complete(id,false,"Native dispatchEvent argument exceeds 16 KiB or is invalid");return;}
  if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native dispatchEvent physical capacity exceeded");return;}
  const auto path=config->GetList("framePath");
  if(path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){DispatchEventAction(id,generation,resolved);});return;
  }
  if(NativeMainDispatchAction(id,generation,config))return;
  if(config->HasKey("nativeHandle")){
    const auto handle=handles_.find(config->GetInt("nativeHandle"));
    if(handle==handles_.end()){Complete(id,false,MissingHandle(config->GetInt("nativeHandle")));return;}
    if(handle->second.node_id.empty()){Complete(id,false,"dispatchEvent: Native handle is not a DOM node");return;}
    const auto frame_id=handle->second.frame_id,node=handle->second.node_id;
    context_retries_.erase(id);
    WithFrame(id,frame_id,[this,id,generation,config,node](int utility){
      if(!DispatchEventCurrent(id,generation))return;
      const auto frame=frame_states_.find(FrameId(config));if(frame==frame_states_.end()){Complete(id,false,"dispatchEvent: Native frame detached");return;}
      const auto session=frame->second.session,loader=frame->second.loader;const auto main=frame->second.context;
      evaluation_groups_[id].insert(session);
      SuiteCutQueryNodeTransfer::AdoptNode(
        [this,id,generation,session](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){if(DispatchEventCurrent(id,generation))DevTools(id,method,params,std::move(done),session);},
        [this,id,generation,config,session,loader,main,utility]{const auto frame=frame_states_.find(FrameId(config));return DispatchEventCurrent(id,generation)&&frame!=frame_states_.end()&&frame->second.session==session&&frame->second.loader==loader&&frame->second.context==main&&frame->second.utility_context==utility;},
        [this,id,generation](const std::string& error){if(DispatchEventCurrent(id,generation))Complete(id,false,"dispatchEvent: "+error);},
        node,utility,"suitecut-automation-"+std::to_string(id),
        [this,id,generation,config](Dict node){DispatchEventInvoke(id,generation,config,node->GetString("objectId").ToString());});
    },true);return;
  }
  QueryInspect(id,config,"if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")return {strictError:"+QueryDiagnosticExpression(config,"nodes")+"};return nodes[0];",true,false,
    [this,id,generation,config](bool ok,const std::string& json){
      if(!DispatchEventCurrent(id,generation))return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote){DispatchEventResult(id,generation,ok,json);return;}
      if(remote->GetType("objectId")!=VTYPE_STRING){locator_waits_[id]="element to be attached before dispatchEvent";RetryContext(id);return;}
      if(remote->GetString("subtype")!="node"){DispatchEventStrictResult(id,generation,remote->GetString("objectId").ToString());return;}
      DispatchEventInvoke(id,generation,config,remote->GetString("objectId").ToString());
    });
}
