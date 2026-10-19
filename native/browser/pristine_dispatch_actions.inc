// SPDX-License-Identifier: MIT
// Private next-slice Source actions. Included after ordinary dispatch helpers.
struct NativeMainDispatchWork {
  int id=0;
  uint64_t generation=0;
  NativePristineKey owner;
  std::string factory,target;
  Dict config;
  std::string packet_json;
  std::vector<ObjectHandle>handles;
  std::vector<std::string>arguments;
  uint32_t wrapped=0;
  size_t index=0;
  bool pure_main=false;
};
bool NativeMainDispatchCurrent(const std::shared_ptr<NativeMainDispatchWork>&w){
  if(!DispatchEventCurrent(w->id,w->generation))return false;
  const auto actual=NativePristineFind(w->owner.frame);
  if(!NativePristineCurrent(w->owner)||!actual||actual->owner!=w->owner||actual->object!=w->factory){Complete(w->id,false,"dispatchEvent: Exact native main document/factory is unavailable");
  return false;
  }return true;
}
void NativeMainDispatchStrict(const std::shared_ptr<NativeMainDispatchWork>&w,const std::string&array){
  if(!NativeMainDispatchCurrent(w))return;
  const auto frame=frame_states_.find(w->owner.frame);
  if(frame==frame_states_.end()||!frame->second.utility_context){Complete(w->id,false,"dispatchEvent: Native isolated diagnostic context unavailable");
  return;
  }
  SuiteCutQueryNodeTransfer::Transfer(
    [this,w](const std::string&method,Dict params,SuiteCutQueryNodeTransfer::Result done){if(NativeMainDispatchCurrent(w))DevTools(w->id,method,params,std::move(done),w->owner.session);},
    [this,w]{return NativeMainDispatchCurrent(w);},
    [this,w](const std::string&error){if(DispatchEventCurrent(w->id,w->generation))Complete(w->id,false,"dispatchEvent: "+error);},array,frame->second.utility_context,"suitecut-automation-"+std::to_string(w->id),
    [this,w](const std::string&actual){
      if(!NativeMainDispatchCurrent(w))return;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",actual);params->SetString("functionDeclaration","function(){return {strictError:"+QueryDiagnosticExpression(w->config,"this")+"};}");params->SetString("objectGroup","suitecut-automation-"+std::to_string(w->id));params->SetBool("returnByValue",false);
      DevTools(w->id,"Runtime.callFunctionOn",params,[this,w](bool ok,const std::string&json){
        if(!NativeMainDispatchCurrent(w))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
        if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetType("objectId")!=VTYPE_STRING){DispatchEventResult(w->id,w->generation,ok,json);return;}DispatchEventStrictResult(w->id,w->generation,remote->GetString("objectId").ToString());
      },w->owner.session);
    });
}
void NativeMainDispatchResult(const std::shared_ptr<NativeMainDispatchWork>&w,bool ok,const std::string&json){
  if(!DispatchEventCurrent(w->id,w->generation))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);
  auto result=parsed?parsed->GetDictionary():nullptr;
  auto remote=result?result->GetDictionary("result"):nullptr;
  // Main selectors/WebIDL conversions may throw author errors after side
  // effects. Never derive replay permission from exception message text.
  if(!ok||!result||result->HasKey("exceptionDetails")||!remote){DispatchEventResult(w->id,w->generation,ok,json);
  return;
  }
  if(remote->GetType("objectId")!=VTYPE_STRING){Complete(w->id,false,"dispatchEvent: Native main dispatch observation unavailable");
  return;
  }
  auto params=CefDictionaryValue::Create();
  params->SetString("objectId",remote->GetString("objectId"));
  params->SetBool("ownProperties",true);
  DevTools(w->id,"Runtime.getProperties",params,[this,w](bool ok,const std::string&json){
    if(!DispatchEventCurrent(w->id,w->generation))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto fields=result?result->GetList("result"):nullptr;
    if(!ok||!fields||fields->GetSize()>8){Complete(w->id,false,"dispatchEvent: Native main dispatch metadata unavailable");return;}
    bool ready=false,observed=false,detached=false;std::string strict;
    for(size_t i=0;i<fields->GetSize();++i){auto field=fields->GetDictionary(i);auto value=field?field->GetDictionary("value"):nullptr;if(!field||!value)continue;const auto name=field->GetString("name").ToString();if(name=="ready"&&value->GetString("type")=="boolean")ready=value->GetBool("value");if(name=="dispatched"&&value->GetString("type")=="boolean")observed=true;if(name=="detached"&&value->GetString("type")=="boolean")detached=value->GetBool("value");if(name=="strictNodes"&&value->GetString("subtype")=="array")strict=value->GetString("objectId").ToString();}
    if(!strict.empty()){NativeMainDispatchStrict(w,strict);return;}
    if(ready&&observed){Complete(w->id,true);return;}
    if(!ready&&w->pure_main){context_retries_[w->id]=[this,w]{DispatchEventAction(w->id,w->generation,w->config);};locator_waits_[w->id]="element to be attached before main dispatchEvent";RetryContext(w->id);return;}
    if(!ready&&detached){Complete(w->id,false,"dispatchEvent: Element is not attached to the DOM");return;}
    Complete(w->id,false,"dispatchEvent: Native main dispatch observation malformed");
  },w->owner.session);
}
void NativeMainDispatchSend(const std::shared_ptr<NativeMainDispatchWork>&w){
  if(!NativeMainDispatchCurrent(w))return;
  const auto mask=std::to_string(w->wrapped);
  const auto strict=w->config->GetType("strict")==VTYPE_BOOL&&!w->config->GetBool("strict")?"false":"true";
  const auto declaration=w->pure_main?"function(packet,target,...handles){return this('select',null,packet,handles,function(nativeQueryIntrinsics){'use strict';return "+QueryExpression(w->config,"nativeQueryIntrinsics")+";},"+strict+","+mask+");}":"function(packet,target,...handles){return this('dispatch',target,packet,handles,undefined,undefined,"+mask+");}";
  // Retain only validated JSON and actual object-ID strings across callbacks.
  // Parse the complete final graph once: no retained CefList/CefValue packet
  // or child wrapper is reparented into a different CEF value controller.
  std::string json="{\"objectId\":"+Literal(w->factory)+",\"functionDeclaration\":"+Literal(declaration)+",\"arguments\":[{\"value\":"+w->packet_json+"},";
  json+=w->pure_main?"{\"value\":null}":"{\"objectId\":"+Literal(w->target)+"}";
  for(const auto&object:w->arguments)json+=",{\"objectId\":"+Literal(object)+"}";
  json+="],\"returnByValue\":false,\"objectGroup\":"+Literal("suitecut-automation-"+std::to_string(w->id))+"}";
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);
  auto params=parsed?parsed->GetDictionary():nullptr;
  if(!params){Complete(w->id,false,"dispatchEvent: Native main final params graph is invalid");return;}
  context_retries_.erase(w->id);
  if(NativeMainDispatchCurrent(w))DevTools(w->id,"Runtime.callFunctionOn",params,[this,w](bool ok,const std::string&json){NativeMainDispatchResult(w,ok,json);},w->owner.session);
}
void NativeMainDispatchArguments(const std::shared_ptr<NativeMainDispatchWork>&w){
  if(!NativeMainDispatchCurrent(w))return;
  if(w->index==w->handles.size()){NativeMainDispatchSend(w);
  return;
  }
  const auto handle=w->handles[w->index];
  const auto source=frame_states_.find(handle.frame_id);
  if(source==frame_states_.end()||source->second.context!=handle.context||source->second.context_identity!=handle.context_identity||handle.renderer_epoch!=native_pristine_renderer_epoch_||source->second.session!=handle.session){Complete(w->id,false,"dispatchEvent: Native handle context identity was retired");
  return;
  }
  if(handle.node_id.empty()){
    if(handle.frame_id!=w->owner.frame||handle.session!=w->owner.session||handle.context!=w->owner.context||handle.context_identity!=w->owner.identity){Complete(w->id,false,"JSHandles can be evaluated only in the context they were created! Native opaque dispatch argument belongs to a different frame context");
  return;
  }
    w->arguments.push_back(handle.object_id);
  w->wrapped|=uint32_t{1}<<w->index;
  ++w->index;
  NativeMainDispatchArguments(w);
  return;
  }
  const auto destination=frame_states_.find(w->owner.frame);
  if(destination==frame_states_.end()||handle.session!=w->owner.session||(handle.frame_id!=w->owner.frame&&(source->second.origin.empty()||source->second.origin!=destination->second.origin))){Complete(w->id,false,"dispatchEvent: Cross-origin DOM argument transfer is unsupported");
  return;
  }
  const auto identity=source->second.context_identity;
  SuiteCutQueryNodeTransfer::AdoptNode([this,w](const auto&method,Dict params,auto done){if(NativeMainDispatchCurrent(w))DevTools(w->id,method,params,std::move(done),w->owner.session);},
    [this,w,handle,identity]{const auto actual=frame_states_.find(handle.frame_id);return NativeMainDispatchCurrent(w)&&actual!=frame_states_.end()&&actual->second.context_identity==identity&&actual->second.context==handle.context;},
    [this,w](const auto&error){if(DispatchEventCurrent(w->id,w->generation))Complete(w->id,false,"dispatchEvent: "+error);},handle.node_id,w->owner.context,"suitecut-automation-"+std::to_string(w->id),
    [this,w](Dict actual){const auto object=actual->GetString("objectId").ToString();w->arguments.push_back(object);++w->index;NativeMainDispatchArguments(w);});
}
void NativeMainDispatchTarget(const std::shared_ptr<NativeMainDispatchWork>&w,const std::string&node){
  if(!NativeMainDispatchCurrent(w))return;
  SuiteCutQueryNodeTransfer::AdoptNode([this,w](const auto&method,Dict params,auto done){if(NativeMainDispatchCurrent(w))DevTools(w->id,method,params,std::move(done),w->owner.session);},[this,w]{return NativeMainDispatchCurrent(w);},
    [this,w](const auto&error){if(DispatchEventCurrent(w->id,w->generation))Complete(w->id,false,"dispatchEvent: "+error);},node,w->owner.context,"suitecut-automation-"+std::to_string(w->id),
    [this,w](Dict actual){w->target=actual->GetString("objectId").ToString();NativeMainDispatchArguments(w);});
}
bool NativeMainDispatchAction(int id,uint64_t generation,Dict config){
  bool opaque=false;
  bool required=QueryNeedsMainWorld(config);
  auto ids=config->GetList("handles");
  if(!ids)return false;
  for(size_t i=0;i<ids->GetSize();++i){const auto found=handles_.find(ids->GetInt(i));
  if(found!=handles_.end()&&found->second.node_id.empty()){required=true;opaque=true;}
  }
  if(!required)return false;
  if(!DispatchEventCurrent(id,generation))return true;
  if(MaterializeConsoleArguments(id,config,[this,id,generation,config]{DispatchEventAction(id,generation,config);}))return true;
  if(QueryUsesMultipleWorlds(config)){
    if(!opaque)return false;
    Complete(id,false,"dispatchEvent: Opaque main dispatch for mixed selector realms is unsupported");
    return true;
  }
  auto factory=NativePristineFind(FrameId(config));
  if(!factory){
    const auto owner=NativePristineOwner(FrameId(config));
  auto*record=owner?NativePristineRecord(*owner):nullptr;
    if(record&&record->ticket&&record->unavailable.empty()){context_retries_[id]=[this,id,generation,config]{DispatchEventAction(id,generation,config);
  };
  locator_waits_[id]="proven native main dispatch bootstrap";
  RetryContext(id);
  }
    else Complete(id,false,"dispatchEvent: Pristine main-world dispatch is unavailable for this actual document"+
      (record&&!record->unavailable.empty()?": "+record->unavailable:std::string{}));
  return true;
  }
  const auto packet_json=config->GetString("eventInitJSON").ToString();
  {
    const auto packet=CefParseJSON(packet_json,JSON_PARSER_RFC);
    if(!packet||packet->GetType()!=VTYPE_LIST||ids->GetSize()>32){Complete(id,false,"Invalid native main dispatch packet");return true;}
  }
  auto w=std::make_shared<NativeMainDispatchWork>();
  w->id=id;
  w->generation=generation;
  w->owner=factory->owner;
  w->factory=factory->object;
  w->packet_json=packet_json;
  w->config=config;
  w->pure_main=QueryNeedsMainWorld(config)&&!config->HasKey("nativeHandle");
  for(size_t i=0;i<ids->GetSize();++i){const auto found=ids->GetType(i)==VTYPE_INT?handles_.find(ids->GetInt(i)):handles_.end();
  if(found==handles_.end()){Complete(id,false,"dispatchEvent: Native argument handle is disposed or unavailable");
  return true;
  }w->handles.push_back(found->second);
  }
  evaluation_groups_[id].insert(w->owner.session);
  if(w->pure_main){NativeMainDispatchArguments(w);
  return true;
  }
  if(config->HasKey("nativeHandle")){const auto handle=handles_.find(config->GetInt("nativeHandle"));
  if(handle==handles_.end()||handle->second.node_id.empty()||handle->second.frame_id!=w->owner.frame||handle->second.context_identity!=w->owner.identity){Complete(id,false,"dispatchEvent: Fixed native target context is unavailable");
  return true;
  }NativeMainDispatchTarget(w,handle->second.node_id);
  return true;
  }
  QueryInspect(id,config,"if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")return {strictError:"+QueryDiagnosticExpression(config,"nodes")+"};return nodes[0];",true,false,[this,w](bool ok,const std::string&json){
    if(!NativeMainDispatchCurrent(w))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("result"):nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")||!node){DispatchEventResult(w->id,w->generation,ok,json);return;}
    if(node->GetType("objectId")!=VTYPE_STRING){locator_waits_[w->id]="element before main dispatchEvent";RetryContext(w->id);return;}if(node->GetString("subtype")!="node"){DispatchEventStrictResult(w->id,w->generation,node->GetString("objectId").ToString());return;}NativeMainDispatchTarget(w,node->GetString("objectId").ToString());
  });
  return true;
}
