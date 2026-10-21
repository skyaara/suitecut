// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Engine::Alive() const -> std::shared_ptr<bool> { return alive_; }

auto Engine::CaptureRate() const -> int { return fps_; }

auto Engine::VideoMetadata(int page) const -> Dict {
  const auto video = context_video_->Metadata(page);
  if (!video) return nullptr;
  auto result = CefDictionaryValue::Create();
  result->SetString("id", video->id);
  return result;
}

auto Engine::worker_network() -> suitecut::NativeContextWorkerNetwork& { return *worker_network_; }

auto Engine::ContextWorkerNetwork(int context, const std::string& owner, Dict worker, Dict info)
    -> void {
  if (!worker_network_->Observe(context, owner, worker, info))
    writer_.Error("Native context worker network observation rejected");
}

auto Engine::ContextNetwork(int page, Dict info) -> void {
  if (!context_network_->Observe(contexts_->ContextId(page), page, info))
    writer_.Error("Native context network journal rejected payload");
}

auto Engine::ContextConsole(int page, Dict info) -> void {
  if (!context_console_->Observe(contexts_->ContextId(page), page, info))
    writer_.Error("Native context console journal rejected payload");
}

auto Engine::AcceptedInspector() const noexcept
    -> std::shared_ptr<suitecut::AcceptedInspectorWork> {
  return inspector_work_;
}

auto Engine::AcceptedReader() const noexcept -> std::shared_ptr<suitecut::AcceptedReaderWork> {
  return reader_work_;
}

#if defined(__linux__)
#else
auto Engine::AcceptedSourceTicket() const noexcept -> uint64_t { return 0; }
#endif

auto Engine::settings() -> suitecut::ContextSettingsRegistry& { return settings_; }

auto Engine::geolocation() -> suitecut::NativeContextGeolocation& { return geolocation_; }

auto Engine::authentication() -> suitecut::NativeContextAuthenticationControls& {
  return authentication_;
}

auto Engine::BrowserInfo(int page) const -> Dict {
  auto info = contexts_->BrowserExtraInfo(page);
  info->SetBool("suitecutPopup", contexts_->IsPopup(page));
  info->SetBool("suitecutInitialEmptyDocument", !contexts_->IsPopup(page));
  if (!contexts_->IsPopup(page)) info->SetBool("suitecutContextSettingsBootstrap", true);
  return info;
}

auto Engine::network_policy() -> suitecut::NativeContextNetworkPolicy& { return network_policy_; }

auto Engine::network_archive() -> suitecut::NativeNetworkArchives& { return *network_archive_; }

auto Engine::websocket_routes() -> suitecut::NativeWebSocketRoutes& { return *websocket_routes_; }

auto Engine::har_routes() -> suitecut::NativeContextHARRoutes& { return *context_har_; }

auto Engine::har_recording() -> suitecut::NativeHARRecording& { return *har_recording_; }

auto Engine::har_files() -> suitecut::NativeHARReplayFiles& { return *har_files_; }

auto Engine::network_headers() -> suitecut::NativeContextNetworkHeaders& {
  return network_headers_;
}

auto Engine::traces() const -> std::shared_ptr<suitecut::TraceControls> { return traces_; }

auto Engine::screenshot_comparisons() const -> std::shared_ptr<suitecut::ScreenshotComparisons> {
  return screenshot_comparisons_;
}

auto Engine::screenshot_inputs() const -> std::shared_ptr<suitecut::ScreenshotInputs> {
  return screenshot_inputs_;
}

auto Engine::TraceClaim(bool dom) -> bool {
  if (trace_physical_ >= 16) return false;
  ++trace_physical_;
  if (dom) ++trace_dom_physical_;
  return true;
}

auto Engine::TraceRelease(bool dom) -> void {
  if (trace_physical_ && (!dom || trace_dom_physical_)) {
    --trace_physical_;
    if (dom) --trace_dom_physical_;
  } else
    writer_.Error("Native trace physical ownership underflow");
}

auto Engine::contexts() const -> std::shared_ptr<SuiteCutContextControls> { return contexts_; }

auto Engine::http() const -> std::shared_ptr<SuiteCutHTTPControls> { return http_; }

auto Engine::downloads() const -> std::shared_ptr<suitecut::DownloadControls> { return downloads_; }

auto Engine::recordings() const -> std::shared_ptr<suitecut::RecordingControls> {
  return recordings_;
}

#if defined(OS_LINUX) || defined(__APPLE__)
auto Engine::streaming() const -> std::shared_ptr<suitecut::StreamingControls> {
  return streaming_;
}
#endif

auto Engine::selectors() const -> const SuiteCutSelectorRegistry& { return selectors_; }

auto Engine::HTTPDiagnostics(Dict output) const -> void {
  const auto source_lifetimes = suitecut::NativeSourceLifetimes().Inspect();
  output->SetDouble("nativeSourcesConstructed", double(source_lifetimes.constructed));
  output->SetDouble("nativeSourcesDestroyed", double(source_lifetimes.destroyed));
  output->SetDouble("nativeSourcesLive", double(source_lifetimes.live));
  output->SetDouble("nativeSourceRegistryEntries", double(sources_.size()));
  output->SetDouble("nativeContextRegistryEntries", double(contexts_->ContextCount()));
  output->SetDouble("nativePageRegistryEntries", double(contexts_->PageCount()));
  const auto tasks = suitecut::NativeProcessTaskWork().Inspect();
  output->SetDouble("nativeTasksQueued", double(tasks.queued));
  output->SetDouble("nativeTasksExecuting", double(tasks.executing));
  output->SetDouble("nativeTasksReturnedHeld", double(tasks.returned_held));
  output->SetDouble("nativeTasksAdmitted", double(tasks.admitted));
  output->SetDouble("nativeTasksReturned", double(tasks.returned));
  output->SetDouble("nativeTasksDestroyed", double(tasks.destroyed));
  output->SetDouble("nativeTasksNeverExecuted", double(tasks.never_executed));
  output->SetBool("nativeTasksPoisoned", tasks.poisoned);
  const auto allocator = suitecut::InspectNativeGlibcMemory();
  output->SetBool("nativeGlibcAllocatorStatsAvailable", allocator.available);
  if (allocator.available) {
    output->SetDouble("nativeGlibcArenaBytes", double(allocator.arena_bytes));
    output->SetDouble("nativeGlibcAllocatedBytes", double(allocator.allocated_bytes));
    output->SetDouble("nativeGlibcFreeBytes", double(allocator.free_bytes));
    output->SetDouble("nativeGlibcMmapBytes", double(allocator.mmap_bytes));
    output->SetDouble("nativeGlibcTopReleasableBytes", double(allocator.top_releasable_bytes));
  }
  const auto receipts = inspector_work_->Inspect();
  output->SetInt("nativeAcceptedInspectorChannels", int(receipts.channels));
  output->SetInt("nativeAcceptedInspectorQueued", int(receipts.queued));
  output->SetInt("nativeAcceptedInspectorSubmitted", int(receipts.submitted));
  output->SetInt("nativeAcceptedInspectorCallbacks", int(receipts.callbacks));
  output->SetBool("nativeAcceptedInspectorPoisoned", receipts.poisoned);
  output->SetBool("nativeAcceptedInspectorReceiptLaneReady", inspector_work_->ReceiptLaneReady());
  output->SetString("nativeAcceptedInspectorAccepted", std::to_string(receipts.accepted));
  output->SetString("nativeAcceptedInspectorReplies", std::to_string(receipts.replied));
  output->SetString("nativeAcceptedInspectorRefused", std::to_string(receipts.refused));
  output->SetString("nativeAcceptedInspectorUnwound", std::to_string(receipts.unwound));
  output->SetInt("nativeRequestBodyPhysical", int(suitecut::NativeRequestBodyReads::Physical()));
  output->SetInt("nativePrivateBodyQueuedReads",
                 int(suitecut::NativePrivateResponseSession::Waiting()));
  output->SetInt("nativePrivateBodyInnerReceipts",
                 int(suitecut::NativePrivateResponseSession::Physical()));
  output->SetInt("nativePrivateBodySessionReservations",
                 int(suitecut::NativePrivateResponseSession::Sessions()));
  output->SetInt("nativePrivateBodyRecords",
                 int(suitecut::NativePrivateResponseSession::Records()));
  output->SetInt("nativeImageStreamPhysical", int(suitecut::NativeResponseStreams::Physical()));
  output->SetDouble("nativeImageStreamRetainedBytes",
                    double(suitecut::NativeResponseStreams::RetainedBytes()));
  output->SetInt("nativeImageStreamEntries", int(suitecut::NativeResponseStreams::Entries()));
  output->SetInt("nativeContextUnroutePending", int(context_unroute_all_->pending()));
  output->SetInt("nativeContextRoutePhysical", int(context_routes_->physical()));
  output->SetInt("nativeContextRoutePending", int(context_routes_->pending()));
  output->SetInt("nativeContextRouteTargets", int(context_routes_->targets()));
  output->SetDouble("nativeContextRouteMetadataBytes",
                    double(suitecut::NativeContextRouteRegistration::retained_bytes()));
  output->SetInt("nativeContextHARPhysical", int(context_har_->physical()));
  output->SetInt("nativeContextHARTargets", int(context_har_->targets()));
  output->SetInt("nativeContextHARPending", int(context_har_->pending()));
  output->SetDouble("nativeContextHARPolicyBytes", double(context_har_->retained_bytes()));
  output->SetInt("nativeHARFilePhysical", int(har_files_->Physical()));
  output->SetInt("nativeHARCommitPhysical", int(har_recording_->Physical()));
  output->SetDouble("nativeHARRetainedBytes", double(network_archive_->retained_bytes()));
  output->SetDouble("nativeHARPageMetadataBytes", double(network_archive_->page_metadata_bytes()));
  output->SetDouble("nativeHARReplayBytes",
                    double(suitecut::NativeHARReplayFiles::RetainedBytes()));
  http_->Diagnostics(output);
  downloads_->Diagnostics(output);
  workers_->Diagnostics(output);
  traces_->Diagnostics(output);
  output->SetInt("nativeTraceDOMPhysical", int(trace_dom_physical_));
  output->SetInt("nativeTraceProtocolPhysical", int(trace_physical_));
  output->SetInt("nativeTracePreviewPhysical", int(trace_preview_->PhysicalCount()));
  output->SetInt("nativeTraceSourcesPhysical", int(trace_sources_->PhysicalCount()));
  output->SetInt("nativeTraceBackgroundPhysical", int(trace_background_.PhysicalCount()));
  output->SetInt("nativeTraceBackgroundFailures", int(trace_background_.Failures()));
  output->SetInt("nativeTraceBackgroundWorkerLimit", int(trace_background_.WorkerLimit()));
  output->SetInt("nativeTraceBackgroundWorkers", int(trace_background_.WorkerCount()));
  output->SetDouble("nativeTraceBackgroundCapacity", double(trace_background_.Capacity()));
  output->SetDouble("nativeTraceBackgroundWorkerStartFailures",
                    double(trace_background_.WorkerStartFailures()));
  output->SetInt("nativeArtifactInputReceipts", int(ArtifactInputReceipts()));
  output->SetInt("nativeArtifactInputRetained", int(screenshot_inputs_->RetainedCount()));
  output->SetInt("nativeScreenshotInputLogical", int(screenshot_inputs_->LogicalCount()));
  output->SetInt("nativeScreenshotInputPhysical", int(screenshot_inputs_->PhysicalCount()));
  output->SetInt("nativeScreenshotComparisonLogical", int(screenshot_comparisons_->LogicalCount()));
  output->SetInt("nativeScreenshotComparisonPhysical",
                 int(screenshot_comparisons_->PhysicalCount()));
  output->SetDouble("nativeScreenshotComparisonBytes", double(screenshot_comparisons_->Bytes()));
  output->SetInt("nativeContextVideoPhysical", static_cast<int>(context_video_->Physical()));
  output->SetInt("nativeRecordingPhysical", static_cast<int>(recordings_->PhysicalCount()));
  output->SetInt("nativeRecordingMetadata", static_cast<int>(recordings_->MetadataCount()));
#if defined(OS_LINUX) || defined(__APPLE__)
  output->SetInt("nativeStreamingPhysical", static_cast<int>(streaming_->PhysicalCount()));
  output->SetInt("nativeStreamingMetadata", static_cast<int>(streaming_->MetadataCount()));
#endif
}

}  // namespace suitecut::browser
