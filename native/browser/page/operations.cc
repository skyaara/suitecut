// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::CompleteVisibilityEmpty(int id, uint64_t generation) -> void {
  const auto scope = visibility_snapshots_.Get(id);
  if (!scope || (generation && scope->generation != generation) ||
      !operations_.IsCurrent(id, scope->generation))
    return;
  if (operations_.IsExpired(id, scope->generation, Now())) {
    Complete(id, false, "Native operation timed out");
    return;
  }
  if (!scope->validated) {
    Complete(id, false,
             "Native visibility selector validation was interrupted by document destruction");
    return;
  }
  Complete(id, true, "", "{\"result\":{\"type\":\"boolean\",\"value\":false}}");
}

auto Source::CompleteVisibilityInFrame(const std::string& frame) -> void {
  for (const auto& [id, generation] : visibility_snapshots_.InFrame(frame))
    CompleteVisibilityEmpty(id, generation);
}

auto Source::CompleteVisibilityInContext(const std::string& session, int context,
                                         const std::string& identity) -> void {
  for (const auto& [id, generation] : visibility_snapshots_.InContext(session, context, identity))
    CompleteVisibilityEmpty(id, generation);
}

auto Source::VisibilityScopeGone(int id) const -> bool {
  const auto scope = visibility_snapshots_.Get(id);
  if (!scope) return false;
  const auto frame = frame_states_.find(scope->frame);
  if (frame == frame_states_.end()) return suitecut::VisibilitySnapshotControls::Gone(*scope, {});
  const auto& current = frame->second;
  return suitecut::VisibilitySnapshotControls::Gone(
      *scope, suitecut::VisibilitySnapshotControls::Current{
                  current.session, current.loader, current.context_identity,
                  current.utility_identity, current.context, current.utility_context});
}

auto Source::CompleteCountZero(int id) -> void {
  Complete(id, true, "", "{\"result\":{\"type\":\"object\",\"value\":[\"primitive\",0]}}");
}

auto Source::CompleteCountsInFrame(const std::string& frame) -> void {
  CompleteVisibilityInFrame(frame);
  std::vector<int> ids;
  for (const auto& [id, current] : operation_frames_)
    if (current == frame && count_queries_.contains(id)) ids.push_back(id);
  for (const int id : ids) CompleteCountZero(id);
}

auto Source::CompleteCountsInContext(const std::string& session, int context,
                                     const std::string& identity) -> void {
  CompleteVisibilityInContext(session, context, identity);
  std::vector<int> ids;
  for (const auto& [id, current] : count_contexts_)
    if (current.first == session && (!context || current.second == context)) ids.push_back(id);
  for (const int id : ids) CompleteCountZero(id);
}

auto Source::Complete(int id, bool ok, const std::string& error, const std::string& json) -> void {
  engine_.websocket_routes().Cancel(id);
  const auto handler_owner = operations_.Get(id);
  const suitecut::NativeWorkOriginFrame handler_origin(engine_.LocatorHandlerCommandOrigin(
      id, page_id_, handler_owner ? handler_owner->generation : 0));
  if (NativeWidgetMouseComplete(id, ok, error, json)) return;
  if (ScreenshotAssertionCompleteRequested(id, ok, error, json)) return;
  if (ok && !AssertionDeadlinePublicationCurrent(id)) return;
  if (ok && !ReferenceFrameOperationCurrent(id)) return;
  if (ok)
    if (const auto bounds = element_bounds_work_.find(id); bounds != element_bounds_work_.end()) {
      const auto work = bounds->second;
      if (!ElementBoundsCurrent(work)) return;
    }
  if (ok)
    if (const auto predicate = locator_predicate_work_.find(id);
        predicate != locator_predicate_work_.end()) {
      const auto work = predicate->second;
      if (!LocatorPredicateCurrent(work)) return;
    }
  if (ok)
    if (const auto suggestion = locator_suggestion_work_.find(id);
        suggestion != locator_suggestion_work_.end())
      if (!LocatorSuggestionCurrent(suggestion->second)) return;
  if (ok)
    if (const auto snapshot = aria_json_work_.find(id); snapshot != aria_json_work_.end())
      if (!AriaJSONCurrent(snapshot->second)) return;
  const auto artifact_operation = operations_.Get(id);
  const uint64_t reference_generation = artifact_operation ? artifact_operation->generation : 0;
  if (ok && artifact_input_configs_.contains(id) && artifact_operation &&
      operations_.IsExpired(id, artifact_operation->generation, Now())) {
    Complete(id, false, "Native artifact original deadline expired during rendering");
    return;
  }
  Dict committed_navigation;
  const auto navigation = navigations_.find(id);
  if (ok && navigation != navigations_.end()) {
    const auto frame = frame_states_.find(navigation->second.frame);
    if (frame != frame_states_.end()) committed_navigation = FrameJson(frame->second);
  }
  if (operations_.Contains(id) && std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") &&
      command_diagnostic_messages_++ < 4000)
    std::fprintf(
        stderr, "native completion page=%d id=%d ok=%d closing=%d navigation=%d observation=%d\n",
        page_id_, id, ok, closing_page_, navigations_.contains(id), expect_results_.contains(id));
  const auto form = form_clicks_.find(id);
  if (ok && form != form_clicks_.end() && operations_.Contains(id)) {
    auto done = std::move(form->second);
    form_clicks_.erase(form);
    CleanupInput(id);
    context_retries_.erase(id);
    done();
    return;
  }
  if (ok && !closing_page_ && network_ && network_publication_operations_.contains(id)) {
    const auto operation = operations_.Get(id);
    const auto cursor = network_->QueuedPublication();
    if (operation && cursor > network_->PublishedPublication()) {
      const auto admitted =
          network_publication_results_.Hold(id, operation->generation, cursor, json);
      if (admitted != suitecut::NativeNetworkPublicationResults::Admission::Full) {
        WakeDeadlines();
        return;
      }
      Complete(id, false, "Native deferred network-publication results exceed 64 owners or 16 MiB");
      return;
    }
  }
  network_publication_results_.Cancel(id);
  network_publication_operations_.erase(id);
  if (ok && !PublishReferenceFrameBatch(id)) return;
  AssertionDeadlineForget(id);
  if (!operations_.Finish(id)) return;
  engine_.ForgetLocatorHandlerCommand(id);
  LocatorHandlerCancelRemoval(id);
  ScreenshotAssertionFinished(id);
  locator_suggestion_work_.erase(id);
  if (!ok && native_highlights_) native_highlights_->Cancel(id, reference_generation);
  if (snapshot_references_) snapshot_references_->Cancel(id, reference_generation, Now());
  reference_frame_batches_.erase(id);
  if (native_virtual_time_) native_virtual_time_->Cancel(id, Now());
  native_virtual_time_close_fence_.Finished(id, reference_generation);
  NativeVirtualTimePumpClose();
  NativeTraceEnd(id, ok, error, json);
  engine_.recordings()->CancelCommand(id);
#if defined(OS_LINUX) || defined(__APPLE__)
  engine_.streaming()->CancelCommand(id);
#endif
  if (native_coverage_) native_coverage_->Cancel(id);
  NativeDeveloperCancel(id);
  if (native_touch_) native_touch_->Cancel(id);
  WorkerCancel(id);
  ax_captures_.erase(id);
  ScreenshotComparisonCancel(id);
  artifact_configs_.erase(id);
  artifact_attempts_.erase(id);
  artifact_input_configs_.erase(id);
  FormCancelFolder(id);
  if (element_states_) element_states_->Cancel(id);
  if (selector_waits_) selector_waits_->Cancel(id);
  CancelConsoleMaterialization(id);
  action_pauses_.Cancel(id);
  LocatorHandlerCancel(id);
  url_predicates_.Cancel(id);
  url_waits_.erase(id);
  event_predicates_.Cancel(id);
  websocket_predicates_.erase(id);
  crash_wait_owners_.erase(id);
  action_navigations_.erase(id);
  NativeDropCancel(id);
  NativeManualCancel(id, !ok);
  NativeDragCancel(id);
  form_clicks_.erase(id);
  form_uploads_.erase(id);
  if (native_idle_) native_idle_->Cancel(id, reference_generation);
  engine_.downloads()->Cancel(id);
  if (artifacts_) artifacts_->Cancel(id);
  if (sockets_) sockets_->Cancel(id);
  if (network_) network_->Cancel(id);
  engine_.har_files().Cancel(id);
  if (const auto pending = har_pending_.find(id); pending != har_pending_.end()) {
    const auto route = pending->second;
    har_pending_.erase(pending);
    if (!ok && !closing_page_ && network_)
      network_->UnbindReplay(route, [this](bool success, const std::string& reason) {
        if (!success && !closing_page_) NetworkFatal(reason);
      });
  }
  if (bindings_) {
    bindings_->ArgumentFinished(id, ok);
    bindings_->Cancel(id);
  }
  CleanupInput(id);
  RestoreLocatorModifiers(id);
  context_retries_.erase(id);
  locator_attempts_.erase(id);
  locator_logs_.erase(id);
  locator_waits_.erase(id);
  expect_results_.erase(id);
  first_observations_.erase(id);
  lazy_queries_.erase(id);
  context_retry_queued_.erase(id);
  count_queries_.erase(id);
  count_contexts_.erase(id);
  for (auto it = devtools_.begin(); it != devtools_.end();) {
    if (it->second.owner == id && !it->second.retain_after_cancel)
      it = devtools_.erase(it);
    else
      ++it;
  }
  navigation_waiters_.erase(id);
  navigations_.erase(id);
  body_waiters_.erase(id);
  native_dom_getters_.erase(id);
  element_bounds_operations_.erase(id);
  element_bounds_work_.erase(id);
  locator_predicate_work_.erase(id);
  locator_predicate_operations_.erase(id);
  aria_json_work_.erase(id);
  aria_json_operations_.erase(id);
  if (const auto scope = visibility_snapshots_.Get(id))
    visibility_snapshots_.Remove(id, scope->generation);
  operation_frames_.erase(id);
  author_query_evaluations_.erase(id);
  const auto session = operation_sessions_[id];
  operation_sessions_.erase(id);
  event_waiters_.erase(id);
  if (navigation_ == id) {
    navigation_ = 0;
    if (!ok && browser_) browser_->StopLoad();
  }
  const auto pending_handle = pending_handle_groups_.find(id);
  if (pending_handle != pending_handle_groups_.end()) {
    auto params = CefDictionaryValue::Create();
    params->SetString("objectGroup", pending_handle->second);
    pending_handle_groups_.erase(pending_handle);
    if (browser_) SendDevTools("Runtime.releaseObjectGroup", params, session);
  }
  const auto groups = evaluation_groups_.find(id);
  if (groups != evaluation_groups_.end()) {
    const auto sessions = groups->second;
    evaluation_groups_.erase(groups);
    for (const auto& session : sessions)
      if (browser_) {
        auto params = CefDictionaryValue::Create();
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        SendDevTools("Runtime.releaseObjectGroup", params, session);
      }
  }
  const auto& failure_reason =
      (!ok && network_fatal_ && network_fatal_->pending() && !closed_reason_.empty())
          ? closed_reason_
          : error;
  writer_.Reply(id, ok, failure_reason, std::vector<uint8_t>(json.begin(), json.end()),
                committed_navigation);
}

auto Source::WakeDeadlines() -> void {
  if (!deadline_timer_active_) {
    deadline_timer_active_ = true;
    CefPostDelayedTask(
        TID_UI,
        new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },
                 suitecut::AcceptedTaskWork::Kind::Work, LocatorHandlerMonitorOrigin()),
        10);
  }
}

auto Source::BindingCommandsNeedTick() const -> bool {
  for (const auto& [id, call] : binding_calls_)
    if (!call.expired) return true;
  return false;
}

auto Source::Deadlines() -> void {
  if (handle_disposals_) handle_disposals_->Tick(Now());
  if (snapshot_references_) snapshot_references_->Tick(Now());
  ScreenshotAssertionTick();
  NativeHighlightTick();
  NativeTraceTick();
  NativePristineTick();
  NativeDebuggerTick();
  NativeDropTick();
  if (!NativeDropBusy()) {
    StartInput("mouse");
    StartInput("keyboard");
  }
  NativeDeveloperTick();
  if (native_coverage_) native_coverage_->Tick(Now());
  if (page_errors_) page_errors_->Tick();
  if (element_states_) element_states_->Tick();
  if (selector_waits_) selector_waits_->Tick();
  AssertionDeadlineTick();
  OwnerFrameDocumentsTick();
  CheckNavigations();
  CheckActionNavigations();
  PumpInput();
  PumpFormUploads();
  LocatorHandlerTick();
  if (sockets_) sockets_->Tick(Now());
  native_websocket_explicit_payloads_.Tick();
  NativeWebSocketRoutePump();
  PumpWebSocketPredicates();
  if (request_reads_) request_reads_->Tick(Now());
  if (private_response_) private_response_->Tick(Now());
  if (image_streams_) image_streams_->Tick(Now());
  if (network_) network_->Tick(Now());
  if (network_) {
    auto ready = network_publication_results_.Ready(
        network_->PublishedPublication(), [this](int owner, uint64_t generation) {
          return operations_.IsCurrent(owner, generation) &&
                 !operations_.IsExpired(owner, generation, Now());
        });
    for (auto& result : ready) {
      if (!operations_.IsCurrent(result.owner, result.generation)) continue;
      network_publication_operations_.erase(result.owner);
      Complete(result.owner, true, "", result.json);
    }
  }
  if (network_fatal_) network_fatal_->Tick();
  bool network_expired = false;
  for (auto& [id, call] : network_calls_)
    if (!call.expired && Now() >= call.deadline) {
      call.expired = true;
      network_expired = true;
    }
  if (network_expired) NetworkFatal("Native network command deadline expired");
  WorkerTick();
  if (native_virtual_time_) native_virtual_time_->Tick(Now());
  if (bindings_) bindings_->Tick(Now());
  // Native command deadlines cancel logical registration ownership. Keep
  // physical installation callbacks until Chromium acknowledges them so a
  // late document script identifier can still be removed after cancellation.
  for (auto& [id, call] : binding_calls_)
    if (Now() >= call.deadline) call.expired = true;
  if (dialogs_) dialogs_->Tick(Now());
  for (const int id : operations_.Expired(Now())) {
    if (screenshot_assertion_retirement_.Contains(id) || assertion_deadlines_.Contains(id))
      continue;
    if (engine_.screenshot_comparisons()->Contains(page_id_, id)) {
      engine_.screenshot_comparisons()->Deadline(page_id_, id);
      continue;
    }
    // A retrying assertion always gets its first DOM observation. Tiny user
    // deadlines bound retries; the initial transport round trip has a bounded
    // one-second watchdog and remains immediately cancellable.
    const auto first = first_observations_.find(id);
    if (first != first_observations_.end() && Now() < first->second) continue;
    const auto observed = expect_results_.find(id);
    if (observed != expect_results_.end()) {
      const auto snapshot = observed->second;
      Complete(id, true, "", snapshot);
      continue;
    }
    const auto url_wait = url_waits_.find(id);
    if (url_wait != url_waits_.end()) {
      Complete(id, false, url_wait->second.api + ": Timeout exceeded while waiting for URL");
      continue;
    }
    const auto nav = navigations_.find(id);
    if (nav != navigations_.end()) {
      const auto error = nav->second.api + ": Timeout " + std::to_string(nav->second.timeout) +
                         "ms exceeded.\nnavigating to \"" + nav->second.url +
                         "\", waiting until \"" + nav->second.wait + "\"";
      Complete(id, false, error);
    } else if (!ArtifactTimeoutMessage(id).empty()) {
      Complete(id, false, ArtifactTimeoutMessage(id));
    } else if (locator_logs_.contains(id)) {
      Complete(id, false,
               "Native operation timed out (Timeout exceeded)\nCall log:\n - " + locator_logs_[id] +
                   (locator_waits_.contains(id) ? "\n - waiting for " + locator_waits_[id] : ""));
    } else
      Complete(id, false, "Native operation timed out");
  }
  NativeVirtualTimePumpClose();
  RendererRecoveryHandlerTick();
  const bool non_network_deadlines_ready =
      (operations_.Size() == 0 || LocatorHandlerLogicalReady()) &&
      !LocatorHandlerNeedsSourceTick() && !RendererRecoveryHandlerPending() &&
      (!handle_disposals_ || !handle_disposals_->Size()) &&
      (!snapshot_references_ || !snapshot_references_->NeedsTick()) &&
      !NativeHighlightNeedsTick() && !NativeVirtualTimeNeedsTick() && !NativeTraceNeedsTick() &&
      !NativePristineNeedsTick() && !NativeDebuggerNeedsTick() && !NativeDropNeedsTick() &&
      !NativeDeveloperNeedsTick() && (!native_coverage_ || !native_coverage_->NeedsTick()) &&
      !WorkerNeedsTick() && pending_uploads_.empty() && network_calls_.empty() &&
      (!dialogs_ || !dialogs_->Size()) && (!sockets_ || !sockets_->pending()) &&
      native_routed_events_.empty() && !native_routed_waiter_ &&
      !native_websocket_explicit_payloads_.Pending() &&
      (!private_response_ || !private_response_->NeedsTick()) &&
      (!image_streams_ || !image_streams_->NeedsTick()) && !BindingCommandsNeedTick() &&
      (!page_errors_ || !page_errors_->NeedsTick()) &&
      (!element_states_ || !element_states_->NeedsTick()) &&
      (!selector_waits_ || !selector_waits_->NeedsTick()) &&
      !screenshot_assertion_retirement_.Size() && !OwnerFrameDocumentsNeedTick() &&
      (!bindings_ || !bindings_->NeedsTick());
  if (!browser_ || (non_network_deadlines_ready && (!network_ || !network_->NeedsTick()))) {
    deadline_timer_active_ = false;
    return;
  }
  const bool passive_network_monitor = non_network_deadlines_ready && network_ &&
                                       network_->NeedsTick() && !network_->NeedsTick(true) &&
                                       AcceptedWorkReady(true);
  // One short-lived timer for the whole registry. Completed operations must
  // not retain one delayed Source reference per command until their timeout.
  CefPostDelayedTask(
      TID_UI,
      new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },
               passive_network_monitor ? suitecut::AcceptedTaskWork::Kind::PassiveNetworkMonitor
                                       : suitecut::AcceptedTaskWork::Kind::Work,
               LocatorHandlerMonitorOrigin()),
      10);
}

}  // namespace suitecut::browser
