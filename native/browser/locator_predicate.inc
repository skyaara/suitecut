// SPDX-License-Identifier: MIT
// Include inside Source. C++ reselects and polls; each author predicate runs once
// on the actual selected node adopted into that frame's genuine main world.
struct LocatorPredicateWork {
  int owner=0;uint64_t generation=0;
  Dict config;std::string frame,node;
  std::map<std::string,SnapshotReferenceDocument> documents;
  std::vector<ObjectHandle> handles;size_t argument=0;
  CefRefPtr<CefListValue> arguments=CefListValue::Create();
};
std::set<int> locator_predicate_operations_;
std::map<int,std::shared_ptr<LocatorPredicateWork>> locator_predicate_work_;
bool LocatorPredicateCurrent(std::shared_ptr<LocatorPredicateWork> work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  const auto active=locator_predicate_work_.find(work->owner);
  if(active==locator_predicate_work_.end()||active->second!=work)return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"waitForFunction: Native operation timed out");return false;}
  for(const auto& [frame,document]:work->documents){
    const auto current=ReferenceDocument(frame);
    if(!current||*current!=document){Complete(work->owner,false,"waitForFunction: Native predicate document changed");return false;}
  }
  return true;
}
void LocatorPredicateSend(const std::shared_ptr<LocatorPredicateWork>& work,const std::string& session,
                          const std::string& method,Dict params,DevToolsCallback done){
  if(!LocatorPredicateCurrent(work))return;
  DevTools(work->owner,method,params,[this,work,done=std::move(done)](bool ok,const std::string& json)mutable{
    if(LocatorPredicateCurrent(work))done(ok,json);
  },session,false,kMaxResponseBytes,true);
}
void LocatorPredicateFailure(const std::shared_ptr<LocatorPredicateWork>& work,Dict result,const std::string& fallback){
  if(!LocatorPredicateCurrent(work))return;
  auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
  auto exception=details?details->GetDictionary("exception"):nullptr;
  auto error=exception?exception->GetValue("description"):nullptr;
  if(!error&&exception&&exception->GetString("type")=="string")error=exception->GetValue("value");
  std::u16string units;
  if(error&&error->GetType()!=VTYPE_STRING&&!suitecut::DevToolsReadTag(error,units))error=nullptr;
  std::string quoted;
  if(!error||!suitecut::DevToolsLosslessJSON(error,quoted,524288))quoted=suitecut::InboundQuote(fallback);
  // This is an explicit native error-string field, never an author object or
  // a recursive reinterpretation of a user predicate's return value.
  auto output=CefDictionaryValue::Create();output->SetString("predicateErrorJSON",quoted);
  if(LocatorPredicateCurrent(work))Complete(work->owner,true,"",Json(output));
}
void LocatorPredicateRetry(const std::shared_ptr<LocatorPredicateWork>& work){
  if(!LocatorPredicateCurrent(work))return;
  locator_waits_[work->owner]="element predicate to be truthy before waitForFunction";
  // Actual in-flight calls/groups remain in SelectorWaits until their ACKs.
  locator_predicate_work_.erase(work->owner);RetryContext(work->owner);
}
void LocatorPredicateInvoke(const std::shared_ptr<LocatorPredicateWork>& work){
  if(!LocatorPredicateCurrent(work))return;
  const auto document=work->documents.at(work->frame);
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->node);
  params->SetString("functionDeclaration",work->config->GetString("function"));
  params->SetBool("awaitPromise",true);params->SetBool("returnByValue",true);
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
  params->SetList("arguments",work->arguments->Copy());
  LocatorPredicateSend(work,document.session,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")){LocatorPredicateFailure(work,result,"Native locator predicate execution failed");return;}
    auto remote=result->GetDictionary("result");
    if(!remote||remote->GetType("value")!=VTYPE_BOOL){Complete(work->owner,false,"Invalid native locator predicate boolean result");return;}
    if(!remote->GetBool("value")){LocatorPredicateRetry(work);return;}
    if(LocatorPredicateCurrent(work))Complete(work->owner,true,"","{}");
  });
}
void LocatorPredicateArguments(const std::shared_ptr<LocatorPredicateWork>& work){
  if(!LocatorPredicateCurrent(work))return;
  if(work->argument==work->handles.size()){LocatorPredicateInvoke(work);return;}
  const auto handle=work->handles[work->argument];
  const auto source=work->documents.at(handle.frame_id),destination=work->documents.at(work->frame);
  if(handle.frame_id==work->frame){
    auto argument=CefDictionaryValue::Create();argument->SetString("objectId",handle.object_id);
    work->arguments->SetDictionary(work->argument++,argument);LocatorPredicateArguments(work);return;
  }
  // JS wrappers stay in their creation realm. Only a real DOM backend node may
  // be adopted across same-origin frames within the same actual target session.
  const auto source_frame=frame_states_.find(handle.frame_id),target_frame=frame_states_.find(work->frame);
  if(handle.node_id.empty()||source.session!=destination.session||source_frame==frame_states_.end()||target_frame==frame_states_.end()||source_frame->second.origin.empty()||source_frame->second.origin!=target_frame->second.origin){Complete(work->owner,false,"waitForFunction: Unable to adopt handle from a different document");return;}
  SuiteCutQueryNodeTransfer::AdoptNode(
    [this,work,source](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){LocatorPredicateSend(work,source.session,method,params,std::move(done));},
    [this,work]{return LocatorPredicateCurrent(work);},
    [this,work](const std::string& error){if(LocatorPredicateCurrent(work))Complete(work->owner,false,error);},
    handle.node_id,destination.main,"suitecut-automation-"+std::to_string(work->owner),[this,work,destination](Dict node){
      auto params=CefDictionaryValue::Create();params->SetString("objectId",node->GetString("objectId"));
      params->SetString("functionDeclaration","function(){return {value:this};}");
      params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
      LocatorPredicateSend(work,destination.session,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto wrapper=result?result->GetDictionary("result"):nullptr;
        if(!ok||!wrapper||result->HasKey("exceptionDetails")||wrapper->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"waitForFunction: Adopted handle wrapper unavailable");return;}
        auto argument=CefDictionaryValue::Create();argument->SetString("objectId",wrapper->GetString("objectId"));
        work->arguments->SetDictionary(work->argument++,argument);LocatorPredicateArguments(work);
      });
    });
}
void LocatorPredicateAdopt(const std::shared_ptr<LocatorPredicateWork>& work){
  if(!LocatorPredicateCurrent(work))return;
  const auto document=work->documents.at(work->frame);
  SuiteCutQueryNodeTransfer::AdoptNode(
    [this,work,document](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){LocatorPredicateSend(work,document.session,method,params,std::move(done));},
    [this,work]{return LocatorPredicateCurrent(work);},
    [this,work](const std::string& error){if(LocatorPredicateCurrent(work))Complete(work->owner,false,error);},
    work->node,document.main,"suitecut-automation-"+std::to_string(work->owner),[this,work](Dict node){work->node=node->GetString("objectId").ToString();LocatorPredicateArguments(work);});
}
void LocatorPredicateSelect(const std::shared_ptr<LocatorPredicateWork>& work){
  if(!LocatorPredicateCurrent(work))return;
  QueryInspect(work->owner,work->config,"if(nodes.length>1)throw new Error("+QueryDiagnosticExpression(work->config,"nodes")+");return nodes[0]?.isConnected?nodes[0]:undefined;",true,false,
    [this,work](bool ok,const std::string& json){
      if(!LocatorPredicateCurrent(work))return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")){LocatorPredicateFailure(work,result,"Native locator predicate selection failed");return;}
      auto remote=result->GetDictionary("result");
      if(remote&&(remote->GetString("type")=="undefined"||remote->GetString("subtype")=="null")){LocatorPredicateRetry(work);return;}
      if(!remote||remote->GetString("subtype")!="node"||remote->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"Native locator predicate selection is not a DOM node");return;}
      work->node=remote->GetString("objectId").ToString();LocatorPredicateAdopt(work);
    });
}
void LocatorPredicateAction(int id,uint64_t generation,Dict config){
  if(!operations_.IsCurrent(id,generation))return;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"waitForFunction: Native operation timed out");return;}
  locator_predicate_operations_.insert(id);
  if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native locator predicate physical capacity exceeded");return;}
  if(config->GetType("function")!=VTYPE_STRING||config->GetString("function").length()>49152||config->HasKey("nativeHandle")){Complete(id,false,"Invalid native locator predicate function or query root");return;}
  if(!PrepareSnapshotReferenceScope(id,config))return;
  if(const auto path=config->GetList("framePath");path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){LocatorPredicateAction(id,generation,resolved);});return;
  }
  if(MaterializeConsoleArguments(id,config,[this,id,generation,config]{LocatorPredicateAction(id,generation,config);}))return;
  const auto ids=config->GetList("handles");if(!ids||ids->GetSize()>1024){Complete(id,false,"Invalid native locator predicate arguments");return;}
  auto work=std::make_shared<LocatorPredicateWork>();work->owner=id;work->generation=generation;work->config=config;work->frame=FrameId(config);
  locator_predicate_work_[id]=work;
  WithFrame(id,work->frame,[this,work,ids](int){
    if(!LocatorPredicateCurrent(work))return;
    const auto document=ReferenceDocument(work->frame);if(!document){Complete(work->owner,false,"Native locator predicate document unavailable");return;}
    work->documents.emplace(work->frame,*document);
    operation_frames_[work->owner]=work->frame;operation_sessions_[work->owner]=document->session;
    for(size_t i=0;i<ids->GetSize();++i){
      if(ids->GetType(i)!=VTYPE_INT){Complete(work->owner,false,"Invalid native locator predicate handle");return;}
      const auto handle=handles_.find(ids->GetInt(i));
      if(handle==handles_.end()){Complete(work->owner,false,MissingHandle(ids->GetInt(i)));return;}
      const auto source=ReferenceDocument(handle->second.frame_id);
      if(!source||source->main!=handle->second.context||source->session!=handle->second.session||source->main_unique!=handle->second.context_identity||handle->second.renderer_epoch!=native_pristine_renderer_epoch_){Complete(work->owner,false,"waitForFunction: Argument creation context unavailable");return;}
      if(!work->documents.contains(handle->second.frame_id)&&work->documents.size()>=64){Complete(work->owner,false,"Native locator predicate argument-document limit exceeded");return;}
      work->documents.emplace(handle->second.frame_id,*source);work->handles.push_back(handle->second);
    }
    LocatorPredicateSelect(work);
  },true);
}
