// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
#include "controllers/artifacts/screenshot_input_engine.inc"
#include "controllers/dom/locator_handler_engine.inc"
void Engine::Route(const std::string& line) {
  auto parsed = CefParseJSON(line, JSON_PARSER_RFC);
  auto message = parsed ? parsed->GetDictionary() : nullptr;
  if (!message || message->GetInt("version") != 1 || message->GetInt("id") < 1) {
    writer_.ReleaseReply();
    writer_.Error("Invalid native command");
    return;
  }
  const int id = message->GetInt("id");
  const auto command = message->GetString("command").ToString();
  if (LocatorHandlerControl(id, command, message)) return;
  double locator_handler_deadline = 0;
  if (message->HasKey("locatorHandlerOrigin") &&
      !LocatorHandlerAdmission(message, locator_handler_deadline)) {
    writer_.Reply(id, false, "Native locator handler child admission is stale or expired");
    return;
  }
  if (message->HasKey("locatorHandlerOrigin") &&
      !suitecut::NativeLocatorHandlerCallbackCommandAllowed(
          command, message->GetString("action").ToString())) {
    writer_.Reply(id, false,
                  "This native browser command is not supported inside a locator handler callback");
    return;
  }
  if (message->HasKey("locatorHandlerOrigin") && command == "input" &&
      message->GetString("backend") == "cef") {
    writer_.Reply(id, false, "Direct CEF input is not supported inside a locator handler callback");
    return;
  }
  auto handler_native_origin = suitecut::CurrentNativeWorkOrigin();
  if (message->HasKey("locatorHandlerOrigin")) {
    uint64_t token = 0;
    Source::ActionHookInteger(message->GetDictionary("locatorHandlerOrigin"), "token", token);
    handler_native_origin.locator_handler_invocation = token;
  }
  if (handler_native_origin.locator_handler_invocation &&
      !suitecut::BindCurrentNativeLocatorHandlerTask(
          handler_native_origin.locator_handler_invocation)) {
    writer_.Reply(id, false, "Native locator handler task ownership is unavailable");
    return;
  }
  const suitecut::NativeWorkOriginFrame handler_origin(handler_native_origin);
  if (closing_) {
    writer_.Reply(id, false, "Native browser is closing");
    return;
  }
  bool duplicate = operations_.Contains(id);
#if defined(__linux__)
  duplicate = duplicate || (native_work_ && native_work_->command == id && !native_work_->granted &&
                            !native_work_->refused);
#endif
  for (const auto& [page, source] : sources_)
    duplicate = duplicate || source->operations_.Contains(id);
  if (duplicate) {
    writer_.ReleaseReply();
    writer_.Error("Duplicate active native command ID");
    return;
  }
  if (command == "viewerIdle") {
    // A viewer handover observes the existing physical-work predicate without
    // adding an operation owner of its own. The original reader Work task must
    // return before this coordinator can truthfully inspect task retirement.
    if (!CefPostTask(TID_UI, new Task(
                                 [this, alive = alive_, id] {
                                   if (!*alive) return;
                                   auto result = CefDictionaryValue::Create();
                                   result->SetInt("viewerIdleVersion", 1);
                                   result->SetBool("idle", AcceptedWorkReady(true));
                                   const auto json = Json(result);
                                   writer_.Reply(id, true, "",
                                                 std::vector<uint8_t>(json.begin(), json.end()));
                                 },
                                 suitecut::AcceptedTaskWork::Kind::BarrierCoordinator)))
      writer_.Reply(id, false, "Could not schedule native viewer idle probe");
    return;
  }
#if defined(__linux__)
  if (command == "nativeWorkBarrier") {
    OriginalAcceptedWorkBarrier(id, message);
    return;
  }
#endif
  if (command == "cancel" && operations_.Contains(message->GetInt("target"))) {
    CompleteContext(message->GetInt("target"), false, nullptr, "Native context operation canceled");
    writer_.Reply(id, true);
    return;
  }
  if (command == "selectors") {
    const auto action = message->GetString("action").ToString();
    if (action == "diagnostics") {
      auto result = CefDictionaryValue::Create();
      result->SetInt("engines", static_cast<int>(selectors_.entries().size()));
      result->SetInt("sourceBytes", static_cast<int>(selectors_.bytes()));
      const auto json = Json(result);
      writer_.Reply(id, true, "", std::vector<uint8_t>(json.begin(), json.end()));
      return;
    }
    if (action != "register" || message->GetType("name") != VTYPE_STRING ||
        message->GetType("source") != VTYPE_STRING ||
        (message->HasKey("contentScript") && message->GetType("contentScript") != VTYPE_BOOL)) {
      writer_.Reply(id, false, "Invalid native selector registration");
      return;
    }
    std::string error;
    const bool ok = selectors_.Register(message->GetString("name"), message->GetString("source"),
                                        message->GetBool("contentScript"), error);
    writer_.Reply(id, ok, error);
    return;
  }
  if (command == "context" || command == "http") {
    double timeout = 30000;
    if (message->HasKey("timeoutMs")) {
      if (message->GetType("timeoutMs") != VTYPE_INT || message->GetInt("timeoutMs") < 0 ||
          message->GetInt("timeoutMs") > 120000) {
        writer_.Reply(id, false, "Invalid native timeout");
        return;
      }
      timeout = message->GetInt("timeoutMs");
    }
    const auto context_action = message->GetString("action").ToString();
    const bool cleanup =
        (command == "http" && (context_action == "dispose" || context_action == "resolveLookup")) ||
        (command == "context" &&
         (context_action == "close" || context_action == "closePage" ||
          context_action == "workerNetworkBody" || context_action == "screenshotInput"));
    auto operation = cleanup ? operations_.BeginControl(id, Now(), timeout)
                             : operations_.Begin(id, Now(), timeout);
    if (!operation) {
      writer_.Reply(id, false, "Too many pending native context operations or duplicate ID");
      return;
    }
    if (locator_handler_deadline &&
        (!operation->deadline_ms || operation->deadline_ms > locator_handler_deadline) &&
        !operations_.BindOriginalDeadline(id, operation->generation, locator_handler_deadline)) {
      CompleteContext(id, false, nullptr,
                      "Native locator handler child original deadline is invalid");
      return;
    }
    if (!TrackLocatorHandlerCommand(id, 0, operation->generation)) {
      CompleteContext(id, false, nullptr, "Native locator handler command capacity exceeded");
      return;
    }
    operation = operations_.Get(id);
    if (!deadline_timer_active_) {
      deadline_timer_active_ = true;
      CefPostDelayedTask(TID_UI,
                         new Task(
                             [this, alive = alive_] {
                               if (*alive) Deadlines();
                             },
                             suitecut::AcceptedTaskWork::Kind::Work, LocatorHandlerMonitorOrigin()),
                         10);
    }
    if (command == "http") {
      http_->Handle(
          id, message,
          [this, alive = alive_, id, generation = operation->generation](bool ok, Dict result,
                                                                         const std::string& error) {
            if (*alive && operations_.IsCurrent(id, generation))
              CompleteContext(id, ok, result, error);
          },
          operations_.Get(id)->deadline_ms);
      return;
    }
    const auto action = message->GetString("action").ToString();
    auto params = message->GetDictionary("params");
    params = params ? params->Copy(false) : CefDictionaryValue::Create();
    params->SetInt("internalOwner", id);
    if (action == "websocket") {
      const int context = params->GetInt("contextId");
      if (!contexts_->RequestContextForContext(context)) {
        CompleteContext(id, false, nullptr, "Actual native WebSocket context is closed");
        return;
      }
      const auto original_deadline = operation->deadline_ms;
      auto handle = [this, alive = alive_, id, generation = operation->generation, context, params,
                     original_deadline](bool ok, const std::string& error) {
        if (!*alive || !operations_.IsCurrent(id, generation)) return;
        if (!ok) {
          CompleteContext(id, false, nullptr, error);
          return;
        }
        websocket_routes_->Command(
            id, context, 0, params, original_deadline,
            [this, alive, id, generation](bool accepted, Dict result, const std::string& reason) {
              if (*alive && operations_.IsCurrent(id, generation)) {
                if (accepted && operations_.IsExpired(id, generation, Now()))
                  CompleteContext(id, false, nullptr,
                                  "Native WebSocket policy original command deadline exceeded");
                else
                  CompleteContext(id, accepted, result, reason);
              }
            });
      };
      workers_->WebSocketPolicyReady(context, std::move(handle));
      return;
    }
    if (action == "video") {
      context_video_->Handle(id, operation->generation, params->GetInt("contextId"), params,
                             operation->deadline_ms,
                             [this, alive = alive_, id, generation = operation->generation](
                                 bool ok, const std::string& path, const std::string& error) {
                               if (!*alive || !operations_.IsCurrent(id, generation)) return;
                               auto result = CefDictionaryValue::Create();
                               if (!path.empty()) result->SetString("path", path);
                               CompleteContext(id, ok, result, error);
                             });
      return;
    }
    if (action == "screenshotInput") {
      ScreenshotInputControl(id, params, operation->generation);
      return;
    }
    if (action == "download") {
      downloads_->Handle(id, params, operation->deadline_ms,
                         [this, alive = alive_, id, generation = operation->generation](
                             bool ok, Dict result, const std::string& error) {
                           if (*alive && operations_.IsCurrent(id, generation))
                             CompleteContext(id, ok, result, error);
                         });
      return;
    }
    if (action == "har") {
      const int context = params->GetInt("contextId");
      if (!contexts_->RequestContextForContext(context)) {
        CompleteContext(id, false, nullptr, "Native HAR replay context is closed");
        return;
      }
      const auto method = params->GetString("action").ToString();
      if (method == "record") {
        std::string error;
        const bool ok = har_recording_->Configure(context, params->GetDictionary("options"), error);
        CompleteContext(id, ok, nullptr, error);
        return;
      }
      if (method != "replay") {
        CompleteContext(id, false, nullptr, "Invalid native context HAR action");
        return;
      }
      const auto registration = params->GetString("registrationId").ToString(),
                 pattern = params->HasKey("url") ? params->GetString("url").ToString() : "**",
                 not_found = params->HasKey("notFound") ? params->GetString("notFound").ToString()
                                                        : "abort";
      if (registration.empty() || registration.size() > 128 || pattern.empty() ||
          pattern.size() > 8192 || (not_found != "abort" && not_found != "fallback")) {
        CompleteContext(id, false, nullptr, "Invalid native context HAR route");
        return;
      }
      const auto admission_order = suitecut::NativeRouteRegistrationOrder::Reserve();
      if (!admission_order) {
        CompleteContext(id, false, nullptr, "Native route registration order exhausted");
        return;
      }
      har_files_->Read(
          id, params->GetString("path"), operation->deadline_ms,
          [this, alive = alive_, id, context, generation = operation->generation, registration,
           pattern, not_found, admission_order = *admission_order](
              suitecut::NativeHARReplayFiles::Replay replay, const std::string& error) {
            if (!*alive || !operations_.IsCurrent(id, generation)) return;
            const auto operation = operations_.Get(id);
            if (!operation || operations_.IsExpired(id, generation, Now())) {
              CompleteContext(id, false, nullptr,
                              "Native HAR original context command deadline exceeded");
              return;
            }
            if (!contexts_->RequestContextForContext(context)) {
              CompleteContext(id, false, nullptr,
                              "Native HAR replay context closed during file read");
              return;
            }
            if (!replay) {
              CompleteContext(id, false, nullptr, error);
              return;
            }
            if (!context_har_->Install(
                    id, context,
                    {registration, pattern, std::move(replay), not_found == "fallback",
                     admission_order},
                    operation->deadline_ms,
                    [this, alive, id, generation](bool ok, const std::string& reason) {
                      if (*alive && operations_.IsCurrent(id, generation))
                        CompleteContext(id, ok, nullptr, reason);
                    }))
              CompleteContext(id, false, nullptr, "Invalid or excessive native context HAR policy");
          },
          not_found == "fallback");
      return;
    }
    if (action == "setExtraHTTPHeaders") {
      const auto context = params->GetInt("contextId");
      if (!contexts_->RequestContextForContext(context) ||
          !network_headers_.Set(id, context, params->GetDictionary("headers"),
                                operation->deadline_ms,
                                [this, alive = alive_, id, generation = operation->generation](
                                    bool ok, const std::string& error) {
                                  if (*alive && operations_.IsCurrent(id, generation))
                                    CompleteContext(id, ok, nullptr, error);
                                }))
        CompleteContext(id, false, nullptr,
                        "Invalid native context HTTP headers or capacity exceeded");
      return;
    }
    if (action == "setHTTPCredentials") {
      const int context = params->GetInt("contextId");
      const auto cef = contexts_->RequestContextForContext(context);
      auto config = CefDictionaryValue::Create();
      if (params->GetType("credentials") == VTYPE_NULL)
        config->SetList("httpCredentials", CefListValue::Create());
      else if (params->GetType("credentials") == VTYPE_DICTIONARY)
        config->SetDictionary("httpCredentials", params->GetDictionary("credentials")->Copy(false));
      else if (params->GetType("credentials") == VTYPE_LIST)
        config->SetList("httpCredentials", params->GetList("credentials")->Copy());
      else {
        CompleteContext(id, false, nullptr,
                        "Native context HTTP credentials must be a credential, array or null");
        return;
      }
      suitecut::ContextSettings parsed;
      std::string error;
      if (!cef || !suitecut::ContextSettings::Parse(config, parsed, error)) {
        CompleteContext(id, false, nullptr,
                        error.empty() ? "Native browser context is closed" : error);
        return;
      }
      if (!authentication_.Set(
              id, context, std::move(parsed.http_credentials_all), operation->deadline_ms,
              [cef](suitecut::NativeContextAuthenticationControls::Result done) {
                cef->ClearHttpAuthCredentials(
                    new suitecut::NativeContextAuthenticationClear(cef, std::move(done)));
              },
              [this, alive = alive_, id, context, generation = operation->generation](
                  bool ok, const std::string& error) {
                if (!*alive) return;
                const bool expired =
                    error == suitecut::NativeContextAuthenticationControls::DeadlineError();
                if (!ok && !expired) AuthenticationFatal(context, error);
                if (operations_.IsCurrent(id, generation))
                  CompleteContext(id, ok, nullptr,
                                  expired ? "Native context operation timed out" : error);
              }))
        CompleteContext(id, false, nullptr,
                        "Native context HTTP credential update is pending or exceeds capacity");
      WakeTrace();
      return;
    }
    if (action == "setGeolocation") {
      const int context = params->GetInt("contextId");
      auto input = CefDictionaryValue::Create();
      if (params->GetType("geolocation") == VTYPE_NULL)
        input->SetNull("geolocation");
      else if (params->GetType("geolocation") == VTYPE_DICTIONARY)
        input->SetDictionary("geolocation", params->GetDictionary("geolocation")->Copy(false));
      else {
        CompleteContext(id, false, nullptr, "Native geolocation must be coordinates or null");
        return;
      }
      suitecut::ContextSettings parsed;
      std::string error;
      if (!contexts_->RequestContextForContext(context) ||
          !suitecut::ContextSettings::Parse(input, parsed, error)) {
        CompleteContext(id, false, nullptr,
                        error.empty() ? "Native browser context is closed" : error);
        return;
      }
      std::optional<suitecut::NativeGeolocationValue::Coordinates> coordinates;
      if (parsed.geolocation)
        coordinates = suitecut::NativeGeolocationValue::Coordinates{parsed.geolocation->latitude,
                                                                    parsed.geolocation->longitude,
                                                                    parsed.geolocation->accuracy};
      if (!geolocation_.Set(id, context, std::move(coordinates), operation->deadline_ms,
                            [this, alive = alive_, id, generation = operation->generation](
                                bool ok, const std::string& error) {
                              if (*alive && operations_.IsCurrent(id, generation))
                                CompleteContext(id, ok, nullptr, error);
                            }))
        CompleteContext(id, false, nullptr,
                        "Native geolocation update is pending or exceeds capacity");
      return;
    }
    if (action == "setOffline") {
      const auto context = params->GetInt("contextId");
      if (!contexts_->RequestContextForContext(context) ||
          params->GetType("offline") != VTYPE_BOOL ||
          !network_policy_.Set(id, context, params->GetBool("offline"), operation->deadline_ms,
                               [this, alive = alive_, id, generation = operation->generation](
                                   bool ok, const std::string& error) {
                                 if (*alive && operations_.IsCurrent(id, generation))
                                   CompleteContext(id, ok, nullptr, error);
                               }))
        CompleteContext(id, false, nullptr,
                        "Invalid native context offline policy or capacity exceeded");
      return;
    }
    if (action == "workerNetworkBody") {
      const bool accepted =
          workers_->NetworkBody(params->GetInt("contextId"), params->GetString("owner"),
                                params->GetString("bodyId"), params->GetString("stage"));
      CompleteContext(id, accepted, nullptr,
                      accepted ? "" : "Actual worker network body owner is unavailable");
      return;
    }
    if (action == "routes") {
      HandleContextRoutes(id, params, operation->deadline_ms, operation->generation);
      return;
    }
    if (action == "route") {
      const int context = params->GetInt("contextId");
      if (!contexts_->RequestContextForContext(context) ||
          !worker_network_->HandleRoute(
              id, context, params->GetString("owner"), params, operation->deadline_ms,
              [this, alive = alive_, id, generation = operation->generation](
                  bool ok, Dict result, const std::string& error) {
                if (*alive && operations_.IsCurrent(id, generation))
                  CompleteContext(id, ok, result, error);
              }))
        CompleteContext(id, false, nullptr, "Actual route owner is unavailable or command invalid");
      return;
    }
    if (action == "workerNetwork") {
      const int context = params->GetInt("contextId");
      if (!contexts_->RequestContextForContext(context) ||
          !worker_network_->Handle(id, context, params->GetString("requestId"), params,
                                   operation->deadline_ms,
                                   [this, alive = alive_, id, generation = operation->generation](
                                       bool ok, Dict result, const std::string& error) {
                                     if (*alive && operations_.IsCurrent(id, generation))
                                       CompleteContext(id, ok, result, error);
                                   })) {
        CompleteContext(id, false, nullptr,
                        "Actual worker network request is unavailable or command invalid");
      }
      return;
    }
    if (action == "waitNetwork") {
      const auto context = params->GetInt("contextId");
      const auto cursor = params->GetType("afterSequence");
      if (!contexts_->RequestContextForContext(context) ||
          (cursor != VTYPE_INT && cursor != VTYPE_DOUBLE) ||
          !context_network_->Wait(id, context, params->GetString("event"),
                                  suitecut::DictionaryNumber(params, "afterSequence"),
                                  operation->deadline_ms,
                                  [this, alive = alive_, id, generation = operation->generation](
                                      bool ok, Dict result, const std::string& error) {
                                    if (*alive && operations_.IsCurrent(id, generation))
                                      CompleteContext(id, ok, result, error);
                                  }))
        CompleteContext(id, false, nullptr,
                        "Invalid native context network wait or capacity exceeded");
      return;
    }
    if (action == "trace") {
      traces_->Handle(id, params->GetInt("contextId"), params, operation->deadline_ms,
                      [this, alive = alive_, id, generation = operation->generation](
                          bool ok, Dict result, const std::string& error) {
                        if (*alive && operations_.IsCurrent(id, generation))
                          CompleteContext(id, ok, result, error);
                      });
      return;
    }
    if (action == "worker") {
      workers_->Handle(id, params->GetInt("contextId"), params, operation->deadline_ms,
                       [this, alive = alive_, id, generation = operation->generation,
                        console = params->GetString("action") == "console" ||
                                  params->GetString("action") == "waitEventPredicate" ||
                                  params->GetString("action") == "continueEventPredicate"](
                           bool ok, Dict result, const std::string& error) {
                         if (*alive && operations_.IsCurrent(id, generation))
                           CompleteContext(id, ok, result, error, console);
                       });
      return;
    }
    if (action == "waitConsole") {
      const auto context = params->GetInt("contextId");
      const auto cursor = params->GetType("afterSequence");
      if (!contexts_->RequestContextForContext(context) ||
          (cursor != VTYPE_INT && cursor != VTYPE_DOUBLE) ||
          !context_console_->Wait(id, context, suitecut::DictionaryNumber(params, "afterSequence"),
                                  operation->deadline_ms,
                                  [this, alive = alive_, id, generation = operation->generation](
                                      bool ok, Dict result, const std::string& error) {
                                    if (*alive && operations_.IsCurrent(id, generation))
                                      CompleteContext(id, ok, result, error, true);
                                  }))
        CompleteContext(id, false, nullptr,
                        "Invalid native context console wait or capacity exceeded");
      return;
    }
    if (action == "close" && handler_native_origin.locator_handler_invocation &&
        params->GetType("contextId") == VTYPE_INT) {
      for (const auto& [owner, source] : locator_handler_leases_) {
        (void)owner;
        const auto work = source->locator_handler_active_;
        if (work && work->token == handler_native_origin.locator_handler_invocation)
          work->close_contexts.insert(params->GetInt("contextId"));
      }
    }
    if (action == "closePage" && handler_native_origin.locator_handler_invocation &&
        params->GetType("pageId") == VTYPE_INT) {
      for (const auto& [owner, source] : locator_handler_leases_) {
        (void)owner;
        const auto work = source->locator_handler_active_;
        if (work && work->token == handler_native_origin.locator_handler_invocation)
          work->close_pages.insert(params->GetInt("pageId"));
      }
    }
    if (action == "closePage" && params->GetType("pageId") == VTYPE_INT) {
      const int page = params->GetInt("pageId"), context = contexts_->ContextId(page);
      const auto lease = traces_->CaptureLease(context);
      if (lease.context) {
        const auto call = traces_->NextCall();
        if (call) {
          auto event = suitecut::TracePageCloseMetadata(params, page, Now());
          event->SetString("callId", "call@" + std::to_string(call));
          traces_->Before(context, call, event);
          trace_context_api_.emplace(id, std::make_pair(lease, call));
        } else
          traces_->CaptureError(lease, "Native trace context action identity exhausted");
      }
    }
    if ((action == "create" || action == "newOwnedPage") && params->HasKey("recordVideo")) {
      const auto generation = operation->generation;
      context_video_->Prepare(
          id, generation, params->GetDictionary("recordVideo"), operation->deadline_ms,
          [this, alive = alive_, id, generation, action, params](
              suitecut::NativeContextVideo::Prepared prepared, const std::string& error) {
            if (!*alive) return;
            if (!operations_.IsCurrent(id, generation) ||
                operations_.IsExpired(id, generation, Now()) || closing_) {
              context_video_->Discard(std::move(prepared));
              if (operations_.IsCurrent(id, generation))
                CompleteContext(id, false, nullptr,
                                "Native context video preparation exceeded its original deadline");
              return;
            }
            if (!prepared) {
              CompleteContext(id, false, nullptr, error);
              return;
            }
            video_preparations_.emplace(id, VideoPreparation{generation, std::move(prepared)});
            ContextCommand(id, generation, action, params);
          });
    } else
      ContextCommand(id, operation->generation, action, params);
    return;
  }
#include "controllers/network/route_callback_engine.inc"
  int page = 1;
  if (message->HasKey("pageId")) {
    if (message->GetType("pageId") != VTYPE_INT || message->GetInt("pageId") < 1) {
      writer_.Reply(id, false, "Invalid native page ID");
      return;
    }
    page = message->GetInt("pageId");
  }
  const auto source = sources_.find(page);
  if (source == sources_.end()) {
    writer_.Reply(id, command == "cancel",
                  command == "cancel" ? "" : "Native page is closed or unknown");
    return;
  }
  source->second->Command(line);
}

}  // namespace suitecut::browser
