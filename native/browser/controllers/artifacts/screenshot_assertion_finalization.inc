// SPDX-License-Identifier: MIT
// Source-owned final publication. Every producer receipt remains independent of
// the logical operation, including successful comparisons frozen before cleanup.
struct ScreenshotAssertionWork {
  suitecut::ScreenshotAssertionRetirement::Identity identity;
  std::string input_token,artifact_root,error,json;
  Dict result;
  std::shared_ptr<const suitecut::ScreenshotArtifactPublication>publication;
  std::shared_ptr<suitecut::ScreenshotByteBudget::Lease>bytes;
  bool frozen=false,processing=false,publishing=false,files_started=false;
};
struct ScreenshotAssertionFileWork {
  suitecut::ScreenshotAssertionRetirement::Identity identity;
  suitecut::ScreenshotAssertionRetirement::Ticket physical;
};
std::map<int,std::shared_ptr<ScreenshotAssertionWork>>screenshot_assertion_work_;
std::map<uint64_t,ScreenshotAssertionFileWork>screenshot_assertion_files_;
bool ScreenshotAssertionOwns(int owner)const{return screenshot_assertion_work_.contains(owner);}
bool ScreenshotAssertionStart(int owner,uint64_t generation,double deadline,const std::string&input_token,Dict config){
  if(input_token.empty()||input_token.size()>128||!config||screenshot_assertion_work_.contains(owner))return false;
  const auto root=config->GetType("artifactsDir");
  if(root!=VTYPE_INVALID&&root!=VTYPE_STRING)return false;
  const auto screenshot=config->GetDictionary("screenshot");
  if(!screenshot||screenshot->HasKey("beforeScreenshot")||screenshot->HasKey("__testHookBeforeScreenshot")||screenshot->HasKey("clientToken"))return false;
  const auto path=config->GetString("artifactsDir").ToString();
  if(path.size()>32768||(!path.empty()&&(path[0]!='/'||path.find('\0')!=std::string::npos)))return false;
  try{
    auto work=std::make_shared<ScreenshotAssertionWork>();work->identity={page_id_,owner,generation};work->input_token=input_token;work->artifact_root=path;
    screenshot_assertion_work_.emplace(owner,std::move(work));
    if(!ScreenshotAssertionScopeBegin(owner,generation,deadline)){screenshot_assertion_work_.erase(owner);return false;}
    if(!ScreenshotAssertionPrepared({page_id_,owner,generation})){
      screenshot_assertion_retirement_.Fail({page_id_,owner,generation},"Native screenshot prepared owner rejected",Now());return false;
    }
    return true;
  }catch(const std::bad_alloc&){return false;}
}
std::shared_ptr<suitecut::ScreenshotComparisons::AssertionObserver> ScreenshotAssertionObserver(suitecut::ScreenshotAssertionRetirement::Identity identity){
  auto observer=std::make_shared<suitecut::ScreenshotComparisons::AssertionObserver>();
  const auto lifetime=CefRefPtr<Source>(this);
  observer->admit=[this,identity,lifetime](uint64_t job,uint64_t sample,suitecut::ScreenshotComparisons::AssertionWork kind){
    (void)lifetime;
    using Compare=suitecut::ScreenshotComparisons;
    if(kind!=Compare::AssertionWork::Files)return ScreenshotAssertionPixelsBegin(identity,sample,job,kind==Compare::AssertionWork::Validation);
    const auto entry=screenshot_assertion_retirement_.Get(identity);
    if(!entry||!entry->frozen||entry->canceled||!entry->error.empty()||!entry->observed||!ScreenshotAssertionPinsCurrent(identity)||screenshot_assertion_files_.contains(job))return false;
    auto ticket=screenshot_assertion_retirement_.Begin(identity,suitecut::ScreenshotAssertionRetirement::Work::Files,0,"",Now());
    if(!ticket)return false;
    try{const bool inserted=screenshot_assertion_files_.emplace(job,ScreenshotAssertionFileWork{identity,*ticket}).second;if(!inserted)screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);return inserted;}catch(const std::bad_alloc&){screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);return false;}
  };
  observer->submitting=[this,identity,lifetime](uint64_t job){
    (void)lifetime;
    const auto file=screenshot_assertion_files_.find(job);
    if(file!=screenshot_assertion_files_.end())return file->second.identity==identity&&screenshot_assertion_retirement_.EnterSubmission(file->second.physical);
    return ScreenshotAssertionPixelsSubmitted(identity,job);
  };
  observer->observed=[this,identity,lifetime](uint64_t job){
    (void)lifetime;return ScreenshotAssertionPixelsObserved(identity,job);};
  observer->unobserved=[this,identity,lifetime](uint64_t sample,Dict actual){
    (void)lifetime;
    const auto entry=screenshot_assertion_retirement_.Get(identity);
    if(!entry||!entry->frozen||entry->canceled||!entry->error.empty()){ScreenshotAssertionReceiptFinished(identity);return;}
    std::string error;
    if(!engine_.screenshot_comparisons()->ValidateCaptured(network_context_id_,page_id_,identity.owner,sample,actual,entry->cleanup_deadline,ScreenshotAssertionObserver(identity),error))
      screenshot_assertion_retirement_.Fail(identity,error.empty()?"Native received capture validation admission failed":error,Now());
    ScreenshotAssertionReceiptFinished(identity);
  };
  observer->settled=[this,identity,lifetime](uint64_t job,bool submitted,bool valid,const std::string&error){
    (void)lifetime;
    const auto file=screenshot_assertion_files_.find(job);
    if(file==screenshot_assertion_files_.end()){ScreenshotAssertionPixelsSettled(identity,job,submitted,valid,error);return;}
    if(file->second.identity!=identity)return;
    const auto ticket=file->second.physical;screenshot_assertion_files_.erase(file);
    if(!error.empty())screenshot_assertion_retirement_.Fail(identity,error,Now());
    if(!error.empty())engine_.screenshot_comparisons()->Cancel(page_id_,identity.owner);
    if(submitted)screenshot_assertion_retirement_.Acknowledge(ticket,valid,error,Now());
    else if(!screenshot_assertion_retirement_.WithdrawUnsubmitted(ticket))screenshot_assertion_retirement_.Acknowledge(ticket,false,error.empty()?"Native screenshot file submission rejected":error,Now());
    ScreenshotAssertionReceiptFinished(identity);
  };
  return observer;
}
void ScreenshotAssertionFreeze(suitecut::ScreenshotAssertionRetirement::Identity identity,bool ok,Dict result,const std::string&error){
  const auto found=screenshot_assertion_work_.find(identity.owner);if(found==screenshot_assertion_work_.end()||found->second->identity!=identity)return;
  const auto work=found->second;
  if(!error.empty())screenshot_assertion_retirement_.Fail(identity,error,Now());
  if(!ok||!error.empty())engine_.screenshot_comparisons()->Cancel(page_id_,identity.owner);
  if(!work->frozen){
    work->frozen=true;
    if(ok&&result){
      const auto actual=result->GetString("actual").ToString(),diff=result->GetString("diff").ToString();
      const size_t retained=6*(actual.size()+diff.size())+65536;
      try{work->bytes=engine_.screenshot_comparisons()->Budget()->Reserve(retained);}catch(const std::bad_alloc&){}
      if(!work->bytes)screenshot_assertion_retirement_.Fail(identity,"Native screenshot final publication byte budget exceeded",Now());
      else work->result=result;
    }
    if(!ok)screenshot_assertion_retirement_.Fail(identity,error.empty()?"Native screenshot comparison failed":error,Now());
    screenshot_assertion_retirement_.Freeze(identity,ok&&result&&result->GetBool("passed"),Now());
    // Remove the logical capture first. Its native cleanup plan and actual
    // capture callback survive independently of this map/job transition.
    if(artifacts_)artifacts_->Cancel(identity.owner);
    try{if(selector_waits_&&selector_waits_->Contains(identity.owner))selector_waits_->Pause(identity.owner);}catch(const std::bad_alloc&){screenshot_assertion_retirement_.Fail(identity,"Native screenshot group-release admission allocation failed",Now());NetworkFatal("Native screenshot group-release admission allocation failed");}
    context_retries_.erase(identity.owner);context_retry_queued_.erase(identity.owner);
  }
  ScreenshotAssertionReceiptFinished(identity);WakeDeadlines();
}
void ScreenshotAssertionOriginalDeadline(suitecut::ScreenshotAssertionRetirement::Identity identity){
  if(engine_.screenshot_comparisons()->Contains(page_id_,identity.owner))engine_.screenshot_comparisons()->Deadline(page_id_,identity.owner);
  else ScreenshotAssertionFreeze(identity,true,nullptr,"");
}
void ScreenshotAssertionReleaseFailed(int owner){
  const auto scope=screenshot_assertion_work_.find(owner);if(scope==screenshot_assertion_work_.end())return;
  ScreenshotAssertionFreeze(scope->second->identity,false,nullptr,"Actual native screenshot object-group release failed");
}
// The Complete intercept leaves original operations admitted while actual raw
// captures, restore/paint/group ACKs, decoder/IO and UI releases are pending.
bool ScreenshotAssertionCompleteRequested(int owner,bool ok,const std::string&error,const std::string&json){
  const auto found=screenshot_assertion_work_.find(owner);if(found==screenshot_assertion_work_.end())return false;
  const auto work=found->second;
  if(work->publishing){
    const auto entry=screenshot_assertion_retirement_.Get(work->identity);
    if(ok&&(!ScreenshotAssertionPinsCurrent(work->identity)||(entry&&entry->requested==suitecut::ScreenshotAssertionRetirement::Outcome::Passed&&entry->original_deadline&&Now()>=entry->original_deadline))){
      work->publishing=false;work->json.clear();screenshot_assertion_retirement_.Fail(work->identity,"Native screenshot document or original deadline changed at actual publication",Now());
    }else if(ok||error==work->error)return false;
    else{work->publishing=false;work->json.clear();}
  }
  if(!ok){
    engine_.screenshot_comparisons()->Cancel(page_id_,owner);
    ScreenshotAssertionFreeze(work->identity,false,nullptr,error);return true;
  }
  auto parsed=json.empty()?nullptr:CefParseJSON(json,JSON_PARSER_RFC);
  ScreenshotAssertionFreeze(work->identity,true,parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr,"");return true;
}
bool ScreenshotAssertionCaptureResult(int owner,uint64_t generation,bool ok,const std::string&json){
  const auto found=screenshot_assertion_work_.find(owner);if(found==screenshot_assertion_work_.end())return false;
  const auto work=found->second;
  const auto sample=screenshot_comparison_samples_.find(owner);
  if(sample==screenshot_comparison_samples_.end()||!ScreenshotAssertionCaptureDeliverable(work->identity,sample->second.sample,generation))return true;
  const auto serial=sample->second.sample;
  const auto parsed=ok?CefParseJSON(json,JSON_PARSER_RFC):nullptr;
  const auto result=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;
  const auto entry=screenshot_assertion_retirement_.Get(work->identity);
  if(!ok||!result){ScreenshotAssertionFreeze(work->identity,false,nullptr,"Actual native screenshot capture failed");}
  else if(entry&&entry->frozen){
    if(entry->error.empty()&&!entry->canceled){
      std::string error;
      if(!engine_.screenshot_comparisons()->ValidateCaptured(network_context_id_,page_id_,owner,serial,result,entry->cleanup_deadline,ScreenshotAssertionObserver(work->identity),error))
        ScreenshotAssertionFreeze(work->identity,false,nullptr,error.empty()?"Native late capture validation rejected":error);
    }
  }else{
    if(artifacts_)artifacts_->Cancel(owner);
    engine_.screenshot_comparisons()->Captured(page_id_,owner,serial,ok,"",result);
  }
  ScreenshotAssertionCaptureDelivered(work->identity,serial);
  ScreenshotAssertionReceiptFinished(work->identity);return true;
}
void ScreenshotAssertionReceiptFinished(suitecut::ScreenshotAssertionRetirement::Identity identity){
  using Policy=suitecut::ScreenshotAssertionRetirement;
  const auto found=screenshot_assertion_work_.find(identity.owner);if(found==screenshot_assertion_work_.end()||found->second->identity!=identity)return;
  const auto work=found->second;if(work->processing)return;work->processing=true;
  struct Reset{bool&value;~Reset(){value=false;}}reset{work->processing};
  const auto entry=screenshot_assertion_retirement_.Get(identity);if(!entry)return;
  // A retained renderer sender can discover a protocol/schema failure while
  // suppressing its ordinary continuation. Start native restoration now; a
  // logical map must not wait for the physical watchdog to notice that fault.
  if(entry->frozen&&!work->frozen)ScreenshotAssertionFreeze(identity,false,nullptr,entry->error.empty()?"Native screenshot renderer work failed":entry->error);
  if(!work->frozen)return;
  if(!ScreenshotAssertionPinsCurrent(identity))ScreenshotAssertionFreeze(identity,false,nullptr,"Native screenshot document changed during physical retirement");
  if(Now()>=entry->cleanup_deadline)ScreenshotAssertionFreeze(identity,false,nullptr,"Native screenshot original cleanup deadline expired before publication");
  if(!work->files_started){
    work->files_started=true;
    if(work->result&&!work->result->GetBool("passed")&&entry->observed&&entry->error.empty()&&!entry->canceled&&!work->artifact_root.empty()){
      std::string error;
      const bool accepted=engine_.screenshot_comparisons()->WriteArtifacts(network_context_id_,page_id_,identity.owner,work->artifact_root,work->result->GetString("actual").ToString(),work->result->GetString("diff").ToString(),entry->cleanup_deadline,ScreenshotAssertionObserver(identity),
        [this,work](std::shared_ptr<const suitecut::ScreenshotArtifactPublication> publication){
          work->publication=publication;
          if(publication->committed&&work->result){
            if(!work->result->SetString("actualPath",publication->actual_path)||(!publication->diff_path.empty()&&!work->result->SetString("diffPath",publication->diff_path)))
              screenshot_assertion_retirement_.Fail(work->identity,"Native committed screenshot artifact path publication failed",Now());
          }
          if(!publication->committed||publication->cleanup_failed)screenshot_assertion_retirement_.Fail(work->identity,publication->error.empty()?"Actual native screenshot artifact publication failed":publication->error,Now());
        },error);
      if(!accepted)screenshot_assertion_retirement_.Fail(identity,error.empty()?"Native screenshot file admission rejected":error,Now());
    }
  }
  bool capture_pending=false;for(const auto&[key,capture]:screenshot_assertion_captures_){(void)key;if(capture.identity==identity&&capture.accepted&&(!capture.delivered||(capture.received&&capture.valid&&!capture.used&&entry->error.empty()&&!entry->canceled)))capture_pending=true;}
  const bool groups_ready=!selector_waits_||!selector_waits_->Contains(identity.owner)||selector_waits_->Ready(identity.owner);
  if(!entry->cleanup_closed&&!capture_pending&&!screenshot_assertion_retirement_.Physical(identity,Policy::Work::Renderer)&&!screenshot_assertion_cleanup_.contains(identity.owner)&&!screenshot_assertion_views_.contains(identity.owner)&&
      (!artifacts_||(!artifacts_->CapturePhysicalOwned(identity.owner)&&!artifacts_->RestorationPhysicalOwned(identity.owner)))&&groups_ready)
    screenshot_assertion_retirement_.CleanupClosed(identity);
  const auto outcome=screenshot_assertion_retirement_.Ready(identity,Now());
  if(outcome==Policy::Outcome::Pending)return;
  if(!work->publishing){
    bool ok=false;Dict payload;
    if(outcome==Policy::Outcome::Passed||outcome==Policy::Outcome::ObservedFailure){
      if(!work->result){screenshot_assertion_retirement_.Fail(identity,"Native screenshot comparison publication is missing",Now());}
      else{
        payload=work->result;auto receipt=CefDictionaryValue::Create();receipt->SetString("type","native-screenshot-assertion");receipt->SetInt("version",1);receipt->SetInt("operationId",identity.owner);receipt->SetString("generation",std::to_string(identity.generation));receipt->SetString("inputToken",work->input_token);receipt->SetDouble("deadlineMs",entry->original_deadline);receipt->SetString("outcome",outcome==Policy::Outcome::Passed?"passed":"observedMismatch");receipt->SetBool("observed",true);payload->SetDictionary("assertion",receipt);ok=true;
      }
    }else if(outcome==Policy::Outcome::Deadline){
      payload=CefDictionaryValue::Create();payload->SetString("type","native-assertion-deadline");payload->SetInt("version",1);payload->SetInt("operationId",identity.owner);payload->SetString("generation",std::to_string(identity.generation));payload->SetString("matcher","toHaveScreenshot");payload->SetDouble("deadlineMs",entry->original_deadline);payload->SetBool("observed",false);
      work->error="Native screenshot assertion original deadline expired without an admitted pixel comparison";
    }
    if(!ok&&work->error.empty()){
      work->error=entry->error.empty()?"Native screenshot failed before actual baseline/capture validation":entry->error;
      if(work->publication&&work->publication->committed){work->error+="\nCommitted actual: "+work->publication->actual_path;if(!work->publication->diff_path.empty())work->error+="\nCommitted difference: "+work->publication->diff_path;}
    }
    work->json=payload?Json(payload):"";work->publishing=true;
  }
  Complete(identity.owner,work->error.empty(),work->error,work->json);
}
void ScreenshotAssertionFinished(int owner){
  const auto found=screenshot_assertion_work_.find(owner);if(found==screenshot_assertion_work_.end())return;
  const auto identity=found->second->identity;
  if(ScreenshotAssertionScopeRetire(identity))screenshot_assertion_work_.erase(found);
}
void ScreenshotAssertionTick(){
  for(const auto identity:screenshot_assertion_retirement_.Owners()){
    const auto entry=screenshot_assertion_retirement_.Get(identity);if(!entry)continue;
    if(!entry->frozen&&entry->original_deadline&&Now()>=entry->original_deadline)ScreenshotAssertionOriginalDeadline(identity);
    ScreenshotAssertionReceiptFinished(identity);
    if(screenshot_assertion_retirement_.Watchdog(identity,Now()))NetworkFatal("Native screenshot physical retirement original cleanup deadline exceeded");
  }
}
void ScreenshotAssertionChannelClosed(){
  screenshot_assertion_retirement_.ChannelClosed(page_id_,Now());
  if(artifacts_)artifacts_->AssertionChannelClosed();
  ScreenshotAssertionCaptureChannelClosed();
  ScreenshotAssertionCleanupChannelClosed();ScreenshotAssertionViewChannelClosed();
  for(const auto identity:screenshot_assertion_retirement_.Owners())ScreenshotAssertionFreeze(identity,false,nullptr,"Actual native screenshot channel closed");
}
std::string ScreenshotAssertionCommandFrame(int owner,Dict params,const std::string&session)const{
  int context=0;
  if(params&&params->GetType("contextId")==VTYPE_INT)context=params->GetInt("contextId");
  else if(params&&params->GetType("executionContextId")==VTYPE_INT)context=params->GetInt("executionContextId");
  if(context){for(const auto&[id,frame]:frame_states_)if(frame.session==session&&(frame.context==context||frame.utility_context==context))return id;return "";}
  const auto frame=operation_frames_.find(owner);
  return frame==operation_frames_.end()?main_frame_id_:frame->second;
}
bool ScreenshotAssertionRoute(int owner,const std::string&method,Dict params,SuiteCutArtifactController::ResponseStage stage,SuiteCutArtifactController::Callback callback,std::optional<std::string>target_session=std::nullopt){
  const auto found=screenshot_assertion_work_.find(owner);if(found==screenshot_assertion_work_.end())return false;
  const auto identity=found->second->identity;
  const auto session=target_session.value_or(operation_sessions_[owner]);
  const auto frame=ScreenshotAssertionCommandFrame(owner,params,session);
  uint64_t sample=0,generation=0;
  if(stage==SuiteCutArtifactController::ResponseStage::PNG){
    const auto current=screenshot_comparison_samples_.find(owner);
    if(current!=screenshot_comparison_samples_.end())sample=current->second.sample;
    generation=artifacts_?artifacts_->Generation(owner):0;
  }
  ScreenshotAssertionSubmit(identity,frame,session,method,params,stage,false,[callback=std::move(callback)](bool,bool ok,const std::string&json){callback(ok,json);},sample,generation);return true;
}
void ScreenshotAssertionRetryGroups(int owner,uint64_t generation,std::function<void()>done){
  const auto found=screenshot_assertion_work_.find(owner);if(found==screenshot_assertion_work_.end()||found->second->identity.generation!=generation)return;
  const auto entry=screenshot_assertion_retirement_.Get(found->second->identity);
  if(!entry||entry->frozen)return;
  if(entry->original_deadline&&Now()>=entry->original_deadline){ScreenshotAssertionOriginalDeadline(found->second->identity);return;}
  if(SelectorWaits().Ready(owner)){SelectorWaits().Resume(owner);done();return;}
  if(!CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),owner,generation,done=std::move(done)]{self->ScreenshotAssertionRetryGroups(owner,generation,done);}),10))
    ScreenshotAssertionFreeze(found->second->identity,false,nullptr,"Native screenshot selector-release continuation rejected");
}
