// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) -> void {
  if (frame->IsMain() && !settings_bootstrap_ && settings_page_ && !settings_started_) {
    settings_empty_seen_ = true;
    SettingsStart();
  }
  if (frame->IsMain()) {
    last_cef_load_url_ = frame->GetURL().ToString();
    const auto current = frame_states_.find(main_frame_id_);
    if (current != frame_states_.end() && current->second.url == last_cef_load_url_) {
      // The CEF client can attach after a popup's early CDP load events. This
      // real embedder load-end callback supplies the completed milestones.
      current->second.milestones.insert("commit");
      current->second.milestones.insert("DOMContentLoaded");
      current->second.milestones.insert("load");
      CheckNavigations();
    }
  }
  if (frame->IsMain() && !ready_) {
    // Handshake only after the initial blank document loads, so its load-end
    // cannot accidentally resolve the operator's first navigation command.
    initial_loaded_ = true;
    MaybeReady();
    return;
  }
  if (frame->IsMain() && navigation_) {
    Complete(navigation_, status < 400, status < 400 ? "" : "HTTP navigation failed");
  }
  if (frame->IsMain() && ready_) {
    auto response = CefDictionaryValue::Create();
    response->SetString("url", frame->GetURL());
    response->SetInt("status", status);
    const auto waiters = navigation_waiters_;
    for (const int id : waiters) Complete(id, true, "", Json(response));
  }
}

auto Source::OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                         const CefString&, const CefString&) -> void {
  if (frame->IsMain() && recovery_requested_ && code != ERR_ABORTED) {
    recovery_requested_ = false;
    RendererRecoveryHandlerFailed("Native handler renderer recovery navigation failed");
    std::vector<int> failed;
    for (const auto& [id, nav] : navigations_)
      if (nav.recovery) failed.push_back(id);
    for (const int id : failed)
      Complete(id, false,
               "Native renderer recovery navigation failed (error " +
                   std::to_string(static_cast<int>(code)) + ")");
  }
  if (frame->IsMain() && navigation_ && code != ERR_ABORTED) {
    Complete(navigation_, false, "Native navigation failed");
  }
}

auto Source::OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) -> void {
  if (!closing_page_ && network_ &&
      !engine_.network_archive().PageTitle(network_context_id_, "page:" + std::to_string(page_id_),
                                           title.ToString()))
    NetworkFatal("Native HAR actual page title capacity exceeded");
}

auto Source::OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, TransitionType) -> void {
  if (frame->IsMain()) {
    for (const auto& [id, handle] : handles_) {
      auto params = CefDictionaryValue::Create();
      params->SetString("objectGroup", handle.group);
      SendDevTools("Runtime.releaseObjectGroup", params, handle.session);
    }
    ClearConsoleRawArguments(true);
    handles_.clear();
  }
}

auto Source::OnRenderViewReady(CefRefPtr<CefBrowser>) -> void {
  ++renderer_views_;
  CheckRendererRecovery();
}

auto Source::OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus, int,
                                       const CefString&) -> void {
  if (engine_.websocket_routes().HasPolicy(engine_.contexts()->ContextId(page_id_), page_id_) ||
      engine_.websocket_routes().Owns(NativeWebSocketPolicyOwner("")) || native_routed_active_ ||
      native_routed_configuring_)
    NativeWebSocketPolicyLoseRoot(
        "Native WebSocket renderer terminated; recreate the page because backend V1 has no "
        "authenticated agent handoff");
  NativeHARPrimaryReset();
  NativeFrameDebuggerRootRealmInvalidated();
  NativeDeveloperRealmDestroyed();
  // A clock owns the current document, not a recoverable renderer epoch.
  // Keep private Inspector receipts until actual OnBeforeClose settles them.
  engine_.contexts()->SnapshotRendererTerminated(page_id_);
  NativeVirtualTimeRendererTerminated();
  native_coverage_inventory_.Detached("");
  NativeCoverageRendererTerminated();
  engine_.screenshot_inputs()->PageClosed(page_id_);
  recovery_requested_ = false;
  failed_renderer_views_ = renderer_views_;
  RendererRecoveryHandlerFailed("Native renderer terminated during handler recovery");
  engine_.recordings()->ClosePage(page_id_);
#if defined(OS_LINUX) || defined(__APPLE__)
  engine_.streaming()->FailPage(page_id_);
#endif
  NativeDeveloperChanged();
  renderer_failed_ = true;
  NativePristineRendererFailed();
  SnapshotReferenceRendererClosed();
  NativeHighlightDetached("");
  RememberFailedEpoch();
  const char* reason = "Browser renderer terminated";
  if (page_id_ == 1) {
    for (const int id : operations_.Ids()) Complete(id, false, reason);
    Error(reason);
  } else {
    ChildRendererCrash(reason);
    if (!ready_ && browser_) {
      closed_reason_ = reason;
      browser_->GetHost()->CloseBrowser(true);
    }
  }
}

auto Source::CheckBodies() -> void {
  std::vector<int> ready;
  for (const auto& [id, wait] : body_waiters_) {
    const auto document = documents_.find(wait.key);
    if (!wait.dispatched && (document == documents_.end() || document->second.finished))
      ready.push_back(id);
  }
  for (const int id : ready) {
    const auto wait = body_waiters_.find(id);
    if (wait == body_waiters_.end()) continue;
    const auto document = documents_.find(wait->second.key);
    if (document == documents_.end()) {
      Complete(id, false, "Native response is no longer retained");
      continue;
    }
    if (!document->second.error.empty()) {
      Complete(id, false, document->second.error);
      continue;
    }
    wait->second.dispatched = true;
    wait->second.frame = document->second.frame;
    wait->second.loader = document->second.loader;
    operation_sessions_[id] = document->second.session;
    auto params = CefDictionaryValue::Create();
    params->SetString("requestId", document->second.request);
    DevTools(id, "Network.getResponseBody", params,
             [this, id](bool success, const std::string& json) {
               Complete(id, success, success ? "" : "Native response body failed", json);
             });
  }
}

auto Source::BeginNavigation(int id, Dict message, const std::string& command, int timeout)
    -> void {
  const auto frame_id = FrameId(message);
  const auto frame = frame_states_.find(frame_id);
  if (frame == frame_states_.end()) {
    Complete(id, false, "Frame was detached");
    return;
  }
  const auto wait = message->GetType("waitUntil") == VTYPE_STRING
                        ? message->GetString("waitUntil").ToString()
                        : "load";
  if (wait != "commit" && wait != "domcontentloaded" && wait != "load" && wait != "networkidle") {
    Complete(id, false, "Invalid native navigation load state");
    return;
  }
  Navigation nav;
  nav.frame = frame_id;
  nav.wait = wait;
  nav.timeout = timeout;
  nav.native_download_after = engine_.downloads()->NavigationSequence();
  nav.api = message->GetString("api").ToString();
  if (nav.api != "page.goto" && nav.api != "frame.goto" && nav.api != "page.reload" &&
      nav.api != "page.goBack" && nav.api != "page.goForward" &&
      nav.api != "page.waitForNavigation" && nav.api != "frame.waitForNavigation" &&
      nav.api != "page.waitForLoadState" && nav.api != "frame.waitForLoadState") {
    Complete(id, false, "Invalid native navigation API");
    return;
  }
  operation_frames_[id] = frame_id;
  operation_sessions_[id] = frame->second.session;
  if (command == "historyBack" || command == "historyForward") {
    if (frame_id != main_frame_id_) {
      Complete(id, false, "Native history navigation requires the main frame");
      return;
    }
    const bool back = command == "historyBack";
    if (back ? !browser_->CanGoBack() : !browser_->CanGoForward()) {
      Complete(id, true, "", "null");
      return;
    }
    navigations_[id] = nav;
    DevTools(id, "Page.getNavigationHistory", nullptr,
             [this, id, back](bool ok, const std::string& json) {
               auto found = navigations_.find(id);
               if (found == navigations_.end()) return;
               auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
               auto result = parsed ? parsed->GetDictionary() : nullptr;
               auto entries = result ? result->GetList("entries") : nullptr;
               if (!ok || !entries || result->GetType("currentIndex") != VTYPE_INT) {
                 Complete(id, false, "Native browser history is unavailable");
                 return;
               }
               const int target = result->GetInt("currentIndex") + (back ? -1 : 1);
               if (target < 0 || static_cast<size_t>(target) >= entries->GetSize()) {
                 Complete(id, true, "", "null");
                 return;
               }
               auto entry = entries->GetDictionary(target);
               if (!entry || entry->GetType("id") != VTYPE_INT ||
                   entry->GetType("url") != VTYPE_STRING) {
                 Complete(id, false, "Invalid native history entry");
                 return;
               }
               auto frame = frame_states_.find(found->second.frame);
               if (frame == frame_states_.end()) {
                 Complete(id, false, "Frame was detached");
                 return;
               }
               auto& nav = found->second;
               nav.next = true;
               nav.after = frame->second.navigation;
               nav.url = frame->second.url;
               nav.previous_loader = frame->second.loader;
               nav.history_entry = entry->GetInt("id");
               nav.history_url = entry->GetString("url").ToString();
               auto params = CefDictionaryValue::Create();
               params->SetInt("entryId", nav.history_entry);
               DevTools(id, "Page.navigateToHistoryEntry", params,
                        [this, id](bool ok, const std::string&) {
                          auto found = navigations_.find(id);
                          if (found == navigations_.end()) return;
                          if (!ok) {
                            Complete(id, false, "Native history navigation failed");
                            return;
                          }
                          found->second.dispatched = true;
                          CheckNavigations();
                        });
             });
    return;
  }
  if (command == "reload") {
    nav.next = true;
    nav.after = frame->second.navigation;
    nav.url = frame->second.url;
    nav.previous_loader = frame->second.loader;
    navigations_[id] = nav;
    DevTools(id, "Page.reload", nullptr, [this, id](bool ok, const std::string&) {
      const auto found = navigations_.find(id);
      if (found == navigations_.end()) return;
      if (!ok) {
        Complete(id, false, "Native reload failed");
        return;
      }
      found->second.dispatched = true;
      CheckNavigations();
    });
    return;
  }
  if (command != "navigateFrame") {
    nav.wait_only = command == "waitLoadState";
    nav.next = !nav.wait_only;
    nav.dispatched = true;
    if (message->GetType("afterNavigation") == VTYPE_INT)
      nav.after = message->GetInt("afterNavigation");
    else
      nav.after = frame->second.navigation;
    nav.url = frame->second.url;
    nav.previous_loader = frame->second.loader;
    navigations_[id] = nav;
    CheckNavigations();
    return;
  }
  nav.url = message->GetString("url").ToString();
  if (nav.url.empty() || nav.url.size() > 8192 || nav.url.find('\0') != std::string::npos) {
    Complete(id, false, "Invalid native navigation URL");
    return;
  }
  CefURLParts parts;
  if (!CefParseURL(nav.url, parts)) {
    Complete(id, false, "Invalid native navigation URL");
    return;
  }
  const auto scheme = CefString(&parts.scheme).ToString();
  if (scheme != "http" && scheme != "https" && scheme != "about" && scheme != "data" &&
      scheme != "file") {
    Complete(id, false, "Unsupported native navigation URL scheme");
    return;
  }
  std::vector<int> superseded;
  for (const auto& [owner, old] : navigations_)
    if (old.frame == frame_id && !old.wait_only && (!old.next || old.replace_initial))
      superseded.push_back(owner);
  for (const int owner : superseded)
    Complete(owner, false, "Native navigation was interrupted by another navigation");
  nav.after = frame->second.navigation;
  navigations_[id] = nav;
  auto params = CefDictionaryValue::Create();
  params->SetString("url", nav.url);
  params->SetString("frameId", frame_id);
  if (message->GetType("referer") == VTYPE_STRING)
    params->SetString("referrer", message->GetString("referer"));
  if (!ReplaceInitialNavigation(id, params)) DispatchFrameNavigation(id, params);
}

auto Source::CheckNavigations() -> void {
  CheckRendererRecovery();
  std::vector<int> history_checks;
  std::vector<std::pair<int, std::string>> complete;
  for (auto& [id, nav] : navigations_) {
    if (!nav.dispatched || !nav.download_abort.empty() || (nav.recovery && renderer_failed_))
      continue;
    const auto frame = frame_states_.find(nav.frame);
    if (frame == frame_states_.end()) continue;
    if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000)
      std::fprintf(stderr,
                   "native navigation id=%d loader=%s frameLoader=%s sameDocument=%d after=%llu "
                   "frameNavigation=%llu wait=%s\n",
                   id, nav.loader.c_str(), frame->second.loader.c_str(), nav.same_document,
                   static_cast<unsigned long long>(nav.after),
                   static_cast<unsigned long long>(frame->second.navigation), nav.wait.c_str());
    if (nav.history_entry >= 0 && !nav.history_verified) {
      // A newly committed document can still be swapping renderer endpoints.
      // Its real Runtime context marks when the page command channel is usable.
      if (!frame->second.context || frame->second.navigation <= nav.after ||
          frame->second.url != nav.history_url || nav.history_pending ||
          nav.history_candidate == frame->second.navigation)
        continue;
      nav.history_pending = true;
      nav.history_candidate = frame->second.navigation;
      history_checks.push_back(id);
      continue;
    }
    if (nav.next) {
      if (frame->second.navigation <= nav.after) continue;
      if (nav.loader.empty()) {
        if (frame->second.loader == nav.previous_loader) {
          complete.emplace_back(id, "null");
          continue;
        }
        nav.loader = frame->second.loader;
      }
    }
    if (nav.same_document) {
      if (frame->second.navigation > nav.after) complete.emplace_back(id, "null");
      continue;
    }
    if (!nav.wait_only && frame->second.loader != nav.loader) {
      if (!nav.next && frame->second.navigation > nav.after &&
          frame->second.milestones.contains("commit"))
        nav.loader = frame->second.loader;
      else
        continue;
    }
    const auto milestone = nav.wait == "domcontentloaded" ? "DOMContentLoaded" : nav.wait;
    bool done = nav.wait == "commit" ? frame->second.milestones.contains("commit")
                                     : frame->second.milestones.contains(milestone);
    if (nav.wait == "networkidle") {
      double last = frame->second.network_activity;
      for (const auto& [child, state] : frame_states_)
        if (Descendant(child, nav.frame)) last = std::max(last, state.network_activity);
      done = Now() - last >= 500;
      for (const auto& [key, request] : active_requests_)
        if (Descendant(request.frame, nav.frame)) {
          done = false;
          break;
        }
    }
    if (!done) continue;
    if (nav.wait_only) {
      complete.emplace_back(id, "null");
      continue;
    }
    const Document* document = nullptr;
    for (const auto& [key, candidate] : documents_)
      if (candidate.document && candidate.loader == frame->second.loader &&
          candidate.frame == nav.frame && candidate.response) {
        document = &candidate;
        break;
      }
    if (!document) {
      complete.emplace_back(id, "null");
      continue;
    }
    auto response =
        suitecut::NativeResponseWireInfo(document->response, document->request, document->session,
                                         document->frame, document->method);
    complete.emplace_back(id, Json(response));
  }
  for (const int id : history_checks) {
    if (!navigations_.contains(id)) continue;
    DevTools(id, "Page.getNavigationHistory", nullptr,
             [this, id](bool ok, const std::string& json) {
               auto found = navigations_.find(id);
               if (found == navigations_.end()) return;
               found->second.history_pending = false;
               auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
               auto result = parsed ? parsed->GetDictionary() : nullptr;
               auto entries = result ? result->GetList("entries") : nullptr;
               if (!ok || !entries || result->GetType("currentIndex") != VTYPE_INT) {
                 Complete(id, false, "Native history commit identity is unavailable");
                 return;
               }
               const int current = result->GetInt("currentIndex");
               if (current < 0 || static_cast<size_t>(current) >= entries->GetSize()) {
                 Complete(id, false, "Invalid committed native history entry");
                 return;
               }
               auto entry = entries->GetDictionary(current);
               auto frame = frame_states_.find(found->second.frame);
               if (entry && entry->GetInt("id") == found->second.history_entry &&
                   frame != frame_states_.end() && frame->second.url == found->second.history_url)
                 found->second.history_verified = true;
               CheckNavigations();
             });
  }
  for (const auto& [id, json] : complete) {
    const auto nav = navigations_.find(id);
    const auto operation = operations_.Get(id);
    if (nav != navigations_.end() && nav->second.recovery && operation &&
        operations_.IsExpired(id, operation->generation, Now()))
      Complete(id, false, "Native renderer recovery original action deadline exceeded");
    else
      Complete(id, true, "", json);
  }
}

}  // namespace suitecut::browser
