// SPDX-License-Identifier: MIT
// Native structured snapshots retain their selected semantic capture and exact
// document through actual result publication; no YAML parse or author callback.
struct AriaJSONWork {
  int owner=0;uint64_t generation=0;Dict config;
  std::optional<SnapshotReferenceDocument> document;
};
std::set<int> aria_json_operations_;
std::map<int,std::shared_ptr<AriaJSONWork>> aria_json_work_;
bool AriaJSONCurrent(std::shared_ptr<AriaJSONWork> work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  const auto active=aria_json_work_.find(work->owner);if(active==aria_json_work_.end()||active->second!=work)return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"ariaSnapshotJSON: Native operation timed out");return false;}
  if(work->document){const auto document=ReferenceDocument(work->document->frame);if(!document||*document!=*work->document){Complete(work->owner,false,"Native structured accessibility document changed");return false;}}
  return true;
}
void ReadAriaJSON(const std::shared_ptr<AriaJSONWork>& work,const std::string& capture){
  if(!AriaJSONCurrent(work))return;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",capture);
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));params->SetBool("returnByValue",true);
  params->SetString("functionDeclaration","function(){if(this.scopeCount===0)return {count:0};if(this.scopeCount!==1)throw new Error('strict mode violation: native structured snapshot scope');return {count:1,json:JSON.stringify(("+std::string(kSuiteCutNativeAriaJSON)+")(this.forest,{boxes:"+std::string(work->config->GetBool("accessibilityBoxes")?"true":"false")+(work->config->HasKey("snapshotDepth")?",depth:"+std::to_string(work->config->GetInt("snapshotDepth")):std::string())+"}))};}");
  DevTools(work->owner,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){
    if(!AriaJSONCurrent(work))return;
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
    const auto remote=result?result->GetDictionary("result"):nullptr;const auto value=remote?remote->GetDictionary("value"):nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")||!value||value->GetType("count")!=VTYPE_INT){
      const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;const auto exception=details?details->GetDictionary("exception"):nullptr;
      Complete(work->owner,false,exception?suitecut::DevToolsString(exception->GetValue("description")):"Native structured accessibility snapshot unavailable");return;
    }
    if(value->GetInt("count")==0){aria_json_work_.erase(work->owner);RetryContext(work->owner);return;}
    if(value->GetInt("count")!=1||value->GetType("json")!=VTYPE_STRING){Complete(work->owner,false,"Native structured accessibility result is invalid");return;}
    const auto snapshot=value->GetString("json").ToString();
    const auto valid=snapshot.size()<=65536?CefParseJSON(suitecut::StorageJSONValidationProjection(snapshot),JSON_PARSER_RFC):nullptr;
    if(!valid||valid->GetType()!=VTYPE_LIST){Complete(work->owner,false,"Native structured accessibility JSON exceeds bounds or is invalid");return;}
    auto response=CefDictionaryValue::Create();response->SetString("snapshotJSON",snapshot);
    if(AriaJSONCurrent(work))Complete(work->owner,true,"",Json(response));
  },work->document->session,false,1048576,true);
}
void AriaJSONAction(int id,uint64_t generation,Dict config){
  if(!operations_.IsCurrent(id,generation))return;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"ariaSnapshotJSON: Native operation timed out");return;}
  aria_json_operations_.insert(id);
  if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native structured accessibility physical capacity exceeded");return;}
  if(config->HasKey("nativeHandle")){Complete(id,false,"Invalid native structured snapshot root");return;}
  if((config->HasKey("accessibilityBoxes")&&config->GetType("accessibilityBoxes")!=VTYPE_BOOL)||
     (config->HasKey("snapshotDepth")&&(config->GetType("snapshotDepth")!=VTYPE_INT||config->GetInt("snapshotDepth")<0||config->GetInt("snapshotDepth")>64))){Complete(id,false,"Invalid native structured accessibility options");return;}
  if(!PrepareSnapshotReferenceScope(id,config))return;
  if(const auto path=config->GetList("framePath");path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){AriaJSONAction(id,generation,resolved);});return;
  }
  auto work=std::make_shared<AriaJSONWork>();work->owner=id;work->generation=generation;work->config=config;aria_json_work_[id]=work;
  WithFrame(id,FrameId(config),[this,work](int){
    if(!AriaJSONCurrent(work))return;
    work->document=ReferenceDocument(FrameId(work->config));if(!work->document){Complete(work->owner,false,"Native structured accessibility document unavailable");return;}
    CaptureDOMSemantic(work->owner,work->config,[](const std::string&,const std::string&){},[this,work](const std::string& capture){if(AriaJSONCurrent(work))ReadAriaJSON(work,capture);});
  },true);
}
