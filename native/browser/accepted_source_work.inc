// SPDX-License-Identifier: MIT
// Included inside Source. Only actual pending automation/backend ownership is
// joined. Frame state, handles, timers, listeners, routes, uploaded File backing
// and paused virtual-time policy remain resident under the worker fixture.
  bool AcceptedWorkReady(bool viewer = false)const{
    if(locator_handler_active_||!browser_||!ready_||closing_page_||renderer_failed_||child_bootstrap_failed_||
       operations_.Size()||RendererRecoveryHandlerPending()||native_debugger_closing_requested_||NativePristineNeedsTick()||!devtools_.empty()||!devtools_dispatch_.empty()||
       !context_retries_.empty()||!context_retry_queued_.empty()||
       !input_work_.empty()||!mouse_queue_.empty()||!keyboard_queue_.empty()||mouse_input_||keyboard_input_||
       !send_keys_physical_.empty()||send_keys_physical_count_||!send_keys_cleanup_commands_.empty()||
       !drag_work_.empty()||NativeDropBusy()||!native_drop_transport_.empty()||
       native_file_selection_||!form_clicks_.empty()||!form_uploads_.empty()||!folder_observers_.empty()||folder_operation_||
       !pending_uploads_.empty()||!pending_handle_groups_.empty()||
       !network_calls_.empty()||!har_pending_.empty()||!binding_calls_.empty()||
       !event_waiters_.empty()||!body_waiters_.empty()||!navigation_waiters_.empty()||
       native_websocket_explicit_payloads_.Pending()||event_predicates_.Size()||!websocket_predicates_.empty()||
       !pending_child_bootstraps_.empty()||!init_snapshot_requests_.empty()||!init_snapshot_continuations_.empty()||
       worker_physical_||!worker_calls_.empty()||!worker_work_.empty()||WorkerNeedsTick()||
       network_publication_results_.size()||!network_publication_operations_.empty()||
       !ax_captures_.empty()||ax_physical_||native_media_physical_||!native_media_queue_.empty()||(native_idle_&&native_idle_->Pending())||
       !console_arguments_.empty()||!console_materializations_.empty()||
       !artifact_document_work_.empty()||artifact_view_owner_||
       screenshot_assertion_retirement_.Size()||screenshot_assertion_retirement_.Physical()||
       !native_trace_protocol_.empty()||native_trace_png_readbacks_||native_trace_aria_readbacks_||
       !native_highlight_calls_.empty()||NativeHighlightProcessPhysical()||
       reference_dispatch_.Pending()||suitecut::SnapshotReferenceDispatch::ProcessPending()||
       !reference_frame_batches_.empty()||suitecut::OwnerFrameControls::ProcessPhysicalSize()||
       (network_fatal_&&(network_fatal_->pending()||network_fatal_->requested())))return false;
    if((artifacts_&&(artifacts_->Size()||artifacts_->PhysicalCount()))||
       (bindings_&&!bindings_->AcceptedWorkReady())||(credentials_&&credentials_->Busy())||
       (dialogs_&&!dialogs_->AcceptedWorkReady())||(sockets_&&sockets_->pending())||
       (network_&&!network_->AcceptedWorkReady(viewer))||
       (request_reads_&&request_reads_->physical())||(response_reads_&&response_reads_->physical())||
       (private_response_&&(private_response_->waiting()||private_response_->physical()))||
       (image_streams_&&image_streams_->physical())||
       (page_errors_&&(page_errors_->Captures()||page_errors_->Physical()))||
       (handle_disposals_&&(handle_disposals_->Size()||handle_disposals_->Physical()))||
       (element_states_&&(element_states_->Size()||element_states_->PhysicalSize()||element_states_->Pending()))||
       (selector_waits_&&(selector_waits_->Size()||selector_waits_->PhysicalSize()))||
       (owner_frame_controls_&&owner_frame_controls_->PhysicalSize())||
       (native_dom_domains_&&(native_dom_domains_->Physical()||native_dom_domains_->PendingReleases()))||
       // Published AI references are resident state during a viewer handover;
       // their actual installs, releases and borrows must still retire first.
       (snapshot_references_&&((!viewer&&snapshot_references_->Active())||snapshot_references_->InstallPhysical()||snapshot_references_->Physical()||snapshot_references_->Borrows()))||
       (native_pristine_factories_&&native_pristine_factories_->PhysicalCount())||
       (native_coverage_&&(native_coverage_->PhysicalCount()||native_coverage_->NeedsTick()))||
       (native_debugger_domains_&&native_debugger_domains_->PhysicalCount())||
       (native_developer_debugger_&&native_developer_debugger_->PhysicalCount())||
       (native_frame_debugger_scopes_&&!native_frame_debugger_scopes_->AcceptedWorkReady())||
       (native_frame_developer_debuggers_&&!native_frame_developer_debuggers_->AcceptedWorkReady())||
       (native_worker_developer_debuggers_&&!native_worker_developer_debuggers_->AcceptedWorkReady())||
       (native_trace_phases_&&native_trace_phases_->Size())||
       (native_touch_&&(native_touch_->physical()||native_touch_->queued()))||
       (settings_page_&&(settings_page_->physical()||settings_page_->pending())))return false;
    if(native_virtual_time_){const auto state=native_virtual_time_->State();if(state.physical||state.active||state.queued||state.closing)return false;}
    return !native_virtual_time_close_fence_.NeedsTick();
  }
