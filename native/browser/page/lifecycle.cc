// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
Source::Source(Writer& writer, Engine& engine, int page, int width, int height, double scale,
               bool i420)
    : writer_(writer),
      engine_(engine),
      page_id_(page),
      width_(width),
      height_(height),
      capture_width_(i420 ? ((static_cast<int>(std::ceil(width * scale)) + 1) & ~1)
                          : static_cast<int>(std::ceil(width * scale))),
      capture_height_(i420 ? ((static_cast<int>(std::ceil(height * scale)) + 1) & ~1)
                           : static_cast<int>(std::ceil(height * scale))),
      scale_(scale),
      i420_(i420),
      handle_namespace_(HandleNamespace()) {
  context_settings_ = engine_.settings().Get(engine_.contexts()->ContextId(page_id_));
  authentication_snapshot_ = engine_.authentication().Get(engine_.contexts()->ContextId(page_id_));
  initial_empty_document_ = !engine_.contexts()->IsPopup(page_id_);
  initial_empty_ready_ = !initial_empty_document_;
  settings_bootstrap_ = initial_empty_document_;
}

auto Source::StopReader() -> void {
  {
    std::lock_guard<std::mutex> lock(command_mutex_);
    stopped_ = true;
  }
  command_capacity_.notify_all();
  writer_.WakeReaders();
  if (reader_.joinable()) reader_.join();
}

auto Source::SendPacket(Dict header, std::vector<uint8_t> data) -> void {
  header->SetInt("pageId", page_id_);
  writer_.Send(header, std::move(data));
}

auto Source::Error(const char* message) -> void {
  // A Source failure belongs to its page: close that page with the reason and
  // keep the browser and its other pages running. Only a page that has no CEF
  // browser yet, or a first page that never became ready, fails the launch.
  if (closing_page_) return;
  if (browser_ && (page_id_ != 1 || ready_)) {
    if (closed_reason_.empty()) closed_reason_ = message;
    NetworkFatal(message);
    return;
  }
  auto header = Header("error");
  header->SetString("error", message);
  SendPacket(header);
}

auto Source::GetFrameHandler() -> CefRefPtr<CefFrameHandler> { return this; }

auto Source::OnFrameCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnFrameCreated(browser, frame);
  if (settings_bootstrap_ && !settings_committed_ && frame->IsMain()) {
    if (!engine_.contexts()->CredentialReadyStore()->RegisterBootstrap(
            browser->GetIdentifier(), browser->GetHost()->GetRequestContext())) {
      SettingsFail("Native initial document identity capacity exceeded");
      return;
    }
  }
  if (!engine_.contexts()->InitScriptFrameCreated(page_id_, frame))
    Degraded("Native context init script IPC sent untracked past its capacity");
}

auto Source::OnFrameDestroyed(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnFrameDestroyed(browser, frame);
  engine_.contexts()->InitScriptFrameDestroyed(page_id_, frame->GetIdentifier().ToString());
}

auto Source::OnFrameDetached(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnFrameDetached(browser, frame);
  engine_.contexts()->InitScriptFrameDestroyed(page_id_, frame->GetIdentifier().ToString());
}

auto Source::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                      CefProcessId sender, CefRefPtr<CefProcessMessage> message)
    -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnProcessMessageReceived(browser, frame, sender, message);
  if (SettingsMessage(frame, sender, message)) return true;
  if (engine_.contexts()->InitScriptMessage(page_id_, frame, sender, message)) return true;
  if (sender == PID_RENDERER && message->GetName() == "suitecutInitScriptBootstrapError") {
    if (closing_page_ || !browser_) return true;
    const auto error = message->GetArgumentList()->GetString(0).ToString();
    // A document that cannot bootstrap is usually being replaced: a click
    // that navigates terminates its scripts, and an ad frame goes away while
    // it starts. It costs that document only; as in Playwright, the page stays
    // and the next document bootstraps on its own.
    const bool main = !frame || frame->IsMain();
    Degraded(main ? "Native init script bootstrap failed in the main frame"
                  : "Native init script bootstrap failed in a child frame");
    ConsoleEvent("error",
                 std::string("Native init script bootstrap failed in ") +
                     (main ? "the main frame: " : "a child frame: ") + error,
                 nullptr);
    return true;
  }
  if (sender == PID_RENDERER && message->GetName() == "suitecutInitScriptError") {
    ConsoleEvent("error", message->GetArgumentList()->GetString(0).ToString(), nullptr);
    return true;
  }
  return false;
}

auto Source::GetPermissionHandler() -> CefRefPtr<CefPermissionHandler> { return this; }

auto Source::GetDialogHandler() -> CefRefPtr<CefDialogHandler> { return this; }

auto Source::OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode,
                          const CefString& routed_arg2, const CefString& routed_arg3,
                          const std::vector<CefString>& routed_arg4,
                          const std::vector<CefString>& routed_arg5,
                          const std::vector<CefString>& routed_arg6,
                          CefRefPtr<CefFileDialogCallback> callback) -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnFileDialog(browser, mode, routed_arg2, routed_arg3, routed_arg4, routed_arg5,
                             routed_arg6, callback);
  return FormFileDialog(mode, callback);
}

auto Source::OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t routed_arg1,
                                    const CefString& origin, uint32_t requested,
                                    CefRefPtr<CefPermissionPromptCallback> callback) -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnShowPermissionPrompt(browser, routed_arg1, origin, requested, callback);
  const auto granted = engine_.contexts()->AllowedPermissions(page_id_, origin.ToString());
  callback->Continue(suitecut::NativePermissionRequestGranted(requested, granted)
                         ? CEF_PERMISSION_RESULT_ACCEPT
                         : CEF_PERMISSION_RESULT_DENY);
  return true;
}

auto Source::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> routed_arg1,
                                            const CefString& origin, uint32_t requested,
                                            CefRefPtr<CefMediaAccessCallback> callback) -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnRequestMediaAccessPermission(browser, routed_arg1, origin, requested, callback);
  callback->Continue(requested &
                     engine_.contexts()->AllowedMediaPermissions(page_id_, origin.ToString()));
  return true;
}

auto Source::GetLifeSpanHandler() -> CefRefPtr<CefLifeSpanHandler> { return this; }

auto Source::DoClose(CefRefPtr<CefBrowser> browser) -> bool {
  if (auto tab = AdoptedTab(browser)) return tab->DoClose(browser);
  return engine_.contexts()->BeforeUnloadCloseApproved(page_id_, browser);
}

auto Source::GetRenderHandler() -> CefRefPtr<CefRenderHandler> { return this; }

auto Source::GetAudioHandler() -> CefRefPtr<CefAudioHandler> { return this; }

auto Source::GetLoadHandler() -> CefRefPtr<CefLoadHandler> { return this; }

auto Source::GetRequestHandler() -> CefRefPtr<CefRequestHandler> { return this; }

auto Source::GetJSDialogHandler() -> CefRefPtr<CefJSDialogHandler> { return this; }

auto Source::GetDownloadHandler() -> CefRefPtr<CefDownloadHandler> { return this; }

auto Source::GetContextMenuHandler() -> CefRefPtr<CefContextMenuHandler> { return this; }

auto Source::GetDisplayHandler() -> CefRefPtr<CefDisplayHandler> { return this; }

auto Source::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                             const CefString& address) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnAddressChange(browser, frame, address);
  if (!ready_ || !frame->IsMain()) return;
  const auto url = address.ToString();
  if (url.size() > kMaxObservedURL) {
    Error("Native address exceeds 2 MiB");
    return;
  }
  auto header = Header("navigation");
  header->SetString("url", url);
  SendPacket(header);
}

auto Source::CanDownload(CefRefPtr<CefBrowser> browser, const CefString& routed_arg1,
                         const CefString& routed_arg2) -> bool {
  if (auto tab = AdoptedTab(browser)) return tab->CanDownload(browser, routed_arg1, routed_arg2);
  return engine_.downloads()->CanDownload(page_id_);
}

auto Source::OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> item,
                              const CefString& name, CefRefPtr<CefBeforeDownloadCallback> callback)
    -> bool {
  if (auto tab = AdoptedTab(browser)) return tab->OnBeforeDownload(browser, item, name, callback);
  return engine_.downloads()->Before(page_id_, item, name.ToString(), callback);
}

auto Source::OnDownloadUpdated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> item,
                               CefRefPtr<CefDownloadItemCallback> callback) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnDownloadUpdated(browser, item, callback);
  engine_.downloads()->Updated(page_id_, item, callback);
}

auto Source::GetResourceRequestHandler(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                       CefRefPtr<CefRequest> request, bool navigation,
                                       bool download, const CefString& routed_arg5,
                                       bool& routed_arg6)
    -> CefRefPtr<CefResourceRequestHandler> {
  if (auto tab = AdoptedTab(browser))
    return tab->GetResourceRequestHandler(browser, frame, request, navigation, download,
                                          routed_arg5, routed_arg6);
  if (std::getenv("SUITECUT_NATIVE_RESOURCE_DIAGNOSTICS")) {
    const auto data = request->GetPostData();
    CefPostData::ElementVector elements;
    if (data) data->GetElements(elements);
    std::fprintf(stderr,
                 "native cef resource page=%d browser=%d id=%llu type=%d frame=%s method=%s url=%s "
                 "excluded=%d elements=%zu\n",
                 page_id_, browser ? browser->GetIdentifier() : 0,
                 static_cast<unsigned long long>(request->GetIdentifier()),
                 request->GetResourceType(), frame ? frame->GetIdentifier().ToString().c_str() : "",
                 request->GetMethod().ToString().c_str(), request->GetURL().ToString().c_str(),
                 data ? data->HasExcludedElements() : false, elements.size());
    for (size_t i = 0; i < std::min(elements.size(), size_t(16)); ++i)
      std::fprintf(stderr, "native cef post element index=%zu type=%d bytes=%zu\n", i,
                   elements[i]->GetType(), elements[i]->GetBytesCount());
  }
  if (auto icon = suitecut::HeadlessBrowserIconHandler(
          CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-headless") == "1",
          request))
    return icon;
  return engine_.downloads()->ResourceHandler(page_id_, frame, request, navigation, download);
}

auto Source::OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& origin, JSDialogType type,
                        const CefString& message, const CefString& default_value,
                        CefRefPtr<CefJSDialogCallback> callback, bool& suppress) -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnJSDialog(browser, origin, type, message, default_value, callback, suppress);
  const auto name = type == JSDIALOGTYPE_ALERT     ? "alert"
                    : type == JSDIALOGTYPE_CONFIRM ? "confirm"
                                                   : "prompt";
  if (dialogs_ && dialogs_->Show(name, origin, message, default_value, callback, Now())) {
    suppress = false;
    WakeDeadlines();
    return true;
  }
  suppress = true;
  return false;
}

auto Source::OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser, const CefString& message,
                                  bool routed_arg2, CefRefPtr<CefJSDialogCallback> callback)
    -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnBeforeUnloadDialog(browser, message, routed_arg2, callback);
  const auto token = engine_.contexts()->BeforeUnloadCloseToken(page_id_);
  SuiteCutDialogControls::Decision decision;
  if (token)
    decision = [self = CefRefPtr<Source>(this), token](bool accept) {
      if (!accept) self->engine_.contexts()->BeforeUnloadCloseCanceled(self->page_id_, token);
    };
  if (dialogs_ &&
      dialogs_->Show("beforeunload", browser_ ? browser_->GetMainFrame()->GetURL().ToString() : "",
                     message, "", callback, Now(), std::move(decision))) {
    WakeDeadlines();
    return true;
  }
  callback->Continue(true, CefString());
  return true;
}

auto Source::OnResetDialogState(CefRefPtr<CefBrowser> browser) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnResetDialogState(browser);
  if (dialogs_) dialogs_->Clear();
}

auto Source::OnDialogClosed(CefRefPtr<CefBrowser> browser) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnDialogClosed(browser);
  if (dialogs_) dialogs_->Closed();
}

auto Source::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> routed_arg1,
                                 CefRefPtr<CefContextMenuParams> routed_arg2,
                                 CefRefPtr<CefMenuModel> model)
    -> void {
  if (auto tab = AdoptedTab(browser))
    return tab->OnBeforeContextMenu(browser, routed_arg1, routed_arg2, model);
  // Automation pages never show a menu; a person's Chrome window keeps one.
  if (ChromeWindowMode()) RemoveInspectionItems(model);
  else model->Clear();
  if (model->GetCount()) ContextMenus::Get().Opened(browser->GetIdentifier());
}

auto Source::OnContextMenuDismissed(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>) -> void {
  ContextMenus::Get().Dismissed(browser->GetIdentifier());
}

auto Source::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->GetViewRect(browser, rect);
  rect = CefRect(0, 0, artifact_view_owner_ ? artifact_view_width_ : width_,
                 artifact_view_owner_ ? artifact_view_height_ : height_);
  if (!artifact_view_owner_) {
    reported_view_width_ = rect.width;
    reported_view_height_ = rect.height;
  }
}

auto Source::GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& info) -> bool {
  if (auto tab = AdoptedTab(browser)) return tab->GetScreenInfo(browser, info);
  info.device_scale_factor = static_cast<float>(scale_);
  const auto screen = context_settings_ && context_settings_->screen
                          ? *context_settings_->screen
                          : suitecut::ContextSettings::Size{width_, height_};
  info.rect = info.available_rect = CefRect(0, 0, screen.width, screen.height);
  return true;
}

auto Source::AdoptedTab(const CefRefPtr<CefBrowser>& browser) -> CefRefPtr<Source> {
  // Before its own browser exists, every callback belongs to this page.
  if (!own_browser_id_ || !browser || browser->GetIdentifier() == own_browser_id_) return nullptr;
  const int id = browser->GetIdentifier();
  if (const auto found = tabs_.find(id); found != tabs_.end()) return found->second;
  auto tab = engine_.TabClient(network_context_id_, width_, height_);
  if (!tab) return nullptr;
  tabs_.emplace(id, tab);
  return tab;
}

auto Source::OnAfterCreated(CefRefPtr<CefBrowser> browser) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnAfterCreated(browser);
  const suitecut::NativeWorkOriginFrame native_creation(native_creation_origin_.Origin());
  browser_ = browser;
  own_browser_id_ = browser->GetIdentifier();
  if (ChromeWindowMode()) ChromeScreencastUpdate();
  NativeInspectorOpened(engine_.AcceptedInspector(), browser->GetIdentifier(),
                        suitecut::AcceptedInspectorWork::Kind::Page);
  const auto native_inspector_callback = NativeInspectorCallback();
  NativeDebuggerDomains();  // Install before settings and author bootstrap can release.
  const auto policy_error =
      engine_.contexts()->RequestPolicyError(engine_.contexts()->ContextId(page_id_));
  if (!policy_error.empty()) {
    Error(policy_error.c_str());
    suitecut::browser::CloseBrowser(browser, true);
    return;
  }
  CredentialPrepare();
  dialogs_ = std::make_unique<SuiteCutDialogControls>([this](Dict event) {
    const auto json = Json(event);
    const auto sequence = ++event_sequence_;
    const auto name = event->GetString("state") == "closed" ? "dialogclosed" : "dialog";
    JournalEvent(sequence, name, json);
    auto header = Header("dialog");
    header->SetDouble("sequence", static_cast<double>(sequence));
    SendPacket(header, std::vector<uint8_t>(json.begin(), json.end()));
    std::vector<int> waiters;
    for (const auto& [id, event] : event_waiters_)
      if (event == name) waiters.push_back(id);
    for (const int id : waiters) Complete(id, true, "", json);
  });
  sockets_ = std::make_unique<suitecut::NativeWebSocketControls>(
      [this](Dict info, const std::vector<uint8_t>& bytes) {
        auto header = Header("websocket");
        header->SetInt("pageId", page_id_);
        header->SetDictionary("socket", info->Copy(false));
        const bool accepted = writer_.TryEvent(header, bytes);
        if (accepted) WebSocketPredicatePublished(info);
        return accepted;
      },
      [this] { WakeDeadlines(); },
      [this](const std::string& error) {
        Error(error.c_str());
        if (browser_) suitecut::browser::CloseBrowser(browser_, true);
      });
  sockets_->degraded = [this](const std::string& error) { Degraded(error); };
  sockets_->SetObserver([this](Dict event, const std::vector<uint8_t>& bytes, double observed) {
    NativeTraceWebSocket(event, bytes, observed);
  });
  network_ = std::make_unique<suitecut::NetworkControls>(
      [this](const std::string& session, const std::string& method, Dict params,
             suitecut::NetworkControls::Result callback) {
        NativeNetworkSend(session, method, params, std::move(callback), 0);
      },
      [this](Dict info) {
        auto header = Header("network");
        const auto json = Json(info);
        const auto event = info->GetString("event").ToString();
        if (event == "route" || event == "routematch") {
          if (ContextWorkerNetworkEvent(info)) return;
          engine_.ContextRoute(engine_.contexts()->ContextId(page_id_),
                               "page:" + std::to_string(page_id_), page_id_, nullptr, info);
        }
        if (event == "request" || event == "response" || event == "requestfinished" ||
            event == "requestfailed") {
          if (ContextWorkerNetworkEvent(info)) return;
          engine_.ContextNetwork(page_id_, info);
          auto request = info->GetDictionary("request");
          if (request && request->GetBool("serviceWorker")) return;
          const auto sequence = ++event_sequence_;
          header->SetDouble("sequence", static_cast<double>(sequence));
          JournalEvent(sequence, event, json);
        }
        auto request = info->GetDictionary("request");
        if (request && request->GetBool("serviceWorker")) return;
        SendPacket(header, std::vector<uint8_t>(json.begin(), json.end()));
      },
      [this](const std::string& error) { NetworkFatal(error); },
      [this] {
        if (!deadline_timer_active_) {
          deadline_timer_active_ = true;
          CefPostDelayedTask(
              TID_UI,
              new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },
                       suitecut::AcceptedTaskWork::Kind::Work, LocatorHandlerMonitorOrigin()),
              10);
        }
      },
      [] { return Now(); },
      [this](uint64_t id, size_t offset, size_t total, std::span<const uint8_t> bytes,
             bool canceled) {
        auto header = Header("networkbody");
        header->SetInt("pageId", page_id_);
        header->SetString("bodyId", std::to_string(id));
        header->SetInt("offset", static_cast<int>(offset));
        header->SetInt("totalBytes", static_cast<int>(total));
        if (canceled) header->SetBool("canceled", true);
        return writer_.TryEvent(header, std::vector<uint8_t>(bytes.begin(), bytes.end()));
      });
  network_->degraded = [this](const std::string& error) { Degraded(error); };
  network_->SetBoundedSender([this](const std::string& session, const std::string& method,
                                    Dict params, suitecut::NetworkControls::Result done,
                                    double deadline) {
    NativeNetworkSend(session, method, params, std::move(done), deadline);
  });
  network_->SetRouteFetch(
      [http = engine_.http(), page = page_id_](
          int owner, const std::string&, const std::string&, Dict request, Dict options,
          double deadline, std::shared_ptr<void> lease,
          std::function<bool(bool, Dict, const std::string&)> done) {
        http->RouteFetch(owner, page, request, options, std::move(done), deadline,
                         std::move(lease));
      },
      [http = engine_.http()](int owner) { http->Cancel(owner); });
  network_->SetSessionHandoff(
      [this](const std::string& request, const std::string& previous, const std::string& session) {
        NativeFrameRequestSession(request, previous, session);
      },
      [this](const std::string& frame, const std::string& root) {
        return Descendant(frame, root);
      });
  const int archive_context = engine_.contexts()->ContextId(page_id_);
  network_context_id_ = archive_context;
  const auto archive_owner = "page:" + std::to_string(page_id_);
  if (!engine_.network_archive().Attach(
          archive_context, archive_owner,
          [self = CefRefPtr<Source>(this)](const std::string& session, const std::string& request,
                                           double deadline,
                                           suitecut::NativeHARCapture::BodyReply reply) {
            self->NativeReadResponseBody(session, request, deadline, std::move(reply));
          })) {
    Error("Native network archive owner capacity exceeded");
    suitecut::browser::CloseBrowser(browser, true);
    return;
  }
  if (!engine_.network_archive().PageCreated(
          archive_context, archive_owner, "page@" + std::to_string(page_id_),
          std::chrono::duration<double, std::milli>(
              std::chrono::system_clock::now().time_since_epoch())
              .count())) {
    NetworkFatal("Native HAR actual page metadata capacity exceeded");
    return;
  }
  if (!engine_.worker_network().Attach(
          archive_context, archive_owner, false,
          [self = CefRefPtr<Source>(this)](int owner, Dict command, double deadline,
                                           suitecut::NativeContextWorkerNetwork::Reply reply) {
            if (self->closing_page_ || !self->network_ || (deadline && Now() >= deadline)) {
              reply(false, nullptr,
                    "Actual page network observation channel is closed or deadline expired");
              return;
            }
            self->network_->Command(
                owner, command,
                [reply = std::move(reply)](bool ok, const std::string& error,
                                           const std::string& json) {
                  auto value = ok ? CefParseJSON(json, JSON_PARSER_RFC) : nullptr;
                  reply(ok, value ? value->GetDictionary() : nullptr, error);
                },
                deadline, suitecut::NativeContextWorkerNetwork::RouteOperationGeneration(command),
                self->network_context_id_);
            self->WakeDeadlines();
          },
          [self = CefRefPtr<Source>(this)](const std::string& session, const std::string& request,
                                           double deadline,
                                           suitecut::NativeHARCapture::BodyReply reply) {
            self->NativeReadResponseBody(session, request, deadline, std::move(reply));
          },
          [self = CefRefPtr<Source>(this)](int owner) {
            if (self->network_) self->network_->Cancel(owner);
          })) {
    NetworkFatal("Native page worker network observer capacity exceeded");
    return;
  }
  network_->SetObserver(
      [this, archive_context, archive_owner](Dict event) {
        const auto kind = event->GetString("event");
        if (kind != "request" && kind != "requestupdated" && kind != "response" &&
            kind != "requestfinished" && kind != "requestfailed")
          return;
        if (!engine_.network_archive().Observe(archive_context, archive_owner, event, 0)) {
          Error("Native network archive metadata rejected");
        }
        if (engine_.network_archive().NeedsTick()) engine_.WakeTrace();
      },
      [this, archive_context] { return engine_.network_archive().Active(archive_context); });
  network_->SetRequestBodyReader(
      [this](const std::string& session, const std::string& request, double deadline,
             suitecut::NativeNetworkPostPreparation::Data done,
             suitecut::NativeNetworkPostPreparation::Receipt receipt) {
        NativeReadRequestBody(session, request, deadline, std::move(done), std::move(receipt));
      },
      [this, archive_context, archive_owner](const std::string& id) {
        auto reserved = engine_.network_archive().ReservePost(archive_context, archive_owner, id);
        if (engine_.network_archive().NeedsTick()) engine_.WakeTrace();
        return suitecut::NativeNetworkPostPreparation::Reservation{std::move(reserved.data),
                                                                   std::move(reserved.receipt)};
      });
  bindings_ = std::make_unique<suitecut::BindingControls>(
      std::to_string(page_id_) + "_" + handle_namespace_,
      [this](const std::string& session, const std::string& method, Dict params,
             suitecut::BindingControls::Result callback) {
        const int id = AllocateDevToolsId();
        if (!id || binding_calls_.size() >= 1024 || closing_page_) {
          callback(false, "Native binding command capacity exceeded or page closed");
          return;
        }
        binding_calls_.emplace(
            id, BindingCall{session, suitecut::BindNativeLocatorHandlerAsync(std::move(callback)),
                            Now() + 30000});
        if (!SubmitDevTools(id, method, params, session)) {
          const auto found = binding_calls_.find(id);
          if (found != binding_calls_.end()) {
            auto callback = std::exchange(found->second.callback, nullptr);
            binding_calls_.erase(found);
            callback(false, "Native binding command dispatch failed");
          }
        }
        if (!deadline_timer_active_) {
          deadline_timer_active_ = true;
          CefPostDelayedTask(
              TID_UI,
              new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },
                       suitecut::AcceptedTaskWork::Kind::Work, LocatorHandlerMonitorOrigin()),
              10);
        }
      },
      [this](int owner, const suitecut::BindingControls::Target& target, Dict message, Dict params,
             suitecut::BindingControls::Result callback) {
        const auto frame = frame_states_.find(target.frame);
        if (frame == frame_states_.end() || frame->second.context != target.context ||
            frame->second.session != target.session) {
          callback(false, "Native binding context was destroyed");
          return;
        }
        operation_frames_[owner] = target.frame;
        operation_sessions_[owner] = target.session;
        auto config = message->Copy(false);
        config->SetString("frame", target.frame);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(owner));
        evaluation_groups_[owner].insert(target.session);
        HandleArguments(
            owner, config, params, true,
            [this, owner, target, params, callback = std::move(callback)]() mutable {
              const auto frame = frame_states_.find(target.frame);
              if (frame == frame_states_.end() || frame->second.context != target.context ||
                  frame->second.session != target.session) {
                callback(false, "Native binding context was destroyed");
                return;
              }
              DevTools(owner, "Runtime.callFunctionOn", params,
                       [callback = std::move(callback)](bool ok, const std::string& json) {
                         auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                         auto result = parsed ? parsed->GetDictionary() : nullptr;
                         callback(ok && result && !result->HasKey("exceptionDetails"), json);
                       });
            });
      },
      [this](Dict info) {
        if (info->GetString("event") == "call") {
          const auto metadata = info->GetDictionary("frame");
          const auto frame =
              metadata ? frame_states_.find(metadata->GetString("id")) : frame_states_.end();
          if (frame != frame_states_.end()) info->SetDictionary("frame", FrameJson(frame->second));
        }
        auto header = Header("binding");
        const auto json = Json(info);
        SendPacket(header, std::vector<uint8_t>(json.begin(), json.end()));
      },
      [this] {
        if (!deadline_timer_active_) {
          deadline_timer_active_ = true;
          CefPostDelayedTask(
              TID_UI,
              new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },
                       suitecut::AcceptedTaskWork::Kind::Work, LocatorHandlerMonitorOrigin()),
              10);
        }
      },
      [] { return Now(); });
  std::string inherited_error;
  if (!engine_.contexts()->SeedPageBindings(page_id_, inherited_error)) {
    Error(inherited_error.c_str());
    suitecut::browser::CloseBrowser(browser, true);
    return;
  }
  if (!engine_.PageCreated(page_id_, browser)) return;
  if (blank_tab_) browser->GetMainFrame()->LoadURL("about:blank");
  browser->GetHost()->SetFocus(true);
  native_frame_debugger_registering_ = true;
  observer_ = browser->GetHost()->AddDevToolsMessageObserver(this);
  if (!NativeFrameDebuggerObserved()) return;
  NativeWorkerDebuggerObserved();
  bootstrap_tree_ = AllocateDevToolsId();
  if (!NativeFrameDebuggerTreePrepare(bootstrap_tree_, "")) return;
  NativeWorkerTargetDiscoveryStart();
  NativeStartPrivateResponseSession();
  CredentialStart();
  NativePristineStart();
  SettingsPrepare();
  EnableSession("");
  worker_websocket_attach_ = [this](const std::string& session, const std::string&,
                                    const std::string&, double,
                                    suitecut::NativeWebSocketRoutes::Result done) {
    NativeWebSocketPolicyAttach(session, std::move(done));
  };
  worker_websocket_detach_ = [this](const std::string& session) {
    NativeWebSocketPolicyDetach(session);
  };
  worker_websocket_required_ = [this] {
    return engine_.websocket_routes().HasPolicy(engine_.contexts()->ContextId(page_id_), page_id_);
  };
  worker_websocket_failure_ = [this](const std::string& error) {
    NetworkFatal("Inherited native WebSocket worker policy failed: " + error);
  };
  NativeWebSocketPolicyAttach("", [this](bool ok, const std::string& error) {
    if (!ok) NetworkFatal(error);
  });
  if (!engine_.network_policy().Attach(
          engine_.contexts()->ContextId(page_id_), "page:" + std::to_string(page_id_),
          [self = CefRefPtr<Source>(this)](bool offline,
                                           suitecut::NativeContextNetworkPolicy::Result done) {
            if (self->closing_page_ || !self->network_) {
              done(false, "Native policy target is closed");
              return;
            }
            auto params = CefDictionaryValue::Create();
            params->SetString("action", "setOffline");
            params->SetBool("offline", offline);
            self->network_->Command(
                0, params,
                [done = std::move(done)](bool ok, const std::string& error, const std::string&) {
                  done(ok, error);
                });
          },
          [self = CefRefPtr<Source>(this)](bool ok, const std::string& error) {
            if (self->closing_page_) return;
            if (!ok) {
              self->Error(error.c_str());
              if (self->browser_) suitecut::browser::CloseBrowser(self->browser_, true);
              return;
            }
            self->NativeStartContextHeaders();
          })) {
    Error("Native network policy target capacity exceeded");
    suitecut::browser::CloseBrowser(browser, true);
    return;
  }
  if (!SubmitDevTools(bootstrap_tree_, "Page.getFrameTree", nullptr, "")) {
    NativeFrameDebuggerMetadataFailure(
        "Native debugger initial root tree dispatch was not accepted");
    return;
  }
  AutoAttach("");
}

auto Source::OnBeforeClose(CefRefPtr<CefBrowser> actual_browser) -> void {
  if (auto tab = AdoptedTab(actual_browser)) {
    // The closed tab keeps its entry: CEF still delivers a late callback for
    // the browser, and adopting it again would reserve a page that never gets
    // a browser and blocks shutdown forever.
    tab->OnBeforeClose(actual_browser);
    return;
  }
  const auto native_inspector_callback = NativeInspectorCallback();
  closing_page_ = true;
  SDKBrowserControlClosed();
  if (native_widget_mouse_) native_widget_mouse_->ChannelClosed();
  engine_.websocket_routes().DetachPrefix("page:" + std::to_string(page_id_) + "/ws:");
  NativeWebSocketPolicyClearScopes("Actual native WebSocket source channel closed");
  native_websocket_explicit_payloads_.Closed(engine_.contexts()->ContextId(page_id_), page_id_);
  CancelSettingsWatchdogs();
  ScreenshotAssertionChannelClosed();
  NativePristineClosed();
  native_virtual_time_close_fence_.Closed();
  if (native_virtual_time_) native_virtual_time_->Closed();
  if (element_states_) element_states_->Close();
  if (selector_waits_) selector_waits_->Close();
  OwnerFrameDocumentsClosed();
  if (snapshot_references_) snapshot_references_->Destroyed();
  if (page_errors_) page_errors_->Close();
  if (request_reads_) {
    request_reads_->CancelAll();
    request_reads_->Close();
  }
  if (private_response_) private_response_->ActualClose();
  if (response_reads_) response_reads_->Close();
  if (image_streams_) image_streams_->Close();
  if (network_fatal_) network_fatal_->Closed();
  context_auth_open_ = false;
  GeolocationClear();
  if (settings_page_) {
    settings_page_->Clear();
  }
  settings_initial_request_ = nullptr;
  engine_.recordings()->ClosePage(page_id_);
#if defined(OS_LINUX) || defined(__APPLE__)
  engine_.streaming()->ClosePage(page_id_);
#endif
  CredentialClose();
  if (native_touch_) native_touch_->Close();
  NativeDebuggerClosed();
  NativeDeveloperClosed();
  NativeCoverageClosed();
  NativeHighlightClosed();
  engine_.screenshot_inputs()->PageClosed(page_id_);
  engine_.screenshot_comparisons()->PageClosed(page_id_);
  screenshot_comparison_samples_.clear();
  WorkerConsoleClear(true);
  NativeWorkerTargetOwnerClosed();
  WorkerClear();
  devtools_dispatch_.clear();
  devtools_dispatch_bytes_ = 0;
  if (dialogs_) dialogs_->Clear();
  if (artifact_view_owner_) ArtifactReleaseView(artifact_view_owner_);
  if (artifacts_) artifacts_->Clear();
  NativeMediaClear();
  if (native_idle_) native_idle_->Close();
  if (sockets_) sockets_->Clear();
  if (network_) {
    network_->HARWorkChannelClosed();
    network_->Clear();
  }
  if (bindings_) bindings_->Clear();
  const auto close_error =
      closed_reason_.empty() ? std::string("Native page is closed") : closed_reason_;
  for (const int id : operations_.Ids()) {
    const auto receipt = expect_results_.find(id);
    auto value =
        receipt == expect_results_.end() ? nullptr : CefParseJSON(receipt->second, JSON_PARSER_RFC);
    auto snapshot =
        value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
    if (snapshot) {
      snapshot->SetString("closedReason", close_error);
      Complete(id, true, "", Json(snapshot));
    } else
      Complete(id, false, close_error);
  }
  if (!screenshot_assertion_retirement_.Size()) operations_.Clear();
  auto binding_calls = std::move(binding_calls_);
  binding_calls_.clear();
  for (auto& [id, call] : binding_calls) call.callback(false, close_error);
  auto network_calls = std::move(network_calls_);
  network_calls_.clear();
  for (auto& [id, call] : network_calls)
    call.callback(false, call.expired ? "Native network command deadline expired" : close_error);
  manual_drag_ = nullptr;
  drag_work_.clear();
  form_clicks_.clear();
  form_uploads_.clear();
  upload_store_.Clear();
  pending_uploads_.clear();
  pending_upload_bytes_ = 0;
  if (artifact_view_owner_) ArtifactReleaseView(artifact_view_owner_);
  if (artifacts_) artifacts_->Clear();
  locator_attempts_.clear();
  locator_logs_.clear();
  locator_waits_.clear();
  locator_modifiers_.clear();
  send_keys_physical_.clear();
  send_keys_physical_count_ = 0;
  send_keys_cleanup_commands_.clear();
  input_work_.clear();
  mouse_queue_.clear();
  keyboard_queue_.clear();
  pressed_keys_.clear();
  devtools_.clear();
  NativeWorkerDebuggerClosed();
  NativeDropClosed();
  if (handle_disposals_) handle_disposals_->Close();
  context_retries_.clear();
  artifact_configs_.clear();
  artifact_attempts_.clear();
  artifact_input_configs_.clear();
  count_queries_.clear();
  count_contexts_.clear();
  visibility_snapshots_.Clear();
  expect_results_.clear();
  evaluation_groups_.clear();
  navigation_waiters_.clear();
  navigations_.clear();
  body_waiters_.clear();
  active_requests_.clear();
  documents_.clear();
  document_order_.clear();
  ClearConsoleRawArguments(true);
  handles_.clear();
  console_journal_.Clear();
  page_error_journal_.Clear();
  pending_handle_groups_.clear();
  pending_child_bootstraps_.clear();
  init_snapshot_requests_.clear();
  init_snapshot_continuations_.clear();
  init_snapshot_continuation_count_ = 0;
  frame_states_.clear();
  operation_frames_.clear();
  operation_sessions_.clear();
  sessions_.clear();
  native_target_parents_.clear();
  session_trees_.clear();
  utility_worlds_.clear();
  event_waiters_.clear();
  event_history_.clear();
  event_history_bytes_ = 0;
  native_file_selection_.reset();  // Actual OnBeforeClose retires any blocked native lane.
  folder_observers_.clear();
  folder_operation_ = 0;
  folder_generation_ = 0;
  folder_path_.clear();
  folder_continued_ = false;
  std::vector<uint8_t>().swap(view_);
  std::vector<uint8_t>().swap(popup_);
  frame_width_ = frame_height_ = popup_width_ = popup_height_ = 0;
  ready_ = show_popup_ = false;
  audio_valid_ = false;
  observer_ = nullptr;
  browser_ = nullptr;
  NativeTraceClear();
  NativeInspectorClosed(actual_browser->GetIdentifier());
  native_creation_handler_receipt_.reset();
  RendererRecoveryHandlerClosed();
  engine_.PageClosed(page_id_);
}

auto Source::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> routed_arg1,
                           int popup_id, const CefString& target_url, const CefString& routed_arg4,
                           CefLifeSpanHandler::WindowOpenDisposition routed_arg5, bool routed_arg6,
                           const CefPopupFeatures& routed_arg7, CefWindowInfo& window,
                           CefRefPtr<CefClient>& client, CefBrowserSettings& routed_arg10,
                           CefRefPtr<CefDictionaryValue>& extra_info, bool* routed_arg12) -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnBeforePopup(browser, routed_arg1, popup_id, target_url, routed_arg4, routed_arg5,
                              routed_arg6, routed_arg7, window, client, routed_arg10, extra_info,
                              routed_arg12);
  if (closing_page_) return true;
  auto popup = engine_.PopupClient(page_id_, popup_id, target_url.ToString());
  if (!popup) return true;
  extra_info = engine_.BrowserInfo(popup->page_id_);
  // Chrome-style popups keep CEF's window so tab dispositions open as tabs.
  if (!ChromeWindowMode()) ConfigurePageWindow(window, popup->width_, popup->height_);
  client = popup;
  return false;
}

auto Source::OnBeforePopupAborted(CefRefPtr<CefBrowser> browser, int popup_id) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnBeforePopupAborted(browser, popup_id);
  engine_.AbortPopup(page_id_, popup_id);
}

}  // namespace suitecut::browser
