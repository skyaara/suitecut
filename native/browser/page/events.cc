// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::ToolEvent(const std::string& method, const void* params, size_t size,
                       const std::string& session) -> void {
  if (method == "Emulation.virtualTimeBudgetExpired") {
    if (session.empty() && size <= 64 && native_virtual_time_)
      native_virtual_time_->BudgetExpired(Now());
    return;
  }
  if (size > 1048576 && method == "Fetch.requestPaused") {
    auto parsed =
        CefParseJSON(std::string(static_cast<const char*>(params), size), JSON_PARSER_RFC);
    if (network_)
      network_->RejectOversizedPause(parsed ? parsed->GetDictionary() : nullptr, session);
    else
      Error("Native interception controller unavailable");
    return;
  }
  if (method == "Runtime.exceptionThrown" && size > 1048576) {
    Error("Native page error event limit exceeded");
    if (browser_) browser_->GetHost()->CloseBrowser(true);
    return;
  }
  if (size > 1048576 ||
      (method != "Page.frameAttached" && method != "Page.frameNavigated" &&
       method != "Page.frameDetached" && method != "Page.navigatedWithinDocument" &&
       method != "Page.frameRequestedNavigation" && method != "Page.frameStartedNavigating" &&
       method != "Page.frameStoppedLoading" && method != "Runtime.executionContextCreated" &&
       method != "Runtime.executionContextDestroyed" &&
       method != "Runtime.executionContextsCleared" && method != "Target.attachedToTarget" &&
       method != "Target.detachedFromTarget" && method != "Target.targetDestroyed" &&
       method != "Target.targetInfoChanged" && method != "Inspector.workerScriptLoaded" &&
       method != "Runtime.consoleAPICalled" && method != "Runtime.bindingCalled" &&
       method != "Runtime.exceptionThrown" && method != "Log.entryAdded" &&
       method != "Page.lifecycleEvent" && method != "Input.dragIntercepted" &&
       method != "Page.fileChooserOpened" && method != "WebAuthn.credentialAdded" &&
       method != "WebAuthn.credentialAsserted" && method != "Network.requestWillBeSent" &&
       method != "Network.responseReceived" && method != "Network.dataReceived" &&
       method != "Network.loadingFinished" && method != "Network.loadingFailed" &&
       method != "Network.requestWillBeSentExtraInfo" &&
       method != "Network.responseReceivedExtraInfo" && method != "Fetch.requestPaused"))
    return;
  if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000)
    std::fprintf(stderr, "native event %s session=%s: %.*s\n", method.c_str(), session.c_str(),
                 static_cast<int>(std::min(size, size_t(8192))), static_cast<const char*>(params));
  // Passive diagnostics only: never route chunk observations through request
  // migration, archive publication, native cursors or SDK event consumers.
  if (method == "Network.dataReceived") return;
  auto parsed = CefParseJSON(std::string(static_cast<const char*>(params), size), JSON_PARSER_RFC);
  auto data = parsed ? parsed->GetDictionary() : nullptr;
  if (data && NativeFrameDebuggerRegistrationEvent(method)) return;
  if (NativeVirtualTimeTargetEvent(method, data, session)) return;
  if (data && method == "Page.frameStartedNavigating")
    NativePristineNavigationStarted(session, data->GetString("frameId").ToString());
  if (data && InitSnapshotTraffic(method, data, session)) return;
  if (!data) return;
  if (native_coverage_ && session.empty() && method == "Page.frameNavigated") {
    NativeCoverageScripts();
    auto frame = data->GetDictionary("frame");
    if (frame && !frame->HasKey("parentId")) native_coverage_->Navigated();
  }
  if (CredentialEvent(method, data, session)) return;
  if (element_states_ && method == "Runtime.executionContextDestroyed" &&
      data->GetInt("executionContextId") > 0)
    element_states_->ContextDestroyed(session, data->GetInt("executionContextId"));
  if (element_states_ && method == "Runtime.executionContextsCleared")
    element_states_->ContextDestroyed(session);
  // Retire exact renderer object ownership before worker-only context events
  // return early; physical private commands remain pinned until their ACK.
  if (page_errors_ && method == "Runtime.executionContextDestroyed" &&
      data->GetInt("executionContextId") > 0)
    page_errors_->ContextDestroyed(session, data->GetInt("executionContextId"));
  if (page_errors_ && method == "Runtime.executionContextsCleared")
    page_errors_->ContextDestroyed(session);
  NativeWorkerDebuggerRuntime(method, data, session);
  NativeDeveloperWorkersChanged();
  WorkerConsoleRuntimeEvent(method, data, session);
  if (WorkerRuntimeEvent(method, data, session)) return;
  WorkerAttributeNetwork(method, data, session);
  if (method == "Network.requestWillBeSent") AttributeInitialPopupRequest(data);
  if (method == "Target.targetDestroyed") {
    NativeWorkerTargetDestroyed(data);
    return;
  }
  if (method == "Target.targetInfoChanged") {
    WorkerUpdated(data);
    return;
  }
  InitialNavigationEvent(method, data, session);
  if (ActionNavigationEvent(method, data)) return;
  if (method == "Runtime.exceptionThrown") {
    NativeVirtualTimeException(data, session);
    ConsoleException(data, session);
    return;
  }
  if (method == "Runtime.bindingCalled") {
    if (bindings_) bindings_->Event(data, session);
    return;
  }
  if (network_ && (method.starts_with("Network.") || method == "Fetch.requestPaused"))
    network_->Event(method, data, session);
  if (method == "Network.requestWillBeSent" || method == "Network.loadingFinished" ||
      method == "Network.loadingFailed") {
    if (session.empty() && private_response_) private_response_->Primary(method, data);
    NativeImageStreamEvent(method, data, session);
  }
  if (method == "Fetch.requestPaused") return;
  if (method == "Page.fileChooserOpened") {
    FileChooserEvent(data, session);
    return;
  }
  if (method == "Input.dragIntercepted") {
    NativeDragEvent(data);
    return;
  }
  if (method == "Page.lifecycleEvent") {
    const auto id = data->GetString("frameId").ToString();
    const auto frame = frame_states_.find(id);
    if (frame != frame_states_.end()) {
      const auto loader = data->GetString("loaderId").ToString();
      if (data->GetString("name") == "init" && loader != frame->second.loader) {
        frame->second.loader = loader;
        frame->second.milestones.clear();
      }
      if (loader == frame->second.loader)
        frame->second.milestones.insert(data->GetString("name").ToString());
      if (ready_ && session.empty() && id == main_frame_id_ && loader == frame->second.loader) {
        const auto timestamp = suitecut::DictionaryNumber(data, "timestamp") * 1000;
        if (!engine_.network_archive().PageLifecycle(
                network_context_id_, "page:" + std::to_string(page_id_), data->GetString("name"),
                timestamp,
                std::chrono::duration<double, std::milli>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count(),
                loader))
          NetworkFatal("Native HAR actual page lifecycle metadata rejected");
      }
    }
    CheckNavigations();
    return;
  } else if (method.starts_with("Network.")) {
    if (method == "Network.requestWillBeSent" || method == "Network.responseReceived" ||
        method == "Network.loadingFinished" || method == "Network.loadingFailed")
      NetworkEvent(method, data, session);
    return;
  } else if (method == "Runtime.consoleAPICalled") {
    const auto worker = worker_sessions_.find(session);
    if (worker != worker_sessions_.end() && worker_targets_.at(worker->second).kind != "worker") {
      auto args = data->GetList("args");
      if (args)
        for (size_t i = 0; i < args->GetSize(); ++i) {
          auto value = args->GetDictionary(i);
          if (value && value->GetType("objectId") == VTYPE_STRING) {
            auto release = CefDictionaryValue::Create();
            release->SetString("objectId", value->GetString("objectId"));
            SendDevTools("Runtime.releaseObject", release, session);
          }
        }
      return;
    }
    auto args = data->GetList("args");
    std::string text;
    if (args)
      for (std::size_t index = 0; index < args->GetSize(); ++index) {
        if (index) text += " ";
        text += ConsolePreview(args->GetDictionary(index));
        if (text.size() > 1000000) break;
      }
    auto stack = data->GetDictionary("stackTrace");
    auto calls = stack ? stack->GetList("callFrames") : nullptr;
    auto location = calls && calls->GetSize() ? calls->GetDictionary(0) : nullptr;
    ConsoleEvent(data->GetString("type"), text, location, Number(data, "timestamp"), args, session,
                 data->GetInt("executionContextId"));
    return;
  } else if (method == "Log.entryAdded") {
    auto entry = data->GetDictionary("entry");
    if (entry && entry->GetString("source") == "worker" &&
        worker_targets_.contains(entry->GetString("workerId")))
      return;
    if (entry) {
      auto location = CefDictionaryValue::Create();
      location->SetString("url", entry->GetString("url"));
      location->SetInt("lineNumber", entry->GetInt("lineNumber"));
      ConsoleEvent(entry->GetString("level").ToString(), entry->GetString("text").ToString(),
                   location, Number(entry, "timestamp"));
    }
    return;
  } else if (method == "Target.attachedToTarget") {
    auto info = data->GetDictionary("targetInfo");
    const auto child = data->GetString("sessionId").ToString();
    if (!info || child.empty() || child.size() > 128) {
      FailChildBootstrap("Invalid native child target identity");
      return;
    }
    const auto worker_generation = worker_generation_;
    if (WorkerAttached(data, session)) {
      NativeWorkerReattached(data, worker_generation);
      NativeWorkerDebuggerAttached(data, worker_generation);
      return;
    }
    if (info->GetString("type") != "iframe") {
      DeferChildBootstrap(child, "", true);
      return;
    }
    if (sessions_.size() >= 1024 && !sessions_.contains(child)) {
      FailChildBootstrap("Native target session limit exceeded");
      return;
    }
    const auto frame_id = info->GetString("targetId").ToString();
    if (frame_id.empty() || frame_id.size() > 256) {
      FailChildBootstrap("Invalid native child frame identity");
      return;
    }
    if (session.size() > 128 || child == session ||
        (native_target_parents_.size() >= 1024 && !native_target_parents_.contains(child)) ||
        (native_target_parents_.contains(child) && native_target_parents_.at(child) != session)) {
      FailChildBootstrap("Invalid native child target parent identity or parent-session limit");
      return;
    }
    if (network_ && !network_->FrameSessionAttached(child, session, frame_id)) return;
    if (!NativeFrameDebuggerAttached(child, session, frame_id)) return;
    // Only actual observed live Inspector edges belong here. NetworkControls
    // separately retains its migration graph while detached request bodies drain.
    native_target_parents_[child] = session;
    sessions_[child] = frame_id;
    const auto frame = frame_states_.find(frame_id);
    if (frame != frame_states_.end()) {
      NativeFrameDebuggerFrameInvalidated(frame_id, frame->second.session);
      frame->second.session = child;
      frame->second.context = 0;
      FrameContextChanged(frame->second);
      frame->second.context_identity.clear();
      frame->second.utility_context = 0;
      frame->second.utility_identity.clear();
    }
    native_pristine_child_waiting_[child] =
        data->GetType("waitingForDebugger") == VTYPE_BOOL && data->GetBool("waitingForDebugger");
    DeferChildBootstrap(child, frame_id, false);
    return;
  } else if (method == "Target.detachedFromTarget") {
    NativeTargetSessionDetached(data->GetString("sessionId").ToString());
    return;
  } else if (method == "Page.frameAttached") {
    auto info = CefDictionaryValue::Create();
    info->SetString("id", data->GetString("frameId"));
    info->SetString("parentId", data->GetString("parentFrameId"));
    StoreFrame(info, "frameattached", session);
  } else if (method == "Page.frameNavigated") {
    auto frame = data->GetDictionary("frame");
    if (frame) {
      StoreFrame(frame, "framenavigated", session,
                 data->GetString("type") == "BackForwardCacheRestore");
      NativePristineCommitted(session, frame->GetString("id").ToString(),
                              data->GetString("type") == "BackForwardCacheRestore");
    }
  } else if (method == "Page.navigatedWithinDocument") {
    if (data->GetString("frameId") == main_frame_id_) {
      initial_history_dirty_ = true;
      initial_history_candidate_ = false;
    }
    const auto found = frame_states_.find(data->GetString("frameId"));
    if (found != frame_states_.end()) {
      const auto url = data->GetString("url").ToString();
      if (url.size() <= 8192) {
        found->second.url = url;
        ++found->second.navigation;
        FrameEvent("framenavigated", found->second);
        CheckNavigations();
        if (found->first == main_frame_id_) {
          if (navigation_) Complete(navigation_, true);
          auto response = CefDictionaryValue::Create();
          response->SetString("url", url);
          response->SetInt("status", 0);
          const auto waiters = navigation_waiters_;
          for (const int id : waiters) Complete(id, true, "", Json(response));
        }
      }
    }
  } else if (method == "Page.frameDetached") {
    const auto id = data->GetString("frameId").ToString();
    if (data->GetString("reason") == "swap") {
      const auto frame = frame_states_.find(id);
      if (frame != frame_states_.end() && frame->second.session == session) {
        NativeFrameDebuggerFrameInvalidated(id, session);
        CompleteVisibilityInFrame(id);
        frame->second.context = 0;
        FrameContextChanged(frame->second);
        frame->second.context_identity.clear();
        frame->second.utility_context = 0;
        frame->second.utility_identity.clear();
      }
    } else
      DetachFrame(id);
  } else if (method == "Runtime.executionContextCreated") {
    auto context = data->GetDictionary("context");
    auto aux = context ? context->GetDictionary("auxData") : nullptr;
    if (aux && (aux->GetBool("isDefault") || context->GetString("name") == "__suitecut_dom__")) {
      const auto id = aux->GetString("frameId").ToString();
      if (id.empty() || id.size() > 128 ||
          (!frame_states_.contains(id) && frame_states_.size() >= 1024))
        return;
      const auto identity = context->GetString("uniqueId").ToString();
      if (identity.empty() || identity.size() > 256) {
        Error("Native execution context unique identity unavailable or too large");
        return;
      }
      auto& frame = frame_states_[id];
      frame.id = id;
      frame.session = session;
      FrameContextChanged(frame);
      if (aux->GetBool("isDefault")) {
        frame.context = context->GetInt("id");
        frame.context_identity = identity;
        frame.origin = context->GetString("origin").ToString();
        NativeFrameDebuggerDefaultObserved(frame);
        NativePristineContextCreated(id);
        if (id == main_frame_id_ && settings_bootstrap_) SettingsCommit();
        EnsureUtilityWorld(id);
        if (bindings_) bindings_->ContextAvailable(FrameJson(frame), session, frame.context);
      } else {
        frame.utility_context = context->GetInt("id");
        frame.utility_identity = identity;
      }
      CheckNavigations();
    }
  } else if (method == "Runtime.executionContextDestroyed") {
    const int context = data->GetInt("executionContextId");
    const auto destroyed_identity = data->GetString("executionContextUniqueId").ToString();
    NativePristineContextDestroyed(session, context, destroyed_identity);
    if (bindings_ && context > 0) bindings_->ContextRemoved(session, context);
    if (context > 0) CompleteCountsInContext(session, context, destroyed_identity);
    for (auto& [id, frame] : frame_states_)
      if (frame.session == session) {
        if (frame.context == context &&
            (destroyed_identity.empty() || frame.context_identity == destroyed_identity)) {
          NativeFrameDebuggerFrameInvalidated(id, session);
          FrameContextChanged(frame);
          frame.context = 0;
          frame.context_identity.clear();
        }
        if (frame.utility_context == context &&
            (destroyed_identity.empty() || frame.utility_identity == destroyed_identity)) {
          FrameContextChanged(frame);
          frame.utility_context = 0;
          frame.utility_identity.clear();
        }
      }
  } else {
    NativeFrameDebuggerContextsCleared(session);
    NativePristineContextsCleared(session);
    CompleteCountsInContext(session);
    if (bindings_) bindings_->ContextRemoved(session);
    for (auto& [id, frame] : frame_states_)
      if (frame.session == session) {
        FrameContextChanged(frame);
        frame.context = 0;
        frame.utility_context = 0;
        frame.context_identity.clear();
        frame.utility_identity.clear();
      }
  }
}

}  // namespace suitecut::browser
