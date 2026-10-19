// SPDX-License-Identifier: MIT
// Included by Source and the controlled Source sender host. No timeout text is
// classified here. Only this native admission/clock can issue the typed fault.
suitecut::AssertionDeadlineControls assertion_deadlines_;
std::map<int,SnapshotReferenceDocument> assertion_deadline_documents_;
bool AssertionDeadlineAdmit(int id,uint64_t generation,const std::string& matcher,const SnapshotReferenceDocument& document){
  const auto entry=operations_.Get(id);
  if(!entry||entry->generation!=generation||!entry->deadline_ms)return true;
  if(!assertion_deadlines_.Admit(id,generation,entry->deadline_ms,matcher)){Complete(id,false,"Native assertion deadline admission rejected");return false;}
  assertion_deadline_documents_.emplace(id,document);
  if(!SelectorWaits().Admit(id,generation)||!ReferenceTemporaryGroups(id)){Complete(id,false,"Native assertion physical ownership unavailable");return false;}
  return true;
}
// Actual Evaluate dispatch boundary, after WithFrame has a real utility context.
// Context setup alone never supplies a typed no-observation assertion receipt.
bool AssertionDeadlineDispatch(int id,uint64_t generation,const std::string& matcher,const std::string& frame){
  if(!operations_.IsCurrent(id,generation))return false;
  if(assertion_deadlines_.Get(id,generation))return !AssertionDeadlineExpire(id,generation);
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out before native assertion query dispatch");return false;}
  const auto document=ReferenceDocument(frame);
  if(!document){Complete(id,false,"Native assertion document identity unavailable");return false;}
  return AssertionDeadlineAdmit(id,generation,matcher,*document);
}
bool AssertionDeadlineCurrent(int id,uint64_t generation){
  const auto pinned=assertion_deadline_documents_.find(id);const auto* work=assertion_deadlines_.Get(id,generation);
  if(!work||!operations_.IsCurrent(id,generation))return false;
  const auto actual=pinned==assertion_deadline_documents_.end()?std::optional<SnapshotReferenceDocument>{}:ReferenceDocument(pinned->second.frame);
  return actual&&*actual==pinned->second;
}
void AssertionDeadlineReleaseFailed(int id){
  if(const auto entry=operations_.Get(id);entry&&assertion_deadlines_.Get(id,entry->generation))AssertionDeadlineFailure(id,entry->generation,"Native selector wait object-group release failed");
}
void AssertionDeadlineFailure(int id,uint64_t generation,const std::string& error){
  if(assertion_deadlines_.Fail(id,generation,error,Now())){SelectorWaits().Pause(id);WakeDeadlines();}
}
bool AssertionDeadlineExpire(int id,uint64_t generation){
  const auto* work=assertion_deadlines_.Get(id,generation);if(!work)return false;
  if(!operations_.IsCurrent(id,generation))return true;
  if(!AssertionDeadlineCurrent(id,generation)){AssertionDeadlineFailure(id,generation,"Native assertion document changed");return true;}
  if(!work->frozen&&!operations_.IsExpired(id,generation,Now()))return false;
  if(!work->frozen&&expect_results_.contains(id)){CompleteExpiredExpectation(id,generation);return true;}
  if(!work->frozen&&work->admitted){AssertionDeadlineFailure(id,generation,"Native assertion result publication exceeded its original deadline");return true;}
  if(assertion_deadlines_.Freeze(id,generation,Now())){SelectorWaits().Pause(id);first_observations_.erase(id);WakeDeadlines();}
  return true;
}
// The ordinary Complete path may genuinely defer on network-publication ACKs.
// Recheck pins/original clock there; a predeadline admitted success is never
// relabeled as a no-observed-value deadline when its later publication stalls.
bool AssertionDeadlinePublicationCurrent(int id){
  const auto operation=operations_.Get(id);
  if(!operation||!assertion_deadlines_.Get(id,operation->generation))return true;
  if(!AssertionDeadlineCurrent(id,operation->generation)){AssertionDeadlineFailure(id,operation->generation,"Native assertion document changed");return false;}
  return expect_results_.contains(id)||!AssertionDeadlineExpire(id,operation->generation);
}
bool AssertionDeadlineScalar(CefRefPtr<CefValue> value)const{
  const auto list=value&&value->GetType()==VTYPE_LIST?value->GetList():nullptr;if(!list||!list->GetSize()||list->GetType(0)!=VTYPE_STRING)return false;
  const auto tag=list->GetString(0).ToString();
  if(tag=="undefined"||tag=="null")return list->GetSize()==1;
  if(list->GetSize()!=2)return false;
  if(tag=="primitive")return list->GetType(1)==VTYPE_STRING||list->GetType(1)==VTYPE_BOOL||list->GetType(1)==VTYPE_INT||list->GetType(1)==VTYPE_DOUBLE;
  if(tag=="number")return list->GetType(1)==VTYPE_STRING&&(list->GetString(1)=="NaN"||list->GetString(1)=="-0"||list->GetString(1)=="Infinity"||list->GetString(1)=="-Infinity");
  if(tag!="utf16"||list->GetType(1)!=VTYPE_STRING)return false;
  const auto text=list->GetString(1).ToString();return text.size()%4==0&&text.find_first_not_of("0123456789abcdef")==std::string::npos;
}
// A late response can validate its actual protocol/schema, but its DOM value
// is never admitted or converted into Received after the original clock.
bool AssertionDeadlineObserve(int id,uint64_t generation,bool ok,const std::string& json){
  if(!assertion_deadlines_.Get(id,generation))return true;
  if(!AssertionDeadlineCurrent(id,generation)){AssertionDeadlineFailure(id,generation,"Native assertion document changed");return false;}
  const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;
  if(!ok||!result){AssertionDeadlineFailure(id,generation,"Native assertion protocol response failed: "+json.substr(0,2048));return false;}
  if(result->HasKey("exceptionDetails")){
    const auto details=result->GetDictionary("exceptionDetails");const auto exception=details?details->GetDictionary("exception"):nullptr;
    const auto error=exception?suitecut::DevToolsString(exception->GetValue("description")):details?suitecut::DevToolsString(details->GetValue("text")):std::string{};
    AssertionDeadlineFailure(id,generation,error.empty()?"Native assertion renderer exception":error);return false;
  }
  const auto remote=result->GetDictionary("result");const auto value=remote?remote->GetDictionary("value"):nullptr;
  if(value&&value->GetType("strictError")==VTYPE_STRING){AssertionDeadlineFailure(id,generation,value->GetString("strictError").ToString());return false;}
  if(remote&&remote->GetString("type")=="object"&&value&&value->GetType("waiting")==VTYPE_BOOL&&value->GetBool("waiting")){
    assertion_deadlines_.Validated(id,generation);return true;
  }
  const auto observed=value?value->GetDictionary("value"):nullptr;
  if(!remote||remote->GetString("type")!="object"||!value||value->GetType("waiting")!=VTYPE_BOOL||value->GetBool("waiting")||!observed||observed->GetType("matches")!=VTYPE_BOOL||observed->GetType("count")!=VTYPE_INT||observed->GetInt("count")<0||!AssertionDeadlineScalar(observed->GetValue("actual"))||(observed->HasKey("preview")&&observed->GetType("preview")!=VTYPE_STRING)){
    AssertionDeadlineFailure(id,generation,"Invalid native assertion observation schema");return false;
  }
  assertion_deadlines_.Validated(id,generation);return true;
}
void AssertionDeadlineForget(int id){
  const auto* entry=assertion_deadlines_.Get(id);if(entry)assertion_deadlines_.Cancel(id,entry->generation);
  assertion_deadline_documents_.erase(id);
}
void AssertionDeadlineTick(){
  for(const int id:assertion_deadlines_.Owners()){
    const auto operation=operations_.Get(id);if(!operation||!assertion_deadlines_.Get(id,operation->generation)){AssertionDeadlineForget(id);continue;}
    if(!AssertionDeadlineExpire(id,operation->generation))continue;
    const auto* current=assertion_deadlines_.Get(id,operation->generation);if(!current)continue;
    const auto verdict=assertion_deadlines_.Ready(id,operation->generation,SelectorWaits().Ready(id));
    if(verdict==suitecut::AssertionDeadlineControls::Verdict::Hold){
      if(assertion_deadlines_.Watchdog(id,operation->generation,Now())){NetworkFatal("Native assertion physical retirement deadline exceeded");Complete(id,false,"Native assertion physical retirement deadline exceeded");}
      continue;
    }
    const auto work=*current;
    if(verdict==suitecut::AssertionDeadlineControls::Verdict::HardFailure){Complete(id,false,work.error.empty()?"Native operation timed out before native assertion validation":work.error);continue;}
    auto fault=CefDictionaryValue::Create();fault->SetString("type","native-assertion-deadline");fault->SetInt("version",1);fault->SetInt("operationId",id);fault->SetString("generation",std::to_string(work.generation));fault->SetString("matcher",work.matcher);fault->SetDouble("deadlineMs",work.deadline);fault->SetBool("observed",false);
    Complete(id,false,"Native assertion original deadline expired without an admitted value",Json(fault));
  }
}
