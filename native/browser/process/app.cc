// SPDX-License-Identifier: MIT
#include "process/app.h"
namespace suitecut::browser {
App::App(Writer& writer) : writer_(writer), startup_started_(Now()) {}

auto App::GetBrowserProcessHandler() -> CefRefPtr<CefBrowserProcessHandler> { return this; }

auto App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) -> void {
  RegisterSuiteCutInitScheme(registrar);
}

auto App::OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command)
    -> void {
#if defined(__APPLE__)
  // Disposable incognito automation profiles have no durable cookie secrets
  // to protect. Chromium otherwise queries the shared Safe Storage key even
  // for these sessions, prompting again after every ad-hoc signed rebuild.
  // Use its process-local test keychain only for nonpersistent profiles;
  // explicitly persistent profiles retain real OS-backed encryption.
  if (command->GetSwitchValue("suitecut-persistent-profile") != "1")
    command->AppendSwitch("use-mock-keychain");
#endif
  // Context storage state has one localStorage/IndexedDB/OPFS namespace per
  // origin, including third-party frames. Keep this browser policy explicit.
  std::vector<std::string> disabled_features{"ThirdPartyStoragePartitioning"};
#if defined(__linux__)
  if (command->GetSwitchValue("suitecut-headless") == "1") {
    // Chromium paint holding keeps a new primary page's input router inactive
    // until it produces visually different content. A transparent/empty OSR
    // document must accept real input without an on-demand screenshot redraw.
    disabled_features.emplace_back("PaintHolding");
  }
#endif
  auto disabled = command->GetSwitchValue("disable-features").ToString();
  for (const auto& feature : disabled_features) {
    bool present = false;
    for (size_t start = 0; start <= disabled.size();) {
      const auto end = disabled.find(',', start);
      auto token = disabled.substr(start, end == std::string::npos ? end : end - start);
      const auto first = token.find_first_not_of(" \t");
      const auto last = token.find_last_not_of(" \t");
      if (first != std::string::npos) token = token.substr(first, last - first + 1);
      if (token == feature || token.starts_with(feature + "<") || token.starts_with(feature + ":"))
        present = true;
      if (end == std::string::npos) break;
      start = end + 1;
    }
    if (!present) disabled += (disabled.empty() ? "" : ",") + feature;
  }
  command->AppendSwitchWithValue("disable-features", disabled);
  command->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
  command->AppendSwitch("disable-background-timer-throttling");
  command->AppendSwitch("disable-renderer-backgrounding");
#if defined(__linux__)
  if (command->GetSwitchValue("suitecut-headless") == "1") {
    command->AppendSwitchWithValue("ozone-platform", "headless");
    // Keep display-free layout and screenshot pixels free of native scrollbar
    // widgets from startup, without resizing the page during each capture.
    command->AppendSwitch("hide-scrollbars");
    command->AppendSwitch("disable-gpu");
    // Software compositing remains display-free; explicitly select the CEF
    // bundled software GL driver because Chromium removed implicit fallback.
    command->AppendSwitchWithValue("use-gl", "angle");
    command->AppendSwitchWithValue("use-angle", "swiftshader");
    command->AppendSwitch("enable-unsafe-swiftshader");
  }
#endif
}

auto App::OnContextInitialized() -> void {
  auto command = CefCommandLine::GetGlobalCommandLine();
  const int width = std::atoi(command->GetSwitchValue("suitecut-width").ToString().c_str());
  const int height = std::atoi(command->GetSwitchValue("suitecut-height").ToString().c_str());
  const auto parsed_fps =
      ParseSuiteCutFrameRate(command->GetSwitchValue("suitecut-fps").ToString());
  if (!parsed_fps) {
    CefQuitMessageLoop();
    return;
  }
  const int fps = *parsed_fps;
  const double scale = std::atof(command->GetSwitchValue("suitecut-scale").ToString().c_str());
  const auto pixel_format = command->GetSwitchValue("suitecut-pixel-format").ToString();
  if (width < 2 || height < 2 || (scale != 1 && scale != 2) || width * scale > 3840 ||
      height * scale > 2160) {
    CefQuitMessageLoop();
    return;
  }
  if ((pixel_format != "bgra" && pixel_format != "i420") ||
      (pixel_format == "i420" && ((width % 2) || (height % 2)))) {
    CefQuitMessageLoop();
    return;
  }
  CefWindowInfo window;
  window.SetAsWindowless(0);
#if defined(__APPLE__)
  window.shared_texture_enabled =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-shared-texture") != "0";
#endif
  CefBrowserSettings settings;
  settings.windowless_frame_rate = fps;
  settings.background_color = CefColorSetARGB(255, 0, 0, 0);
  engine_ = std::make_unique<Engine>(writer_, width, height, scale, pixel_format == "i420", fps);
#if defined(__linux__)
  engine_->BindAcceptedWork(native_work_endpoint_);
#endif
  const suitecut::NativeWorkOriginFrame actual_worker_startup(
      suitecut::NativeWorkOrigin::ResidentWorker(engine_->AcceptedSourceTicket()));
  Dict launch_video;
  int startup_timeout_ms = 0;
  std::string launch_error;
  if (command->HasSwitch("suitecut-record-video") &&
      !suitecut::ParseLaunchVideoOptions(
          command->GetSwitchValue("suitecut-record-video").ToString(),
          command->GetSwitchValue("suitecut-startup-timeout-ms").ToString(), launch_video,
          startup_timeout_ms, launch_error)) {
    writer_.Error(launch_error.c_str());
    CefQuitMessageLoop();
    return;
  }
  if (!engine_->Initialize(launch_video,
                           launch_video ? startup_started_ + startup_timeout_ms : 0)) {
    CefQuitMessageLoop();
    return;
  }
}

auto App::ReleaseSource() -> void { engine_.reset(); }

}  // namespace suitecut::browser
