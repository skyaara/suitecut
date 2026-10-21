// SPDX-License-Identifier: MIT
// Include inside Source. No visibility, enabled or focus ownership requirement.
void BlurResult(int id,uint64_t generation,Dict config,std::function<void()> retry,bool ok,const std::string& json) {
  if(!operations_.IsCurrent(id,generation))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
  if(!ok||!result||result->HasKey("exceptionDetails")){
    const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
    const auto exception=details?details->GetDictionary("exception"):nullptr;
    auto error=exception?exception->GetString("description").ToString():"Native blur failed";
    error=error.substr(0,error.find('\n'));
    // The helper checks attachment before the side effect. Only that exact
    // failure permits reacquisition; an unknown context loss cannot replay blur.
    if(!config->HasKey("nativeHandle")&&retry&&error.find("Element is not attached")!=std::string::npos){
      context_retries_[id]=std::move(retry);locator_waits_[id]="element to be attached before blur";RetryContext(id);return;
    }
    Complete(id,false,"blur: "+error);return;
  }
  const auto remote=result->GetDictionary("result");const auto state=remote?remote->GetDictionary("value"):nullptr;
  if(!state||!state->GetBool("ready")){Complete(id,false,"Native blur state is unavailable");return;}
  Complete(id,true);
}

void BlurInvoke(int id,uint64_t generation,Dict config,const std::string& object) {
  const auto found=context_retries_.find(id);
  auto retry=found==context_retries_.end()?std::function<void()>{}:found->second;
  context_retries_.erase(id); // No replay after a real blur event can navigate.
  auto params=CefDictionaryValue::Create();params->SetString("objectId",object);
  params->SetString("functionDeclaration","function(){return ("+std::string(kSuiteCutBlur)+")(this);}");
  params->SetBool("returnByValue",true);params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  DevTools(id,"Runtime.callFunctionOn",params,[this,id,generation,config,retry=std::move(retry)](bool ok,const std::string& json)mutable{
    BlurResult(id,generation,config,std::move(retry),ok,json);
  });
}

void BlurAction(int id,uint64_t generation,Dict config) {
  if(!operations_.IsCurrent(id,generation)||!browser_)return;
  // Fixed handles also retain every real DOM-adoption/blur/release ACK. An
  // aborted operation cannot be retired while its utility command is in flight.
  if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native blur physical capacity exceeded");return;}
  const auto path=config->GetList("framePath");
  if(path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){BlurAction(id,generation,resolved);});return;
  }
  if(config->HasKey("nativeHandle")){
    context_retries_.erase(id);
    EvaluateFixedNode(id,config,"("+std::string(kSuiteCutBlur)+")(this)",[this,id,generation,config](bool ok,const std::string& json){
      BlurResult(id,generation,config,{},ok,json);
    });return;
  }
  QueryInspect(id,config,"if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")throw new Error('Strict locator matched multiple elements');return nodes[0];",true,false,
    [this,id,generation,config](bool ok,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote){FormError(id,"blur",result);return;}
      if(remote->GetType("objectId")!=VTYPE_STRING){locator_waits_[id]="element to be attached before blur";RetryContext(id);return;}
      BlurInvoke(id,generation,config,remote->GetString("objectId").ToString());
    });
}
