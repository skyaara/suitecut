// SPDX-License-Identifier: MIT
#pragma once
#include "runtime/dependencies.h"
#include "protocol/values.h"
#include "protocol/writer.h"
#include "runtime/tasks.h"
namespace suitecut::browser {
class Source;
class Engine {
 public:
  Engine(Writer& writer, int width, int height, double scale, bool i420, int fps);
  ~Engine();
  std::shared_ptr<bool> Alive() const;
  int CaptureRate() const;
  bool Initialize(Dict launch_video = nullptr, double startup_deadline = 0);
  bool StartupReady(int page);
  void StartupFailed(const std::string& error);
  Dict VideoMetadata(int page) const;
  bool CreatePage(int page, int context, CefRefPtr<CefRequestContext> request_context);
  CefRefPtr<Source> PopupClient(int opener, int popup_id, const std::string& initial_url);
  bool PageCreated(int page, CefRefPtr<CefBrowser> browser);
  void PageClosed(int page);
  void AbortPopup(int opener, int popup_id);
  void Route(const std::string& line);
  void CloseAll();
  void Idle();
  bool CanShutdown() const;
  void Deadlines();
  void CompleteContext(int id, bool ok, Dict result = nullptr, const std::string& error = "",
                       bool lossless_console = false);
  void ContextCommand(int id, uint64_t generation, const std::string& action, Dict params);
  suitecut::NativeContextWorkerNetwork& worker_network();
  void ContextWorkerNetwork(int context, const std::string& owner, Dict worker, Dict info);
  void ContextNetwork(int page, Dict info);
  void ContextConsole(int page, Dict info);
  std::shared_ptr<suitecut::AcceptedInspectorWork> AcceptedInspector() const noexcept;
  std::shared_ptr<suitecut::AcceptedReaderWork> AcceptedReader() const noexcept;
#if defined(__linux__)
  void BindAcceptedWork(suitecut::runner::RetainedSourceEndpoint* endpoint);
  uint64_t AcceptedSourceTicket() const noexcept;
  static double AcceptedWorkNow();
  void PumpAcceptedWork();
  void PostAcceptedWork();
  void OriginalAcceptedWorkBarrier(int id, Dict message);
#else
  uint64_t AcceptedSourceTicket() const noexcept;
#endif
  bool AcceptedWorkReady(bool viewer = false) const;
  uint64_t RetainLocatorHandler(int page, CefRefPtr<Source> source);
  void WakeLocatorHandlers();
  void TickLocatorHandlers();
  bool LocatorHandlerControl(int id, const std::string& command, Dict message);
  bool LocatorHandlerAdmission(Dict message, double& deadline) const;
  void LocatorHandlerChannelClosed();
  bool TrackLocatorHandlerCommand(int id, int page, uint64_t generation);
  void ForgetLocatorHandlerCommand(int id);
  suitecut::NativeWorkOrigin LocatorHandlerCommandOrigin(int id, int page,
                                                         uint64_t generation) const;
  bool LocatorHandlerDescendantsReady(uint64_t invocation) const;
  std::string http_user_agent_;
  suitecut::ContextSettingsRegistry& settings();
  suitecut::NativeContextGeolocation& geolocation();
  suitecut::NativeContextAuthenticationControls& authentication();
  void AuthenticationFatal(int context, const std::string& error);
  Dict BrowserInfo(int page) const;
  suitecut::NativeContextNetworkPolicy& network_policy();
  suitecut::NativeNetworkArchives& network_archive();
#include "controllers/network/context_route_engine.inc"
  suitecut::NativeWebSocketRoutes& websocket_routes();
  suitecut::NativeContextHARRoutes& har_routes();
  void HARPolicyFailure(const std::string& owner, const std::string& error);
  suitecut::NativeHARRecording& har_recording();
  suitecut::NativeHARReplayFiles& har_files();
  suitecut::NativeContextNetworkHeaders& network_headers();
  std::shared_ptr<suitecut::TraceControls> traces() const;
  std::shared_ptr<suitecut::ScreenshotComparisons> screenshot_comparisons() const;
  void ScreenshotInputControl(int id, Dict params, uint64_t generation);
  std::shared_ptr<suitecut::ScreenshotInputs> screenshot_inputs() const;
  size_t ArtifactInputReceipts() const;
  bool TraceClaim(bool dom = true);
  void TraceRelease(bool dom = true);
  std::vector<CefRefPtr<Source>> OwnerFrameSources(int page) const;
  bool TracePending(int context) const;
  void TracePaint(int page, int css_width, int css_height, double now, const void* pixels,
                  int width, int height);
  void WakeTrace();
  std::shared_ptr<SuiteCutContextControls> contexts() const;
  std::shared_ptr<SuiteCutHTTPControls> http() const;
  std::shared_ptr<suitecut::DownloadControls> downloads() const;
  std::shared_ptr<suitecut::RecordingControls> recordings() const;
#if defined(OS_LINUX) || defined(__APPLE__)
  std::shared_ptr<suitecut::StreamingControls> streaming() const;
  bool SelectStreamingSource(const std::string&, int page, std::string& error);
#endif
  const SuiteCutSelectorRegistry& selectors() const;
  void HTTPDiagnostics(Dict output) const;

 private:
  Writer& writer_;
#if defined(__linux__)
  struct NativeWorkBarrier {
    suitecut::runner::RetainedSourceWorkWire::Challenge challenge;
    int command = 0;
    suitecut::NativeStreamCut reader{};
    std::optional<suitecut::runner::RetainedSourceWorkWire::Cuts> cuts;
    bool observed = false, granted = false, refused = false;
    uint64_t resume_generation = 0;
  };
  suitecut::runner::RetainedSourceEndpoint* native_work_endpoint_ = nullptr;
  std::unique_ptr<suitecut::AcceptedEngineWorkWake> native_work_wake_;
  std::optional<NativeWorkBarrier> native_work_;
  uint64_t native_work_nonce_ = 0, native_work_generation_ = 0;
  bool native_work_installed_ = false, native_work_posted_ = false;
#endif
  std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work_ =
      std::make_shared<suitecut::AcceptedInspectorWork>();
  std::shared_ptr<suitecut::AcceptedReaderWork> reader_work_ =
      std::make_shared<suitecut::AcceptedReaderWork>();
  int width_, height_, fps_;
  double scale_;
  bool i420_, closing_ = false, deadline_timer_active_ = false;
  suitecut::NativeShutdownFence shutdown_fence_;
  std::map<int, CefRefPtr<Source>> sources_;
  std::map<int, CefRefPtr<Source>> locator_handler_leases_;
  uint64_t locator_handler_invocation_ = 0;
  struct LocatorHandlerCommand {
    suitecut::NativeWorkOrigin origin;
    int page = 0;
    uint64_t generation = 0;
  };
  std::map<int, LocatorHandlerCommand> locator_handler_commands_;
  CefRefPtr<Source> reader_source_;
  std::shared_ptr<SuiteCutContextControls> contexts_;
  std::shared_ptr<SuiteCutContextWorkers> workers_;
  std::shared_ptr<suitecut::TraceControls> traces_;
  std::shared_ptr<suitecut::TracePreview> trace_preview_;
  std::shared_ptr<suitecut::TraceSources> trace_sources_;
  size_t trace_physical_ = 0, trace_dom_physical_ = 0;
  std::map<int, std::pair<suitecut::TraceJournal::Lease, uint64_t>> trace_context_api_;
  suitecut::TraceBackground trace_background_;
  std::shared_ptr<suitecut::ScreenshotComparisons> screenshot_comparisons_;
  std::shared_ptr<suitecut::ScreenshotInputs> screenshot_inputs_;
  std::shared_ptr<SuiteCutUtilityBrowsers> utilities_;
  std::shared_ptr<SuiteCutHTTPControls> http_;
  std::shared_ptr<suitecut::DownloadControls> downloads_;
  std::shared_ptr<suitecut::RecordingControls> recordings_;
  std::shared_ptr<suitecut::NativeContextVideo> context_video_;
  struct VideoPreparation {
    uint64_t generation;
    suitecut::NativeContextVideo::Prepared value;
  };
  std::map<int, VideoPreparation> video_preparations_;
#if defined(OS_LINUX) || defined(__APPLE__)
  std::shared_ptr<suitecut::StreamingControls> streaming_;
#endif
  std::unique_ptr<suitecut::NativeContextConsoleControls> context_console_;
  std::unique_ptr<suitecut::NativeContextNetworkControls> context_network_;
  std::unique_ptr<suitecut::NativeContextWorkerNetwork> worker_network_;
  suitecut::NativeContextNetworkPolicy network_policy_;
  std::unique_ptr<suitecut::NativeNetworkArchives> network_archive_;
  std::unique_ptr<suitecut::NativeWebSocketRoutes> websocket_routes_;
  std::unique_ptr<suitecut::NativeContextRoutes> context_routes_;
  std::unique_ptr<suitecut::NativeContextHARRoutes> context_har_;
  std::unique_ptr<suitecut::NativeContextUnrouteAll> context_unroute_all_;
  std::shared_ptr<suitecut::NativeHARRecording> har_recording_;
  std::shared_ptr<suitecut::NativeHARReplayFiles> har_files_;
  suitecut::NativeContextNetworkHeaders network_headers_;
  suitecut::ContextSettingsRegistry settings_;
  suitecut::NativeContextGeolocation geolocation_;
  suitecut::NativeContextAuthenticationControls authentication_;
  std::map<int, std::shared_ptr<suitecut::NativeNetworkFatalTeardown>> authentication_failures_;
  SuiteCutOperationRegistry operations_;
  SuiteCutOperationRegistry startup_operations_;
  SuiteCutSelectorRegistry selectors_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace suitecut::browser
