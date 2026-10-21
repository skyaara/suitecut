// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::Command(const std::string& line) -> void {
  if (!browser_ || closing_page_) {
    auto parsed = CefParseJSON(line, JSON_PARSER_RFC);
    auto message = parsed ? parsed->GetDictionary() : nullptr;
    if (message)
      writer_.Reply(message->GetInt("id"), false, "Native page is closed");
    else
      writer_.ReleaseReply();
    return;
  }
  auto value = CefParseJSON(line, JSON_PARSER_RFC);
  auto message = value ? value->GetDictionary() : nullptr;
  if (!message || message->GetInt("version") != 1 || message->GetInt("id") < 1) {
    writer_.ReleaseReply();
    Error("Invalid native command");
    return;
  }
  const int id = message->GetInt("id");
  const auto command = message->GetString("command").ToString();
  const auto failed_action = message->GetString("action").ToString();
  const bool failed_owner_control =
      (command == "debugger" && failed_action == "close") || command == "diagnostics" ||
      command == "recoverPage" || command == "frameSubscription" ||
      command == "continueEventPredicate" ||
      ((command == "waitEventPredicate" || command == "waitEvent") &&
       message->GetString("event") == "crash") ||
      command == "cancel" || command == "download" || command == "networkBody" ||
      command == "routeCallback" ||
      ((command == "streaming" || command == "recording") &&
       (failed_action == "status" || failed_action == "stop" || failed_action == "cancel"));
  if (command == "recoverPage" && native_websocket_policy_recovery_blocked_) {
    writer_.Reply(
        id, false,
        "Native WebSocket renderer ownership retired; recreate the page before routing again");
    return;
  }
  if (renderer_failed_ && !failed_owner_control) {
    writer_.Reply(id, false, "Browser renderer terminated");
    return;
  }
  if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && command_diagnostic_messages_++ < 4000)
    std::fprintf(stderr, "native command page=%d id=%d command=%.64s\n", page_id_, id,
                 command.c_str());
  if (operations_.Contains(id)) {
    writer_.ReleaseReply();
    Error("Duplicate active native command ID");
    return;
  }
  if (command == "claimConsoleArguments") {
    ClaimConsoleArguments(id, message);
    return;
  }
  if (command == "acknowledgeConsole") {
    AcknowledgeConsoleDelivery(id, message);
    return;
  }
  // Raw-frame observation remains available at operation capacity and does
  // not acquire an authored DOM/input operation or change rendering cadence.
  if (command == "frameSubscription") {
    if (message->GetType("enabled") != VTYPE_BOOL) {
      writer_.Reply(id, false, "Invalid native frame subscription");
      return;
    }
    frame_transport_enabled_ = message->GetBool("enabled");
    // Replace any queued pre-subscription frame before acknowledging a new
    // subscription. An already-writing packet precedes this ACK on stdout.
    if (frame_transport_enabled_ && !native_view_stale_) EmitFrame();
    writer_.Reply(id, true);
    if (frame_transport_enabled_) {
      // The cached frame may itself reach Node before the ACK. Request a
      // genuine paint after it so a static page always resumes delivery.
      browser_->GetHost()->Invalidate(PET_VIEW);
    }
    return;
  }
  if (command == "locatorHandlerRegister") {
    LocatorHandlerRegister(id, message);
    return;
  }
  if (command == "continueAction") {
    ContinueAction(id, message);
    return;
  }
  if (command == "continueURLPredicate") {
    ContinueURLPredicate(id, message);
    return;
  }
  if (command == "continueEventPredicate") {
    ContinueEventPredicate(id, message);
    return;
  }
  // Cancellation is control traffic: it remains available at operation capacity.
  if (command == "cancel") {
    if (message->GetType("target") != VTYPE_INT || message->GetInt("target") < 1 ||
        message->GetInt("target") == id) {
      writer_.Reply(id, false, "Invalid cancellation target");
      return;
    }
    Complete(message->GetInt("target"), false, "Native operation canceled");
    writer_.Reply(id, true);
    return;
  }
  double timeout = 30000;
  if (message->HasKey("timeoutMs")) {
    if (message->GetType("timeoutMs") != VTYPE_INT || message->GetInt("timeoutMs") < 0 ||
        message->GetInt("timeoutMs") > 120000) {
      writer_.Reply(id, false, "Invalid native timeout");
      return;
    }
    timeout = message->GetInt("timeoutMs");
  }
  const auto binding_action = message->GetString("action").ToString();
  const bool binding_control =
      command == "bindings" &&
      (binding_action == "resolve" || binding_action == "argument" || binding_action == "stage");
  const bool recording_control = (command == "recording" || command == "streaming") &&
                                 (binding_action == "stop" || binding_action == "cancel");
  const bool handle_disposal_control =
      command == "handle" && (binding_action == "dispose" || binding_action == "disposeMany");
  const bool highlight_control =
      command == "highlight" && (binding_action == "hide" || binding_action == "claim");
  const auto operation =
      (command == "locatorHandlerRemove" || highlight_control || handle_disposal_control ||
       binding_control ||
       (command == "debugger" && (binding_action == "open" || binding_action == "close" ||
                                  binding_action == "resume" || binding_action == "stepOver" ||
                                  binding_action == "stepInto" || binding_action == "stepOut")) ||
       command == "recoverPage" || command == "networkBody" || recording_control)
          ? operations_.BeginControl(id, Now(), timeout)
          : operations_.Begin(id, Now(), timeout);
  if (!operation) {
    writer_.Reply(id, false, "Too many pending native operations");
    return;
  }
  if (!deadline_timer_active_) {
    deadline_timer_active_ = true;
    CefPostDelayedTask(
        TID_UI,
        new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },
                 suitecut::AcceptedTaskWork::Kind::Work, LocatorHandlerMonitorOrigin()),
        10);
  }
  if (!engine_.TrackLocatorHandlerCommand(id, page_id_, operation->generation)) {
    Complete(id, false, "Native locator handler command capacity exceeded");
    return;
  }
  if (command == "evaluate" || command == "queryEvaluate" || command == "locator" ||
      command == "input" || command == "form")
    network_publication_operations_.insert(id);
  if (!PrepareSnapshotReferenceScope(id, message)) return;
  if (message->HasKey("locatorHandlerOrigin")) {
    double cap = 0;
    if (!engine_.LocatorHandlerAdmission(message, cap) ||
        (cap && (!operation->deadline_ms || operation->deadline_ms > cap) &&
         !operations_.BindOriginalDeadline(id, operation->generation, cap))) {
      Complete(id, false, "Native locator handler child admission is stale or expired");
      return;
    }
  }
  if (NativeTraceBegin(id, message, command, timeout)) return;
  DispatchCommand(id, message, command, timeout);
  if (suitecut::CurrentNativeWorkOrigin().locator_handler_invocation)
    if (const auto retry = context_retries_.find(id); retry != context_retries_.end()) {
      auto callback = retry->second;
      const auto origin = suitecut::CurrentNativeWorkOrigin();
      retry->second = [origin, callback = std::move(callback)] {
        const suitecut::NativeWorkOriginFrame scope(origin);
        callback();
      };
    }
}

auto Source::DispatchCommand(int id, Dict message, const std::string& command, double timeout)
    -> void {
  const auto operation = operations_.Get(id);
  if (!operation || !browser_ || closing_page_) return;
  if (SDKBrowserControl(id, operation->generation, message, command, timeout)) return;
  if (command == "locatorHandlerRemove") {
    LocatorHandlerRemove(id, message);
    return;
  }
  if (command == "highlight") {
    NativeHighlightCommand(id, message);
    return;
  }
  if (command == "pickLocator") {
    BeginLocatorPick(id, message);
    return;
  }
  if (command == "suggestLocator") {
    BeginLocatorSuggestion(id, message);
    return;
  }
  if (command == "normalizeLocator") {
    context_retries_[id] = [this, id, message] { BeginLocatorNormalization(id, message); };
    lazy_queries_[id] = message;
    BeginLocatorNormalization(id, message);
    return;
  }
  if (command == "snapshotReferences") {
    context_retries_[id] = [this, id, message] { BeginReferenceSnapshot(id, message); };
    lazy_queries_[id] = message;
    BeginReferenceSnapshot(id, message);
    return;
  }
  if (command == "recoverPage") {
    if (NativeVirtualTimeOwned()) {
      Complete(id, false,
               "Renderer recovery is unsupported while native current-document virtual time owns "
               "this target");
      return;
    }
    BeginRendererRecovery(id, message, static_cast<int>(timeout));
    return;
  }
  if (command == "waitTimeout") {
    if ((message->GetType("ms") != VTYPE_INT && message->GetType("ms") != VTYPE_DOUBLE) ||
        !std::isfinite(Number(message, "ms")) || Number(message, "ms") < 0) {
      Complete(id, false, "Invalid native wait duration");
      return;
    }
    operation_frames_[id] = FrameId(message);
    native_wait_until_[id] = Now() + Number(message, "ms");
    if (Number(message, "ms") == 0) Complete(id, true);
    return;
  }
  if (NativeVirtualTimeOwned() &&
      (command == "navigate" || command == "navigateFrame" || command == "reload" ||
       command == "historyBack" || command == "historyForward")) {
    Complete(id, false,
             "Navigation is unsupported while native current-document virtual time is installed");
    return;
  }
  if (command == "clock") {
    NativeVirtualTimeCommand(id, operation->generation, message);
    return;
  }
  if (command == "bringToFront") {
    NativeBringToFront(id, operation->generation);
    return;
  }
  if (message->HasKey("nativeRootHandle") &&
      ((command != "queryEvaluate" && command != "handle") ||
       message->GetType("nativeRootHandle") != VTYPE_INT ||
       message->GetInt("nativeRootHandle") < 1 || message->GetType("query") != VTYPE_LIST ||
       message->HasKey("nativeHandle") || message->HasKey("framePath") ||
       (command == "handle" &&
        (message->GetString("action") != "create" || message->HasKey("expression") ||
         message->HasKey("function") || message->HasKey("queryFunction"))))) {
    Complete(id, false, "Invalid native scoped selector request");
    return;
  }
  if (command == "queryEvaluate") {
    if (message->HasKey("nativeAssertionKind") &&
        (message->GetType("nativeAssertionKind") != VTYPE_STRING ||
         !suitecut::AssertionDeadlineControls::Supported(
             message->GetString("nativeAssertionKind").ToString()) ||
         !message->GetBool("dom") || !message->GetBool("expectation") ||
         !message->GetBool("all"))) {
      Complete(id, false, "Invalid native assertion deadline entry");
      return;
    }
    if ((message->HasKey("diagnosticExpressionJSON") &&
         (message->GetType("diagnosticExpressionJSON") != VTYPE_STRING ||
          message->GetString("diagnosticExpressionJSON").length() > 16384)) ||
        (message->HasKey("strictDiagnostics") &&
         (message->GetType("strictDiagnostics") != VTYPE_BOOL ||
          (message->GetBool("strictDiagnostics") &&
           (!message->GetBool("dom") || !message->GetBool("expectation")))))) {
      Complete(id, false, "Invalid native query diagnostic request");
      return;
    }
    if (message->HasKey("visibilitySnapshot") &&
        (message->GetType("visibilitySnapshot") != VTYPE_BOOL ||
         (message->GetBool("visibilitySnapshot") &&
          (!message->GetBool("domGetter") || !message->GetBool("dom") ||
           !message->GetBool("immediate") || !message->GetBool("visibility") ||
           message->GetBool("all") || message->GetBool("count") ||
           message->GetBool("expectation") || message->GetBool("elementState") ||
           message->GetBool("stableState") || message->GetBool("accessibilitySnapshot") ||
           message->HasKey("handles"))))) {
      Complete(id, false, "Invalid native visibility snapshot request");
      return;
    }
    if (message->HasKey("domGetter")) {
      if (message->GetType("domGetter") != VTYPE_BOOL ||
          (message->GetBool("domGetter") &&
           (!message->GetBool("dom") || message->GetBool("count") ||
            message->GetBool("expectation")))) {
        Complete(id, false, "Invalid native DOM getter request");
        return;
      }
      if (message->GetBool("domGetter")) {
        if ((message->HasKey("strict") && message->GetType("strict") != VTYPE_BOOL) ||
            (message->HasKey("immediate") && message->GetType("immediate") != VTYPE_BOOL) ||
            (message->GetBool("immediate") && message->GetBool("all"))) {
          Complete(id, false, "Invalid native DOM getter strictness or immediate mode");
          return;
        }
        native_dom_getters_.insert(id);
      }
    }
    if (message->HasKey("nativeAccessibilitySnapshot") ||
        message->HasKey("nativeSemanticSnapshotJson") || message->HasKey("nativeQueryNodes") ||
        (message->HasKey("accessibilitySnapshot") &&
         message->GetType("accessibilitySnapshot") != VTYPE_BOOL) ||
        (message->HasKey("accessibilityBoxes") &&
         message->GetType("accessibilityBoxes") != VTYPE_BOOL) ||
        (message->HasKey("accessibilitySemantics") &&
         message->GetType("accessibilitySemantics") != VTYPE_BOOL)) {
      Complete(id, false, "Invalid native accessibility snapshot request");
      return;
    }
    if (message->HasKey("count") && message->GetType("count") != VTYPE_BOOL) {
      Complete(id, false, "Invalid native locator count snapshot");
      return;
    }
    const bool count = message->GetBool("count");
    if (!count && message->GetType("function") != VTYPE_STRING) {
      Complete(id, false, "Native locator evaluation requires a function");
      return;
    }
    auto config = message->Copy(false);
    if (message->GetBool("visibilitySnapshot")) {
      if (!visibility_snapshots_.Admit(id, operation->generation)) {
        Complete(id, false, "Native visibility snapshot capacity exceeded");
        return;
      }
      // This native mode has a fixed boolean result; arbitrary author
      // callbacks cannot be run against a replacement or absent document.
      config->SetString("function", "function(element,state){return !!state?.visible;}");
    }
    if (count) {
      if (message->GetBool("expectation") || message->HasKey("handles")) {
        Complete(id, false, "Invalid native locator count snapshot");
        return;
      }
      count_queries_.insert(id);
      config->SetBool("all", true);
      config->SetBool("dom", true);
      config->SetString("function", "function(nodes){return ['primitive',nodes.length];}");
    }
    // First slice: a direct native utility-world scalar assertion with an
    // already admitted full document identity. Mixed/custom/fixed/frame-path
    // and accessibility captures retain the existing hard-error contract.
    const auto assertion_path = config->GetList("framePath");
    if (config->HasKey("nativeAssertionKind") &&
        ((assertion_path && assertion_path->GetSize()) || FrameId(config) != main_frame_id_ ||
         config->HasKey("nativeHandle") || config->HasKey("nativeRootHandle") ||
         config->HasKey("handles") || config->GetBool("accessibility") ||
         config->GetBool("accessibilitySnapshot") || QueryNeedsMainWorld(config) ||
         QueryNeedsIsolatedWorld(config) || QueryUsesMultipleWorlds(config)))
      config->Remove("nativeAssertionKind");
    if (config->GetBool("expectation") && !config->HasKey("nativeAssertionKind"))
      first_observations_[id] = Now() + 1000;
    if (const auto path = config->GetList("framePath"); path && path->GetSize())
      lazy_queries_[id] = config;
    context_retries_[id] = [this, id, generation = operation->generation, config] {
      QueryEvaluate(id, generation, config);
    };
    QueryEvaluate(id, operation->generation, config);
  } else if (command == "streaming") {
    StreamingCommand(id, operation->generation, message);
  } else if (command == "recording") {
    RecordingCommand(id, operation->generation, message);
  } else if (command == "console") {
    ConsoleCommand(id, message);
  } else if (command == "dialog") {
    const auto action = message->GetString("action").ToString();
    if (action == "subscribe" && message->GetType("enabled") == VTYPE_BOOL) {
      dialogs_->Subscribe(message->GetBool("enabled"));
      Complete(id, true);
      return;
    }
    if (action != "answer" || message->GetType("dialogId") != VTYPE_STRING ||
        message->GetType("accept") != VTYPE_BOOL ||
        message->GetType("promptText") != VTYPE_STRING) {
      Complete(id, false, "Invalid native dialog command");
      return;
    }
    std::string error;
    const bool ok = dialogs_->Answer(message->GetString("dialogId"), message->GetBool("accept"),
                                     message->GetString("promptText"), error);
    Complete(id, ok, error);
  } else if (command == "bindings") {
    if (!bindings_) {
      Complete(id, false, "Native binding page unavailable");
      return;
    }
    if (message->GetString("action") == "argument") {
      Dict config;
      std::string error;
      if (!bindings_->PrepareArgument(id, message, config, error)) {
        Complete(id, false, error);
        return;
      }
      Handle(id, config);
      return;
    }
    bindings_->Command(id, message,
                       [this, id](bool ok, const std::string& error, const std::string& json) {
                         Complete(id, ok, error, json);
                       });
  } else if (command == "urlPredicate") {
    StartURLPredicate(id, operation->generation, message);
  } else if (command == "waitEventPredicate") {
    StartEventPredicate(id, operation->generation, message);
  } else if (command == "networkBody") {
    const bool accepted = network_ && network_->BodyAcknowledged(message->GetString("bodyId"),
                                                                 message->GetString("stage"));
    Complete(id, accepted, accepted ? "" : "Invalid native network body acknowledgement", "{}");
  } else if (command == "websocket") {
    const auto websocket_action = message->GetString("action").ToString();
    if (websocket_action.starts_with("routePayload") &&
        message->GetString("payloadScope") == "explicit") {
      NativeWebSocketExplicitPayloadCommand(id, operation->generation, message);
      return;
    }
    if (websocket_action == "routeRegister" || websocket_action == "routeUnregister" ||
        websocket_action == "routeUnregisterAll" || websocket_action == "routePolicyCommand" ||
        websocket_action.starts_with("routePayload")) {
      NativeWebSocketPolicyCommand(id, operation->generation, message);
      return;
    }
    if (message->GetString("action").ToString().starts_with("route")) {
      NativeWebSocketRouteCommand(id, operation->generation, message);
      return;
    }
    if (message->GetString("action") == "waitPredicate") {
      StartWebSocketPredicate(id, operation->generation, message);
      return;
    }
    if (!sockets_ || message->GetString("action") != "waitEvent" ||
        !sockets_->Wait(
            id, message->GetString("socketId"), message->GetString("event"),
            suitecut::DictionaryNumber(message, "afterSequence"), operation->deadline_ms,
            [this, id, generation = operation->generation](
                bool ok, Dict info, const std::vector<uint8_t>& bytes, const std::string& error) {
              if (!operations_.IsCurrent(id, generation)) return;
              if (!ok) {
                Complete(id, false, error);
                return;
              }
              auto result = CefDictionaryValue::Create();
              result->SetDictionary("info", info->Copy(false));
              result->SetString("payloadBase64", CefBase64Encode(bytes.data(), bytes.size()));
              Complete(id, true, "", Json(result));
            }))
      Complete(id, false, "Invalid native WebSocket event wait or capacity exceeded");
  } else if (command == "har") {
    const auto action = message->GetString("action").ToString();
    if (action == "record") {
      std::string error;
      auto config = message->GetDictionary("options");
      const bool ok = engine_.har_recording().Configure(network_context_id_, config, error);
      Complete(id, ok, error, "{}");
      return;
    }
    if (action != "replay") {
      Complete(id, false, "Invalid native HAR action");
      return;
    }
    const auto registration = message->GetString("registrationId").ToString(),
               pattern = message->HasKey("url") ? message->GetString("url").ToString() : "**",
               not_found = message->HasKey("notFound") ? message->GetString("notFound").ToString()
                                                       : "abort";
    if (registration.empty() || registration.size() > 128 || pattern.empty() ||
        pattern.size() > 8192 || (not_found != "abort" && not_found != "fallback")) {
      Complete(id, false, "Invalid native HAR replay registration");
      return;
    }
    const auto admission_order = suitecut::NativeRouteRegistrationOrder::Reserve();
    if (!admission_order) {
      Complete(id, false, "Native route registration order exhausted");
      return;
    }
    har_pending_[id] = registration;
    engine_.har_files().Read(
        id, message->GetString("path"), operation->deadline_ms,
        [self = CefRefPtr<Source>(this), id, generation = operation->generation, registration,
         pattern, not_found, admission_order = *admission_order](
            suitecut::NativeHARReplayFiles::Replay replay, const std::string& error) {
          if (!self->operations_.IsCurrent(id, generation) || self->closing_page_) return;
          if (self->operations_.IsExpired(id, generation, Now())) {
            self->Complete(id, false, "Native HAR original command deadline exceeded");
            return;
          }
          if (!replay) {
            self->Complete(id, false, error);
            return;
          }
          self->network_->BindReplay(
              registration, pattern, std::move(replay), not_found == "fallback",
              [self, id, generation](bool ok, const std::string& reason) {
                if (!self->operations_.IsCurrent(id, generation)) return;
                if (self->operations_.IsExpired(id, generation, Now()))
                  self->Complete(id, false, "Native HAR original command deadline exceeded");
                else
                  self->Complete(id, ok, reason, "{}");
              },
              self->operations_.Get(id)->deadline_ms, false, admission_order);
        },
        not_found == "fallback");
  } else if (command == "network") {
    if (!network_) {
      Complete(id, false, "Native network session unavailable");
      return;
    }
    network_->Command(
        id, message,
        [this, id, generation = operation->generation](bool ok, const std::string& error,
                                                       const std::string& json) {
          if (operations_.IsCurrent(id, generation)) Complete(id, ok, error, json);
        },
        operation->deadline_ms, operation->generation, network_context_id_);
  } else if (command == "form") {
    if (message->GetString("action") == "dragTo") {
      NativeDragStart(id, operation->generation, message);
      return;
    }
    if (message->GetString("action") == "fileChooserSubscribe") {
      FileChooserSubscribe(id, message);
      return;
    }
    if (message->GetString("action") == "fileChooserElement") {
      FileChooserElement(id, message);
      return;
    }
    FormStart(id, operation->generation, message);
  } else if (command == "debugger") {
    NativeDeveloperCommand(id, message);
  } else if (command == "coverage") {
    NativeCoverageCommand(id, message);
  } else if (command == "artifact") {
    if (FrameId(message) != main_frame_id_) {
      Complete(id, false, "Native page artifacts require the main frame");
      return;
    }
    if (message->GetString("action") == "emulateIdleState") {
      NativeIdleBegin(id, operation->generation, message, operation->deadline_ms);
      return;
    }
    if (message->GetString("action") == "capturePrepare") {
      ArtifactInputPrepare(id, message);
      return;
    }
    if (message->GetString("action") == "captureCommit") {
      ArtifactInputCommit(id, message);
      return;
    }
    if (message->GetString("action") == "comparePrepare") {
      ScreenshotInputPrepare(id, message);
      return;
    }
    if (message->GetString("action") == "compareCommit") {
      ScreenshotInputCommit(id, message);
      return;
    }
    if (message->GetString("action") == "compareScreenshot") {
      ScreenshotComparisonStart(id, message);
      return;
    }
    artifact_configs_[id] = message->Copy(false);
    artifact_configs_[id]->SetInt("timeoutMs", static_cast<int>(timeout));
    context_retries_[id] = [this, id] { ArtifactStart(id); };
    ArtifactStart(id);
  } else if (command == "gc") {
    WithFrame(id, FrameId(message), [this, id](int) {
      DevTools(id, "HeapProfiler.collectGarbage", nullptr, [this, id](bool ok, const std::string&) {
        Complete(id, ok, ok ? "" : "Native garbage collection failed");
      });
    });
  } else if (command == "viewport") {
    if (artifacts_ && artifacts_->ViewportLocked()) {
      Complete(id, false, "Native viewport resize unavailable during screenshot rasterization");
      return;
    }
    const int width = message->GetInt("width"), height = message->GetInt("height");
    if (message->GetType("width") != VTYPE_INT || message->GetType("height") != VTYPE_INT ||
        width < 1 || height < 1 || width > 3840 / scale_ || height > 2160 / scale_) {
      Complete(id, false, "Invalid native viewport dimensions");
      return;
    }
    width_ = width;
    height_ = height;
    // Cached old-view bytes must not acquire the new view's CSS geometry.
    frame_css_width_ = frame_css_height_ = 0;
    browser_->GetHost()->NotifyScreenInfoChanged();
    browser_->GetHost()->WasResized();
    operation_sessions_[id] = "";
    const auto params = context_settings_
                            ? context_settings_->ViewportMetrics({width, height}, scale_)
                            : suitecut::ContextSettings{}.ViewportMetrics({width, height}, scale_);
    DevTools(id, "Emulation.setDeviceMetricsOverride", params,
             [this, id, width, height](bool ok, const std::string&) {
               auto response = CefDictionaryValue::Create();
               response->SetInt("width", width);
               response->SetInt("height", height);
               Complete(id, ok, ok ? "" : "Native viewport resize failed", Json(response));
             });
  } else if (command == "input") {
    if (message->GetString("kind") == "touch" && message->GetString("action") != "tap") {
      BeginTouch(id, message);
      return;
    }
    BeginInput(id, operation->generation, message);
  } else if (command == "navigateFrame" || command == "reload" || command == "historyBack" ||
             command == "historyForward" || command == "waitLoadState" ||
             command == "waitFrameNavigation") {
    BeginNavigation(id, message, command, static_cast<int>(timeout));
  } else if (command == "responseBody") {
    const auto session = message->GetString("session").ToString();
    const auto request = message->GetString("requestId").ToString();
    if (session.size() > 128 || request.empty() || request.size() > 256 ||
        (!session.empty() && !sessions_.contains(session) && !worker_sessions_.contains(session))) {
      Complete(id, false, "Native response context is unavailable");
      return;
    }
    const auto key = session + "\n" + request;
    const auto document = documents_.find(key);
    if (document == documents_.end()) {
      Complete(id, false, "Native response is no longer retained");
      return;
    }
    body_waiters_[id] = {key};
    CheckBodies();
  } else if (command == "navigate") {
    const auto url = message->GetString("url").ToString();
    if (!url.starts_with("https://") && !url.starts_with("http://")) {
      Complete(id, false, "Navigation requires HTTP(S)");
      return;
    }
    if (navigation_) {
      Complete(id, false, "Navigation already in progress");
      return;
    }
    navigation_ = id;
    browser_->GetMainFrame()->LoadURL(url);
  } else if (command == "devtools") {
    const auto method = message->GetString("method").ToString();
    if (method.starts_with("Input.") && NativeDropBusy()) {
      Complete(id, false, "Raw Input command conflicts with native drag completion ownership");
      return;
    }
    // Every SuiteCut page uses a CEF windowless view. Chromium's window
    // snapshot path dereferences the absent Aura platform window in OSR.
    const auto raw_params = message->GetDictionary("params");
    if (method == "Page.captureScreenshot" && raw_params &&
        raw_params->GetType("fromSurface") == VTYPE_BOOL && !raw_params->GetBool("fromSurface")) {
      Complete(id, false,
               "Page.captureScreenshot fromSurface:false requires a platform window; SuiteCut CEF "
               "offscreen pages support surface capture only");
      return;
    }
    if (method == "Emulation.setVirtualTimePolicy") {
      if (NativeVirtualTimeOwned()) {
        Complete(id, false, "Native virtual-time controller owns this target's policy");
        return;
      }
      native_virtual_time_raw_used_ = true;
    }
    if (NativeVirtualTimeOwned() &&
        (method == "Performance.disable" || method == "Page.disable" ||
         method == "Runtime.disable" || method == "Page.navigate" || method == "Page.reload" ||
         method == "Page.navigateToHistoryEntry")) {
      Complete(id, false,
               "This raw command conflicts with native current-document virtual-time ownership");
      return;
    }
    if (message->HasKey("frame") &&
        (message->GetType("frame") != VTYPE_STRING || message->GetString("frame").empty() ||
         message->GetString("frame").length() > 256)) {
      Complete(id, false, "Invalid native Inspector frame identity");
      return;
    }
    if ((message->HasKey("frame") || method == "Page.getFrameTree") &&
        operations_.IsExpired(id, operation->generation, Now())) {
      Complete(id, false, "Native operation timed out");
      return;
    }
    const auto dispatch = [this, id, generation = operation->generation, message, method] {
      if (!operations_.IsCurrent(id, generation)) return;
      if ((message->HasKey("frame") || method == "Page.getFrameTree") &&
          operations_.IsExpired(id, generation, Now())) {
        Complete(id, false, "Native operation timed out");
        return;
      }
      const auto frame = frame_states_.find(FrameId(message));
      if (message->HasKey("frame") && frame == frame_states_.end()) {
        Complete(id, false, "Native frame detached or unavailable");
        return;
      }
      if (NativePristineRawLifecycleConflict(method, operation_sessions_[id])) {
        if (operations_.IsExpired(id, generation, Now())) {
          Complete(id, false, "Native operation timed out");
          return;
        }
        Complete(
            id, false,
            "Raw Runtime/Page disable conflicts with native pristine document lifecycle ownership");
        return;
      }
      if ((method == "DOM.enable" || method == "DOM.disable" || method.starts_with("Overlay.")) &&
          operations_.IsExpired(id, generation, Now())) {
        Complete(id, false, "Native operation timed out");
        return;
      }
      if (NativeRawOverlayConflict(operation_sessions_[id], method)) {
        Complete(id, false, "Raw Overlay mutation conflicts with native highlight ownership");
        return;
      }
      if (NativeRawDOM(id, generation, operation_sessions_[id], method,
                       operations_.Get(id)->deadline_ms))
        return;
      if (method.starts_with("Overlay.") && !method.starts_with("Overlay.get"))
        native_raw_overlay_sessions_.insert(operation_sessions_[id]);
      const auto loader = frame == frame_states_.end() ? std::string{} : frame->second.loader;
      const auto epoch =
          frame == frame_states_.end() ? uint64_t{} : frame->second.context_generation;
      const bool raw_debugger = method.starts_with("Debugger.");
      uint64_t raw_receipt = 0;
      if (raw_debugger) {
        if (operations_.IsExpired(id, generation, Now())) {
          Complete(id, false, "Native operation timed out");
          return;
        }
        const auto lease = operations_.Get(id);
        if (!lease) return;
        raw_receipt =
            NativeDebuggerDomains()->BeginRaw(operation_sessions_[id], method, lease->deadline_ms);
        if (!raw_receipt) {
          Complete(id, false,
                   "Raw Debugger command conflicts with native factory/coverage ownership, pending "
                   "physical work or bounded admission");
          return;
        }
      }
      if (operation_sessions_[id].empty() &&
          (method == "Network.disable" || method == "Network.enable"))
        NativeHARPrimaryReset();
      DevToolsCallback actual_reply = [this, id, generation, message, method, loader, epoch,
                                       raw_receipt](bool success, const std::string& json) {
        if (!operations_.IsCurrent(id, generation)) return;
        if (raw_receipt && operations_.IsExpired(id, generation, Now())) {
          Complete(id, false, "Native operation timed out");
          return;
        }
        if (method.starts_with("Overlay.") && operations_.IsExpired(id, generation, Now())) {
          Complete(id, false, "Native operation timed out");
          return;
        }
        if (success && method == "Page.getFrameTree") {
          if (json.size() > 1048576) {
            Complete(id, false, "Native Inspector frame tree exceeds 1 MiB");
            return;
          }
          const auto frame = frame_states_.find(FrameId(message));
          if (message->HasKey("frame") &&
              (frame == frame_states_.end() || frame->second.session != operation_sessions_[id] ||
               frame->second.loader != loader || frame->second.context_generation != epoch)) {
            Complete(id, false, "Native frame document changed during Inspector tree capture");
            return;
          }
          const auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
          std::string output;
          if (!suitecut::InspectorFrameTreeJSON(parsed ? parsed->GetDictionary() : nullptr,
                                                output)) {
            Complete(id, false, "Invalid or oversized native Inspector frame tree");
            return;
          }
          if (operations_.IsExpired(id, generation, Now())) {
            Complete(id, false, "Native operation timed out");
            return;
          }
          Complete(id, true, "", output);
          return;
        }
        if (success && method == "Overlay.disable")
          native_raw_overlay_sessions_.erase(operation_sessions_[id]);
        Complete(id, success, success ? "" : "DevTools command failed", json);
      };
      if (raw_debugger) {
        const auto operation = operations_.Get(id);
        if (!operation) {
          if (native_debugger_domains_) native_debugger_domains_->EndRaw(raw_receipt, false, false);
          return;
        }
        NativeDebuggerRawSend(id, raw_receipt, operation_sessions_[id], method,
                              message->GetDictionary("params"), operation->deadline_ms,
                              std::move(actual_reply));
      } else
        DevTools(id, method, message->GetDictionary("params"), std::move(actual_reply),
                 std::nullopt, false, method == "Page.getFrameTree" ? 1048576 : kMaxResponseBytes,
                 method == "Page.getFrameTree");
    };
    if (message->HasKey("frame"))
      WithFrame(id, FrameId(message), [dispatch](int) { dispatch(); });
    else
      dispatch();
  } else if (command == "evaluate") {
    if (message->GetType("function") == VTYPE_STRING) {
      auto params = CefDictionaryValue::Create();
      params->SetString("functionDeclaration", message->GetString("function"));
      operation_frames_[id] = FrameId(message);
      const auto frame = frame_states_.find(FrameId(message));
      if (frame == frame_states_.end()) {
        Complete(id, false, "Native frame detached");
        return;
      }
      operation_sessions_[id] = frame->second.session;
      params->SetBool("awaitPromise", true);
      params->SetBool("returnByValue", true);
      params->SetBool("userGesture", true);
      evaluation_groups_[id].insert(operation_sessions_[id]);
      params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
      HandleArguments(id, message, params, true, [this, id, params] {
        DevTools(id, "Runtime.callFunctionOn", params,
                 [this, id](bool ok, const std::string& json) {
                   Complete(id, ok, ok ? "" : "Native handle argument evaluation failed", json);
                 });
      });
    } else {
      if (message->GetType("expression") != VTYPE_STRING) {
        Complete(id, false, "Evaluation requires an expression");
        return;
      }
      Evaluate(
          id, message->GetString("expression"),
          [this, id](bool success, const std::string& json) {
            Complete(id, success, success ? "" : "Native evaluation failed", json);
          },
          FrameId(message));
    }
  } else if (command == "waitNavigation") {
    navigation_waiters_.insert(id);
  } else if (command == "waitEvent") {
    const auto event = message->GetString("event").ToString();
    if (event != "frameattached" && event != "framenavigated" && event != "framedetached" &&
        event != "console" && event != "pageerror" && event != "crash" && event != "dialog" &&
        event != "dialogclosed" && event != "filechooser" && event != "download" &&
        event != "worker" && event != "workerclosed") {
      Complete(id, false, "Unsupported native event wait");
      return;
    }
    if (message->HasKey("afterSequence")) {
      const double after = message->GetType("afterSequence") == VTYPE_INT
                               ? message->GetInt("afterSequence")
                               : message->GetDouble("afterSequence");
      if ((message->GetType("afterSequence") != VTYPE_INT &&
           message->GetType("afterSequence") != VTYPE_DOUBLE) ||
          !std::isfinite(after) || after < 0 || std::floor(after) != after ||
          after > static_cast<double>(event_sequence_)) {
        Complete(id, false, "Invalid native event sequence");
        return;
      }
      if (!event_history_.empty() &&
          after < static_cast<double>(event_history_.front().sequence - 1)) {
        Complete(id, false, "Native event journal capacity exceeded before wait registration");
        return;
      }
      for (const auto& record : event_history_)
        if (static_cast<double>(record.sequence) > after && record.event == event) {
          Complete(id, true, "", record.json);
          return;
        }
    }
    event_waiters_[id] = event;
    if (event == "crash") crash_wait_owners_.insert(id);
  } else if (command == "worker") {
    WorkerDispatch(id, message);
  } else if (command == "handle") {
    if (message->HasKey("nativeSelectorWaitState") || message->HasKey("nativeQueryNodes") ||
        message->HasKey("nativeFrameOwnerChild")) {
      Complete(id, false, "Invalid native selector-wait internal field");
      return;
    }
    if (message->HasKey("selectorState")) {
      const auto state = message->GetString("selectorState").ToString();
      if (message->GetType("selectorState") != VTYPE_STRING ||
          (state != "attached" && state != "visible") || message->GetString("action") != "create" ||
          message->GetType("query") != VTYPE_LIST || message->HasKey("expression") ||
          message->HasKey("function") || message->HasKey("queryFunction") ||
          message->GetBool("all") ||
          (message->HasKey("strict") && message->GetType("strict") != VTYPE_BOOL)) {
        Complete(id, false, "Invalid native selector-wait request");
        return;
      }
      if (!SelectorWaits().Admit(id, operation->generation)) {
        Complete(id, false, "Native selector-wait capacity exceeded");
        return;
      }
      auto config = message->Copy(false);
      config->Remove("selectorState");
      config->SetString("nativeSelectorWaitState", state);
      config->SetBool("predicate", true);
      config->SetInt("pollInterval", 20);
      context_retries_[id] = [this, id, config] { Handle(id, config); };
      Handle(id, config);
    } else if (message->GetBool("predicate")) {
      auto config = message->Copy(false);
      context_retries_[id] = [this, id, config] { Handle(id, config); };
      Handle(id, config);
    } else
      Handle(id, message);
  } else if (command == "diagnostics") {
    auto response = CefDictionaryValue::Create();
    WorkerDiagnostics(response);
    if (network_) network_->Diagnostics(response);
    engine_.HTTPDiagnostics(response);
    response->SetInt("contextAuthenticationUpdates",
                     static_cast<int>(engine_.authentication().pending()));
    response->SetInt("contextAuthenticationPhysicalFences",
                     static_cast<int>(engine_.authentication().physical()));
    response->SetInt("contextAuthenticationTargets",
                     static_cast<int>(engine_.authentication().targets()));
    const auto& auth_receipt = engine_.authentication().LastReceipt();
    response->SetInt("contextAuthenticationReceiptOwner", auth_receipt.owner);
    response->SetDouble("contextAuthenticationReceiptDeadline", auth_receipt.deadline);
    response->SetDouble("contextAuthenticationReceiptAt", auth_receipt.at);
    if (credentials_) credentials_->Diagnostics(response);
    response->SetInt("pendingNetworkPublicationResults",
                     static_cast<int>(network_publication_results_.size()));
    response->SetDouble("pendingNetworkPublicationResultBytes",
                        static_cast<double>(network_publication_results_.bytes()));
    response->SetInt("pendingPrivateBodyQueuedReads",
                     private_response_ ? static_cast<int>(private_response_->waiting()) : 0);
    response->SetInt("pendingPrivateBodyInnerReceipts",
                     private_response_ ? static_cast<int>(private_response_->physical()) : 0);
    response->SetInt("privateBodyRecords",
                     private_response_ ? static_cast<int>(private_response_->records()) : 0);
    response->SetBool("privateBodySessionReady", private_response_ && private_response_->ready());
    if (private_response_) {
      response->SetString("privateBodyTargetId", private_response_->target());
      response->SetString("privateBodyInspectorSessionId", private_response_->session());
    }
    if (!private_response_error_.empty())
      response->SetString("privateBodyUnavailable", private_response_error_);
    response->SetInt("pendingImageStreamCommands",
                     image_streams_ ? static_cast<int>(image_streams_->physical()) : 0);
    response->SetInt("imageStreamEntries",
                     image_streams_ ? static_cast<int>(image_streams_->entries()) : 0);
    response->SetDouble("imageStreamBytes",
                        image_streams_ ? static_cast<double>(image_streams_->bytes()) : 0);
    response->SetInt("pendingRequestBodyReads",
                     request_reads_ ? static_cast<int>(request_reads_->physical()) : 0);
    response->SetInt("pendingNetworkCommands", static_cast<int>(network_calls_.size()));
    response->SetBool("networkFatalPending", network_fatal_ && network_fatal_->pending());
    response->SetBool("networkFatalCloseRequested", network_fatal_ && network_fatal_->requested());
    size_t expired_network = 0;
    for (const auto& [request, call] : network_calls_)
      if (call.expired) ++expired_network;
    response->SetInt("expiredNetworkCommands", static_cast<int>(expired_network));
    response->SetInt("pendingAXCaptures", static_cast<int>(ax_captures_.size()));
    response->SetInt("pendingAXPhysicalCommands", static_cast<int>(ax_physical_));
    response->SetInt("pendingMediaPhysicalCommands", static_cast<int>(native_media_physical_));
    response->SetInt("queuedMediaCommands", static_cast<int>(native_media_queue_.size()));
    response->SetDouble("pendingAXCaptureBytes", static_cast<double>(ax_capture_bytes_));
    response->SetInt("pendingBindingCommands", static_cast<int>(binding_calls_.size()));
    response->SetInt("pendingPageErrorCaptures",
                     page_errors_ ? static_cast<int>(page_errors_->Captures()) : 0);
    response->SetInt("pendingPageErrorPhysical",
                     page_errors_ ? static_cast<int>(page_errors_->Physical()) : 0);
    response->SetInt("nativeHandleDisposals",
                     handle_disposals_ ? static_cast<int>(handle_disposals_->Size()) : 0);
    response->SetInt("nativeHandleDisposalPhysical",
                     handle_disposals_ ? static_cast<int>(handle_disposals_->Physical()) : 0);
    response->SetInt("nativeHandleDisposalHandles",
                     handle_disposals_ ? static_cast<int>(handle_disposals_->Handles()) : 0);
    response->SetInt("nativeHandleDisposalFailures",
                     handle_disposals_ ? static_cast<int>(handle_disposals_->Failures()) : 0);
    response->SetInt("pageErrorRetainedBytes",
                     page_errors_ ? static_cast<int>(page_errors_->Bytes()) : 0);
    response->SetInt("pageErrorGroups",
                     page_errors_ ? static_cast<int>(page_errors_->Groups()) : 0);
    response->SetInt("nativeElementStates",
                     element_states_ ? static_cast<int>(element_states_->Size()) : 0);
    response->SetInt("nativeSelectorWaits",
                     selector_waits_ ? static_cast<int>(selector_waits_->Size()) : 0);
    response->SetInt("nativeSelectorWaitPhysical",
                     selector_waits_ ? static_cast<int>(selector_waits_->PhysicalSize()) : 0);
    response->SetInt("nativeOwnerFrameDocuments", static_cast<int>(owner_frame_documents_.size()));
    response->SetInt("nativeOwnerFrameGroups", static_cast<int>(OwnerFrameDocumentGroups()));
    response->SetInt(
        "nativeOwnerFramePhysical",
        owner_frame_controls_ ? static_cast<int>(owner_frame_controls_->PhysicalSize()) : 0);
    response->SetInt("nativeOwnerFrameProcessPhysical",
                     static_cast<int>(suitecut::OwnerFrameControls::ProcessPhysicalSize()));
    response->SetInt("nativeElementStatePhysical",
                     element_states_ ? static_cast<int>(element_states_->PhysicalSize()) : 0);
    response->SetInt("nativeElementStateReleases",
                     element_states_ ? static_cast<int>(element_states_->Pending()) : 0);
    response->SetInt("bindingControlOperations", static_cast<int>(operations_.Controls()));
    if (bindings_) bindings_->Diagnostics(response);
    response->SetInt("pendingUploadGroups", static_cast<int>(pending_uploads_.size()));
    response->SetInt("pendingUploadBytes", static_cast<int>(pending_upload_bytes_));
    response->SetInt("activeHandles", static_cast<int>(handles_.size()));
    response->SetInt("nativeDOMGetters", static_cast<int>(native_dom_getters_.size()));
    response->SetInt("nativeElementBounds", static_cast<int>(element_bounds_operations_.size()));
    response->SetInt("nativeLocatorPredicates",
                     static_cast<int>(locator_predicate_operations_.size()));
    response->SetInt("nativeAriaJSON", static_cast<int>(aria_json_operations_.size()));
    response->SetInt("nativeVisibilitySnapshots", static_cast<int>(visibility_snapshots_.Size()));
    RendererRecoveryDiagnostics(response);
    response->SetInt("pausedActions", static_cast<int>(action_pauses_.Size()));
    response->SetInt("pendingURLWaits", static_cast<int>(url_waits_.size()));
    response->SetInt("pendingURLPredicates", static_cast<int>(url_predicates_.Size()));
    response->SetInt("pendingConsoleArguments", static_cast<int>(console_arguments_.size()));
    response->SetInt("consoleArgumentBytes", static_cast<int>(console_argument_bytes_));
    response->SetInt("consoleMessages", static_cast<int>(console_journal_.Size()));
    response->SetInt("consoleJournalBytes", static_cast<int>(console_journal_.Bytes()));
    response->SetInt("pageErrors", static_cast<int>(page_error_journal_.Size()));
    response->SetInt("pageErrorJournalBytes", static_cast<int>(page_error_journal_.Bytes()));
    response->SetInt("pendingConsoleMaterializations",
                     static_cast<int>(console_materializations_.size()));
    response->SetInt("pendingEventPredicates", static_cast<int>(event_predicates_.Size()));
    response->SetInt("eventPredicateBytes", static_cast<int>(event_predicates_.PendingBytes()));
    response->SetInt("dialogs", dialogs_ ? static_cast<int>(dialogs_->Size()) : 0);
    response->SetInt("pendingFirstObservations", static_cast<int>(first_observations_.size()));
    response->SetInt("lazyQueries", static_cast<int>(lazy_queries_.size()));
    response->SetInt("countQueries", static_cast<int>(count_queries_.size()));
    response->SetInt("countContexts", static_cast<int>(count_contexts_.size()));
    response->SetInt("pendingChildBootstraps", static_cast<int>(pending_child_bootstraps_.size()));
    response->SetInt("contextRetryQueued", static_cast<int>(context_retry_queued_.size()));
    response->SetInt("nativeScreenshotAssertionScopes",
                     int(screenshot_assertion_retirement_.Size()));
    response->SetInt("nativeScreenshotAssertionPhysical",
                     int(screenshot_assertion_retirement_.Physical()));
    response->SetInt("artifacts", artifacts_ ? static_cast<int>(artifacts_->Size()) : 0);
    NativePristineFactories().Diagnostics(response);
    response->SetInt("pristineDispatchSourceContexts", native_pristine_contexts_.size());
    response->SetInt("pristineDispatchKnownContexts", native_pristine_seen_.Count());
    response->SetInt("pristineDispatchSourceSessions", native_pristine_sessions_.size());
    if (native_debugger_domains_) native_debugger_domains_->Diagnostics(response);
    NativeDeveloperDiagnostics(response);
    if (native_coverage_) native_coverage_->Diagnostics(response);
    response->SetInt("artifactPhysicalCapture",
                     artifacts_ ? static_cast<int>(artifacts_->PhysicalCount()) : 0);
    response->SetInt("nativeTracePhaseJobs",
                     native_trace_phases_ ? int(native_trace_phases_->Size()) : 0);
    response->SetDouble("nativeTracePNGReadbacks", double(native_trace_png_readbacks_));
    response->SetDouble("nativeTraceARIAReadbacks", double(native_trace_aria_readbacks_));
    response->SetInt("artifactDocumentJobs", static_cast<int>(artifact_document_work_.size()));
    response->SetInt("artifactDocumentContexts", static_cast<int>(ArtifactDocumentContextCount()));
    response->SetInt("formClicks", static_cast<int>(form_clicks_.size()));
    response->SetInt("temporaryUploadBytes", static_cast<int>(upload_store_.Bytes()));
    response->SetInt("temporaryUploadInputs", static_cast<int>(upload_store_.Inputs()));
    response->SetInt("pendingHandles", static_cast<int>(pending_handle_groups_.size()));
    response->SetInt("evaluationGroups", static_cast<int>(evaluation_groups_.size()));
    response->SetInt("nativeHighlights",
                     native_highlights_ ? static_cast<int>(native_highlights_->Active()) : 0);
    response->SetInt("nativeHighlightResidents",
                     native_highlights_ ? static_cast<int>(native_highlights_->Resident()) : 0);
    response->SetInt("nativeHighlightPhysical", static_cast<int>(native_highlight_calls_.size()));
    response->SetInt("nativeHighlightProcessPhysical",
                     static_cast<int>(NativeHighlightProcessPhysical()));
    response->SetInt("nativeDOMDomainOwners",
                     native_dom_domains_ ? static_cast<int>(native_dom_domains_->Owners()) : 0);
    response->SetInt("nativeDOMDomainPhysical",
                     native_dom_domains_ ? static_cast<int>(native_dom_domains_->Physical()) : 0);
    response->SetInt(
        "nativeDOMDomainReleases",
        native_dom_domains_ ? static_cast<int>(native_dom_domains_->PendingReleases()) : 0);
    response->SetInt("nativeSnapshotReferenceDispatches",
                     static_cast<int>(reference_dispatch_.Pending()));
    response->SetInt("nativeSnapshotReferenceProcessDispatches",
                     static_cast<int>(suitecut::SnapshotReferenceDispatch::ProcessPending()));
    response->SetInt("nativeSnapshotFrameBatches",
                     static_cast<int>(reference_frame_batches_.size()));
    response->SetInt("nativeSnapshotReferences",
                     snapshot_references_ ? static_cast<int>(snapshot_references_->Active()) : 0);
    response->SetInt("nativeSnapshotReferenceResidents",
                     snapshot_references_ ? static_cast<int>(snapshot_references_->Resident()) : 0);
    response->SetInt("nativeSnapshotReferenceGroups",
                     snapshot_references_ ? static_cast<int>(snapshot_references_->Groups()) : 0);
    response->SetInt("nativeSnapshotReferenceBytes",
                     snapshot_references_ ? static_cast<int>(snapshot_references_->Bytes()) : 0);
    response->SetInt(
        "nativeSnapshotReferenceInstalls",
        snapshot_references_ ? static_cast<int>(snapshot_references_->InstallPhysical()) : 0);
    response->SetInt("nativeSnapshotReferenceReleases",
                     snapshot_references_ ? static_cast<int>(snapshot_references_->Physical()) : 0);
    response->SetInt("nativeSnapshotReferenceBorrows",
                     snapshot_references_ ? static_cast<int>(snapshot_references_->Borrows()) : 0);
    response->SetInt("nativeSnapshotReferenceProcessInstalls",
                     static_cast<int>(suitecut::SnapshotReferences::GlobalInstallPhysical()));
    response->SetInt("nativeSnapshotReferenceProcessReleases",
                     static_cast<int>(suitecut::SnapshotReferences::GlobalReleasePhysical()));
    response->SetInt("nativeSnapshotReferenceProcessResidents",
                     static_cast<int>(suitecut::SnapshotReferences::GlobalResident()));
    response->SetDictionary("nativeVirtualTime", NativeVirtualTimeInfo());
    response->SetInt("pendingDevTools", static_cast<int>(devtools_.size()));
    response->SetInt("queuedDevTools", static_cast<int>(devtools_dispatch_.size()));
    response->SetInt("queuedDevToolsBytes", static_cast<int>(devtools_dispatch_bytes_));
    response->SetInt("otherOperations", static_cast<int>(operations_.Size()) - 1);
    response->SetInt("frames", static_cast<int>(frame_states_.size()));
    response->SetInt("sessions", static_cast<int>(sessions_.size()));
    response->SetInt("pendingSessionTrees", static_cast<int>(session_trees_.size()));
    response->SetInt("navigations", static_cast<int>(navigations_.size()));
    response->SetInt("actionNavigations", static_cast<int>(action_navigations_.size()));
    int action_navigation_intents = 0;
    for (const auto& [owner, wait] : action_navigations_)
      action_navigation_intents += static_cast<int>(wait.intents.size());
    response->SetInt("actionNavigationIntents", action_navigation_intents);
    response->SetInt("bodyWaits", static_cast<int>(body_waiters_.size()));
    response->SetInt("activeRequests", static_cast<int>(active_requests_.size()));
    response->SetInt("documentResponses", static_cast<int>(documents_.size()));
    response->SetInt("eventHistoryBytes", static_cast<int>(event_history_bytes_));
    int default_contexts = 0, dom_contexts = 0;
    for (const auto& [frame, state] : frame_states_) {
      if (state.context) ++default_contexts;
      if (state.utility_context) ++dom_contexts;
    }
    response->SetInt("defaultContexts", default_contexts);
    response->SetInt("domContexts", dom_contexts);
    response->SetInt("pendingDomWorlds", static_cast<int>(utility_worlds_.size()));
    response->SetInt("inputWork", static_cast<int>(input_work_.size()));
    response->SetInt("pendingSendKeysPhysicalCommands",
                     static_cast<int>(send_keys_physical_count_));
    response->SetInt("retainedSendKeysOwners", static_cast<int>(send_keys_physical_.size()));
    response->SetInt("queuedInput", static_cast<int>(mouse_queue_.size() + keyboard_queue_.size()));
    response->SetInt("pressedKeys", static_cast<int>(pressed_keys_.size()));
    response->SetInt("keyModifiers", key_modifiers_);
    response->SetInt("mouseButtons", mouse_buttons_);
    response->SetInt("nativeDragOperations", static_cast<int>(drag_work_.size()));
    response->SetInt("nativeDragCompletionRecords",
                     native_drop_ ? static_cast<int>(native_drop_->Retained()) : 0);
    response->SetInt("nativeDragCompletionCallbacks",
                     static_cast<int>(native_drop_transport_.size()));
    response->SetInt("nativeDragCompletionClosing",
                     native_drop_ ? static_cast<int>(native_drop_->Closing()) : 0);
    response->SetBool("nativeDragCompletionAdmissionFailed", native_drop_failed_owner_ != 0);
    response->SetInt("nativeDragGlobalRetainedRecords",
                     static_cast<int>(NativeDropBudget()->records));
    response->SetDouble("nativeDragGlobalRetainedBytes",
                        static_cast<double>(NativeDropBudget()->bytes));
    response->SetBool("nativeManualDragActive", manual_drag_ != nullptr);
    response->SetInt("pendingTouchPhysicalCommands",
                     native_touch_ ? static_cast<int>(native_touch_->physical()) : 0);
    response->SetInt("queuedTouchCommands",
                     native_touch_ ? static_cast<int>(native_touch_->queued()) : 0);
    response->SetInt("contextSettingsTargets",
                     settings_page_ ? static_cast<int>(settings_page_->targets()) : 0);
    response->SetInt("contextSettingsPending",
                     settings_page_ ? static_cast<int>(settings_page_->pending()) : 0);
    response->SetBool("contextSettingsPhysical", settings_page_ && settings_page_->physical());
    response->SetInt("geolocationTargets", static_cast<int>(engine_.geolocation().targets()));
    response->SetInt("geolocationUpdates", static_cast<int>(engine_.geolocation().pending()));
    response->SetInt("geolocationLogicalUpdates",
                     static_cast<int>(engine_.geolocation().logical()));
    response->SetInt("geolocationInFlight", static_cast<int>(engine_.geolocation().inFlight()));
    response->SetString("initialDocumentURL", initial_document_url_);
    response->SetString("initialDocumentState", initial_document_state_);
    response->SetString("initialCEFFrame", initial_cef_frame_);
    response->SetInt("initialDocumentHistory", initial_document_history_);
    response->SetInt("initialExecutionContext", initial_execution_context_);
    response->SetBool("initialHistoryCandidate", initial_history_candidate_);
    response->SetInt("initialHistoryEntry", initial_history_entry_);
    response->SetBool("touchEnabled", native_touch_ && native_touch_->enabled());
    response->SetBool("touchContactHeld", native_touch_ && native_touch_->held());
    Complete(id, true, "", Json(response));
  } else if (command == "locator") {
    const auto selector = suitecut::InboundDisplay(suitecut::InboundString(message, "selector"));
    const auto action = message->GetString("action").ToString();
    if (selector.empty() ||
        (action != "click" && action != "tap" && action != "hover" &&
         action != "scrollIntoViewIfNeeded" && action != "selectText" && action != "fill" &&
         action != "clear" && action != "focus" && action != "blur" && action != "dispatchEvent" &&
         action != "inputValue" && action != "boundingBox" && action != "waitForFunction" &&
         action != "ariaSnapshotJSON" && action != "press" && action != "type" &&
         action != "sendKeys") ||
        ((action == "fill" || action == "type" || action == "sendKeys") &&
         message->GetType("value") != VTYPE_STRING)) {
      Complete(id, false, "Invalid native locator action");
      return;
    }
    if (action == "tap" && (!native_touch_ || !native_touch_->enabled())) {
      Complete(id, false, "Touchscreen requires enabled native touch emulation (hasTouch)");
      return;
    }
    if ((action == "waitForFunction" || action == "ariaSnapshotJSON") &&
        (message->HasKey("nativeQueryNodes") || message->HasKey("formNode") ||
         message->HasKey("nativeAccessibilitySnapshot") ||
         message->HasKey("nativeSemanticSnapshotJson"))) {
      Complete(id, false, "Invalid native locator predicate internal field");
      return;
    }
    auto config = message->Copy(false);
    const auto text = action == "clear" ? std::string() : config->GetString("value").ToString();
    if (config->HasKey("diagnosticExpressionJSON") &&
        (config->GetType("diagnosticExpressionJSON") != VTYPE_STRING ||
         config->GetString("diagnosticExpressionJSON").length() > 16384)) {
      Complete(id, false, "Invalid native action diagnostic expression");
      return;
    }
    if ((config->HasKey("pauseBeforeStable") &&
         config->GetType("pauseBeforeStable") != VTYPE_BOOL) ||
        (config->GetBool("pauseBeforeStable") &&
         (config->GetType("clientToken") != VTYPE_STRING ||
          config->GetString("clientToken").ToString().empty() ||
          config->GetString("clientToken").ToString().size() > 128))) {
      Complete(id, false, "Invalid native action hook identity");
      return;
    }
    locator_logs_[id] = action + " action" + (config->GetBool("trial") ? " (trial run)" : "") +
                        "\n - resolving " + selector.substr(0, 1024);
    if (config->HasKey("nativeHandle")) {
      if (config->GetType("nativeHandle") != VTYPE_INT) {
        Complete(id, false, "Invalid native element handle");
        return;
      }
      const auto handle = handles_.find(config->GetInt("nativeHandle"));
      if (handle == handles_.end()) {
        Complete(id, false, MissingHandle(config->GetInt("nativeHandle")));
        return;
      }
      config->SetString("frame", handle->second.frame_id);
    }
    if (config->HasKey("modifiers")) {
      auto modifiers = config->GetList("modifiers");
      if (!modifiers || modifiers->GetSize() > 5) {
        Complete(id, false, "Invalid native input modifiers");
        return;
      }
      for (size_t index = 0; index < modifiers->GetSize(); ++index) {
        auto key = modifiers->GetType(index) == VTYPE_STRING
                       ? SuiteCutDescribeKey(modifiers->GetString(index), 0)
                       : std::nullopt;
        if (!key || !key->modifier) {
          Complete(id, false, "Invalid native input modifier");
          return;
        }
      }
      SetupLocatorModifiers(id, config);
    }
    context_retries_[id] = [this, id, generation = operation->generation, selector, action, text,
                            config] {
      LocatorAction(id, generation, selector, action, text, config);
    };
    LocatorAction(id, operation->generation, selector, action, text, config);
  } else
    Complete(id, false, "Unknown native command");
}

auto Source::ReadCommands() -> void {
  SuiteCutCommandFramer framer;
  char buffer[4096];
  while (!stopped_) {
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    if (poll(&fd, 1, 100) <= 0) continue;
    const auto size = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (size < 0 && errno == EINTR) continue;
    if (size <= 0) break;
    if (!framer.Push(std::string_view(buffer, static_cast<size_t>(size)), [this](std::string line) {
          // Pause the pipe reader at the UI-work bound. Load is backpressure,
          // not malformed input; the OS pipe then bounds transport buffering.
          {
            std::unique_lock<std::mutex> lock(command_mutex_);
            command_capacity_.wait(lock, [this] { return stopped_ || queued_commands_ < 64; });
            if (stopped_) return false;
            ++queued_commands_;
          }
          suitecut::AcceptedPostReceipt native_post(engine_.AcceptedReader());
          if (!writer_.ReserveReply(stopped_)) {
            {
              std::lock_guard<std::mutex> lock(command_mutex_);
              --queued_commands_;
            }
            command_capacity_.notify_one();
            return false;
          }
          const auto native_reader = engine_.AcceptedReader();
          const auto native_ticket = native_post.Ticket();
          auto task = new Task([self = CefRefPtr<Source>(this), line = std::move(line),
                                native_reader, native_ticket] {
            {
              std::lock_guard<std::mutex> lock(self->command_mutex_);
              --self->queued_commands_;
            }
            self->command_capacity_.notify_one();
            {
              const suitecut::AcceptedRouteFrame native_route(native_reader, native_ticket,
                                                              self->engine_.AcceptedSourceTicket());
              self->engine_.Route(line);
            }
            self->writer_.WorkChanged();
          });
          native_post.AboutToPost();
          const bool native_posted = CefPostTask(TID_UI, task);
          native_post.Returned(native_posted);
          writer_.WorkChanged();
          if (!native_posted) {
            writer_.ReleaseReply();
            {
              std::lock_guard<std::mutex> lock(command_mutex_);
              --queued_commands_;
            }
            command_capacity_.notify_one();
            return false;
          }
          return true;
        })) {
      if (!stopped_) Error("Invalid or rejected native command stream");
      stopped_ = true;
    }
  }
  if (!stopped_ && !framer.End()) Error("Truncated native command stream");
  CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
                self->engine_.LocatorHandlerChannelClosed();
                self->engine_.CloseAll();
              }));
}

}  // namespace suitecut::browser
