// SPDX-License-Identifier: MIT
// Include inside Source. Selection remains atomic in the actual utility world.
void SelectTextAction(int id,uint64_t generation,Dict config) {
  if(!operations_.IsCurrent(id,generation)||!browser_)return;
  if(config->HasKey("force")&&config->GetType("force")!=VTYPE_BOOL){Complete(id,false,"Invalid native text selection force option");return;}
  const auto path=config->GetList("framePath");
  if(path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){SelectTextAction(id,generation,resolved);});return;
  }
  if(!locator_handlers_.empty()&&LocatorHandlerBefore(id,generation,config,[this,id,generation,config]{SelectTextAction(id,generation,config);}))return;
  browser_->GetHost()->SetFocus(true);
  const auto select="("+std::string(kSuiteCutSelectText)+")(element,e=>("+std::string(kSuiteCutDomQuery)+")([],e,{visibilityOnly:true}).visible,"+(config->GetBool("force")?"true":"false")+")";
  DevToolsCallback callback=[this,id,generation,config](bool ok,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")){
      const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
      const auto exception=details?details->GetDictionary("exception"):nullptr;
      auto error=exception?exception->GetString("description").ToString():"Native text selection failed";
      error=error.substr(0,error.find('\n'));
      if(config->HasKey("nativeHandle")||error.find("Element is not attached")==std::string::npos){Complete(id,false,"selectText: "+error);return;}
      SelectTextRetry(id,generation,config,"element to be attached");return;
    }
    const auto remote=result->GetDictionary("result");const auto state=remote?remote->GetDictionary("value"):nullptr;
    if(!state){Complete(id,false,"Native text selection state is unavailable");return;}
    if(!state->GetBool("ready")){SelectTextRetry(id,generation,config,state->GetString("reason").ToString());return;}
    Complete(id,true);
  };
  if(config->HasKey("nativeHandle")){
    EvaluateFixedNode(id,config,"(()=>{const element=this;return "+select+";})()",std::move(callback));
  }else{
    QueryInspect(id,config,"if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")throw new Error('Strict locator matched multiple elements');const element=nodes[0];if(!element)return {ready:false,reason:'element to be attached'};return "+select+";",true,true,std::move(callback));
  }
}

void SelectTextRetry(int id,uint64_t generation,Dict config,const std::string& reason) {
  if(!operations_.IsCurrent(id,generation))return;
  locator_waits_[id]=reason;
  auto release=CefDictionaryValue::Create();release->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  DevTools(id,"Runtime.releaseObjectGroup",release,[this,id,generation,config](bool ok,const std::string&){
    if(!ok){Complete(id,false,"Native text selection temporary group release failed");return;}
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,config]{self->SelectTextAction(id,generation,config);}),20);
  });
}
