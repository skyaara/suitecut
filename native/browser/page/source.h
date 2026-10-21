// SPDX-License-Identifier: MIT
#pragma once
#include "runtime/engine.h"
namespace suitecut::browser {
class Source : public CefClient,
               public CefLifeSpanHandler,
               public CefRenderHandler,
               public CefAudioHandler,
               public CefLoadHandler,
               public CefRequestHandler,
               public CefDevToolsMessageObserver,
               public CefJSDialogHandler,
               public CefDownloadHandler,
               public CefContextMenuHandler,
               public CefDisplayHandler,
               public CefPermissionHandler,
               public CefDialogHandler,
               public CefFrameHandler {
  // Declared first so every other Source member is destroyed before its real
  // object lifetime is retired; constructor unwinding is balanced as well.
  suitecut::NativeSourceLifetimeReceipt native_source_lifetime_;

 public:
  Source(Writer& writer, Engine& engine, int page, int width, int height, double scale, bool i420);
  friend class Engine;
  void StopReader();
  void SendPacket(Dict header, std::vector<uint8_t> data = {});
  void Error(const char* message);
  CefRefPtr<CefFrameHandler> GetFrameHandler() override;
  void OnFrameCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) override;
  void OnFrameDestroyed(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame) override;
  void OnFrameDetached(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                CefProcessId sender, CefRefPtr<CefProcessMessage> message) override;
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override;
  CefRefPtr<CefDialogHandler> GetDialogHandler() override;
  bool OnFileDialog(CefRefPtr<CefBrowser>, FileDialogMode mode, const CefString&, const CefString&,
                    const std::vector<CefString>&, const std::vector<CefString>&,
                    const std::vector<CefString>&,
                    CefRefPtr<CefFileDialogCallback> callback) override;
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString& origin,
                              uint32_t requested,
                              CefRefPtr<CefPermissionPromptCallback> callback) override;
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                      const CefString& origin, uint32_t requested,
                                      CefRefPtr<CefMediaAccessCallback> callback) override;
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  CefRefPtr<CefRenderHandler> GetRenderHandler() override;
  CefRefPtr<CefAudioHandler> GetAudioHandler() override;
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  CefRefPtr<CefRequestHandler> GetRequestHandler() override;
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override;
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override;
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override;
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                       const CefString& address) override;
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                        const CefString& name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser> browser,
                                                                 CefRefPtr<CefFrame> frame,
                                                                 CefRefPtr<CefRequest> request,
                                                                 bool navigation, bool download,
                                                                 const CefString&, bool&) override;
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString& origin, JSDialogType type,
                  const CefString& message, const CefString& default_value,
                  CefRefPtr<CefJSDialogCallback> callback, bool& suppress) override;
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString& message, bool,
                            CefRefPtr<CefJSDialogCallback> callback) override;
  void OnResetDialogState(CefRefPtr<CefBrowser>) override;
  void OnDialogClosed(CefRefPtr<CefBrowser>) override;
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                           CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) override;
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void NativeStartContextHeaders();
  void NativeStartContextHAR();
#include "controllers/network/context_route_source.inc"
  bool ContextWorkerNetworkEvent(Dict event);
  void NativeStartPrivateResponseSession();
  static bool NativePrivateBodyFallback(bool actual_ok, const std::string& actual_json);
  std::string NativePrimaryBodyError(int owner, const std::string& json);
  void NativeImageStreamEvent(const std::string& method, Dict actual, const std::string& session);
  void NativeReadResponseBody(const std::string& session, const std::string& request,
                              double deadline, suitecut::NativeResponseBodyReads::Reply reply);
  void NativeReadRequestBody(const std::string& session, const std::string& request,
                             double deadline, suitecut::NativeRequestBodyReads::Reply reply,
                             suitecut::NativeRequestBodyReads::Receipt receipt);
  void OnBeforeClose(CefRefPtr<CefBrowser> actual_browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int popup_id,
                     const CefString& target_url, const CefString&,
                     CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures&,
                     CefWindowInfo& window, CefRefPtr<CefClient>& client, CefBrowserSettings&,
                     CefRefPtr<CefDictionaryValue>& extra_info, bool*) override;
  void OnBeforePopupAborted(CefRefPtr<CefBrowser>, int popup_id) override;
  [[gnu::noinline]] void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
                                 const RectList& dirty, const void* buffer, int width,
                                 int height) override;
#if defined(__APPLE__)
  void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                          const RectList& dirty, const CefAcceleratedPaintInfo& info) override;
#endif
  void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override;
  bool GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters& params) override;
  void OnAudioStreamStarted(CefRefPtr<CefBrowser>, const CefAudioParameters& params,
                            int channels) override;
  void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** data, int frames,
                           int64_t pts) override;
  void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override;
  void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString&) override;
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override;
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                   const CefString&, const CefString&) override;
  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) override;
  void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, TransitionType) override;
  void OnRenderViewReady(CefRefPtr<CefBrowser>) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus, int,
                                 const CefString&) override;
  bool OnDevToolsMessage(CefRefPtr<CefBrowser>, const void* message, size_t size) override;
  void ToolResult(int id, bool success, const void* result, size_t size,
                  const std::string& session);
  std::set<std::string> init_snapshot_requests_;
  struct ChildBootstrap {
    std::string frame;
    bool ignored = false;
    int phase = 0, remaining = 2;
    double deadline = 0;
  };
  std::map<std::string, ChildBootstrap> pending_child_bootstraps_;
  bool child_bootstrap_task_ = false, child_bootstrap_timer_ = false,
       child_bootstrap_failed_ = false;
  void FailChildBootstrap(const std::string& error);
  void WakeChildBootstrapDeadlines();
  void ChildBootstrapFinished(const std::string& session, const std::string& frame, bool ok,
                              const std::string& error);
  void DeferChildBootstrap(const std::string& session, const std::string& frame, bool ignored);
  void WakeChildBootstraps();
  std::deque<std::pair<Dict, std::string>> init_snapshot_continuations_;
  size_t init_snapshot_continuation_count_ = 0;
  bool init_snapshot_continuation_task_ = false;
  void WakeInitSnapshotContinuations();
  bool InitSnapshotTraffic(const std::string& method, Dict data, const std::string& session);
  void ToolEvent(const std::string& method, const void* params, size_t size,
                 const std::string& session);

 private:
  using DevToolsCallback = std::function<void(bool, const std::string&)>;
  struct DevToolsCall {
    int owner;
    DevToolsCallback callback;
    std::string session;
    bool retain_after_cancel = false;
    size_t response_limit = kMaxResponseBytes;
    bool retained_control = false;
    bool lossless_scalars = false;
    uint64_t artifact_generation = 0;
    bool artifact_capture = false;
    bool selector_group_release = false;
  };
  struct NetworkCall {
    std::string session;
    suitecut::NetworkControls::Result callback;
    double deadline;
    bool expired = false;
  };
  struct BindingCall {
    std::string session;
    suitecut::BindingControls::Result callback;
    double deadline;
    bool expired = false;
  };
  struct UtilityWorld {
    std::string frame, session, loader;
  };
  struct ObjectHandle {
    std::string object_id, group, frame_id;
    int context = 0;
    std::string session, node_id, context_identity;
    uint64_t renderer_epoch = 0;
  };
  struct FrameState {
    std::string id, parent, name, url;
    int context = 0, utility_context = 0;
    std::string session, origin, loader, context_identity, utility_identity;
    uint64_t context_generation = 0;
    std::set<std::string> milestones;
    uint64_t navigation = 0;
    double network_activity = 0;
  };
  void FrameContextChanged(FrameState& frame);
  struct Navigation {
    std::string frame, url, wait, loader, previous_loader, api;
    int timeout = 30000;
    uint64_t after = 0, history_candidate = 0, native_download_after = 0;
    std::string download_abort, download_abort_url;
    bool download_created = false;
    int history_entry = -1;
    std::string history_url;
    bool history_pending = false, history_verified = false;
    bool dispatched = false, same_document = false, wait_only = false, next = false,
         replace_initial = false, recovery = false;
  };
  struct Document {
    std::string frame, request, session, loader, method, url;
    Dict response;
    bool finished = false;
    std::string error;
    bool document = false;
  };
  struct ActiveRequest {
    std::string frame;
    double started;
  };
  struct BodyWait {
    std::string key, frame, loader;
    bool dispatched = false;
  };
#include "controllers/dom/element_state_controller.inc"
#include "controllers/dom/selector_wait_controller.inc"
#include "controllers/dom/frame_element_controller.inc"
#include "controllers/dom/owner_frame_controller.inc"
#include "controllers/dom/query_selection_controller.inc"
#include "controllers/dom/handle_disposal_controller.inc"
#include "controllers/lifecycle/page_error_controller.inc"
#include "controllers/lifecycle/console_controller.inc"
  Dict FrameJson(const FrameState& frame);
  struct EventRecord {
    uint64_t sequence;
    std::string event, json;
  };
  void JournalEvent(uint64_t sequence, const std::string& event, const std::string& json);
  void FrameEvent(const char* event, const FrameState& frame);
  void StoreFrame(Dict info, const char* event, const std::string& session = "",
                  bool restored = false);
  void ImportFrameTree(Dict tree, const std::string& session = "", int observed_tree = 0);
  bool http_user_agent_pending_ = false;
  void MaybeReady();
  void DetachFrame(const std::string& root);
  void InvalidateFrameHandles(const std::string& frame);
  std::string FrameId(Dict message);
  void EnsureUtilityWorld(const std::string& id);
  void WithFrame(int owner, const std::string& frame, std::function<void(int)> action,
                 bool utility = false);
  struct ArgumentWork {
    int owner, context;
    std::uint64_t generation;
    size_t index = 0;
    std::vector<ObjectHandle> handles;
    Dict params;
    CefRefPtr<CefListValue> arguments;
    std::function<void()> done;
  };
  void ArgumentStep(std::shared_ptr<ArgumentWork> work);
  std::string HandleGuid(int id);
  std::string MissingHandle(int id);
  void HandleArguments(int id, Dict message, Dict params, bool destination_receiver,
                       std::function<void()> done);
  void Handle(int id, Dict message, bool frame_ready = false);
#include "controllers/input/input_work.inc"
#include "controllers/input/widget_mouse_source.inc"
  double Number(Dict message, const char* key);
  Dict KeyPacket(const std::string& name, bool down);
#include "controllers/input/input_retirement.inc"
#include "controllers/input/input_queue.inc"
  Dict MousePacket(const std::string& type, const std::string& button, int count);
  void DispatchCefMouse(Dict params);
  void InputCall(std::shared_ptr<InputWork> work, const std::string& method, Dict params,
                 std::function<void()> done);
  void ContinueInput(std::shared_ptr<InputWork> work, int delay = 0);
  void PumpInput();
  void InputStep(std::shared_ptr<InputWork> work);
#include "controllers/input/input_admission.inc"
  void SetupLocatorModifiers(int id, Dict message);
  void RestoreLocatorModifiers(int id);
  void CompleteVisibilityEmpty(int id, uint64_t generation = 0);
  void CompleteVisibilityInFrame(const std::string& frame);
  void CompleteVisibilityInContext(const std::string& session, int context = 0,
                                   const std::string& identity = "");
  bool VisibilityScopeGone(int id) const;
  void CompleteCountZero(int id);
  void CompleteCountsInFrame(const std::string& frame);
  void CompleteCountsInContext(const std::string& session, int context = 0,
                               const std::string& identity = "");
  void Complete(int id, bool ok, const std::string& error = "", const std::string& json = "");
  void WakeDeadlines();
  bool BindingCommandsNeedTick() const;
  void Deadlines();
  int AllocateDevToolsId();
  bool SubmitDevTools(int id, const std::string& method, Dict params, const std::string& session);
  void MarkFinalReleaseSubmitted(int command);
  int SendDevTools(const std::string& method, Dict params, const std::string& session = "");
  void EnableSession(const std::string& session);
  void AutoAttach(const std::string& session);
  void DevTools(int owner, const std::string& method, Dict params, DevToolsCallback callback,
                std::optional<std::string> target_session = std::nullopt,
                bool retained_control = false, size_t response_limit = kMaxResponseBytes,
                bool lossless_scalars = false);
  void Evaluate(int owner, const std::string& expression, DevToolsCallback callback,
                const std::string& frame = "", bool utility = false,
                const std::string& assertion_matcher = "");
  void ResolveFramePath(int id, uint64_t generation, Dict message, size_t index,
                        const std::string& frame, std::function<void(Dict)> done,
                        std::function<void()> unavailable = {});
  void MapFramePoint(int id, const std::string& frame, double x, double y, double width,
                     double height, std::function<void(double, double)> done);
  Dict QueryOptions(bool metadata = false);
  bool QueryNeedsSelectorWorld(Dict message, bool isolated);
  bool QueryNeedsMainWorld(Dict message);
  bool QueryNeedsIsolatedWorld(Dict message);
  bool QueryUsesMultipleWorlds(Dict message);
  void QueryNodesInMainWorld(int id, Dict message, std::function<void(const std::string&)> done);
  void QueryNodesInWorld(int id, Dict message, bool to_utility,
                         std::function<void(const std::string&)> done);
  void QueryInspect(int id, Dict message, const std::string& body, bool utility, bool by_value,
                    DevToolsCallback callback);
  std::string QueryOptionsExpression(bool metadata = false);
  // All callers execute this expression over the already selected utility
  // nodes. Candidate aliases use only authored builtin engines and a single
  // aggregate work budget; registered selector factories are never reentered.
  std::string QueryDiagnosticExpression(Dict message, const std::string& nodes);
  std::string QueryExpression(Dict message, const std::string& native_query_intrinsics = "");
  void EvaluateFixedNode(int id, Dict config, const std::string& expression,
                         DevToolsCallback callback);
#include "controllers/input/send_keys.inc"
#include "controllers/input/targeted_keyboard.inc"
#include "controllers/input/send_keys_sequence.inc"
#include "controllers/dom/scroll_controller.inc"
#include "controllers/input/select_text.inc"
#include "controllers/input/blur.inc"
#include "controllers/debugger/pristine_dispatch_source.inc"
#include "controllers/input/dispatch_event.inc"
#include "controllers/debugger/pristine_dispatch_actions.inc"
#include "controllers/input/input_value.inc"
  void LocatorAction(int id, std::uint64_t generation, const std::string& selector,
                     const std::string& action, const std::string& text, Dict config,
                     std::shared_ptr<InputWork> pointer_work = nullptr);
#include "controllers/context/binding_argument_handle.inc"
#include "controllers/dom/ax_controller.inc"
#include "controllers/dom/dom_semantic_controller.inc"
#include "controllers/dom/snapshot_reference_dispatch.inc"
#include "controllers/dom/snapshot_reference_controller.inc"
#include "controllers/dom/locator_normalization_controller.inc"
#include "controllers/dom/locator_suggestion_controller.inc"
#include "controllers/dom/locator_pick_controller.inc"
#include "controllers/dom/native_highlight_source.inc"
#include "controllers/observability/trace_source.inc"
#include "controllers/debugger/native_debugger_controller.inc"
#include "controllers/observability/coverage_source.inc"
#include "controllers/debugger/developer_debugger_source.inc"
#include "controllers/debugger/developer_debugger_documents_source.inc"
#include "controllers/debugger/developer_debugger_frames_source.inc"
#include "controllers/workers/worker_controller.inc"
#include "controllers/workers/developer_debugger_worker_documents_source.inc"
#include "controllers/workers/developer_debugger_workers_source.inc"
#include "controllers/workers/worker_session_retirement.inc"
#include "controllers/workers/worker_console.inc"
#include "controllers/workers/worker_target_lifetime.inc"
#include "controllers/media/media_emulation.inc"
#include "controllers/input/touch_input.inc"
#include "controllers/input/form_controller.inc"
#include "controllers/input/drag_controller.inc"
#include "controllers/input/file_chooser_controller.inc"
#include "controllers/artifacts/download_controller.inc"
#include "controllers/media/recording_controller.inc"
#include "controllers/media/streaming_controller.inc"
#include "controllers/dom/element_geometry.inc"
#include "controllers/dom/locator_predicate.inc"
#include "controllers/dom/aria_json.inc"
#include "controllers/artifacts/screenshot_assertion_scope.inc"
#include "controllers/artifacts/screenshot_assertion_capture.inc"
#include "controllers/artifacts/screenshot_assertion_sender.inc"
#include "controllers/artifacts/screenshot_assertion_view.inc"
#include "controllers/artifacts/artifact_view_lease.inc"
#include "controllers/artifacts/screenshot_comparison.inc"
#include "controllers/artifacts/artifact_config_inputs.inc"
#include "controllers/artifacts/artifact_navigation.inc"
#include "controllers/artifacts/artifact_documents.inc"
#include "controllers/artifacts/artifact_before_screenshot.inc"
#include "controllers/artifacts/artifact_geometry.inc"
#include "controllers/artifacts/artifact_masks.inc"
#include "controllers/artifacts/screenshot_assertion_cleanup.inc"
#include "controllers/artifacts/screenshot_assertion_finalization.inc"
#include "controllers/lifecycle/action_hooks.inc"
#include "controllers/dom/locator_handler_references.inc"
#include "controllers/dom/locator_handler_source.inc"
#include "controllers/network/websocket_predicates.inc"
#include "controllers/network/websocket_routing.inc"
#include "controllers/network/websocket_policy_source.inc"
#include "controllers/lifecycle/event_predicates.inc"
#include "controllers/lifecycle/virtual_time_source.inc"
#include "controllers/lifecycle/crash_controller.inc"
#include "controllers/lifecycle/recovery_controller.inc"
#include "controllers/work/accepted_inspector_owner.inc"
#include "controllers/debugger/devtools_dispatch.inc"
#include "controllers/lifecycle/url_predicates.inc"
#include "controllers/lifecycle/action_navigation.inc"
  bool Descendant(const std::string& child, const std::string& root);
  void NetworkActivity(const std::string& id);
#include "controllers/network/har_byte_source.inc"
  void NativeNetworkSend(const std::string& session, const std::string& method, Dict params,
                         suitecut::NetworkControls::Result callback, double deadline);
  void NativeFrameRequestSession(const std::string& request, const std::string& previous,
                                 const std::string& session);
  void NativeWorkerInitialRequestSession(const std::string& target, const std::string& parent,
                                         const std::string& child, const std::string& url);
  void AttributeInitialPopupRequest(Dict data);
  void NetworkEvent(const std::string& method, Dict data, const std::string& session);
  void CheckBodies();
  void BeginNavigation(int id, Dict message, const std::string& command, int timeout);
  void CheckNavigations();
  void ScheduleRecordingSnapshot(const std::string& id, int delay = 25);
  [[gnu::noinline]] void EmitFrame(double timestamp = Now(), bool clocked_capture = false);
  void ValidateVisibilitySelectors(int id, uint64_t generation, Dict message);
#include "controllers/lifecycle/expectation_deadline.inc"
#include "controllers/lifecycle/assertion_deadline_source.inc"
  std::set<int> author_query_evaluations_;
  void QueryEvaluate(int id, uint64_t generation, Dict message);
  void RetryContext(int id);
  void Command(const std::string& line);
#include "controllers/lifecycle/page_activation_source.inc"
#include "controllers/lifecycle/sdk_browser_control.inc"
  void DispatchCommand(int id, Dict message, const std::string& command, double timeout);
#include "controllers/work/accepted_source_work.inc"
  void ReadCommands();
  const suitecut::NativeWorkContinuation native_creation_origin_;
  std::shared_ptr<suitecut::NativeLocatorHandlerAsyncWork> native_creation_handler_receipt_ =
      suitecut::RetainNativeLocatorHandlerAsync();
  Writer& writer_;
  Engine& engine_;
  const int page_id_;
  bool closing_page_ = false;
  std::string closed_reason_;
  int width_, height_;
  const int capture_width_, capture_height_;
  double scale_;
  bool i420_;
  bool frame_transport_enabled_ = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue(
                                      "suitecut-frame-transport") != "on-demand";
  int frame_width_ = 0, frame_height_ = 0, popup_width_ = 0, popup_height_ = 0, navigation_ = 0;
  int reported_view_width_ = 0, reported_view_height_ = 0;
  int frame_css_width_ = 0, frame_css_height_ = 0;
  bool renderer_failed_ = false;
  bool show_popup_ = false, ready_ = false, initial_loaded_ = false, deadline_timer_active_ = false;
  CefRect popup_rect_;
  std::vector<uint8_t> view_, popup_;
  uint64_t capture_clock_sequence_ = 0;
  SuiteCutCaptureClock native_paint_clocks_[2];
  std::atomic<double> native_audio_clock_offset_{std::numeric_limits<double>::quiet_NaN()};
  const bool timing_trace_ = std::getenv("SUITECUT_CAPTURE_TIMING_TRACE") != nullptr;
  std::optional<double> native_paint_timestamp_;
  uint64_t native_paints_ = 0;
  bool native_zero_copy_recording_ = false, native_view_stale_ = false;
  double last_view_delivery_ms_ = 0;
  std::string recording_snapshot_pending_;
#if defined(__APPLE__)
  std::unique_ptr<suitecut::MacCapturePool> native_capture_pool_;
  std::shared_ptr<suitecut::MacCaptureFrame> native_capture_snapshot_;
#endif

  // Damage is relative to the previous CEF paint, including paints deliberately
  // withheld during screenshot leases. Recover with one complete snapshot.
  bool view_damage_valid_ = false, popup_damage_valid_ = false;
  std::atomic<bool> stopped_{false}, audio_valid_{false};
  SuiteCutAudioClock audio_clock_;
  std::mutex command_mutex_;
  std::condition_variable command_capacity_;
  int queued_commands_ = 0;
  SuiteCutOperationRegistry operations_;
  std::map<std::string, FrameState> frame_states_;
  std::set<int> native_dom_getters_;
  suitecut::VisibilitySnapshotControls visibility_snapshots_;
  std::map<int, std::string> operation_frames_;
  std::map<int, std::string> operation_sessions_;
  std::map<std::string, std::string> sessions_;
  std::map<std::string, std::string> native_target_parents_;
  std::map<int, std::string> session_trees_;
  int next_devtools_ = 0;
  int diagnostic_network_enable_ =
      0;  // 0 unseen, positive real dispatch, -1 actual reply observed.
  size_t diagnostic_messages_ = 0;
  size_t command_diagnostic_messages_ = 0;
  std::map<int, std::string> event_waiters_;
  std::deque<EventRecord> event_history_;
  size_t event_history_bytes_ = 0;
  uint64_t event_sequence_ = 0;
  std::string main_frame_id_, last_cef_load_url_;
  int bootstrap_tree_ = 0;
  std::map<int, DevToolsCall> devtools_;
  std::map<int, std::function<void()>> context_retries_;
  std::map<int, Dict> artifact_configs_;
  std::map<int, uint64_t> artifact_attempts_;
  std::map<int, uint64_t> locator_attempts_;
  std::map<int, std::string> locator_logs_, locator_waits_;
  std::map<int, std::string> expect_results_;
  std::map<int, double> first_observations_;
  std::map<int, Dict> lazy_queries_;
  std::set<int> count_queries_;
  std::map<int, std::pair<std::string, int>> count_contexts_;
  std::set<int> context_retry_queued_;
  std::map<int, std::set<std::string>> evaluation_groups_;
  std::map<int, UtilityWorld> utility_worlds_;
  std::set<int> navigation_waiters_;
  std::map<int, Navigation> navigations_;
  std::map<int, BodyWait> body_waiters_;
  std::map<int, std::string> har_pending_;
  std::map<std::string, ActiveRequest> active_requests_;
  std::map<std::string, Document> documents_;
  std::deque<std::string> document_order_;

  std::map<int, ObjectHandle> handles_;
  std::unique_ptr<SuiteCutArtifactController> artifacts_;
  bool network_policy_ready_ = false;
  bool popup_initial_navigation_observed_ = false;
  std::unique_ptr<suitecut::NativeWebSocketControls> sockets_;
  suitecut::NativeNetworkPublicationResults network_publication_results_;
  std::set<int> network_publication_operations_;
  int network_context_id_ = 0;
  std::unique_ptr<suitecut::NativeRequestBodyReads> request_reads_;
  std::unique_ptr<suitecut::NativeResponseBodyReads> response_reads_;
  std::unique_ptr<suitecut::NativeResponseStreams> image_streams_;
  std::unique_ptr<suitecut::NativePrivateResponseSession> private_response_;
  bool private_response_ready_ = false;
  std::string private_response_error_;
  std::unique_ptr<suitecut::NetworkControls> network_;
  std::unique_ptr<SuiteCutDialogControls> dialogs_;
  std::map<int, NetworkCall> network_calls_;
#include "controllers/context/credential_source.inc"
#include "controllers/context/context_geolocation_source.inc"
#include "controllers/context/context_settings_source.inc"
#include "controllers/lifecycle/idle_emulation.inc"
#include "controllers/lifecycle/initial_navigation.inc"
  std::map<int, BindingCall> binding_calls_;
  std::unique_ptr<suitecut::BindingControls> bindings_;
  bool file_chooser_subscribed_ = false;
  Dict manual_drag_;
  std::map<int, std::shared_ptr<NativeDragWork>> drag_work_;
  std::map<int, std::function<void()>> form_clicks_;
  std::map<int, std::shared_ptr<SuiteCutUploadFiles>> form_uploads_;
  std::map<int, FormFolderObserver> folder_observers_;
  int folder_operation_ = 0;
  uint64_t folder_generation_ = 0;
  std::string folder_path_;
  bool folder_continued_ = false;
  SuiteCutUploadStore upload_store_;
  std::map<std::string, SuiteCutPendingUpload> pending_uploads_;
  size_t pending_upload_bytes_ = 0;
  std::map<int, std::string> pending_handle_groups_;
  std::map<int, std::shared_ptr<InputWork>> input_work_;
  std::map<int, std::vector<std::string>> locator_modifiers_;
  std::deque<int> mouse_queue_, keyboard_queue_;
  int mouse_input_ = 0, keyboard_input_ = 0, mouse_buttons_ = 0, key_modifiers_ = 0;
  double mouse_x_ = 0, mouse_y_ = 0;
  std::map<std::string, SuiteCutKey> pressed_keys_;
  int next_handle_ = 0;
  const std::string handle_namespace_;
  std::thread reader_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> observer_;
  IMPLEMENT_REFCOUNTING(Source);
};

}  // namespace suitecut::browser
