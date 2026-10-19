// SPDX-License-Identifier: MIT
// Included inside Source and the controlled native cleanup host. Plans contain
// only actual controller/document resources, never authored command packets.
struct ScreenshotAssertionRestoreCommand {
  std::string frame,session,method;Dict params;
  SuiteCutArtifactController::ResponseStage stage=SuiteCutArtifactController::ResponseStage::Empty;
};
struct ScreenshotAssertionCleanupWork {
  suitecut::ScreenshotAssertionRetirement::Identity identity;
  suitecut::ScreenshotAssertionRetirement::Ticket physical;
  std::vector<ScreenshotAssertionRestoreCommand>commands;
  SuiteCutArtifactController::Callback done;
  size_t next=0;bool waiting=false,view=false,failed=false;
};
std::map<int,std::shared_ptr<ScreenshotAssertionCleanupWork>>screenshot_assertion_cleanup_;
void ScreenshotAssertionCleanupFailed(const std::shared_ptr<ScreenshotAssertionCleanupWork>&work,const std::string&error){
  const auto current=screenshot_assertion_cleanup_.find(work->identity.owner);
  if(current==screenshot_assertion_cleanup_.end()||current->second!=work||work->failed)return;
  work->failed=true;
  screenshot_assertion_retirement_.Fail(work->identity,error,Now());
  // The actual dirty-resource obligation stays held until real channel close.
  // NetworkFatal already posts bounded teardown outside the DevTools observer.
  NetworkFatal(error);
}
bool ScreenshotAssertionCleanupBegin(int owner,SuiteCutArtifactController::AssertionCleanupPlan plan,SuiteCutArtifactController::Callback done){
  using Policy=suitecut::ScreenshotAssertionRetirement;
  const auto scope=screenshot_assertion_scopes_.find(owner);
  if(scope==screenshot_assertion_scopes_.end())return false;
  const auto identity=scope->second.identity;
  auto entry=screenshot_assertion_retirement_.Get(identity);
  if(entry&&!entry->frozen&&entry->original_deadline&&Now()>=entry->original_deadline)ScreenshotAssertionOriginalDeadline(identity);
  entry=screenshot_assertion_retirement_.Get(identity);
  if(!entry||screenshot_assertion_cleanup_.contains(owner)||!ScreenshotAssertionPinsCurrent(identity)){
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot cleanup document or owner changed",Now());NetworkFatal("Native screenshot cleanup document or owner changed");return true;
  }
  // This is the native dirty-resource obligation, independent of individual
  // renderer commands. A failed/partially submitted plan cannot disappear in
  // the gaps between sequential actual ACKs.
  auto physical=screenshot_assertion_retirement_.Begin(identity,Policy::Work::Cleanup,0,"",Now());
  if(!physical||!screenshot_assertion_retirement_.EnterSubmission(*physical)){
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot cleanup physical capacity unavailable",Now());NetworkFatal("Native screenshot cleanup physical capacity unavailable");return true;
  }
  try{
    auto work=std::make_shared<ScreenshotAssertionCleanupWork>();work->identity=identity;work->physical=*physical;work->done=std::move(done);work->view=plan.physical_view;
    const auto child=artifact_document_work_.find(owner);
    const size_t children=child==artifact_document_work_.end()?0:child->second->resources.size();
    if(plan.root.size()>3||children>1024||(child!=artifact_document_work_.end()&&child->second->generation!=identity.generation)){
      screenshot_assertion_retirement_.Fail(identity,"Native screenshot cleanup resource identity/capacity changed",Now());NetworkFatal("Native screenshot cleanup resource identity/capacity changed");return true;
    }
    work->commands.reserve(children+plan.root.size());
    if(child!=artifact_document_work_.end()){
      for(const auto&resource:child->second->resources){
        const auto actual=ReferenceDocument(resource.frame);
        if(!actual||actual->session!=resource.session||actual->loader!=resource.loader||actual->utility!=resource.context||!ScreenshotAssertionRememberFrame(identity,resource.frame)){
          screenshot_assertion_retirement_.Fail(identity,"Native screenshot child cleanup document changed",Now());NetworkFatal("Native screenshot child cleanup document changed");return true;
        }
        auto params=CefDictionaryValue::Create();params->SetInt("contextId",resource.context);params->SetString("expression",SuiteCutArtifactController::RestoreExpression(owner));params->SetBool("returnByValue",true);
        work->commands.push_back({resource.frame,resource.session,"Runtime.evaluate",params,SuiteCutArtifactController::ResponseStage::Boolean});
      }
    }
    for(auto&command:plan.root)work->commands.push_back({main_frame_id_,"",std::move(command.method),std::move(command.params),command.stage});
    const auto [position,inserted]=screenshot_assertion_cleanup_.emplace(owner,work);(void)position;
    if(!inserted){ScreenshotAssertionCleanupFailed(work,"Native screenshot cleanup already owns this operation");return true;}
    // All exact resources have been copied into the retained native work before
    // erasing the logical preparation map. Old watchers now cannot restart it.
    if(child!=artifact_document_work_.end())artifact_document_work_.erase(child);
    ScreenshotAssertionCleanupNext(work);return true;
  }catch(const std::bad_alloc&){
    // Keep the accepted cleanup ticket and original document resource map until
    // actual teardown; allocation failure never acknowledges native cleanup.
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot cleanup allocation failed",Now());NetworkFatal("Native screenshot cleanup allocation failed");return true;
  }
}
void ScreenshotAssertionCleanupNext(const std::shared_ptr<ScreenshotAssertionCleanupWork>&work){
  const auto current=screenshot_assertion_cleanup_.find(work->identity.owner);
  if(current==screenshot_assertion_cleanup_.end()||current->second!=work||work->waiting||work->failed)return;
  if(!ScreenshotAssertionPinsCurrent(work->identity)){ScreenshotAssertionCleanupFailed(work,"Native screenshot document changed before cleanup receipt");return;}
  if(work->next<work->commands.size()){
    const auto&command=work->commands[work->next];work->waiting=true;
    ScreenshotAssertionSubmit(work->identity,command.frame,command.session,command.method,command.params,command.stage,true,
      [this,work](bool submitted,bool ok,const std::string&){
        work->waiting=false;
        if(!submitted||!ok){ScreenshotAssertionCleanupFailed(work,"Actual native screenshot restoration failed");return;}
        ++work->next;ScreenshotAssertionCleanupNext(work);
      });return;
  }
  if(work->view){
    work->waiting=true;work->view=false;
    // This helper retains a native paint ticket before real WasResized and
    // waits for an actual matching PET_VIEW callback, not its void return.
    ScreenshotAssertionRestoreView(work->identity,[this,work](bool ok,const std::string&error){
      work->waiting=false;if(!ok){ScreenshotAssertionCleanupFailed(work,error.empty()?"Actual native screenshot view restoration failed":error);return;}
      ScreenshotAssertionCleanupNext(work);
    });return;
  }
  if(!screenshot_assertion_retirement_.Acknowledge(work->physical,true,"",Now()))return;
  auto done=std::move(work->done);screenshot_assertion_cleanup_.erase(current);
  if(done){done(true,"");}
  ScreenshotAssertionReceiptFinished(work->identity);
}
void ScreenshotAssertionCleanupTick(){
  for(const auto&[owner,work]:screenshot_assertion_cleanup_){
    (void)owner;
    if(screenshot_assertion_retirement_.Watchdog(work->identity,Now())){
      screenshot_assertion_retirement_.Fail(work->identity,"Native screenshot physical cleanup original deadline exceeded",Now());
      NetworkFatal("Native screenshot physical cleanup original deadline exceeded");
    }
  }
}
void ScreenshotAssertionCleanupChannelClosed(){
  // Caller first supplies the exact native channel boundary to the policy and
  // controller. No ordinary cancel/navigation/reset can enter this function.
  auto retired=std::move(screenshot_assertion_cleanup_);screenshot_assertion_cleanup_.clear();
  for(auto&[owner,work]:retired){(void)owner;auto done=std::move(work->done);if(done)done(false,"Actual native screenshot channel closed");}
}
