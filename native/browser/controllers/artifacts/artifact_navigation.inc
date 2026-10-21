// SPDX-License-Identifier: MIT
// Included inside Source. Restarts share one registry identity and deadline.
void ArtifactWithContext(int id, uint64_t generation, uint64_t attempt, std::function<void(int)> done) {
  if(ScreenshotAssertionOwns(id)){const auto entry=screenshot_assertion_retirement_.Get({page_id_,id,generation});if(!entry||entry->frozen)return;}
  if (!operations_.IsCurrent(id,generation) || !artifact_attempts_.contains(id) || artifact_attempts_.at(id) != attempt) return;
  const auto frame = frame_states_.find(main_frame_id_);
  if (frame != frame_states_.end()) {
    operation_frames_[id] = main_frame_id_; operation_sessions_[id] = frame->second.session;
    EnsureUtilityWorld(main_frame_id_);
    if (frame->second.utility_context > 0) { done(frame->second.utility_context); return; }
  }
  CefPostDelayedTask(TID_UI,new Task([self = CefRefPtr<Source>(this),id,generation,attempt,done = std::move(done)]() mutable {
    self->ArtifactWithContext(id,generation,attempt,std::move(done));
  }),10);
}
void ArtifactEnsureController() {
    if (!artifacts_) {artifacts_ = std::make_unique<SuiteCutArtifactController>(
      [this](int owner,const std::string& method,Dict params,SuiteCutArtifactController::Callback callback) {
        if(!ScreenshotAssertionOwns(owner))writer_.PublicationAudit().ScreenshotAdmitted();
        if(method=="Emulation.setEmulatedMedia")NativeMediaApply(owner,params,std::move(callback));
        else DevTools(owner,method,params,std::move(callback));
      },
      [this](const std::string& method,Dict params) { SendDevTools(method,params,""); },
      [this](int owner,bool ok,const std::string& error,const std::string& json) { if(!ScreenshotComparisonReceipt(owner,ok,error,json))Complete(owner,ok,error,json); });
      if(native_media_params_)artifacts_->SeedNativeMedia(native_media_params_);
    }
}
void ArtifactStart(int id) {
  const auto operation = operations_.Get(id); const auto config = artifact_configs_.find(id);
  if (!operation || config == artifact_configs_.end()) return;
  const auto attempt = ++artifact_attempts_[id]; const auto generation = operation->generation;
  ArtifactWithContext(id,generation,attempt,[this,id,generation,attempt](int context) {
    if (!operations_.IsCurrent(id,generation) || !artifact_attempts_.contains(id) || artifact_attempts_.at(id) != attempt) return;
    operation_sessions_[id] = "";
    ArtifactEnsureController();
    artifacts_->SetAssertionSend([this](int owner,const std::string&method,Dict params,SuiteCutArtifactController::ResponseStage stage,auto done){
      if(!ScreenshotAssertionRoute(owner,method,params,stage,done))DevTools(owner,method,params,std::move(done));
    },[this](int owner){return ScreenshotAssertionOwns(owner);});
    artifacts_->SetAssertionCaptured([this](int owner,uint64_t generation,bool ok,const std::string&json){return ScreenshotAssertionCaptureResult(owner,generation,ok,json);});
    artifacts_->SetAssertionCleanup([this](int owner,auto plan,auto done){return ScreenshotAssertionCleanupBegin(owner,std::move(plan),std::move(done));});
    artifacts_->SetRetry([this](int owner) { RetryContext(owner); });
    artifacts_->SetDocumentPreparation([this](int owner,const std::string& expression,auto done) { ArtifactDocuments(owner,expression,std::move(done)); },
      [this](int owner,bool teardown) { ArtifactDocumentCleanup(owner,teardown); });
    artifacts_->SetViewLease([this](int owner,int width,int height,auto done) { ArtifactAcquireView(owner,width,height,std::move(done)); },
      [this](int owner) { ArtifactReleaseView(owner); });
    artifacts_->SetBeforeScreenshot([this](int owner,Dict config,auto done) { ArtifactBeforeScreenshot(owner,config,std::move(done)); });
    artifacts_->SetGeometry([this](int owner,Dict config,auto done) { ArtifactGeometry(owner,config,std::move(done)); });
    const auto input=artifact_input_configs_.find(id);
    const auto config=artifact_configs_.at(id);
    Dict metrics;
    if(config->GetString("action")=="screenshot"&&config->GetBool("fullPage"))
      metrics=context_settings_?context_settings_->ViewportMetrics({width_,height_},scale_):
          suitecut::ContextSettings{}.ViewportMetrics({width_,height_},scale_);
    artifacts_->Begin(id,config,context,scale_,input==artifact_input_configs_.end()?nullptr:input->second,metrics);
  });
}
std::string ArtifactTimeoutMessage(int id) const {
  const auto config = artifact_configs_.find(id);
  const auto detail = artifacts_ ? artifacts_->TimeoutMessage(id) : "";
  if (config == artifact_configs_.end() || config->second->GetString("action") != "screenshot" || !config->second->GetBool("__artifactNavigationRetry")) return detail;
  return "Cannot take a screenshot while page is navigating: Timeout " + std::to_string(config->second->GetInt("timeoutMs")) +
      "ms exceeded.\nCall log:\n - waiting for a current native document after navigation";
}
