// SPDX-License-Identifier: MIT
// Included inside Source. Only actual pending automation/backend ownership is
// joined. Frame state, handles, timers, listeners, routes, uploaded File backing
// and paused virtual-time policy remain resident under the worker fixture.
bool InspectorChannelCrashed(const suitecut::AcceptedInspectorWork::Channel& channel) const {
  return renderer_failed_ && inspector_channel_ && *inspector_channel_ == channel;
}
bool AcceptedWorkReady(bool viewer = false) const { return !AcceptedWorkBlocker(viewer); }
// The first condition keeping native work from being ready, for diagnostics.
const char* AcceptedWorkBlocker(bool viewer = false) const {
  // A crashed page runs no work until recovery, so a viewer can still hand
  // over control of it; recovery itself is joined below.
  if (locator_handler_active_) return "locator_handler_active_";
  if (!browser_) return "!browser_";
  if (!ready_) return "!ready_";
  if (closing_page_) return "closing_page_";
  if ((renderer_failed_ && !viewer)) return "(renderer_failed_&&!viewer)";
  if (child_bootstrap_failed_) return "child_bootstrap_failed_";
  if (operations_.Size()) return "operations_.Size()";
  if (RendererRecoveryHandlerPending()) return "RendererRecoveryHandlerPending()";
  if (native_debugger_closing_requested_) return "native_debugger_closing_requested_";
  if (NativePristineNeedsTick()) return "NativePristineNeedsTick()";
  if (!devtools_.empty()) return "!devtools_.empty()";
  if (!devtools_dispatch_.empty()) return "!devtools_dispatch_.empty()";
  if (!context_retries_.empty()) return "!context_retries_.empty()";
  if (!context_retry_queued_.empty()) return "!context_retry_queued_.empty()";
  if (!input_work_.empty()) return "!input_work_.empty()";
  if (!mouse_queue_.empty()) return "!mouse_queue_.empty()";
  if (!keyboard_queue_.empty()) return "!keyboard_queue_.empty()";
  if (mouse_input_) return "mouse_input_";
  if (keyboard_input_) return "keyboard_input_";
  if (!send_keys_physical_.empty()) return "!send_keys_physical_.empty()";
  if (send_keys_physical_count_) return "send_keys_physical_count_";
  if (!send_keys_cleanup_commands_.empty()) return "!send_keys_cleanup_commands_.empty()";
  if (!drag_work_.empty()) return "!drag_work_.empty()";
  if (NativeDropBusy()) return "NativeDropBusy()";
  if (!native_drop_transport_.empty()) return "!native_drop_transport_.empty()";
  if (native_file_selection_) return "native_file_selection_";
  if (!form_clicks_.empty()) return "!form_clicks_.empty()";
  if (!form_uploads_.empty()) return "!form_uploads_.empty()";
  if (!folder_observers_.empty()) return "!folder_observers_.empty()";
  if (folder_operation_) return "folder_operation_";
  if (!pending_uploads_.empty()) return "!pending_uploads_.empty()";
  if (!pending_handle_groups_.empty()) return "!pending_handle_groups_.empty()";
  if (!network_calls_.empty()) return "!network_calls_.empty()";
  if (!har_pending_.empty()) return "!har_pending_.empty()";
  if (!binding_calls_.empty()) return "!binding_calls_.empty()";
  if (!event_waiters_.empty()) return "!event_waiters_.empty()";
  if (!body_waiters_.empty()) return "!body_waiters_.empty()";
  if (!navigation_waiters_.empty()) return "!navigation_waiters_.empty()";
  if (native_websocket_explicit_payloads_.Pending())
    return "native_websocket_explicit_payloads_.Pending()";
  if (event_predicates_.Size()) return "event_predicates_.Size()";
  if (!websocket_predicates_.empty()) return "!websocket_predicates_.empty()";
  if (!pending_child_bootstraps_.empty()) return "!pending_child_bootstraps_.empty()";
  if (!init_snapshot_requests_.empty()) return "!init_snapshot_requests_.empty()";
  if (!init_snapshot_continuations_.empty()) return "!init_snapshot_continuations_.empty()";
  if (worker_physical_) return "worker_physical_";
  if (!worker_calls_.empty()) return "!worker_calls_.empty()";
  if (!worker_work_.empty()) return "!worker_work_.empty()";
  if (WorkerNeedsTick()) return "WorkerNeedsTick()";
  if (network_publication_results_.size()) return "network_publication_results_.size()";
  if (!network_publication_operations_.empty()) return "!network_publication_operations_.empty()";
  if (!ax_captures_.empty()) return "!ax_captures_.empty()";
  if (ax_physical_) return "ax_physical_";
  if (native_media_physical_) return "native_media_physical_";
  if (!native_media_queue_.empty()) return "!native_media_queue_.empty()";
  if ((native_idle_ && native_idle_->Pending())) return "(native_idle_&&native_idle_->Pending())";
  // Retained console arguments are resident handles during a viewer handover;
  // their materializations are still actual work.
  if ((!viewer && !console_arguments_.empty())) return "(!viewer&&!console_arguments_.empty())";
  if (!console_materializations_.empty()) return "!console_materializations_.empty()";
  if (!artifact_document_work_.empty()) return "!artifact_document_work_.empty()";
  if (artifact_view_owner_) return "artifact_view_owner_";
  if (screenshot_assertion_retirement_.Size()) return "screenshot_assertion_retirement_.Size()";
  if (screenshot_assertion_retirement_.Physical())
    return "screenshot_assertion_retirement_.Physical()";
  if (!native_trace_protocol_.empty()) return "!native_trace_protocol_.empty()";
  if (native_trace_png_readbacks_) return "native_trace_png_readbacks_";
  if (native_trace_aria_readbacks_) return "native_trace_aria_readbacks_";
  if (!native_highlight_calls_.empty()) return "!native_highlight_calls_.empty()";
  if (NativeHighlightProcessPhysical()) return "NativeHighlightProcessPhysical()";
  if (reference_dispatch_.Pending()) return "reference_dispatch_.Pending()";
  if (suitecut::SnapshotReferenceDispatch::ProcessPending())
    return "suitecut::SnapshotReferenceDispatch::ProcessPending()";
  if (!reference_frame_batches_.empty()) return "!reference_frame_batches_.empty()";
  if (suitecut::OwnerFrameControls::ProcessPhysicalSize())
    return "suitecut::OwnerFrameControls::ProcessPhysicalSize()";
  if ((network_fatal_ && (network_fatal_->pending() || network_fatal_->requested())))
    return "(network_fatal_&&(network_fatal_->pending()||network_fatal_->requested()))";
  if ((artifacts_ && (artifacts_->Size() || artifacts_->PhysicalCount())))
    return "(artifacts_&&(artifacts_->Size()||artifacts_->PhysicalCount()))";
  if ((bindings_ && !bindings_->AcceptedWorkReady()))
    return "(bindings_&&!bindings_->AcceptedWorkReady())";
  if ((credentials_ && credentials_->Busy())) return "(credentials_&&credentials_->Busy())";
  if ((dialogs_ && !dialogs_->AcceptedWorkReady()))
    return "(dialogs_&&!dialogs_->AcceptedWorkReady())";
  if ((sockets_ && sockets_->pending())) return "(sockets_&&sockets_->pending())";
  if ((network_ && !network_->AcceptedWorkReady(viewer)))
    return "(network_&&!network_->AcceptedWorkReady(viewer))";
  if ((request_reads_ && request_reads_->physical()))
    return "(request_reads_&&request_reads_->physical())";
  if ((response_reads_ && response_reads_->physical()))
    return "(response_reads_&&response_reads_->physical())";
  if ((private_response_ && (private_response_->waiting() || private_response_->physical())))
    return "(private_response_&&(private_response_->waiting()||private_response_->physical()))";
  if ((image_streams_ && image_streams_->physical()))
    return "(image_streams_&&image_streams_->physical())";
  if ((page_errors_ && (page_errors_->Captures() || page_errors_->Physical())))
    return "(page_errors_&&(page_errors_->Captures()||page_errors_->Physical()))";
  if ((handle_disposals_ && (handle_disposals_->Size() || handle_disposals_->Physical())))
    return "(handle_disposals_&&(handle_disposals_->Size()||handle_disposals_->Physical()))";
  if ((element_states_ &&
       (element_states_->Size() || element_states_->PhysicalSize() || element_states_->Pending())))
    return "(element_states_&&(element_states_->Size()||element_states_->PhysicalSize()||element_"
           "states_->Pending()))";
  if ((selector_waits_ && (selector_waits_->Size() || selector_waits_->PhysicalSize())))
    return "(selector_waits_&&(selector_waits_->Size()||selector_waits_->PhysicalSize()))";
  if ((owner_frame_controls_ && owner_frame_controls_->PhysicalSize()))
    return "(owner_frame_controls_&&owner_frame_controls_->PhysicalSize())";
  if ((native_dom_domains_ &&
       (native_dom_domains_->Physical() || native_dom_domains_->PendingReleases())))
    return "(native_dom_domains_&&(native_dom_domains_->Physical()||native_dom_domains_->"
           "PendingReleases()))";
  // Published AI references are resident state during a viewer handover;
  // their actual installs, releases and borrows must still retire first.
  if ((snapshot_references_ &&
       ((!viewer && snapshot_references_->Active()) || snapshot_references_->InstallPhysical() ||
        snapshot_references_->Physical() || snapshot_references_->Borrows())))
    return "(snapshot_references_&&((!viewer&&snapshot_references_->Active())||snapshot_references_"
           "->InstallPhysical()||snapshot_references_->Physical()||snapshot_references_->Borrows())"
           ")";
  if ((native_pristine_factories_ && native_pristine_factories_->PhysicalCount()))
    return "(native_pristine_factories_&&native_pristine_factories_->PhysicalCount())";
  if ((native_coverage_ && (native_coverage_->PhysicalCount() || native_coverage_->NeedsTick())))
    return "(native_coverage_&&(native_coverage_->PhysicalCount()||native_coverage_->NeedsTick()))";
  if ((native_debugger_domains_ && native_debugger_domains_->PhysicalCount()))
    return "(native_debugger_domains_&&native_debugger_domains_->PhysicalCount())";
  if ((native_developer_debugger_ && native_developer_debugger_->PhysicalCount()))
    return "(native_developer_debugger_&&native_developer_debugger_->PhysicalCount())";
  if ((native_frame_debugger_scopes_ && !native_frame_debugger_scopes_->AcceptedWorkReady()))
    return "(native_frame_debugger_scopes_&&!native_frame_debugger_scopes_->AcceptedWorkReady())";
  if ((native_frame_developer_debuggers_ &&
       !native_frame_developer_debuggers_->AcceptedWorkReady()))
    return "(native_frame_developer_debuggers_&&!native_frame_developer_debuggers_->"
           "AcceptedWorkReady())";
  if ((native_worker_developer_debuggers_ &&
       !native_worker_developer_debuggers_->AcceptedWorkReady()))
    return "(native_worker_developer_debuggers_&&!native_worker_developer_debuggers_->"
           "AcceptedWorkReady())";
  if ((native_trace_phases_ && native_trace_phases_->Size()))
    return "(native_trace_phases_&&native_trace_phases_->Size())";
  if ((native_touch_ && (native_touch_->physical() || native_touch_->queued())))
    return "(native_touch_&&(native_touch_->physical()||native_touch_->queued()))";
  if ((settings_page_ && (settings_page_->physical() || settings_page_->pending())))
    return "(settings_page_&&(settings_page_->physical()||settings_page_->pending()))";
  if (native_virtual_time_) {
    const auto state = native_virtual_time_->State();
    if (state.physical || state.active || state.queued || state.closing)
      return "native_virtual_time_";
  }
  if (native_virtual_time_close_fence_.NeedsTick())
    return "native_virtual_time_close_fence_.NeedsTick()";
  return nullptr;
}
