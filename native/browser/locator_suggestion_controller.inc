// SPDX-License-Identifier: MIT
// Included inside Source. C++ ranks finite observations, pins exact original
// handle/document and publishes only after one native identity/uniqueness capture.
struct LocatorSuggestionWork {
  int owner=0,handle_id=0,backend=0;uint64_t generation=0;
  SnapshotReferenceDocument document;std::optional<ObjectHandle>handle;
  int picked_backend=0;Dict pick;std::string inspector_root;std::function<bool()>picking_current;
  std::map<std::string,SnapshotReferenceDocument>ancestors;
  std::string node;std::vector<suitecut::LocatorSuggestionPlan::Candidate>candidates;
  CefRefPtr<CefListValue>omitted;
};
std::map<int,std::shared_ptr<LocatorSuggestionWork>>locator_suggestion_work_;
bool LocatorSuggestionCurrent(std::shared_ptr<LocatorSuggestionWork>work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"Native locator suggestion original deadline expired");return false;}
  const auto actual=ReferenceDocument(work->document.frame);
  if(!actual||*actual!=work->document){Complete(work->owner,false,"Native locator suggestion document changed");return false;}
  if(work->picking_current&&!work->picking_current()){Complete(work->owner,false,"Native locator picking Inspector/frame-parent mapping changed");return false;}
  for(const auto&[frame,pin]:work->ancestors){const auto actual=ReferenceDocument(frame);if(!actual||*actual!=pin){Complete(work->owner,false,"Native locator picking ancestor document changed");return false;}}
  if(work->handle){
    const auto found=handles_.find(work->handle_id);const auto&h=*work->handle;
    if(found==handles_.end()||found->second.object_id!=h.object_id||found->second.group!=h.group||found->second.frame_id!=h.frame_id||found->second.context!=h.context||found->second.session!=h.session||found->second.node_id!=h.node_id||found->second.context_identity!=h.context_identity||found->second.renderer_epoch!=h.renderer_epoch||native_pristine_renderer_epoch_!=h.renderer_epoch){Complete(work->owner,false,"Native locator suggestion retained handle changed or was disposed");return false;}
  }
  return true;
}
void LocatorSuggestionSend(const std::shared_ptr<LocatorSuggestionWork>&work,const std::string&method,Dict params,DevToolsCallback done){
  if(!LocatorSuggestionCurrent(work))return;
  DevTools(work->owner,method,params,[this,work,done=std::move(done)](bool ok,const auto&json){if(LocatorSuggestionCurrent(work))done(ok,json);},work->document.session,true,262144,true);
}
Dict LocatorSuggestionParams(const std::shared_ptr<LocatorSuggestionWork>&work,const std::string&function){
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->node);params->SetString("functionDeclaration",function);
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
  auto serialization=CefDictionaryValue::Create();serialization->SetString("serialization","deep");serialization->SetInt("maxDepth",1);
  auto additional=CefDictionaryValue::Create();additional->SetInt("maxNodeDepth",0);serialization->SetDictionary("additionalParameters",additional);params->SetDictionary("serializationOptions",serialization);return params;
}
void LocatorSuggestionVerify(const std::shared_ptr<LocatorSuggestionWork>&work){
  auto queries=CefListValue::Create();for(size_t i=0;i<work->candidates.size();++i)queries->SetList(i,work->candidates[i].query->Copy());
  auto value=CefValue::Create();value->SetList(queries);const auto encoded=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();
  auto params=LocatorSuggestionParams(work,"function(){return ("+std::string(kSuiteCutLocatorSuggestionVerify)+").call(this,("+std::string(kSuiteCutDomQuery)+"),"+encoded+");}");
  LocatorSuggestionSend(work,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string&json){
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);int backend=0;Dict metadata;std::string error;
    if(!ok||!suitecut::LocatorSuggestionPlan::Capture(parsed?parsed->GetDictionary():nullptr,backend,metadata,error)){Complete(work->owner,false,error.empty()?"Native locator suggestion verification failed":error);return;}
    if(backend!=work->backend){Complete(work->owner,false,"Native locator suggestion selected backend identity changed");return;}
    const auto results=metadata->GetList("results");if(!results||results->GetSize()!=work->candidates.size()){Complete(work->owner,false,"Native locator suggestion verification count is inconsistent");return;}
    auto suggestions=CefListValue::Create();auto diagnostics=work->omitted?work->omitted->Copy():CefListValue::Create();
    for(size_t i=0;i<results->GetSize();++i){
      const auto checked=results->GetDictionary(i);const auto&candidate=work->candidates[i];
      if(!checked||checked->GetSize()!=2||checked->GetType("matches")!=VTYPE_INT||checked->GetInt("matches")<0||checked->GetInt("matches")>1000000||checked->GetType("same")!=VTYPE_BOOL||(checked->GetBool("same")&&checked->GetInt("matches")!=1)){Complete(work->owner,false,"Invalid native locator suggestion identity receipt");return;}
      if(checked->GetBool("same")&&suggestions->GetSize()<suitecut::LocatorSuggestionPlan::kResults){
        auto row=CefDictionaryValue::Create();row->SetString("kind",candidate.kind);row->SetList("query",candidate.query->Copy());row->SetString("code",candidate.code);row->SetInt("matches",1);row->SetBool("sameNode",true);suggestions->SetDictionary(suggestions->GetSize(),row);
      }else if(!checked->GetBool("same")){
        auto row=CefDictionaryValue::Create();row->SetString("kind",candidate.kind);row->SetInt("matches",checked->GetInt("matches"));row->SetString("reason",checked->GetInt("matches")==1?"Unique query selected a different node":"Query is not unique for the selected node");diagnostics->SetDictionary(diagnostics->GetSize(),row);
      }
    }
    if(!suggestions->GetSize()){Complete(work->owner,false,"No supported native locator recipe uniquely selected this Element");return;}
    auto provenance=CefDictionaryValue::Create();provenance->SetString("source",handle_namespace_);provenance->SetString("frame",work->document.frame);provenance->SetString("session",work->document.session);provenance->SetString("loader",work->document.loader);
    provenance->SetString("mainIdentity",work->document.main_unique);provenance->SetString("utilityIdentity",work->document.utility_unique);provenance->SetInt("mainContext",work->document.main);provenance->SetInt("utilityContext",work->document.utility);provenance->SetString("documentGeneration",std::to_string(work->document.context_generation));provenance->SetInt("backendNodeId",backend);
    if(work->handle_id)provenance->SetInt("retainedHandle",work->handle_id);
    if(work->pick)provenance->SetDictionary("point",work->pick->Copy(false));
    auto response=CefDictionaryValue::Create();response->SetDictionary("provenance",provenance);response->SetList("suggestions",suggestions);response->SetList("diagnostics",diagnostics);response->SetBool("pointPicking",bool(work->pick));
    const auto encoded=Json(response);if(encoded.size()>131072){Complete(work->owner,false,"Native locator suggestion reply exceeds byte bounds");return;}
    if(LocatorSuggestionCurrent(work))Complete(work->owner,true,"",encoded);
  });
}
void LocatorSuggestionObserve(const std::shared_ptr<LocatorSuggestionWork>&work){
  auto params=LocatorSuggestionParams(work,"function(){return ("+std::string(kSuiteCutLocatorSuggestionObserve)+").call(this,("+std::string(kSuiteCutDomQuery)+"));}");
  LocatorSuggestionSend(work,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string&json){
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);Dict observation;std::string error;
    if(!ok||!suitecut::LocatorSuggestionPlan::Capture(parsed?parsed->GetDictionary():nullptr,work->backend,observation,error)||!suitecut::LocatorSuggestionPlan::Read(observation,work->candidates,error)){Complete(work->owner,false,error.empty()?"Native locator suggestion observation failed":error);return;}
    if(work->picked_backend&&work->backend!=work->picked_backend){Complete(work->owner,false,"Native picked backend changed before suggestion observation");return;}
    const auto omitted=observation->GetList("omitted");work->omitted=omitted?omitted->Copy():nullptr;if(!work->omitted||work->omitted->GetSize()>24){Complete(work->owner,false,"Native locator suggestion diagnostic capacity exceeded");return;}
    for(size_t i=0;i<work->omitted->GetSize();++i){const auto row=work->omitted->GetDictionary(i);if(!row||row->GetSize()!=2||row->GetType("kind")!=VTYPE_STRING||row->GetString("kind").length()>32||row->GetType("reason")!=VTYPE_STRING||row->GetString("reason").length()>256){Complete(work->owner,false,"Invalid native locator suggestion omission diagnostic");return;}}
    LocatorSuggestionVerify(work);
  });
}
void BeginLocatorSuggestion(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  if(operations_.IsExpired(owner,operation->generation,Now())){Complete(owner,false,"Native locator suggestion original deadline expired");return;}
  for(const char*key:{"nativeQueryNodes","nativeRootHandle","nativeSelectorWaitState","nativeAccessibilitySnapshot","nativeSemanticSnapshotJson","formNode","point"})if(message->HasKey(key)){Complete(owner,false,"Unsupported native locator suggestion target field");return;}
  if(!SelectorWaits().Admit(owner,operation->generation)){Complete(owner,false,"Native locator suggestion physical capacity exceeded");return;}
  if(const auto path=message->GetList("framePath");path&&path->GetSize()){
    if(message->HasKey("nativeHandle")){Complete(owner,false,"Retained locator suggestions cannot borrow a frame path");return;}
    ResolveFramePath(owner,operation->generation,message,0,FrameId(message),[this,owner](Dict resolved){BeginLocatorSuggestion(owner,resolved);});return;
  }
  auto work=std::make_shared<LocatorSuggestionWork>();work->owner=owner;work->generation=operation->generation;
  std::string frame=FrameId(message);
  if(message->HasKey("nativeHandle")){
    if(message->GetType("nativeHandle")!=VTYPE_INT||message->GetInt("nativeHandle")<=0){Complete(owner,false,"Invalid native retained locator suggestion handle");return;}
    work->handle_id=message->GetInt("nativeHandle");const auto handle=handles_.find(work->handle_id);
    if(handle==handles_.end()){Complete(owner,false,MissingHandle(work->handle_id));return;}
    if(handle->second.node_id.empty()){Complete(owner,false,"Native locator suggestion handle is not a DOM node");return;}
    work->handle=handle->second;work->node=handle->second.node_id;frame=handle->second.frame_id;
  }else if(message->GetType("query")!=VTYPE_LIST){Complete(owner,false,"Native locator suggestion requires a locator or retained Element");return;}
  WithFrame(owner,frame,[this,work,frame,message](int){
    if(!operations_.IsCurrent(work->owner,work->generation))return;
    const auto document=ReferenceDocument(frame);if(!document){Complete(work->owner,false,"Native locator suggestion document identity unavailable");return;}
    work->document=*document;locator_suggestion_work_[work->owner]=work;
    if(!LocatorSuggestionCurrent(work))return;
    if(!ReferenceTemporaryGroups(work->owner)){Complete(work->owner,false,"Native locator suggestion group ownership unavailable");return;}
    if(work->handle){
      const auto&h=*work->handle;
      if(h.context!=document->main||h.context_identity!=document->main_unique||h.session!=document->session){Complete(work->owner,false,"Native locator suggestion retained handle realm is no longer current");return;}
      SuiteCutQueryNodeTransfer::AdoptNode(
        [this,work](const auto&method,Dict params,auto done){LocatorSuggestionSend(work,method,params,std::move(done));},
        [this,work]{return LocatorSuggestionCurrent(work);},
        [this,work](const auto&error){if(LocatorSuggestionCurrent(work))Complete(work->owner,false,error);},
        work->node,document->utility,"suitecut-automation-"+std::to_string(work->owner),[this,work](Dict node){work->node=node->GetString("objectId").ToString();LocatorSuggestionObserve(work);});return;
    }
    QueryInspect(work->owner,message,"if(nodes.length!==1)throw new Error('Locator suggestions require exactly one selected Element');return nodes[0];",true,false,[this,work](bool ok,const auto&json){
      if(!LocatorSuggestionCurrent(work))return;
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto node=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!node||node->GetString("subtype")!="node"||node->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"Native locator suggestion requires one connected selected Element");return;}
      work->node=node->GetString("objectId").ToString();LocatorSuggestionObserve(work);
    });
  },true);
}
