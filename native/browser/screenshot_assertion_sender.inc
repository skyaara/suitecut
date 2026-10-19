// SPDX-License-Identifier: MIT
// Included inside Source and the controlled native sender host. This sender is
// used ONLY for native screenshot assertion work, never arbitrary public CDP.
void ScreenshotAssertionSubmit(suitecut::ScreenshotAssertionRetirement::Identity identity,
    const std::string&frame,const std::string&session,const std::string&method,Dict params,
    SuiteCutArtifactController::ResponseStage stage,bool cleanup,
    std::function<void(bool,bool,const std::string&)> done,uint64_t capture_sample=0,uint64_t capture_generation=0){
  using Policy=suitecut::ScreenshotAssertionRetirement;
  const auto reject=[&done]{done(false,false,"{}");};
  const auto entry=screenshot_assertion_retirement_.Get(identity);
  if(!entry||!browser_||(!session.empty()&&!sessions_.contains(session))||!ScreenshotAssertionPinsCurrent(identity)||!ScreenshotAssertionRememberFrame(identity,frame)){
    if(entry)screenshot_assertion_retirement_.Fail(identity,"Native screenshot document or channel changed before dispatch",Now());
    reject();return;
  }
  if(cleanup&&!entry->frozen&&entry->original_deadline&&Now()>=entry->original_deadline){ScreenshotAssertionOriginalDeadline(identity);}
  if(!cleanup){
    // A continuation whose original clock just ended cannot start another
    // render attempt. The comparator's native freeze owns its final reply.
    if(entry->frozen){if(stage==SuiteCutArtifactController::ResponseStage::PNG)reject();return;}
    if(entry->original_deadline&&Now()>=entry->original_deadline){ScreenshotAssertionOriginalDeadline(identity);if(stage==SuiteCutArtifactController::ResponseStage::PNG)reject();return;}
    if(!operations_.IsCurrent(identity.owner,identity.generation)){screenshot_assertion_retirement_.Cancel(identity,"Native screenshot logical owner ended",Now());reject();return;}
  }
  const int tool=AllocateDevToolsId();if(!tool){screenshot_assertion_retirement_.Fail(identity,"Native screenshot command capacity unavailable",Now());reject();return;}
  auto ticket=screenshot_assertion_retirement_.Begin(identity,cleanup?Policy::Work::Restoration:Policy::Work::Renderer,tool,session,Now());
  if(!ticket){screenshot_assertion_retirement_.Fail(identity,"Native screenshot physical admission unavailable",Now());reject();return;}
  std::shared_ptr<suitecut::SelectorWaitControls::Physical> selector;
  try{selector=ScreenshotAssertionSelectorPhysical(identity,params,session,cleanup);}catch(const std::bad_alloc&){
    screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot selector allocation failed before submission",Now());reject();return;
  }
  if(!cleanup&&!selector){screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);screenshot_assertion_retirement_.Fail(identity,"Native screenshot selector physical admission unavailable",Now());reject();return;}
  struct Settlement{bool done=false;std::function<void(bool,bool,const std::string&)>callback;};std::shared_ptr<Settlement> settlement;
  try{settlement=std::make_shared<Settlement>();}catch(const std::bad_alloc&){
    screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);if(selector)selector->Settle();
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot receipt allocation failed before submission",Now());reject();return;
  }
  if((capture_sample||capture_generation)&&
     (cleanup||stage!=SuiteCutArtifactController::ResponseStage::PNG||!ScreenshotAssertionCaptureEnter(identity,capture_sample,capture_generation,tool,frame,session))){
    screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);if(selector)selector->Settle();
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot exact capture admission rejected",Now());reject();return;
  }
  settlement->callback=std::move(done);
  const auto finish=[this,identity,stage,cleanup,selector,ticket=*ticket,settlement,capture_sample](bool submitted,bool ok,const std::string&json){
    if(settlement->done)return;
    settlement->done=true;
    if(submitted&&stage==SuiteCutArtifactController::ResponseStage::PNG)screenshot_assertion_retirement_.CaptureSubmitted(identity);
    const auto observation=suitecut::ObserveScreenshotResponse(stage,ok,json);
    if(!observation.valid)screenshot_assertion_retirement_.Fail(identity,observation.error,Now());
    const bool pins_current=ScreenshotAssertionPinsCurrent(identity);
    if(!pins_current)screenshot_assertion_retirement_.Fail(identity,"Native screenshot document changed at actual renderer receipt",Now());
    if(capture_sample){
      if(submitted)ScreenshotAssertionCaptureReceipt(identity,capture_sample,ticket.command,ticket.session,true,observation.valid&&pins_current);
      else ScreenshotAssertionCaptureRejected(identity,capture_sample,ticket.command);
    }
    const auto current=screenshot_assertion_retirement_.Get(identity);
    if(current&&!current->frozen&&current->original_deadline&&Now()>=current->original_deadline)ScreenshotAssertionOriginalDeadline(identity);
    const auto entry=screenshot_assertion_retirement_.Get(identity);
    // A late ordinary continuation could start a direct native view lease or
    // other non-CDP work. Validate its actual response but never run it after
    // freeze. The PNG callback alone must always retire its capture latch;
    // cleanup callbacks acknowledge only their own native restoration work.
    if(cleanup||stage==SuiteCutArtifactController::ResponseStage::PNG||
       (entry&&!entry->frozen&&pins_current)){
      try{settlement->callback(submitted,ok&&observation.valid&&pins_current,json);}catch(...){screenshot_assertion_retirement_.Fail(identity,"Native screenshot response consumer failed",Now());}
    }
    if(selector)selector->Settle();
    if(submitted)screenshot_assertion_retirement_.Acknowledge(ticket,observation.valid,observation.error,Now());
    else if(!screenshot_assertion_retirement_.WithdrawUnsubmitted(ticket))screenshot_assertion_retirement_.Acknowledge(ticket,false,observation.error,Now());
    ScreenshotAssertionReceiptFinished(identity);WakeDeadlines();
  };
  try{
    const auto [position,inserted]=devtools_.emplace(tool,DevToolsCall{identity.owner,
      [finish](bool ok,const std::string&json){finish(true,ok,json);},
      session,true,kMaxResponseBytes,true,true});
    (void)position;
    if(!inserted){finish(false,false,"{}");return;}
  }catch(const std::bad_alloc&){
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot callback allocation failed before submission",Now());finish(false,false,"{}");return;
  }
  const auto rollback=[this,tool,finish]{const auto found=devtools_.find(tool);if(found!=devtools_.end())devtools_.erase(found);finish(false,false,"{}");};
  if(!ScreenshotAssertionPinsCurrent(identity)||(!cleanup&&!operations_.IsCurrent(identity.owner,identity.generation))){rollback();return;}
  const auto dispatch=screenshot_assertion_retirement_.Get(identity);
  if(!dispatch){rollback();return;}
  if(!cleanup&&dispatch->original_deadline&&Now()>=dispatch->original_deadline){
    devtools_.erase(tool);ScreenshotAssertionOriginalDeadline(identity);
    // No renderer accepted this request. Settle only the local reservation;
    // don't validate an invented raw response or manufacture a physical ACK.
    screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);if(selector)selector->Settle();
    if(capture_sample)ScreenshotAssertionCaptureRejected(identity,capture_sample,tool);
    if(stage==SuiteCutArtifactController::ResponseStage::PNG){
      // The producer acquired a capture latch before asking for submission.
      // It needs an explicit no-submit observation, not a dropped callback.
      // finish is not used because no actual protocol response exists.
      settlement->done=true;try{settlement->callback(false,false,"{}");}catch(...){screenshot_assertion_retirement_.Fail(identity,"Native screenshot no-submit consumer failed",Now());}
    }
    ScreenshotAssertionReceiptFinished(identity);WakeDeadlines();return;
  }
  if(cleanup&&dispatch->frozen&&Now()>=dispatch->cleanup_deadline){rollback();return;}
  // Same-thread synchronous ACKs may occur during SubmitDevTools. Their actual
  // receipt wins over a later false return; Settlement makes both paths unique.
  if(!screenshot_assertion_retirement_.EnterSubmission(*ticket)){rollback();return;}
  if(!SubmitDevTools(tool,method,params,session))rollback();
  else if(stage==SuiteCutArtifactController::ResponseStage::PNG){
    screenshot_assertion_retirement_.CaptureSubmitted(identity);
    if(capture_sample)ScreenshotAssertionCaptureReceipt(identity,capture_sample,tool,session,false,true);
  }
  WakeDeadlines();
}
