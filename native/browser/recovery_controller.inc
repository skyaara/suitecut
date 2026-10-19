// SPDX-License-Identifier: MIT
// A new epoch is admitted only by real renderer, document and Runtime facts.
  uint64_t renderer_views_ = 0, failed_renderer_views_ = 0, recovery_after_renderer_ = 0, recovery_after_navigation_ = 0;
  bool recovery_requested_ = false;
  // Reload has no direct CEF acknowledgment. Each joining handler retains an
  // actual receipt until the new renderer/document/Runtime barrier below, or
  // actual page destruction. Logical navigation cancellation cannot retire it.
  std::map<uint64_t,std::shared_ptr<suitecut::NativeLocatorHandlerAsyncWork>> recovery_handler_receipts_;
  double recovery_handler_cleanup_deadline_ = 0;
  bool recovery_handler_retiring_ = false;
  bool RendererRecoveryHandlerPending()const {return !recovery_handler_receipts_.empty();}
  void RendererRecoveryHandlerClosed() {
    const bool pending=RendererRecoveryHandlerPending();
    recovery_handler_receipts_.clear();recovery_handler_cleanup_deadline_=0;recovery_handler_retiring_=false;
    if(pending)engine_.WakeLocatorHandlers();
  }
  void RendererRecoveryHandlerFailed(const std::string& error) {
    if(!RendererRecoveryHandlerPending()||recovery_handler_retiring_)return;
    recovery_handler_retiring_=true;
    if(closed_reason_.empty())closed_reason_=error;
    if(browser_)browser_->GetHost()->CloseBrowser(true);
  }
  void RendererRecoveryHandlerTick() {
    if(!RendererRecoveryHandlerPending()||recovery_handler_retiring_)return;
    std::array<uint64_t,SuiteCutOperationRegistry::kTotalCapacity> observing{};size_t count=0;
    for(const auto&[id,nav]:navigations_)if(nav.recovery)if(const auto operation=operations_.Get(id)) {
      const auto invocation=engine_.LocatorHandlerCommandOrigin(id,page_id_,operation->generation).locator_handler_invocation;
      if(invocation&&count<observing.size())observing[count++]=invocation;
    }
    bool abandoned=false;
    for(const auto&[invocation,receipt]:recovery_handler_receipts_){(void)receipt;if(std::find(observing.begin(),observing.begin()+count,invocation)==observing.begin()+count){abandoned=true;break;}}
    if(abandoned&&!recovery_handler_cleanup_deadline_)recovery_handler_cleanup_deadline_=Now()+30000;
    // New observers cannot renew the physical cleanup bound of an abandoned
    // native reload. Only the observed recovery barrier or OnBeforeClose ends it.
    if(recovery_handler_cleanup_deadline_&&Now()>=recovery_handler_cleanup_deadline_)
      RendererRecoveryHandlerFailed("Native handler renderer recovery exceeded physical retirement grace");
  }
  std::set<int> failed_epoch_handles_, failed_epoch_pending_handles_, failed_epoch_evaluation_groups_;
  void RememberFailedEpoch() {
    failed_epoch_handles_.clear(); failed_epoch_pending_handles_.clear(); failed_epoch_evaluation_groups_.clear();
    for (const auto& [id, value] : handles_) failed_epoch_handles_.insert(id);
    for (const auto& [id, value] : pending_handle_groups_) failed_epoch_pending_handles_.insert(id);
    for (const auto& [id, value] : evaluation_groups_) failed_epoch_evaluation_groups_.insert(id);
  }
  template<class Entries> static size_t RetiredEpochMetadata(const std::set<int>& old, const Entries& current) {
    return static_cast<size_t>(std::count_if(old.begin(), old.end(), [&](const int id) { return !current.contains(id); }));
  }
  void RendererRecoveryDiagnostics(Dict response) {
    response->SetBool("rendererFailed", renderer_failed_);
    response->SetBool("rendererRecoveryPending", recovery_requested_);
    response->SetDouble("rendererGeneration", static_cast<double>(renderer_views_));
    // These count exact retired metadata entries, not remote release ACKs.
    response->SetInt("failedEpochHandleMetadata", static_cast<int>(failed_epoch_handles_.size()));
    response->SetInt("failedEpochRetiredHandleMetadata", static_cast<int>(RetiredEpochMetadata(failed_epoch_handles_, handles_)));
    response->SetInt("failedEpochPendingHandleMetadata", static_cast<int>(failed_epoch_pending_handles_.size()));
    response->SetInt("failedEpochRetiredPendingHandleMetadata", static_cast<int>(RetiredEpochMetadata(failed_epoch_pending_handles_, pending_handle_groups_)));
    response->SetInt("failedEpochEvaluationGroups", static_cast<int>(failed_epoch_evaluation_groups_.size()));
    response->SetInt("failedEpochRetiredEvaluationGroups", static_cast<int>(RetiredEpochMetadata(failed_epoch_evaluation_groups_, evaluation_groups_)));
  }
  void CheckRendererRecovery() {
    if (!recovery_requested_ || !renderer_failed_ || !browser_ || closing_page_ || recovery_handler_retiring_ ||
        renderer_views_ <= recovery_after_renderer_) return;
    const auto frame = frame_states_.find(main_frame_id_);
    if (frame == frame_states_.end() || frame->second.navigation <= recovery_after_navigation_ ||
        !frame->second.milestones.contains("commit") || !frame->second.context ||
        !frame->second.utility_context) return;
    // The native reload remains physically owned even if its observer timed out.
    recovery_requested_ = false; renderer_failed_ = false;
#if defined(OS_LINUX)
    engine_.streaming()->RecoverPage(page_id_);
#endif
    auto header = Header("pagerecovered");
    header->SetDouble("rendererGeneration", static_cast<double>(renderer_views_));
    header->SetDictionary("mainFrame", FrameJson(frame->second));
    SendPacket(header);
    RendererRecoveryHandlerClosed();
  }
  void BeginRendererRecovery(int id, Dict message, int timeout) {
    const auto operation=operations_.Get(id);
    if(!operation)return;
    if(operations_.IsExpired(id,operation->generation,Now())){Complete(id,false,"Native renderer recovery original action deadline exceeded");return;}
    if(recovery_handler_retiring_){Complete(id,false,"Native renderer recovery is awaiting actual page closure");return;}
    if (page_id_ == 1 || !renderer_failed_ || !browser_ || closing_page_) {
      Complete(id,false,"Native renderer recovery requires a failed, open child page"); return;
    }
    if (FrameId(message) != main_frame_id_) {
      Complete(id,false,"Native renderer recovery requires the main frame"); return;
    }
    const auto frame = frame_states_.find(main_frame_id_);
    if (frame == frame_states_.end()) { Complete(id,false,"Recovery main frame is unavailable"); return; }
    const auto wait = message->GetType("waitUntil") == VTYPE_STRING ? message->GetString("waitUntil").ToString() : "load";
    if (wait != "commit" && wait != "domcontentloaded" && wait != "load" && wait != "networkidle") {
      Complete(id,false,"Invalid native recovery load state"); return;
    }
    const auto invocation=suitecut::CurrentNativeWorkOrigin().locator_handler_invocation;
    if(invocation&&!recovery_handler_receipts_.contains(invocation)) {
      if(recovery_handler_receipts_.size()>=64){Complete(id,false,"Native renderer recovery handler capacity exceeded");return;}
      recovery_handler_receipts_.emplace(invocation,suitecut::RetainNativeLocatorHandlerAsync());
    }
    const bool dispatch = !recovery_requested_;
    if (dispatch) {
      recovery_after_renderer_ = failed_renderer_views_;
      recovery_after_navigation_ = frame->second.navigation;
      recovery_requested_ = true;
    }
    Navigation nav; nav.frame = main_frame_id_; nav.url = frame->second.url; nav.api = "page.reload";
    nav.timeout = timeout; nav.wait = wait; nav.next = true; nav.after = recovery_after_navigation_;
    nav.previous_loader = frame->second.loader; nav.dispatched = true; nav.recovery = true;
    nav.native_download_after = engine_.downloads()->NavigationSequence();
    operation_frames_[id] = main_frame_id_; operation_sessions_[id] = frame->second.session;
    navigations_[id] = std::move(nav);
    if (dispatch) browser_->Reload();
    CheckNavigations();
  }
