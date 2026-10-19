// SPDX-License-Identifier: MIT
// Included inside Source. Only actual selected utility Elements can produce a
// normalized builtin AST. Numeric+unique contexts and native generation are
// pinned from before selection through the atomic backend/AST capture reply.
struct LocatorNormalizationWork {
  int owner=0;uint64_t generation=0;
  SnapshotReferenceDocument document;
};
bool LocatorNormalizationCurrent(const std::shared_ptr<LocatorNormalizationWork>& work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"Native operation timed out");return false;}
  const auto current=ReferenceDocument(work->document.frame);
  if(!current||*current!=work->document){Complete(work->owner,false,"Native locator normalization document changed");return false;}
  return true;
}
void CaptureLocatorNormalization(const std::shared_ptr<LocatorNormalizationWork>& work,const std::string& target){
  if(!LocatorNormalizationCurrent(work))return;
  // All actual frame-path/selection groups now have one physical owner. They
  // must not also be fire-and-forget released by Complete on caller cancel.
  if(!ReferenceTemporaryGroups(work->owner)){Complete(work->owner,false,"Native locator normalization temporary-group ownership unavailable");return;}
  auto params=CefDictionaryValue::Create();params->SetString("objectId",target);
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
  params->SetString("functionDeclaration","function(){const metadata=("+std::string(kSuiteCutLocatorNormalization)+").call(this,("+std::string(kSuiteCutDomQuery)+"));return [this,metadata];}");
  auto options=CefDictionaryValue::Create();options->SetString("serialization","deep");options->SetInt("maxDepth",1);
  auto additional=CefDictionaryValue::Create();additional->SetInt("maxNodeDepth",0);options->SetDictionary("additionalParameters",additional);params->SetDictionary("serializationOptions",options);
  if(!LocatorNormalizationCurrent(work))return;
  DevTools(work->owner,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){
    if(!LocatorNormalizationCurrent(work))return;
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")){
      const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
      const auto exception=details?details->GetDictionary("exception"):nullptr;
      const auto error=exception?suitecut::DevToolsString(exception->GetValue("description")):result?result->GetString("message").ToString():"";
      Complete(work->owner,false,error.empty()?"Native locator normalization capture failed":error.substr(0,4096));return;
    }
    int backend=0;std::string metadata,error;
    if(!suitecut::LocatorNormalizationCapture(result,backend,metadata,error)){Complete(work->owner,false,error);return;}
    auto provenance=CefDictionaryValue::Create();provenance->SetString("source",handle_namespace_);
    provenance->SetString("frame",work->document.frame);provenance->SetString("session",work->document.session);provenance->SetString("loader",work->document.loader);
    provenance->SetString("mainIdentity",work->document.main_unique);provenance->SetString("utilityIdentity",work->document.utility_unique);
    provenance->SetInt("mainContext",work->document.main);provenance->SetInt("utilityContext",work->document.utility);
    provenance->SetString("documentGeneration",std::to_string(work->document.context_generation));provenance->SetInt("backendNodeId",backend);
    auto response=CefDictionaryValue::Create();response->SetString("metadataJSON",metadata);response->SetDictionary("provenance",provenance);
    const auto serialized=Json(response);
    if(serialized.size()>65536){Complete(work->owner,false,"Native normalized locator reply exceeds byte bounds");return;}
    if(LocatorNormalizationCurrent(work))Complete(work->owner,true,"",serialized);
  },work->document.session,true,262144,true);
}
void BeginLocatorNormalization(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  if(operations_.IsExpired(owner,operation->generation,Now())){Complete(owner,false,"Native operation timed out");return;}
  for(const char* field:{"nativeQueryNodes","nativeRootHandle","nativeHandle","nativeSelectorWaitState","nativeAccessibilitySnapshot","nativeSemanticSnapshotJson","formNode"})
    if(message->HasKey(field)){Complete(owner,false,"Invalid reserved native locator normalization field");return;}
  if(message->GetType("query")!=VTYPE_LIST){Complete(owner,false,"Native locator normalization requires a query AST");return;}
  if(!SelectorWaits().Admit(owner,operation->generation)){Complete(owner,false,"Native locator normalization physical capacity exceeded");return;}
  if(const auto path=message->GetList("framePath");path&&path->GetSize()){
    ResolveFramePath(owner,operation->generation,message,0,FrameId(message),[this,owner](Dict resolved){BeginLocatorNormalization(owner,resolved);});return;
  }
  const auto frame=FrameId(message);
  WithFrame(owner,frame,[this,owner,generation=operation->generation,frame,message](int){
    const auto document=ReferenceDocument(frame);
    if(!document){Complete(owner,false,"Native locator normalization document identity unavailable");return;}
    auto work=std::make_shared<LocatorNormalizationWork>();work->owner=owner;work->generation=generation;work->document=*document;
    if(!LocatorNormalizationCurrent(work))return;
    QueryInspect(owner,message,"if(nodes.length>1)throw new Error('Strict locator matched multiple elements');return nodes[0];",true,false,
      [this,work](bool ok,const std::string& json){
      if(!LocatorNormalizationCurrent(work))return;
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
      const auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote){
        const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;
        const auto exception=details?details->GetDictionary("exception"):nullptr;
        const auto error=exception?suitecut::DevToolsString(exception->GetValue("description")):result?result->GetString("message").ToString():"";
        Complete(work->owner,false,error.empty()?"Native locator normalization selected-node execution failed":error.substr(0,4096));return;
      }
      if(remote->GetType("objectId")!=VTYPE_STRING){locator_waits_[work->owner]="element to be attached before locator normalization";RetryContext(work->owner);return;}
      if(remote->GetString("subtype")!="node"){Complete(work->owner,false,"Native locator normalization selected value is not a DOM node");return;}
      CaptureLocatorNormalization(work,remote->GetString("objectId").ToString());
    });
  },true);
}
