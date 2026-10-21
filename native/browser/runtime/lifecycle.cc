// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
bool Engine::Initialize(Dict launch_video, double startup_deadline) {
  const auto context = CefRequestContext::GetGlobalContext();
  if (!contexts_->InitializeDefault(context)) return false;
  if (!launch_video) return CreatePage(1, 1, context);
  // A real startup operation exists before any SDK reader or public page. It
  // shares no client IDs or completion packets with the later command registry.
  const double now = Now();
  if (startup_deadline <= now) {
    StartupFailed("Native launch recording startup deadline expired");
    return true;
  }
  const auto operation = startup_operations_.Begin(1, now, startup_deadline - now);
  if (!operation) {
    StartupFailed("Native launch recording owner admission failed");
    return true;
  }
  const auto generation = operation->generation;
  context_video_->Prepare(
      1, generation, launch_video, startup_deadline,
      [this, alive = alive_, context, generation](suitecut::NativeContextVideo::Prepared prepared,
                                                  const std::string& error) {
        if (!*alive) return;
        if (closing_ || !startup_operations_.IsCurrent(1, generation) ||
            startup_operations_.IsExpired(1, generation, Now())) {
          context_video_->Discard(std::move(prepared));
          if (!closing_)
            StartupFailed("Native launch recording preparation exceeded its original deadline");
          return;
        }
        if (!error.empty()) {
          StartupFailed(error);
          return;
        }
        std::string failure;
        if (!context_video_->Configure(1, prepared, failure)) {
          context_video_->Discard(std::move(prepared));
          StartupFailed(failure);
          return;
        }
        if (!CreatePage(1, 1, context))
          StartupFailed("Native launch recording page creation failed");
      });
  // Rejections are delivered by Prepare as well. Its actual work retirement,
  // and every error close, remain owned by the normal engine shutdown barrier.
  WakeTrace();
  return true;
}
void Engine::StartupFailed(const std::string& error) {
  const auto operation = startup_operations_.Get(1);
  if (operation) context_video_->Cancel(1, operation->generation);
  startup_operations_.Clear();
  writer_.Error(error.c_str());
  // Before CreateBrowser admission, the reserved default page has no CEF
  // callback that could retire it. Do not leave shutdown waiting on that slot.
  if (!sources_.contains(1)) contexts_->PageCreationFailed(1, error);
  CloseAll();
}
bool Engine::StartupReady(int page) {
  if (page != 1 || !startup_operations_.Contains(1)) return !closing_;
  const auto operation = startup_operations_.Get(1);
  if (startup_operations_.IsExpired(1, operation->generation, Now())) {
    StartupFailed("Native launch recording page startup exceeded its original deadline");
    return false;
  }
  startup_operations_.Finish(1);
  return !closing_;
}
bool Engine::CreatePage(int page, int, CefRefPtr<CefRequestContext> context) {
  if (closing_) return false;
  const auto config = settings_.Get(contexts_->ContextId(page));
  const auto view = config && config->viewport ? *config->viewport
                                               : suitecut::ContextSettings::Size{width_, height_};
  const double scale = config && config->device_scale ? *config->device_scale : scale_;
  CefRefPtr<Source> source =
      new Source(writer_, *this, page, view.width, view.height, scale, i420_);
  sources_[page] = source;
  if (page == 1) reader_source_ = source;
  CefWindowInfo window;
  window.SetAsWindowless(0);
#if defined(__APPLE__)
  window.shared_texture_enabled =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-shared-texture") != "0";
#endif
  CefBrowserSettings settings;
  settings.windowless_frame_rate = fps_;
  settings.background_color = CefColorSetARGB(255, 0, 0, 0);
  if (CefBrowserHost::CreateBrowser(window, source, "about:blank", settings, BrowserInfo(page),
                                    context))
    return true;
  sources_.erase(page);
  return false;
}
CefRefPtr<Source> Engine::PopupClient(int opener, int popup_id, const std::string& initial_url) {
  const int page = contexts_->ReservePopup(opener, popup_id, initial_url);
  if (!page) return nullptr;
  const auto parent = sources_.find(opener);
  const int width = parent == sources_.end() ? width_ : parent->second->width_;
  const int height = parent == sources_.end() ? height_ : parent->second->height_;
  const auto config = settings_.Get(contexts_->ContextId(page));
  const double scale = config && config->device_scale ? *config->device_scale : scale_;
  CefRefPtr<Source> source = new Source(writer_, *this, page, width, height, scale, i420_);
  sources_[page] = source;
  return source;
}
bool Engine::PageCreated(int page, CefRefPtr<CefBrowser> browser) {
  const int context = contexts_->ContextId(page);
  const auto found = sources_.find(page);
  if (!context || found == sources_.end()) {
    browser->GetHost()->CloseBrowser(true);
    return false;
  }
  std::optional<suitecut::NativeContextVideo::Descriptor> video;
  if (context_video_->Configured(context)) {
    suitecut::MediaPipelineOptions actual;
    actual.launcher_path = CefCommandLine::GetGlobalCommandLine()->GetProgram().ToString();
    actual.source_width = found->second->capture_width_;
    actual.source_height = found->second->capture_height_;
    actual.output_width = found->second->width_;
    actual.output_height = found->second->height_;
    actual.i420 = found->second->i420_;
    actual.frames_per_second = fps_;
    actual.start_timestamp_ms = Now();
    const auto startup = page == 1 ? startup_operations_.Get(1) : std::nullopt;
    const double deadline = startup ? startup->deadline_ms : contexts_->PageCreationDeadline(page);
    actual.first_frame_deadline =
        Clock::now() +
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(
            deadline ? std::max(0.0, deadline - Now()) : 30000));
    std::string error;
    if (!context_video_->PageCreated(page, context, std::move(actual), video, error)) {
      contexts_->PageCreationFailed(page, error);
      if (page == 1) StartupFailed(error);
      browser->GetHost()->CloseBrowser(true);
      return false;
    }
  }
  // Admission precedes both the popup event and a real newPage reply. Encoder
  // startup is asynchronous and follows actual OSR paint, never a fake frame.
  contexts_->PageCreated(page, browser);
  workers_->Ensure(context);
  if (video) browser->GetHost()->Invalidate(PET_VIEW);
  return true;
}
void Engine::PageClosed(int page) {
  websocket_routes_->PageClosed(contexts_->ContextId(page), page);
  worker_network_->Detach("page:" + std::to_string(page));
  network_archive_->Detach("page:" + std::to_string(page));
  trace_preview_->PageClosed(page);
#if defined(OS_LINUX) || defined(__APPLE__)
  streaming_->PageGone(page);
#endif
  context_routes_->Detach("page:" + std::to_string(page));
  context_har_->Detach("page:" + std::to_string(page));
  network_headers_.Detach("page:" + std::to_string(page));
  authentication_.Detach("page:" + std::to_string(page));
  network_policy_.Detach("page:" + std::to_string(page));
  downloads_->PageGone(page);
  writer_.DropPage(page);
  const auto closed_source = sources_.find(page);
  contexts_->PageClosed(page, closed_source == sources_.end()
                                  ? std::string{}
                                  : closed_source->second->closed_reason_);
  sources_.erase(page);
  Idle();
}
void Engine::AbortPopup(int opener, int popup_id) {
  contexts_->AbortPopup(opener, popup_id);
  for (auto iterator = sources_.begin(); iterator != sources_.end();) {
    if (!contexts_->ContextId(iterator->first) && iterator->first != 1)
      iterator = sources_.erase(iterator);
    else
      ++iterator;
  }
}
void Engine::CompleteContext(int id, bool ok, Dict result, const std::string& error,
                             bool lossless_console) {
  const auto handler_owner = operations_.Get(id);
  const suitecut::NativeWorkOriginFrame handler_origin(
      LocatorHandlerCommandOrigin(id, 0, handler_owner ? handler_owner->generation : 0));
  if (!operations_.Finish(id)) return;
  if (context_video_) context_video_->Cancel(id, handler_owner->generation);
  if (const auto prepared = video_preparations_.find(id); prepared != video_preparations_.end()) {
    auto value = std::move(prepared->second.value);
    video_preparations_.erase(prepared);
    context_video_->Discard(std::move(value));
  }
  ForgetLocatorHandlerCommand(id);
  if (const auto found = trace_context_api_.find(id); found != trace_context_api_.end()) {
    const auto [lease, call] = found->second;
    trace_context_api_.erase(found);
    auto event = CefDictionaryValue::Create();
    event->SetString("type", "after");
    event->SetString("callId", "call@" + std::to_string(call));
    event->SetDouble("endTime", Now());
    if (!ok) {
      auto failure = CefDictionaryValue::Create();
      failure->SetString("message", error);
      event->SetDictionary("error", failure);
    }
    traces_->After(lease.context, call, event);
  }
  traces_->Cancel(id);
  screenshot_inputs_->CancelWait(id);
  contexts_->CancelWait(id);
  workers_->Cancel(id);
  har_files_->Cancel(id);
  context_har_->Cancel(id);
  websocket_routes_->Cancel(id);
  context_unroute_all_->Cancel(id);
  context_routes_->Cancel(id);
  context_console_->Cancel(id);
  context_network_->Cancel(id);
  worker_network_->CancelOwner(id);
  network_policy_.Cancel(id);
  geolocation_.Cancel(id);
  authentication_.Cancel(id);
  network_headers_.Cancel(id);
  http_->Cancel(id);
  downloads_->Cancel(id);
  std::string json = result ? Json(result) : "";
  if (result && lossless_console) {
    auto typed = CefValue::Create();
    typed->SetDictionary(result->Copy(false));
    json.clear();
    if (!suitecut::DevToolsLosslessJSON(typed, json, kMaxResponseBytes)) {
      writer_.Reply(id, false, "Native context console output limit exceeded");
      return;
    }
  }
  writer_.Reply(id, ok, error, std::vector<uint8_t>(json.begin(), json.end()));
}
void Engine::ContextCommand(int id, uint64_t generation, const std::string& action, Dict params) {
  contexts_->Handle(action, params,
                    [this, alive = alive_, id, generation, action](bool ok, Dict result,
                                                                   const std::string& error) {
                      if (!*alive) return;
                      if (!operations_.IsCurrent(id, generation)) {
                        // Native page creation can settle after the original caller was canceled.
                        if (ok && (action == "newPage" || action == "newOwnedPage") && result) {
                          auto close = CefDictionaryValue::Create();
                          close->SetInt("pageId", result->GetInt("pageId"));
                          contexts_->Handle("closePage", close,
                                            [](bool, Dict, const std::string&) {});
                        }
                        return;
                      }
                      CompleteContext(id, ok, result, error);
                    });
  // A validation rejection can happen before context_created consumes its token.
  const auto unused = video_preparations_.find(id);
  if (unused != video_preparations_.end() && unused->second.generation == generation) {
    auto prepared = std::move(unused->second.value);
    video_preparations_.erase(unused);
    context_video_->Discard(std::move(prepared));
  }
}
void Engine::Deadlines() {
  if (!startup_operations_.Expired(Now()).empty())
    StartupFailed("Native launch recording startup exceeded its original deadline");
  TickLocatorHandlers();
  for (const auto& [context, failure] : authentication_failures_) {
    (void)context;
    failure->Tick();
  }
  websocket_routes_->Tick();
  context_video_->Tick();
  screenshot_inputs_->Tick();
  screenshot_comparisons_->Tick();
  trace_preview_->Tick();
  trace_sources_->Tick();
  traces_->Tick(Now());
  har_files_->Tick();
  har_recording_->Tick();
  context_routes_->Tick(Now());
  context_har_->Tick(Now());
  context_unroute_all_->Tick();
  for (const int id : operations_.Expired(Now())) {
    const auto http_error = http_->TimeoutError(id);
    CompleteContext(id, false, nullptr,
                    http_error.empty() ? "Native context operation timed out" : http_error);
  }
  context_console_->Tick(Now());
  context_network_->Tick(Now());
  network_archive_->Tick(Now());
  network_policy_.Tick(Now());
  geolocation_.Tick(Now());
  authentication_.Tick(Now());
  network_headers_.Tick(Now());
  http_->Tick(Now());
  downloads_->Tick();
  Idle();
  if (!startup_operations_.Size() && !context_video_->NeedsTick() &&
      locator_handler_leases_.empty() &&
      ((closing_ && !authentication_.pending() && !authentication_.physical() &&
        authentication_failures_.empty() && !http_->NeedsTick() && !traces_->NeedsTick() &&
        !trace_preview_->PhysicalCount() && !trace_sources_->PhysicalCount() &&
        !trace_background_.PhysicalCount() && !screenshot_comparisons_->NeedsTick() &&
        !screenshot_inputs_->NeedsTick() && !websocket_routes_->Physical() &&
        !websocket_routes_->NeedsTick() && !context_routes_->physical() &&
        !context_routes_->pending() && !context_unroute_all_->pending() &&
        !context_har_->physical() && !context_har_->pending() && !har_files_->Physical() &&
        !har_recording_->NeedsTick() && !network_archive_->NeedsTick()) ||
       (!operations_.Size() && !authentication_.pending() && !authentication_.physical() &&
        authentication_failures_.empty() && !traces_->NeedsTick() &&
        !trace_preview_->PhysicalCount() && !trace_sources_->PhysicalCount() &&
        !trace_background_.PhysicalCount() && !screenshot_comparisons_->NeedsTick() &&
        !screenshot_inputs_->NeedsTick() && !websocket_routes_->Physical() &&
        !websocket_routes_->NeedsTick() && !context_routes_->physical() &&
        !context_routes_->pending() && !context_unroute_all_->pending() &&
        !context_har_->physical() && !context_har_->pending() && !har_files_->Physical() &&
        !har_recording_->NeedsTick() && !network_archive_->NeedsTick() && !http_->NeedsTick() &&
        !downloads_->NeedsTick()))) {
    deadline_timer_active_ = false;
    return;
  }
  CefPostDelayedTask(
      TID_UI,
      new Task(
          [this, alive = alive_] {
            if (*alive) Deadlines();
          },
          locator_handler_leases_.empty() ? suitecut::AcceptedTaskWork::Kind::Work
                                          : suitecut::AcceptedTaskWork::Kind::BarrierCoordinator,
          LocatorHandlerMonitorOrigin()),
      10);
}
void Engine::CloseAll() {
  if (closing_) return;
  closing_ = true;
  if (const auto startup = startup_operations_.Get(1))
    context_video_->Cancel(1, startup->generation);
  startup_operations_.Clear();
  for (const int id : operations_.Ids())
    CompleteContext(id, false, nullptr, "Native browser is closing");
  context_video_->Clear();
  video_preparations_.clear();
  screenshot_inputs_->Clear();
  traces_->Clear();
  trace_preview_->Clear();
  trace_sources_->Clear();
  WakeTrace();
  recordings_->FinalizeAll(Now(), Clock::now() + std::chrono::seconds(60));
#if defined(OS_LINUX) || defined(__APPLE__)
  streaming_->FinalizeAll(Clock::now() + std::chrono::seconds(60));
#endif
  http_->Clear();
  har_files_->Clear();
  contexts_->CloseAll();
  utilities_->Clear();
}
bool Engine::CanShutdown() const {
#if defined(OS_LINUX) || defined(__APPLE__)
  if (streaming_->PhysicalCount()) return false;
#endif
  return closing_ && sources_.empty() && !context_video_->Physical() &&
         locator_handler_leases_.empty() && authentication_failures_.empty() &&
         !authentication_.pending() && !authentication_.physical() &&
         !websocket_routes_->Physical() && !websocket_routes_->NeedsTick() &&
         !context_routes_->physical() && !context_routes_->pending() &&
         !context_unroute_all_->pending() && !context_har_->physical() &&
         !context_har_->pending() && !har_files_->Physical() && !har_recording_->NeedsTick() &&
         !contexts_->PageCount() && !utilities_->BrowserCount() && !workers_->BrowserCount() &&
         !traces_->PhysicalCount() && !trace_preview_->PhysicalCount() &&
         !trace_sources_->PhysicalCount() && !trace_background_.PhysicalCount() &&
         !screenshot_comparisons_->NeedsTick() && !screenshot_inputs_->NeedsTick() &&
         !trace_physical_ && !http_->TracePhysical() && !http_->PhysicalProfiles() &&
         !downloads_->NeedsTick() && !recordings_->PhysicalCount();
}
void Engine::Idle() {
  shutdown_fence_.Request([this] { return CanShutdown(); },
                          [this](std::function<void()> work) {
                            return CefPostTask(TID_UI,
                                               new Task([alive = alive_, work = std::move(work)] {
                                                 if (*alive) work();
                                               }));
                          },
                          [this] {
                            if (reader_source_) reader_source_->StopReader();
                            CefQuitMessageLoop();
                          });
}

}  // namespace suitecut::browser
