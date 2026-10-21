// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::OnDevToolsMessage(CefRefPtr<CefBrowser>, const void* message, size_t size) -> bool {
  const auto native_inspector_callback = NativeInspectorCallback();
  DevToolsObserverScope observer(devtools_observer_depth_);
  if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000)
    std::fprintf(stderr, "native received: %.*s\n", static_cast<int>(std::min(size, size_t(2048))),
                 static_cast<const char*>(message));
  if (size > kMaxResponseBytes + 65536) {
    Error("Native protocol message limit exceeded");
    return true;
  }
  auto parsed = CefParseJSON(std::string(static_cast<const char*>(message), size), JSON_PARSER_RFC);
  auto data = parsed ? parsed->GetDictionary() : nullptr;
  if (!data) {
    // An actual Inspector reply can contain a JavaScript isolated UTF16
    // scalar that CEF dictionaries cannot represent. Parse a separate
    // validation projection only to recover the genuine envelope identity.
    // No projected role/name/value is ever forwarded as browser metadata.
    const auto original = std::string(static_cast<const char*>(message), size);
    const auto projection = suitecut::StorageJSONValidationProjection(original);
    if (projection != original) {
      const auto validated = CefParseJSON(projection, JSON_PARSER_RFC);
      const auto envelope = validated ? validated->GetDictionary() : nullptr;
      if (envelope && envelope->GetType("id") == VTYPE_INT &&
          (!envelope->HasKey("sessionId") || envelope->GetType("sessionId") == VTYPE_STRING)) {
        const int native_id = envelope->GetInt("id");
        const auto native_session = envelope->GetString("sessionId").ToString();
        const auto pending = devtools_.find(native_id);
        const auto trace = native_trace_protocol_.find(native_id);
        const bool trace_scalars =
            trace != native_trace_protocol_.end() && trace->second.session == native_session;
        const auto tree = session_trees_.find(native_id);
        const bool frame_scalars =
            (bootstrap_tree_ && native_id == bootstrap_tree_ && native_session.empty()) ||
            (tree != session_trees_.end() && tree->second == native_session);
        if ((frame_scalars || trace_scalars ||
             (pending != devtools_.end() && pending->second.session == native_session &&
              pending->second.lossless_scalars) ||
             (page_errors_ && page_errors_->Accepts(native_id, native_session))) &&
            !envelope->HasKey("error")) {
          std::string lossless, error;
          if (suitecut::DevToolsLosslessProjection(original, lossless, error)) {
            auto typed = CefParseJSON(lossless, JSON_PARSER_RFC);
            auto packet = typed ? typed->GetDictionary() : nullptr;
            auto result = packet ? packet->GetDictionary("result") : nullptr;
            if (result) {
              const auto json = Json(result);
              ToolResult(native_id, true, json.data(), json.size(), native_session);
              return true;
            }
          }
        }
        constexpr char failure[] =
            R"({"message":"Native DevTools raw UTF16 scalar is unsupported"})";
        ToolResult(envelope->GetInt("id"), false, failure, sizeof(failure) - 1,
                   envelope->GetString("sessionId").ToString());
      }
    }
    if (projection != original) {
      const auto validation = CefParseJSON(projection, JSON_PARSER_RFC);
      const auto envelope = validation ? validation->GetDictionary() : nullptr;
      if (envelope && !envelope->HasKey("id") &&
          (envelope->GetString("method") == "Page.frameNavigated" ||
           envelope->GetString("method") == "Runtime.consoleAPICalled" ||
           envelope->GetString("method") == "Runtime.exceptionThrown" ||
           (native_coverage_ &&
            (envelope->GetString("method") == "Debugger.scriptParsed" ||
             envelope->GetString("method") == "CSS.styleSheetAdded" ||
             envelope->GetString("method") == "Profiler.navigationCoverageFinalized" ||
             envelope->GetString("method") == "CSS.navigationCoverageFinalized")) ||
           (native_debugger_domains_ && (envelope->GetString("method") == "Debugger.scriptParsed" ||
                                         envelope->GetString("method") == "Debugger.paused")))) {
        std::string lossless, cause;
        if (!suitecut::DevToolsLosslessProjection(original, lossless, cause)) {
          Error(cause.c_str());
          return true;
        }
        const auto parsed = CefParseJSON(lossless, JSON_PARSER_RFC);
        const auto event = parsed ? parsed->GetDictionary() : nullptr;
        const auto params = event ? event->GetDictionary("params") : nullptr;
        if (!params) {
          Error("Invalid native lossless runtime event");
          return true;
        }
        if (event->GetType("method") != VTYPE_STRING ||
            (event->HasKey("sessionId") && event->GetType("sessionId") != VTYPE_STRING)) {
          Error("Invalid native lossless event identity");
          return true;
        }
        NativeDeveloperEvent(event->GetString("method").ToString(), params,
                             event->GetString("sessionId").ToString());
        if (NativeDebuggerEvent(event->GetString("method").ToString(), params,
                                event->GetString("sessionId").ToString()))
          return true;
        if (NativeCoverageEvent(event->GetString("method").ToString(), params,
                                event->GetString("sessionId").ToString()))
          return true;
        const auto json = Json(params);
        ToolEvent(event->GetString("method"), json.data(), json.size(),
                  event->GetString("sessionId").ToString());
      }
    }
    return true;
  }
  const auto session = data->GetString("sessionId").ToString();
  if (data->GetType("id") == VTYPE_INT) {
    const int id = data->GetInt("id");
    const bool success = !data->HasKey("error");
    auto result = data->GetDictionary(success ? "result" : "error");
    const auto json = result ? Json(result) : "{}";
    ToolResult(id, success, json.data(), json.size(), session);
  } else if (data->GetType("method") == VTYPE_STRING) {
    auto params = data->GetDictionary("params");
    if (session.empty() && private_response_ &&
        private_response_->Event(data->GetString("method"), params))
      return true;
    NativeHARResponseBytes(data->GetString("method").ToString(), params, session);
    if (data->GetString("method") == "Fetch.requestPaused") {
      if (params && InitSnapshotTraffic("Fetch.requestPaused", params, session)) return true;
      if (network_)
        network_->Event("Fetch.requestPaused", params, session);
      else
        Error("Native interception controller unavailable");
      return true;
    }
    // Already parsed byte entries may accompany a Latin1 rendered POST.
    // Project once into the bounded native publication, avoiding giant copies.
    if (data->GetString("method") == "Network.requestWillBeSent" && params) {
      if (InitSnapshotTraffic("Network.requestWillBeSent", params, session)) return true;
      WorkerAttributeNetwork("Network.requestWillBeSent", params, session);
      AttributeInitialPopupRequest(params);
      if (session.empty() && ready_ && params->GetString("frameId") == main_frame_id_ &&
          params->GetString("type") == "Document")
        if (!engine_.network_archive().PageLifecycle(
                network_context_id_, "page:" + std::to_string(page_id_), "request",
                suitecut::DictionaryNumber(params, "timestamp") * 1000,
                suitecut::DictionaryNumber(params, "wallTime") * 1000,
                params->GetString("loaderId")))
          NetworkFatal("Native HAR actual main document time rejected");
      if (!engine_.network_archive().OriginalRequest(engine_.contexts()->ContextId(page_id_),
                                                     "page:" + std::to_string(page_id_), session,
                                                     params)) {
        Error("Native network archive original request rejected");
      }
      if (engine_.network_archive().NeedsTick()) engine_.WakeTrace();
      if (network_) network_->Event("Network.requestWillBeSent", params, session);
      if (session.empty() && private_response_)
        private_response_->Primary("Network.requestWillBeSent", params);
      NativeImageStreamEvent("Network.requestWillBeSent", params, session);
      NetworkEvent("Network.requestWillBeSent", params, session);
      return true;
    }
    if (data->GetString("method") == "Network.dataReceived" && params) {
      if (InitSnapshotTraffic("Network.dataReceived", params, session)) return true;
      if (network_) network_->Event("Network.dataReceived", params, session);
      NativeImageStreamEvent("Network.dataReceived", params, session);
      return true;
    }
    if (data->GetString("method") == "Fetch.authRequired") {
      if (network_) network_->Event("Fetch.authRequired", params, session);
      return true;
    }
    if (data->GetString("method") == "Network.suiteCutWebSocketRoutedV1") {
      if (!NativeWebSocketPolicyEvent(params, session)) NativeWebSocketRouteEvent(params, session);
      return true;
    }
    if (data->GetString("method").ToString().starts_with("Network.webSocket")) {
      if (sockets_) sockets_->Event(data->GetString("method"), params, session, Now());
      return true;
    }
    NativeDeveloperEvent(data->GetString("method").ToString(), params, session);
    if (NativeDebuggerEvent(data->GetString("method").ToString(), params, session)) return true;
    if (NativeCoverageEvent(data->GetString("method").ToString(), params, session)) return true;
    const auto json = params ? Json(params) : "{}";
    ToolEvent(data->GetString("method").ToString(), json.data(), json.size(), session);
  }
  return true;
}

auto Source::ToolResult(int id, bool success, const void* result, size_t size,
                        const std::string& session) -> void {
  const suitecut::NativeWorkOriginFrame native_origin(NativeInspectorReplyOrigin(id, session));
  NativeInspectorReply(id, session);
  NativeHARPrimaryReceipt(id, success, session);
  // This is the real native reply on the exact root Source channel, not a
  // guessed match to an interleaved process-wide diagnostic reply line.
  if (session.empty() && id == diagnostic_network_enable_) {
    diagnostic_network_enable_ = -1;
    if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000)
      std::fprintf(stderr,
                   "native Network.enable result page=%d browser=%d id=%d session=root ok=%d\n",
                   page_id_, browser_ ? browser_->GetIdentifier() : 0, id, success ? 1 : 0);
  }
  if (element_states_ &&
      element_states_->Complete(
          id, success && size <= 65536,
          size && size <= 65536 ? std::string(static_cast<const char*>(result), size) : "{}",
          session))
    return;
  if (page_errors_ &&
      page_errors_->Complete(id, success && size <= suitecut::PageErrorControls::kResponse,
                             size && size <= suitecut::PageErrorControls::kResponse
                                 ? std::string(static_cast<const char*>(result), size)
                                 : "{}",
                             session))
    return;
  if (NativeTraceProtocolResult(id, success, result, size, session)) return;
  if (WorkerToolResult(id, success, result, size, session)) return;
  const auto binding_call = binding_calls_.find(id);
  if (binding_call != binding_calls_.end()) {
    if (binding_call->second.session != session) {
      Error("Native binding protocol session mismatch");
      return;
    }
    auto callback = std::move(binding_call->second.callback);
    binding_calls_.erase(binding_call);
    if (size > 1048576)
      callback(false, "Native binding protocol response limit exceeded");
    else
      callback(success, size ? std::string(static_cast<const char*>(result), size) : "{}");
    return;
  }
  const auto network_call = network_calls_.find(id);
  if (network_call != network_calls_.end()) {
    if (network_call->second.session != session) {
      Error("Native network protocol session mismatch");
      return;
    }
    const bool expired = network_call->second.expired;
    auto callback = std::move(network_call->second.callback);
    network_calls_.erase(network_call);
    if (expired)
      callback(false, "Native network command deadline expired");
    else if (size > kMaxResponseBytes)
      callback(false, "Native network protocol response limit exceeded");
    else
      callback(success, std::string(static_cast<const char*>(result), size));
    return;
  }
  if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && !success &&
      diagnostic_messages_++ < 2000)
    std::fprintf(stderr, "native protocol error id=%d session=%s: %.*s\n", id, session.c_str(),
                 static_cast<int>(std::min(size, size_t(8192))), static_cast<const char*>(result));
  const auto utility = utility_worlds_.find(id);
  if (utility != utility_worlds_.end()) {
    const auto world = utility->second;
    utility_worlds_.erase(utility);
    const auto frame = frame_states_.find(world.frame);
    if (frame == frame_states_.end() || frame->second.session != session ||
        frame->second.loader != world.loader)
      return;
    if (!success) {
      Error("Native DOM execution world could not be created");
      return;
    }
    auto parsed =
        CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
    auto response = parsed ? parsed->GetDictionary() : nullptr;
    if (response && response->GetType("executionContextId") == VTYPE_INT)
      frame->second.utility_context = response->GetInt("executionContextId");
    CheckNavigations();
    return;
  }
  const auto boot = session_trees_.find(id);
  if (boot != session_trees_.end()) {
    const auto expected = boot->second;
    NativeFrameDebuggerTreeReceipt frame_tree_receipt{this, id, session,
                                                      NativeFrameDebuggerAttachment(session)};
    session_trees_.erase(boot);
    if (expected != session || !sessions_.contains(session)) return;
    if (!success) {
      Error("Native child frame bootstrap failed");
      return;
    }
    auto parsed =
        CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
    auto response = parsed ? parsed->GetDictionary() : nullptr;
    auto tree = response ? response->GetDictionary("frameTree") : nullptr;
    if (tree) ImportFrameTree(tree, session, id);
    return;
  }
  if (id == bootstrap_tree_) {
    NativeFrameDebuggerTreeReceipt frame_tree_receipt{this, id, session,
                                                      NativeFrameDebuggerAttachment(session)};
    bootstrap_tree_ = 0;
    if (!success || size > 1048576) {
      Error("Native frame bootstrap failed");
      return;
    }
    auto parsed =
        CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
    auto response = parsed ? parsed->GetDictionary() : nullptr;
    auto tree = response ? response->GetDictionary("frameTree") : nullptr;
    if (!tree) {
      Error("Native frame tree unavailable");
      return;
    }
    ImportFrameTree(tree, "", id);
    SettingsCommit();
    MaybeReady();
    return;
  }
  const auto found = devtools_.find(id);
  if (found == devtools_.end()) {
    // A canceled/expired evaluation can finish later. Its returned object
    // must not become an unowned remote reference in the renderer.
    if (success && size <= 1048576 && size > 0) {
      auto parsed =
          CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
      auto response = parsed ? parsed->GetDictionary() : nullptr;
      auto remote = response ? response->GetDictionary("result") : nullptr;
      auto details = response ? response->GetDictionary("exceptionDetails") : nullptr;
      std::set<std::string> objects;
      const auto collect = [&objects](Dict remote) {
        if (remote && remote->GetType("objectId") == VTYPE_STRING)
          objects.insert(remote->GetString("objectId"));
      };
      collect(remote);
      if (response) collect(response->GetDictionary("object"));
      if (details) collect(details->GetDictionary("exception"));
      // A canceled metadata query can return several remote references,
      // including accessor functions and symbols, after its group was freed.
      if (response)
        for (const char* key : {"result", "internalProperties", "privateProperties"}) {
          auto fields = response->GetList(key);
          if (fields)
            for (size_t i = 0; i < fields->GetSize(); ++i) {
              auto field = fields->GetDictionary(i);
              if (field)
                for (const char* member : {"value", "get", "set", "symbol"})
                  collect(field->GetDictionary(member));
            }
        }
      for (const auto& object : objects) {
        auto params = CefDictionaryValue::Create();
        params->SetString("objectId", object);
        SendDevTools("Runtime.releaseObject", params, session);
      }
    }
    return;
  }
  if (found->second.session != session) {
    if (SelectorWaitRejectedSession(id, success, result, size, session)) return;
    Error("Native protocol session mismatch");
    return;
  }
  auto callback = std::move(found->second.callback);
  const int owner = found->second.owner;
  const bool retained = found->second.retain_after_cancel;
  const size_t response_limit = found->second.response_limit;
  const auto artifact_generation = found->second.artifact_generation;
  const bool artifact_capture = found->second.artifact_capture;
  devtools_.erase(found);
  // Current preparation replies preserve the ordinary navigation/error path.
  // Cancellation/stale generations and late ACKs still settle their retained
  // callback, without ever acting on a replacement owner.
  const bool artifact_regular = artifact_generation && !artifact_capture &&
                                operations_.IsCurrent(owner, artifact_generation) &&
                                !operations_.IsExpired(owner, artifact_generation, Now()) &&
                                size <= response_limit;
  if (retained && !artifact_regular) {
    if (size > response_limit)
      callback(false, response_limit == kMaxResponseBytes
                          ? "{\"message\":\"Native artifact protocol response exceeds 192 MiB\"}"
                          : "{\"message\":\"Native DevTools response byte limit exceeded\"}");
    else
      callback(success, size ? std::string(static_cast<const char*>(result), size) : "{}");
    return;
  }
  if (!operations_.Contains(owner)) return;
  if (size > kMaxResponseBytes) {
    Complete(owner, false, "Native DevTools response exceeds 192 MiB");
    return;
  }
  const auto bytes = static_cast<const uint8_t*>(result);
  const auto json = size ? std::string(reinterpret_cast<const char*>(bytes), size) : "{}";
  if (image_streams_)
    if (const auto body = body_waiters_.find(owner);
        body != body_waiters_.end() && body->second.dispatched) {
      const auto document = documents_.find(body->second.key);
      if (document != documents_.end()) {
        const auto recovered = image_streams_->Resolve(document->second.session,
                                                       document->second.request, success, json);
        if (recovered.first && (!success || recovered.second != json)) {
          callback(true, recovered.second);
          return;
        }
      }
    }
  if (private_response_ && NativePrivateBodyFallback(success, json))
    if (const auto body = body_waiters_.find(owner);
        body != body_waiters_.end() && body->second.dispatched) {
      const auto document = documents_.find(body->second.key);
      const auto operation = operations_.Get(owner);
      if (operation && document != documents_.end() && document->second.session.empty()) {
        const auto generation = operation->generation;
        const auto deadline = operation->deadline_ms ? operation->deadline_ms : Now() + 30000;
        if (private_response_->Read(
                document->second.request, deadline,
                [this, owner, generation, callback, success, json](bool ok,
                                                                   const std::string& actual) {
                  if (!operations_.IsCurrent(owner, generation)) return;
                  if (closing_page_ || !browser_) {
                    Complete(owner, false,
                             closed_reason_.empty() ? "Native page is closed" : closed_reason_);
                    return;
                  }
                  if (operations_.IsExpired(owner, generation, Now())) {
                    Complete(owner, false, "Native response body original deadline exceeded");
                    return;
                  }
                  if (ok)
                    callback(true, actual);
                  else if (success)
                    callback(true, json);
                  else
                    Complete(owner, false, NativePrimaryBodyError(owner, json));
                }))
          return;
      }
    }
  if (!success) {
    auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
    auto response = parsed ? parsed->GetDictionary() : nullptr;
    std::string message = response && response->GetType("message") == VTYPE_STRING
                              ? response->GetString("message").ToString()
                              : "Native DevTools command failed";
    if (message.find("navigated") != std::string::npos ||
        (message.find("context") != std::string::npos &&
         (message.find("destroyed") != std::string::npos ||
          message.find("find") != std::string::npos)))
      message += " (execution context unavailable after navigation or closure)";
    // A failed body lookup can outlive its actual document resource. Use
    // the original request's loader, rather than the current URL, to explain
    // the failure while preserving Chromium's original error text.
    const auto body_read = body_waiters_.find(owner);
    if (body_read != body_waiters_.end() && body_read->second.dispatched &&
        !body_read->second.loader.empty()) {
      const auto frame = frame_states_.find(body_read->second.frame);
      if (frame != frame_states_.end() && !frame->second.loader.empty() &&
          frame->second.loader != body_read->second.loader)
        message += " (response frame has navigated away)";
    }
    if (count_queries_.contains(owner) &&
        (message.find("Cannot find context") != std::string::npos ||
         message.find("Execution context was destroyed") != std::string::npos ||
         message.find("Inspected target navigated") != std::string::npos ||
         message.find("Could not find object with given id") != std::string::npos)) {
      CompleteCountZero(owner);
      return;
    }
    // An actual frame/context lifecycle event owns empty-scope visibility.
    // Unrelated protocol failures remain errors, never a catch-and-false.
    if (visibility_snapshots_.Contains(owner)) {
      Complete(owner, false, message);
      return;
    }
    if (context_retries_.contains(owner) && (message.find("context") != std::string::npos ||
                                             message.find("navigated") != std::string::npos)) {
      RetryContext(owner);
      return;
    }
    Complete(owner, false, message);
    return;
  }
  callback(true, json);
}

auto Source::AllocateDevToolsId() -> int {
  return !browser_ || next_devtools_ == std::numeric_limits<int>::max() ? 0 : ++next_devtools_;
}

auto Source::SubmitDevTools(int id, const std::string& method, Dict params,
                            const std::string& session) -> bool {
  if (!id || !browser_) return false;
  const bool initial_tree = native_frame_debugger_trees_.Contains(id);
  const auto tree_attachment = native_frame_debugger_trees_.ExpectedAttachment(id, session);
  auto message = CefDictionaryValue::Create();
  message->SetInt("id", id);
  message->SetString("method", method);
  if (params) message->SetDictionary("params", params->Copy(false));
  if (!session.empty()) message->SetString("sessionId", session);
  const auto json = Json(message);
  const bool final_release = method == "Input.dispatchMouseEvent" && params &&
                             params->GetString("type") == "mouseReleased";
  NativeInspectorReserve(id, session);
  if (devtools_observer_depth_) {
    const bool queued =
        QueueDeferredDevTools(id, json, final_release, session, initial_tree, tree_attachment);
    if (!queued) NativeInspectorNeverDispatched(id);
    return queued;
  }
  if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && !session.empty() &&
      diagnostic_messages_++ < 2000) {
    if (method == "Fetch.continueWithAuth")
      std::fprintf(stderr,
                   "native sent session command: Fetch.continueWithAuth (credentials redacted)\n");
    else
      std::fprintf(stderr, "native sent session command: %s\n", json.substr(0, 2048).c_str());
  }
  // Passive receipt provenance only. Root startup submissions reach this
  // direct dispatch path; queued commands are not claimed by this marker.
  if (session.empty() && method == "Network.enable" &&
      std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000) {
    // Only the first actual direct root dispatch owns this diagnostic scalar.
    // A refused send produces no reply marker; no logical readiness uses it.
    if (!diagnostic_network_enable_) diagnostic_network_enable_ = id;
    std::fprintf(stderr, "native Network.enable dispatch page=%d browser=%d id=%d session=root\n",
                 page_id_, browser_->GetIdentifier(), id);
  }
  if (final_release) MarkFinalReleaseSubmitted(id);
  if (!NativeFrameDebuggerBeforeSend(id, session, initial_tree, tree_attachment)) {
    NativeInspectorNeverDispatched(id);
    return false;
  }
  const auto actual_browser = browser_;
  if (!actual_browser) {
    NativeInspectorNeverDispatched(id);
    return false;
  }
  return NativeInspectorCall(id, session, [actual_browser, &json] {
    return actual_browser->GetHost()->SendDevToolsMessage(json.data(), json.size());
  });
}

auto Source::MarkFinalReleaseSubmitted(int command) -> void {
  const auto tool = devtools_.find(command);
  if (tool == devtools_.end()) return;
  const auto input = input_work_.find(tool->second.owner);
  if (input == input_work_.end()) return;
  const auto& work = input->second;
  work->held_mouse = false;
  const int total =
      work->message->GetType("clickCount") == VTYPE_INT ? work->message->GetInt("clickCount") : 1;
  if (work->kind == "mouse" && work->action == "click" && work->phase == 2 && work->count == total)
    work->final_release_submitted = true;
}

auto Source::SendDevTools(const std::string& method, Dict params, const std::string& session)
    -> int {
  const int id = AllocateDevToolsId();
  return SubmitDevTools(id, method, params, session) ? id : 0;
}

auto Source::EnableSession(const std::string& session) -> void {
  SendDevTools("Page.enable", nullptr, session);
  auto life = CefDictionaryValue::Create();
  life->SetBool("enabled", true);
  SendDevTools("Page.setLifecycleEventsEnabled", life, session);
  auto network = CefDictionaryValue::Create();
  network->SetInt("maxTotalBufferSize", 128 * 1048576);
  network->SetInt("maxResourceBufferSize", 128 * 1048576);
  network->SetInt("maxPostDataSize", 2097152);
  if (session.empty())
    NativeHARPrimaryEnable(network);
  else
    SendDevTools("Network.enable", network, session);
  if (network_ && session.empty()) network_->SessionAttached(session);
  if (bindings_ && session.empty()) bindings_->SessionAttached(session);
  SendDevTools("Runtime.enable", nullptr, session);
  SendDevTools("Log.enable", nullptr, session);
  if (session.empty()) {
    auto focus = CefDictionaryValue::Create();
    focus->SetBool("enabled", true);
    SendDevTools("Emulation.setFocusEmulationEnabled", focus);
  }
}

auto Source::AutoAttach(const std::string& session) -> void {
  auto params = CefDictionaryValue::Create();
  params->SetBool("autoAttach", true);
  params->SetBool("waitForDebuggerOnStart", true);
  params->SetBool("flatten", true);
  SendDevTools("Target.setAutoAttach", params, session);
}

auto Source::DevTools(int owner, const std::string& method, Dict params, DevToolsCallback callback,
                      std::optional<std::string> target_session, bool retained_control,
                      size_t response_limit, bool lossless_scalars) -> void {
  const auto handler_owner = operations_.Get(owner);
  auto handler_native_origin = engine_.LocatorHandlerCommandOrigin(
      owner, page_id_, handler_owner ? handler_owner->generation : 0);
  if (const auto input = input_work_.find(owner); input != input_work_.end())
    handler_native_origin = input->second->native_origin;
  const suitecut::NativeWorkOriginFrame handler_origin(handler_native_origin);
  if (!browser_ || !operations_.Contains(owner)) return;
  if (ScreenshotAssertionRoute(owner, method, params,
                               SuiteCutArtifactController::ResponseStage::Unknown, callback,
                               target_session))
    return;
  if (const auto entry = operations_.Get(owner);
      entry && AssertionDeadlineExpire(owner, entry->generation))
    return;
  if (!ReferenceFrameOperationCurrent(owner)) return;
  if (native_dom_getters_.contains(owner) || element_bounds_operations_.contains(owner) ||
      locator_predicate_operations_.contains(owner) || aria_json_operations_.contains(owner)) {
    const auto entry = operations_.Get(owner);
    if (!entry || operations_.IsExpired(owner, entry->generation, Now())) {
      Complete(owner, false, "Native operation timed out");
      return;
    }
  }
  if (const auto bounds = element_bounds_work_.find(owner); bounds != element_bounds_work_.end())
    if (const auto work = bounds->second; work && !ElementBoundsCurrent(work)) return;
  if (const auto predicate = locator_predicate_work_.find(owner);
      predicate != locator_predicate_work_.end())
    if (!LocatorPredicateCurrent(predicate->second)) return;
  if (const auto snapshot = aria_json_work_.find(owner); snapshot != aria_json_work_.end())
    if (!AriaJSONCurrent(snapshot->second)) return;
  // DOM node description/resolution carry native DOM schema fields. Node
  // strings may contain isolated units even when only backend IDs are used.
  if (method == "DOM.describeNode" || method == "DOM.resolveNode") lossless_scalars = true;
  if (element_states_ && element_states_->Contains(owner) && method == "Runtime.callFunctionOn") {
    const auto entry = operations_.Get(owner);
    auto physical = element_states_->Begin(owner);
    if (!entry || !physical) {
      Complete(owner, false, "Native element-state physical capacity exceeded");
      return;
    }
    retained_control = true;
    callback = [this, owner, generation = entry->generation, physical,
                original = std::move(callback)](bool ok, const std::string& json) mutable {
      physical->Settle();
      if (operations_.IsCurrent(owner, generation)) original(ok, json);
    };
  }
  if (selector_waits_ && selector_waits_->Contains(owner)) {
    const auto wait_session = target_session.value_or(operation_sessions_[owner]);
    if (!selector_waits_->Group(owner, "suitecut-automation-" + std::to_string(owner),
                                wait_session) ||
        (params && params->GetType("objectGroup") == VTYPE_STRING &&
         !selector_waits_->Group(owner, params->GetString("objectGroup").ToString(),
                                 wait_session))) {
      Complete(owner, false, "Native selector wait object-group capacity exceeded");
      return;
    }
    // This family transfers the duplicate generic eval tracker before the
    // actual call is submitted, so cancellation cannot bypass the held
    // selector lease with a fire-and-forget release of the same group.
    if ((locator_suggestion_work_.contains(owner) || element_bounds_operations_.contains(owner) ||
         locator_predicate_operations_.contains(owner) || aria_json_operations_.contains(owner) ||
         assertion_deadlines_.Contains(owner)) &&
        !ReferenceTemporaryGroups(owner)) {
      Complete(owner, false, "Native element observation temporary-group ownership unavailable");
      return;
    }
    const auto entry = operations_.Get(owner);
    auto physical = selector_waits_->Begin(owner);
    if (!entry || !physical) {
      Complete(owner, false, "Native selector-wait physical capacity exceeded: " + method);
      return;
    }
    retained_control = true;
    callback = [this, owner, generation = entry->generation, physical,
                original = std::move(callback)](bool ok, const std::string& json) mutable {
      physical->Settle();
      if (operations_.IsCurrent(owner, generation) &&
          (!selector_waits_->Retrying(owner) || assertion_deadlines_.Get(owner, generation)))
        original(ok, json);
    };
  }
  const auto session = target_session.value_or(operation_sessions_[owner]);
  const int assigned = AllocateDevToolsId();
  const bool artifact_physical =
      artifacts_ && artifacts_->PhysicalPending(owner) &&
      (method == "Page.captureScreenshot" || method == "Page.printToPDF");
  uint64_t artifact_generation = 0;
  if (const auto config = artifact_input_configs_.find(owner);
      config != artifact_input_configs_.end()) {
    const auto entry = operations_.Get(owner);
    if (!entry || operations_.IsExpired(owner, entry->generation, Now())) {
      if (artifact_physical) artifacts_->PhysicalDispatchFailed(owner);
      Complete(owner, false, "Native artifact original deadline expired before dispatch");
      return;
    }
    retained_control = true;
    artifact_generation = entry->generation;
    callback = suitecut::RetainArtifactConfiguration(
        config->second,
        [this, owner, generation = entry->generation] {
          return operations_.IsCurrent(owner, generation);
        },
        [this, owner, generation = entry->generation] {
          return operations_.IsExpired(owner, generation, Now());
        },
        [this, owner] {
          Complete(owner, false, "Native artifact original deadline expired during rendering");
        },
        std::move(callback), artifact_physical);
  }
  const bool send_keys_preparation = RetainsKeyboardCall(owner, method);
  if (send_keys_preparation) retained_control = true;
  const bool physical = artifact_physical || retained_control;
  if (!assigned) {
    if (artifact_physical) artifacts_->PhysicalDispatchFailed(owner);
    Complete(owner, false, "Could not allocate DevTools command");
    return;
  }
  if (send_keys_preparation && !RetainKeyboardCall(owner, method, params, callback)) return;
  // CEF may deliver a same-document result synchronously during submission.
  // Register ownership before calling into it, including nested callbacks.
  devtools_.emplace(assigned, DevToolsCall{owner, std::move(callback), session, physical,
                                           std::min(response_limit, kMaxResponseBytes),
                                           retained_control, lossless_scalars, artifact_generation,
                                           artifact_physical && artifact_generation != 0});
  if (!SubmitDevTools(assigned, method, params, session)) {
    devtools_.erase(assigned);
    if (send_keys_preparation) SendKeysPhysicalFinished(owner);
    if (artifact_physical) artifacts_->PhysicalDispatchFailed(owner);
    Complete(owner, false, "Could not dispatch DevTools command");
  }
}

auto Source::Evaluate(int owner, const std::string& expression, DevToolsCallback callback,
                      const std::string& frame, bool utility, const std::string& assertion_matcher)
    -> void {
  WithFrame(
      owner, frame.empty() ? main_frame_id_ : frame,
      [this, owner, expression, frame, assertion_matcher,
       callback = std::move(callback)](int context) {
        if (!assertion_matcher.empty()) {
          const auto entry = operations_.Get(owner);
          if (!entry || !AssertionDeadlineDispatch(owner, entry->generation, assertion_matcher,
                                                   frame.empty() ? main_frame_id_ : frame))
            return;
        }
        evaluation_groups_[owner].insert(operation_sessions_[owner]);
        auto params = CefDictionaryValue::Create();
        params->SetString("expression", expression);
        params->SetInt("contextId", context);
        params->SetBool("awaitPromise", true);
        params->SetBool("returnByValue", true);
        params->SetBool("userGesture", true);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(owner));
        DevTools(owner, "Runtime.evaluate", params, std::move(callback));
      },
      utility);
}

}  // namespace suitecut::browser
