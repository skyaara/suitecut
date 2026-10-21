// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
Engine::Engine(Writer& writer, int width, int height, double scale, bool i420, int fps)
    : writer_(writer),
      width_(width),
      height_(height),
      fps_(fps),
      scale_(scale),
      i420_(i420),
      authentication_([] { return Now(); }) {
  websocket_routes_ = std::make_unique<suitecut::NativeWebSocketRoutes>(
      [] { return Now(); }, [this] { WakeTrace(); },
      [this](Dict event, std::vector<uint8_t> payload) {
        event->SetInt("version", 1);
        event->SetString("type", "websocketroute");
        return writer_.TryEvent(event, std::move(payload));
      });
  network_archive_ = std::make_unique<suitecut::NativeNetworkArchives>(
      [] { return Now(); }, [this](const std::string& error) { writer_.Error(error.c_str()); });
  context_routes_ = std::make_unique<suitecut::NativeContextRoutes>(
      [this](const std::string& owner, const std::string& error) {
        HARPolicyFailure(owner, error);
      },
      [] { return Now(); });
  context_network_ = std::make_unique<suitecut::NativeContextNetworkControls>([this](Dict info) {
    const auto json = Json(info);
    writer_.Send(Header("contextnetwork"), std::vector<uint8_t>(json.begin(), json.end()));
  });
  worker_network_ = std::make_unique<suitecut::NativeContextWorkerNetwork>(
      [this](int context, Dict worker, Dict event) {
        if (!context_network_->ObserveWorker(context, worker, event))
          writer_.Error("Native context worker journal rejected actual metadata");
      });
  worker_network_->SetRouteEmitter(
      [this](int context, const std::string& owner, Dict worker, Dict event) {
        ContextRoute(context, owner, 0, worker, event);
      });
  context_console_ = std::make_unique<suitecut::NativeContextConsoleControls>([this](Dict info) {
    auto typed = CefValue::Create();
    typed->SetDictionary(info->Copy(false));
    std::string json;
    if (!suitecut::DevToolsLosslessJSON(typed, json, 1048576)) {
      writer_.Error("Native context console output limit exceeded");
      return;
    }
    writer_.Send(Header("contextconsole"), std::vector<uint8_t>(json.begin(), json.end()));
  });
  suitecut::RecordingControls::Hooks recording_hooks;
  recording_hooks.dispatch = [alive = alive_](std::function<void()> task) {
    CefPostTask(TID_UI, new Task([alive, task = std::move(task)] {
                  if (*alive) task();
                }));
  };
  recording_hooks.event = [this](int page, const std::string& id, const std::string& phase,
                                 const suitecut::MediaPipelineResult& result) {
    if (context_video_) context_video_->Observe(page, id, phase, result);
    if (page == 1 && startup_operations_.Contains(1)) {
      // There are no public recording subscribers before launch resolves. Keep
      // the real terminal receipt in NativeContextVideo without violating the
      // initial ready/error packet boundary if paint beats page bootstrap.
      if (phase == "failed")
        StartupFailed(result.failure.empty() ? "Native launch recording encoder failed"
                                             : result.failure);
      return;
    }
    auto header = Header("recording");
    header->SetInt("pageId", page);
    const auto json = Json(Source::RecordingResult(id, result, phase));
    writer_.Send(header, std::vector<uint8_t>(json.begin(), json.end()));
  };
  recording_hooks.request_paint = [this](int page, const std::string& id) {
    const auto found = sources_.find(page);
    if (found != sources_.end()) found->second->ScheduleRecordingSnapshot(id);
  };
  recording_hooks.now = [] { return Now(); };
  recording_hooks.physical_settled = [this](int page, int context) {
#if defined(__APPLE__)
    const auto source = sources_.find(page);
    if (source != sources_.end()) {
      source->second->native_capture_pool_.reset();
      source->second->native_zero_copy_recording_ = false;
      source->second->native_capture_snapshot_.reset();
      source->second->recording_snapshot_pending_.clear();
      if (source->second->native_view_stale_ && source->second->browser_ &&
          !source->second->closing_page_)
        source->second->browser_->GetHost()->Invalidate(PET_VIEW);
    }
#endif
    contexts_->ContinueCloseContext(context);
    Idle();
  };
  recordings_ = std::make_shared<suitecut::RecordingControls>(std::move(recording_hooks));
#if defined(OS_LINUX) || defined(__APPLE__)
  suitecut::StreamingControls::Hooks streaming_hooks;
  streaming_hooks.dispatch = [alive = alive_](std::function<void()> task) {
    CefPostTask(TID_UI, new Task([alive, task = std::move(task)] {
                  if (*alive) task();
                }));
  };
  streaming_hooks.event = [this](int page, const std::string& id, const std::string& phase,
                                 const suitecut::StreamPipelineResult& result) {
    auto header = Header("streaming");
    header->SetInt("pageId", page);
    const auto json = Json(Source::StreamingResult(id, result, phase, streaming_->ClientToken(id)));
    writer_.Send(header, std::vector<uint8_t>(json.begin(), json.end()));
  };
  streaming_hooks.physical_settled = [this](int owner_context, int selected_context) {
    contexts_->ContinueCloseContext(owner_context);
    if (selected_context != owner_context) contexts_->ContinueCloseContext(selected_context);
    Idle();
  };
  streaming_ = std::make_shared<suitecut::StreamingControls>(std::move(streaming_hooks));
#endif
  authentication_.SetSettled([this](int context) {
    if (contexts_) contexts_->ContinueCloseContext(context);
    WakeTrace();
    Idle();
  });
  SuiteCutContextControls::Hooks hooks;
  hooks.internal_cookies = [this](int owner, int context, const std::string& action, Dict params,
                                  double deadline, SuiteCutContextControls::Reply reply) {
    return http_ &&
           http_->InternalCookieCommand(owner, context, action, params, deadline, std::move(reply));
  };
  hooks.validate_internal_storage = [this](int owner, int context, Dict state, double deadline,
                                           std::string& error) {
    return http_ && http_->ValidateInternalStorage(owner, context, state, deadline, error);
  };
  hooks.validate_internal_cookies = [this](int owner, int context, CefRefPtr<CefListValue> cookies,
                                           double deadline, std::string& error) {
    return http_ && http_->ValidateInternalCookies(owner, context, cookies, deadline, error);
  };
  hooks.cookie_channel = [this](int owner, int context, suitecut::CookieMethod method, Dict params,
                                double deadline, suitecut::CookieReply reply) {
    workers_->CookieControl(owner, context, method, params, deadline, std::move(reply));
  };
  hooks.cookie_deadline = [this](int owner) {
    const auto operation = operations_.Get(owner);
    return operation ? operation->deadline_ms : 0;
  };
  hooks.cancel_cookie = [this](int owner) { workers_->CancelCookie(owner); };
  hooks.context_created = [this](int context, Dict params, std::string& error) {
    if (params->HasKey("offline") && params->GetType("offline") != VTYPE_BOOL) {
      error = "Native offline option must be a boolean";
      return false;
    }
    if (!network_policy_.Seed(context, params->GetBool("offline"))) {
      error = "Native network context policy capacity exceeded";
      return false;
    }
    auto headers = params->HasKey("extraHTTPHeaders") ? params->GetDictionary("extraHTTPHeaders")
                                                      : CefDictionaryValue::Create();
    if (!network_headers_.Seed(context, headers)) {
      network_policy_.Closed(context);
      error = "Invalid or excessive native context HTTP headers";
      return false;
    }
    if (!settings_.Seed(context, params, error)) {
      network_headers_.Closed(context);
      network_policy_.Closed(context);
      return false;
    }
    const auto config = settings_.Get(context);
    if (!authentication_.Seed(context, config->http_credentials_all)) {
      error = "Native context HTTP authentication capacity exceeded";
      return false;
    }
    suitecut::NativeGeolocationValue geo;
    geo.configured = params->HasKey("geolocation");
    if (config->geolocation)
      geo.coordinates = suitecut::NativeGeolocationValue::Coordinates{
          config->geolocation->latitude, config->geolocation->longitude,
          config->geolocation->accuracy};
    if (!geolocation_.Seed(context, std::move(geo))) {
      error = "Native geolocation context capacity or value is invalid";
      return false;
    }
    if (!config->CaptureDimensions(width_, height_, scale_, error)) return false;
    if (!context_routes_->Seed(context)) {
      error = "Native context route policy capacity exceeded";
      return false;
    }
    if (!context_har_->Seed(context)) {
      error = "Native context HAR policy capacity exceeded";
      return false;
    }
    if (params->HasKey("recordVideo")) {
      const int owner = params->GetInt("internalOwner");
      const auto prepared = video_preparations_.find(owner);
      const auto operation = operations_.Get(owner);
      if (prepared == video_preparations_.end() || !operation ||
          operation->generation != prepared->second.generation) {
        error = "Native context video preparation is missing or stale";
        return false;
      }
      const auto value = prepared->second.value;
      video_preparations_.erase(prepared);
      if (!context_video_->Configure(context, value, error)) return false;
    }
    return !params->HasKey("recordHAR") ||
           har_recording_->Configure(context, params->GetDictionary("recordHAR"), error);
  };
  hooks.context_removed = [this](int context) {
    websocket_routes_->Closed(context);
    if (context_video_) context_video_->Removed(context);
    const auto failure = authentication_failures_.find(context);
    if (failure != authentication_failures_.end()) {
      failure->second->Closed();
      authentication_failures_.erase(failure);
    }
    context_unroute_all_->Closed(context);
    context_routes_->Closed(context);
    context_har_->Closed(context);
    har_recording_->Removed(context);
    geolocation_.Closed(context);
    authentication_.Closed(context);
    network_headers_.Closed(context);
    network_policy_.Closed(context);
    settings_.Drop(context);
  };
  hooks.create_page = [this](int page, int context, CefRefPtr<CefRequestContext> cef) {
    return CreatePage(page, context, cef);
  };
  hooks.before_page_close = [this](int page, double deadline, std::function<void()> continuation) {
    const auto recording_deadline =
        Clock::now() +
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(
            deadline ? std::max(0.0, deadline - Now()) : 60000));
    har_recording_->BeforePageClose(
        contexts_->ContextId(page), "page:" + std::to_string(page), deadline,
        [this, page, recording_deadline, continuation = std::move(continuation)]() mutable {
          recordings_->BeforePageClose(
              page,
              [this, page, continuation = std::move(continuation)]() mutable {
#if defined(OS_LINUX) || defined(__APPLE__)
                streaming_->BeforePageClose(
                    page, Clock::now() + std::chrono::seconds(60),
                    [this, page, continuation = std::move(continuation)]() mutable {
                      downloads_->BeforePageClose(page, std::move(continuation));
                    });
#else
                downloads_->BeforePageClose(page, std::move(continuation));
#endif
              },
              recording_deadline);
        });
  };
  hooks.operation_deadline = hooks.cookie_deadline;
  hooks.operation_now = [] { return Now(); };
  hooks.operation_generation = [this](int owner) {
    const auto operation = operations_.Get(owner);
    return operation ? operation->generation : uint64_t{0};
  };
  hooks.operation_current = [this](int owner, uint64_t generation) {
    return operations_.IsCurrent(owner, generation);
  };
  hooks.context_cleanup_error = [this](int context) {
    const auto error = har_recording_->Error(context);
    return !error.empty() ? error : context_video_->Error(context);
  };
  hooks.page_metadata = [this](int page) -> Dict {
    const auto video = context_video_->Metadata(page);
    if (!video) return nullptr;
    auto result = CefDictionaryValue::Create();
    auto descriptor = CefDictionaryValue::Create();
    descriptor->SetString("id", video->id);
    result->SetDictionary("video", descriptor);
    return result;
  };
  hooks.context_closing = [this](int context, double deadline) {
    if (http_) http_->ContextClosing(context);
    geolocation_.Closing(context);
    authentication_.Closing(context);
    traces_->Closed(context);
    trace_sources_->Closed(context);
#if defined(OS_LINUX) || defined(__APPLE__)
    streaming_->FinalizeContext(context, Clock::now() + std::chrono::seconds(60));
#endif
    recordings_->FinalizeContext(context, Now(), Clock::now() + std::chrono::seconds(60));
    downloads_->ContextClosing(context);
    har_recording_->BeforeClose(context, deadline,
                                [this, context] { workers_->CloseContext(context); });
  };
  hooks.context_cleanup_ready = [this](int context) {
    return authentication_.Ready(context) && (!http_ || http_->ContextReady(context)) &&
           har_recording_->ContextReady(context) && traces_->ContextReady(context) &&
           screenshot_comparisons_->ContextReady(context) &&
           screenshot_inputs_->ContextReady(context) &&
#if defined(OS_LINUX) || defined(__APPLE__)
           streaming_->ContextReady(context) &&
#endif
           recordings_->ContextReady(context) && context_video_->ContextReady(context) &&
           downloads_->ContextCleanupReady(context) && workers_->ContextClosed(context);
  };
  hooks.context_gone = [this](int context) {
    workers_->ContextGone(context);
    worker_network_->Closed(context);
    network_archive_->Closed(context);
    context_console_->Closed(context);
    context_network_->Closed(context);
    if (http_) http_->ContextClosed(context);
    if (downloads_) downloads_->ContextGone(context);
#if defined(OS_LINUX) || defined(__APPLE__)
    if (streaming_) streaming_->ContextGone(context);
#endif
  };
  hooks.event = [this](Dict event) {
    auto header = Header("context");
    const auto json = Json(event);
    writer_.Send(header, std::vector<uint8_t>(json.begin(), json.end()));
  };
  hooks.idle = [this] { Idle(); };
  hooks.page_closing = [this](int page, const std::string& reason) {
    const auto found = sources_.find(page);
    if (found != sources_.end()) {
      found->second->closed_reason_ = reason;
      found->second->native_trace_page_closing_ = true;
      found->second->context_auth_open_ = false;
    }
  };
  hooks.seed_binding = [this](int page, Dict config, std::string& error) {
    const auto source = sources_.find(page);
    if (source == sources_.end() || !source->second->bindings_ || source->second->closing_page_) {
      error = "Native page closed before binding seed";
      return false;
    }
    return source->second->bindings_->SeedInherited(
        suitecut::InboundString(config, "name"), config->GetString("bridge"),
        config->GetString("installer"), config->GetString("codec"), config->GetBool("handle"),
        error);
  };
  hooks.register_binding = [this](int page, int owner, Dict config,
                                  std::function<void(bool, const std::string&)> done) {
    const auto source = sources_.find(page);
    if (source == sources_.end() || !source->second->bindings_ || source->second->closing_page_) {
      done(false, "Native page closed during context binding registration");
      return;
    }
    source->second->bindings_->RegisterInherited(
        owner, config,
        [done = std::move(done)](bool ok, const std::string& error, const std::string&) {
          done(ok, error);
        });
  };
  hooks.remove_binding = [this](int page, int owner, const std::string& name,
                                const std::string& bridge) {
    const auto source = sources_.find(page);
    if (source == sources_.end() || !source->second->bindings_) return;
    if (owner) source->second->bindings_->Cancel(owner);
    source->second->bindings_->RemoveInherited(name, bridge);
  };
  hooks.apply_credentials = [this](int page, int owner,
                                   std::shared_ptr<const suitecut::CredentialPage::Config> config,
                                   suitecut::CredentialPage::Result done) {
    const auto source = sources_.find(page);
    if (source == sources_.end() || source->second->closing_page_ ||
        !source->second->credentials_) {
      done(false, "Native credential page closed");
      return;
    }
    source->second->credentials_->Apply(owner, std::move(config), std::move(done));
  };
  hooks.cancel_credentials = [this](int page, int owner) {
    const auto source = sources_.find(page);
    if (source != sources_.end() && source->second->credentials_)
      source->second->credentials_->Cancel(owner);
  };
  hooks.adopt_credentials = [this](int page,
                                   std::shared_ptr<const suitecut::CredentialPage::Config> config) {
    const auto source = sources_.find(page);
    return source != sources_.end() && !source->second->closing_page_ &&
           source->second->credentials_ &&
           source->second->credentials_->AdoptObserved(std::move(config));
  };
  contexts_ = std::make_shared<SuiteCutContextControls>(std::move(hooks));
  SuiteCutContextWorkers::Hooks worker_hooks;
  worker_hooks.websocket_required = [this, alive = alive_](int context) {
    return *alive && websocket_routes_->HasPolicy(context, 0);
  };
  worker_hooks.websocket_failure = [this, alive = alive_](int context, const std::string& error) {
    if (*alive)
      AuthenticationFatal(context, "Inherited native WebSocket worker policy failed: " + error);
  };
  worker_hooks.attach_websocket = [this, alive = alive_](
                                      int context, const std::string& owner,
                                      suitecut::NativeWebSocketRoutes::Send send,
                                      suitecut::NativeWebSocketRoutes::Fatal fatal,
                                      SuiteCutContextWorkers::NetworkResult ready) {
    if (!*alive) {
      ready(false, "Native Engine is closed");
      return true;
    }
    return websocket_routes_->Attach(context, 0, owner, std::move(send), std::move(fatal),
                                     std::move(ready));
  };
  worker_hooks.detach_websocket = [this, alive = alive_](const std::string& owner) {
    if (*alive) websocket_routes_->DetachPrefix(owner);
  };
  worker_hooks.websocket_event = [this, alive = alive_](const std::string& owner, Dict event) {
    return *alive && websocket_routes_->Event(owner, event);
  };
  worker_hooks.route_fetch = [this, alive = alive_](
                                 int owner, int context, CefRefPtr<CefRequestContext> expected,
                                 Dict request, Dict options, double deadline,
                                 std::shared_ptr<void> lease,
                                 std::function<bool(bool, Dict, const std::string&)> done) {
    if (!*alive || !http_) {
      done(false, nullptr, "Native worker HTTP context engine is unavailable");
      return;
    }
    http_->RouteFetchContext(owner, {std::move(expected), context}, request, options,
                             std::move(done), deadline, std::move(lease));
  };
  worker_hooks.cancel_route_fetch = [this, alive = alive_](int owner) {
    if (*alive && http_) http_->Cancel(owner);
  };
  worker_hooks.network_owner = [this, alive = alive_](
                                   int context, const std::string& owner,
                                   SuiteCutContextWorkers::PublicNetworkCommand command,
                                   SuiteCutContextWorkers::BodyFetch body,
                                   std::function<void(int)> cancel) {
    return *alive && worker_network_->Attach(context, owner, true, std::move(command),
                                             std::move(body), std::move(cancel));
  };
  worker_hooks.network_closed = [this, alive = alive_](const std::string& owner) {
    if (*alive) worker_network_->Detach(owner);
  };
  worker_hooks.network_event = [this, alive = alive_](int context, const std::string& owner,
                                                      Dict worker, Dict event) {
    if (*alive) ContextWorkerNetwork(context, owner, worker, event);
  };
  worker_hooks.network_body =
      [this, alive = alive_](int context, const std::string& owner, uint64_t body, size_t offset,
                             size_t total, std::span<const uint8_t> bytes, bool canceled) {
        if (!*alive) return false;
        auto header = Header("contextnetworkbody");
        header->SetInt("contextId", context);
        header->SetString("owner", owner);
        header->SetString("bodyId", std::to_string(body));
        header->SetInt("offset", int(offset));
        header->SetInt("totalBytes", int(total));
        if (canceled) header->SetBool("canceled", true);
        return writer_.TryEvent(header, std::vector<uint8_t>(bytes.begin(), bytes.end()));
      };
  worker_hooks.authentication = [this](int context) {
    return suitecut::NativeContextAuthentication(settings_.Get(context),
                                                 authentication_.Get(context));
  };
  worker_hooks.attach_authentication =
      [this](int context, const std::string& target,
             suitecut::NativeContextAuthenticationControls::Apply apply,
             SuiteCutContextWorkers::NetworkResult ready) {
        return authentication_.Attach(context, target, std::move(apply), std::move(ready));
      };
  worker_hooks.detach_authentication = [this](const std::string& target) {
    authentication_.Detach(target);
  };
  worker_hooks.attach_network = [this, alive = alive_](
                                    int context, const std::string& target,
                                    SuiteCutContextWorkers::NetworkApply apply,
                                    SuiteCutContextWorkers::NetworkResult ready) {
    if (!*alive) {
      ready(false, "Native engine is closed");
      return true;
    }
    return network_policy_.Attach(context, target, std::move(apply), std::move(ready));
  };
  worker_hooks.detach_network = [this, alive = alive_](const std::string& target) {
    if (*alive) network_policy_.Detach(target);
  };
  worker_hooks.attach_headers = [this, alive = alive_](
                                    int context, const std::string& target,
                                    SuiteCutContextWorkers::HeaderApply apply,
                                    SuiteCutContextWorkers::NetworkResult ready) {
    if (!*alive) {
      ready(false, "Native engine is closed");
      return true;
    }
    return network_headers_.Attach(context, target, std::move(apply), std::move(ready));
  };
  worker_hooks.attach_routes = [this, alive = alive_](int context, const std::string& owner,
                                                      suitecut::NativeContextRoutes::Apply apply,
                                                      SuiteCutContextWorkers::NetworkResult ready) {
    if (!*alive) {
      ready(false, "Native Engine is closed");
      return true;
    }
    return context_routes_->Attach(context, owner, std::move(apply), std::move(ready));
  };
  worker_hooks.detach_routes = [this, alive = alive_](const std::string& owner) {
    if (*alive) context_routes_->Detach(owner);
  };
  worker_hooks.attach_har = [this, alive = alive_](int context, const std::string& owner,
                                                   suitecut::NativeContextHARRoutes::Apply apply,
                                                   SuiteCutContextWorkers::NetworkResult ready) {
    if (!*alive) {
      ready(false, "Native Engine is closed");
      return true;
    }
    return context_har_->Attach(context, owner, std::move(apply), std::move(ready));
  };
  worker_hooks.detach_har = [this, alive = alive_](const std::string& owner) {
    if (*alive) context_har_->Detach(owner);
  };
  worker_hooks.detach_headers = [this, alive = alive_](const std::string& target) {
    if (*alive) network_headers_.Detach(target);
  };
  worker_hooks.resource_owner = [this, alive = alive_](int context, const std::string& owner,
                                                       SuiteCutContextWorkers::BodyFetch fetch) {
    return *alive && network_archive_->Attach(context, owner, std::move(fetch),
                                              suitecut::NativeHARCapture::OwnerKind::WorkerBroker);
  };
  worker_hooks.resource_post = [this, alive = alive_](int context, const std::string& owner,
                                                      const std::string& id) {
    if (!*alive) return SuiteCutContextWorkers::PostReservation{};
    auto reserved = network_archive_->ReservePost(context, owner, id);
    if (network_archive_->NeedsTick()) WakeTrace();
    return SuiteCutContextWorkers::PostReservation{std::move(reserved.data),
                                                   std::move(reserved.receipt)};
  };
  worker_hooks.resource_closed = [this, alive = alive_](const std::string& owner) {
    if (*alive) network_archive_->Detach(owner);
  };
  worker_hooks.resource_request = [this, alive = alive_](int context, const std::string& owner,
                                                         const std::string& session, Dict raw) {
    if (*alive && !network_archive_->OriginalRequest(context, owner, session, raw))
      writer_.Error("Native worker archive original request rejected");
    if (*alive && network_archive_->NeedsTick()) WakeTrace();
  };
  worker_hooks.resource_event = [this, alive = alive_](int context, const std::string& owner,
                                                       Dict event) {
    const auto kind = event->GetString("event");
    if (kind != "request" && kind != "requestupdated" && kind != "response" &&
        kind != "requestfinished" && kind != "requestfailed")
      return;
    if (*alive && !network_archive_->Observe(context, owner, event, 0))
      writer_.Error("Native worker archive metadata rejected");
    if (*alive && network_archive_->NeedsTick()) WakeTrace();
  };
  worker_hooks.now = [] { return Now(); };
  worker_hooks.context = [this](int context) {
    return contexts_->RequestContextForContext(context);
  };
  worker_hooks.event = [this](int context, Dict event) {
    auto record = CefDictionaryValue::Create();
    record->SetInt("contextId", context);
    record->SetDictionary("workerEvent", event);
    const auto json = Json(record);
    writer_.Send(Header("contextworker"), std::vector<uint8_t>(json.begin(), json.end()));
  };
  worker_hooks.console = [this](int context, Dict info) {
    if (!context_console_->ObserveWorker(context, info))
      writer_.Error("Native context worker console journal rejected payload");
  };
  worker_hooks.predicate = [this](int context, Dict snapshot) {
    auto record = CefDictionaryValue::Create();
    record->SetInt("contextId", context);
    record = snapshot->Copy(false);
    record->SetInt("contextId", context);
    auto typed = CefValue::Create();
    typed->SetDictionary(record->Copy(false));
    std::string json;
    if (!suitecut::DevToolsLosslessJSON(typed, json, 1048576)) {
      writer_.Error("Native context console predicate output limit exceeded");
      return;
    }
    writer_.Send(Header("contextworkerpredicate"), std::vector<uint8_t>(json.begin(), json.end()));
  };
  worker_hooks.error = [](int context, const std::string& error) {
    std::fprintf(stderr, "Native context worker broker %d: %s\n", context, error.c_str());
  };
  worker_hooks.idle = [this](int context) {
    contexts_->ContinueCloseContext(context);
    Idle();
  };
  worker_hooks.inspector_work = inspector_work_;
  workers_ = std::make_shared<SuiteCutContextWorkers>(std::move(worker_hooks));
  utilities_ = std::make_shared<SuiteCutUtilityBrowsers>(
      SuiteCutUtilityBrowsers::HTTPObserver{}, [this] { Idle(); }, inspector_work_);
  contexts_->SetUtilities(utilities_);
  SuiteCutHTTPControls::Hooks http_hooks;
  http_hooks.page_context = [this](int page) {
    return SuiteCutHTTPControls::BrowserContext{contexts_->RequestContext(page),
                                                contexts_->ContextId(page)};
  };
  http_hooks.browser_context = [this](int context) {
    return SuiteCutHTTPControls::BrowserContext{contexts_->RequestContextForContext(context),
                                                context};
  };
  http_hooks.create_context =
      [this](std::function<void(SuiteCutHTTPControls::BrowserContext, const std::string&)> done) {
        contexts_->CreateInternal(
            [this, done = std::move(done)](bool ok, Dict result, const std::string& error) {
              const int id = ok && result ? result->GetInt("contextId") : 0;
              done({id ? contexts_->RequestContextForContext(id) : nullptr, id}, error);
            });
      };
  http_hooks.close_context = [this](int context, SuiteCutHTTPControls::Reply reply) {
    auto params = CefDictionaryValue::Create();
    params->SetInt("contextId", context);
    contexts_->Handle("close", params, std::move(reply));
  };
  http_hooks.context_command = [this](int owner, int context, const std::string& action,
                                      Dict params, SuiteCutHTTPControls::Reply reply) {
    params->SetInt("contextId", context);
    params->SetInt("internalOwner", owner);
    if (action == "httpCookieCommit")
      contexts_->HTTPCommitCookies(owner, context, params, std::move(reply));
    else
      contexts_->Handle(action, params, std::move(reply));
  };
  http_hooks.match_cookies = [this](int owner, int context, Dict filter,
                                    CefRefPtr<CefListValue> cookies, double deadline,
                                    SuiteCutHTTPControls::Reply reply) {
    contexts_->MatchHTTPCookies(owner, context, filter, cookies, deadline, std::move(reply));
  };
  http_hooks.cancel_context_command = [this](int owner) { contexts_->CancelWait(owner); };
  http_hooks.physical_owner = [this](int owner) { return contexts_->PhysicalOwner(owner); };
  http_hooks.settled_context = [this](int context) {
    contexts_->ContinueCloseContext(context);
    Idle();
  };
  http_hooks.default_user_agent = [this] { return http_user_agent_; };
  http_hooks.context_headers = [this](int context) { return network_headers_.Snapshot(context); };
  http_hooks.context_policy = [this](int context) {
    const auto config = settings_.Get(context);
    if (!config) return Dict{};
    auto params = CefDictionaryValue::Create();
    if (config->user_agent) params->SetString("userAgent", *config->user_agent);
    if (config->ignore_https_errors)
      params->SetBool("ignoreHTTPSErrors", *config->ignore_https_errors);
    auto current = *config;
    const auto snapshot = authentication_.Get(context);
    const auto credentials = snapshot ? snapshot->Get() : nullptr;
    current.http_credentials_all =
        credentials ? *credentials : suitecut::NativeContextCredentials{};
    current.http_credentials.reset();
    current.WriteHTTPCredentials(params);
    if (config->proxy) {
      const auto& proxy = *config->proxy;
      auto value = CefDictionaryValue::Create();
      value->SetString("server", proxy.server);
      auto bypass = proxy.bypass;
      const std::string loopback = "<-loopback>";
      if (bypass.starts_with(loopback)) bypass.erase(0, loopback.size());
      while (!bypass.empty() && bypass.front() == ';') bypass.erase(0, 1);
      for (auto& item : bypass)
        if (item == ';') item = ',';
      value->SetString("bypass", bypass);
      if (proxy.authentication) {
        value->SetString("username", proxy.username);
        value->SetString("password", proxy.password);
      }
      params->SetDictionary("proxy", value);
    }
    return params;
  };
  http_hooks.lookup_event = [this](const suitecut::HTTPLookupControls::Event& event) {
    auto info = CefDictionaryValue::Create();
    info->SetString("event", event.canceled ? "canceled" : "request");
    info->SetInt("owner", event.owner);
    info->SetInt("apiContextId", event.context);
    info->SetDouble("generation", static_cast<double>(event.generation));
    info->SetString("lookupKey", event.key);
    info->SetString("hostname", event.hostname);
    info->SetInt("port", event.port);
    const auto json = Json(info);
    writer_.Send(Header("httplookup"), std::vector<uint8_t>(json.begin(), json.end()));
  };

  http_hooks.now = [] { return Now(); };
  http_hooks.wake = [this] {
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
  };
  http_ = std::make_shared<SuiteCutHTTPControls>(std::move(http_hooks));
  suitecut::DownloadControls::Hooks download_hooks;
  download_hooks.policy = [this](int page) {
    int owner = page;
    const int opener = contexts_->PopupOpener(page);
    const auto source = sources_.find(page);
    if (opener && source != sources_.end() && source->second->DownloadProvisionalPopup())
      owner = opener;
    return suitecut::DownloadControls::Policy{contexts_->ContextId(page),
                                              contexts_->AcceptDownloads(page), owner};
  };
  download_hooks.event = [this](int page, Dict info) {
    const auto source = sources_.find(page);
    if (source != sources_.end()) source->second->DownloadPublish(info);
  };
  download_hooks.navigation_response = [this](int page) {
    const auto source = sources_.find(page);
    if (source != sources_.end()) source->second->DownloadNavigationChanged();
  };
  download_hooks.now = [] { return Now(); };
  download_hooks.wake = [this] {
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
  };
  download_hooks.settled_context = [this](int context) {
    contexts_->ContinueCloseContext(context);
  };
  download_hooks.close_provisional_page = [this](int page) {
    const auto source = sources_.find(page);
    if (contexts_->PopupOpener(page) && source != sources_.end() &&
        source->second->DownloadProvisionalPopup()) {
      auto params = CefDictionaryValue::Create();
      params->SetInt("pageId", page);
      contexts_->Handle("closePage", params, [](bool, Dict, const std::string&) {});
    }
  };
  const auto download_command = CefCommandLine::GetGlobalCommandLine();
  download_hooks.directory =
      download_command
          ->GetSwitchValue(download_command->HasSwitch("suitecut-downloads-path")
                               ? "suitecut-downloads-path"
                               : "suitecut-profile")
          .ToString();
  downloads_ = std::make_shared<suitecut::DownloadControls>(std::move(download_hooks));
  suitecut::TraceControls::Hooks trace_hooks;
  trace_hooks.action_png = true;
  trace_hooks.aria = true;
  trace_hooks.now = [] { return Now(); };
  trace_hooks.wake = [this] { WakeTrace(); };
  trace_hooks.settled_context = [this](int context) {
    contexts_->ContinueCloseContext(context);
    Idle();
  };
  trace_hooks.background = [this](std::function<void()> work) {
    return trace_background_.Submit(std::move(work));
  };
  trace_hooks.post = [](std::function<void()> work) {
    return CefPostTask(TID_UI, new Task(std::move(work)));
  };
  trace_hooks.raw_directory =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-traces-dir").ToString();
  trace_hooks.context_options = [this](int context) -> Dict {
    if (!contexts_->RequestContextForContext(context)) return nullptr;
    auto info = CefDictionaryValue::Create();
    info->SetInt("version", 1);
    info->SetString("origin", "suitecut-native");
    info->SetString("browserName", "chromium");
    info->SetString("cefVersion", CEF_VERSION);
    info->SetInt("contextId", context);
    info->SetDouble("monotonicTime", Now());
    info->SetDouble("wallTime", std::chrono::duration<double, std::milli>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
    auto options = CefDictionaryValue::Create();
    info->SetDictionary("options", options);
    return info;
  };
  trace_hooks.pending_capture = [this](int context) { return TracePending(context); };
  trace_hooks.begin_session = [this](suitecut::TraceJournal::Lease lease, bool initial,
                                     std::string& error) {
    const auto options = traces_->Configuration(lease.context);
    if (!options || !options->dom) return true;
    const bool begun = network_archive_->BeginTrace(lease.context, lease.session) ||
                       (!initial && network_archive_->ResumeTrace(lease.context, lease.session));
    if (!begun) error = "Native trace network consumer is unavailable";
    return begun;
  };
  trace_hooks.seal_session = [this](suitecut::TraceJournal::Lease lease) {
    network_archive_->SealTrace(lease.context, lease.session);
  };
  trace_hooks.end_session = [this](suitecut::TraceJournal::Lease lease) {
    network_archive_->EndTrace(lease.context, lease.session);
    if (trace_sources_) trace_sources_->Closed(lease.context);
  };
  trace_hooks.source = [this](suitecut::TraceJournal::Lease lease, const std::string& path) {
    if (!trace_sources_->Capture(lease, path))
      traces_->CaptureError(lease, "Native trace source capture capacity exceeded");
  };
  trace_hooks.request_capture = [this](int context) {
    const auto lease = traces_->CaptureLease(context);
    for (const auto& [page, source] : sources_)
      if (contexts_->ContextId(page) == context && source->browser_ && !source->closing_page_) {
        source->NativeTraceDOM(lease, 0, "initial", Now() + 5000);
        source->browser_->GetHost()->Invalidate(PET_VIEW);
        source->WakeDeadlines();
      }
  };
  traces_ = std::make_shared<suitecut::TraceControls>(std::move(trace_hooks));
  network_archive_->SetReservation([this](int context) {
    suitecut::NativeNetworkArchives::Publication publication;
    const auto lease = traces_->SessionLease(context);
    const auto options = traces_->Configuration(context);
    if (!lease.context || !options || !options->dom) return publication;
    publication.snapshot = [traces = traces_, lease](Dict event) {
      auto produced = event->Copy(false);
      auto snapshot = produced->GetDictionary("snapshot");
      auto response = snapshot ? snapshot->GetDictionary("response") : nullptr;
      auto content = response ? response->GetDictionary("content") : nullptr;
      if (content) {
        if (content->GetType("_sha1") == VTYPE_STRING)
          content->SetString("_file", "resources/" + content->GetString("_sha1").ToString());
        content->Remove("text");
        content->Remove("encoding");
      }
      traces->Network(lease, produced);
    };
    publication.resource = [traces = traces_, lease](const std::string& path,
                                                     const std::vector<uint8_t>& bytes) {
      traces->Resource(lease, path, bytes);
    };
    publication.capture_body = [](Dict request, Dict response) {
      auto type = request->GetString("resourceType").ToString();
      std::transform(type.begin(), type.end(), type.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if (type == "script") return false;
      auto headers = response->GetDictionary("headers");
      if (headers) {
        CefDictionaryValue::KeyList keys;
        headers->GetKeys(keys);
        for (const auto& key : keys) {
          auto name = key.ToString();
          std::transform(name.begin(), name.end(), name.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          if (name == "content-type") {
            auto value = headers->GetString(key).ToString();
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (value.find("javascript") != std::string::npos ||
                value.find("ecmascript") != std::string::npos)
              return false;
          }
        }
      }
      return true;
    };
    return publication;
  });
  const auto background = [this](std::function<void()> work) {
    return trace_background_.Submit(std::move(work));
  };
  const auto owner = [](std::function<void()> work) {
    return CefPostTask(TID_UI, new Task(std::move(work)));
  };
  suitecut::ScreenshotComparisons::Hooks screenshot_hooks;
  screenshot_hooks.now = [] { return Now(); };
  screenshot_hooks.background = background;
  screenshot_hooks.post = owner;
  screenshot_hooks.wake = [this, alive = alive_] {
    if (*alive) {
      WakeTrace();
      Idle();
    }
  };
  screenshot_hooks.available = [this, alive = alive_] {
    return *alive &&
           (!screenshot_inputs_ ||
            screenshot_inputs_->AdmissionCount() + screenshot_comparisons_->LogicalCount() < 16);
  };
  screenshot_hooks.physical_available = [this, alive = alive_] {
    return *alive &&
           (!screenshot_inputs_ ||
            screenshot_comparisons_->PhysicalCount() + screenshot_inputs_->PhysicalCount() < 2);
  };
  screenshot_hooks.settled_context = [this, alive = alive_](int context) {
    if (*alive) contexts_->ContinueCloseContext(context);
  };
  screenshot_comparisons_ =
      std::make_shared<suitecut::ScreenshotComparisons>(std::move(screenshot_hooks));
  suitecut::ScreenshotInputs::Hooks input_hooks;
  input_hooks.now = [] { return Now(); };
  input_hooks.background = background;
  input_hooks.post = owner;
  input_hooks.wake = [this, alive = alive_] {
    if (*alive) {
      WakeTrace();
      Idle();
    }
  };
  input_hooks.settled = [this, alive = alive_](int context) {
    if (*alive) contexts_->ContinueCloseContext(context);
  };
  input_hooks.available = [this, alive = alive_] {
    return *alive &&
           screenshot_inputs_->AdmissionCount() + screenshot_comparisons_->LogicalCount() < 16;
  };
  input_hooks.physical_available = [this, alive = alive_] {
    return *alive &&
           screenshot_inputs_->PhysicalCount() + screenshot_comparisons_->PhysicalCount() < 2;
  };
  screenshot_inputs_ = std::make_shared<suitecut::ScreenshotInputs>(
      screenshot_comparisons_->Budget(), std::move(input_hooks));
  suitecut::TracePreview::Hooks preview_hooks;
  preview_hooks.background = background;
  preview_hooks.owner = owner;
  preview_hooks.wake = [this] {
    WakeTrace();
    Idle();
  };
  preview_hooks.error = [this](suitecut::TraceJournal::Lease lease, const std::string& error) {
    traces_->CaptureError(lease, error);
  };
  preview_hooks.captured = [this](suitecut::TraceJournal::Lease lease, Dict event,
                                  const std::string& file, std::vector<uint8_t> bytes) {
    traces_->Captured(lease, event, file, std::move(bytes));
  };
  trace_preview_ = std::make_shared<suitecut::TracePreview>(std::move(preview_hooks));
  suitecut::TraceSources::Hooks source_hooks;
  source_hooks.background = background;
  source_hooks.owner = owner;
  source_hooks.wake = [this] {
    WakeTrace();
    Idle();
  };
  source_hooks.error = [this](suitecut::TraceJournal::Lease lease, const std::string& error) {
    traces_->CaptureError(lease, error);
  };
  source_hooks.captured = [this](suitecut::TraceJournal::Lease lease, const std::string& file,
                                 std::vector<uint8_t> bytes) {
    traces_->SourceResource(lease, file, std::move(bytes));
  };
  trace_sources_ = std::make_shared<suitecut::TraceSources>(std::move(source_hooks));
  context_har_ = std::make_unique<suitecut::NativeContextHARRoutes>(
      [this](const std::string& owner, const std::string& error) {
        HARPolicyFailure(owner, error);
      },
      [] { return Now(); });
  context_unroute_all_ = std::make_unique<suitecut::NativeContextUnrouteAll>(
      *context_routes_, *context_har_, [] { return Now(); });
  suitecut::NativeHARRecording::Hooks har_hooks;
  har_hooks.background = background;
  har_hooks.owner = owner;
  har_hooks.now = [] { return Now(); };
  har_hooks.available = [this] { return trace_background_.Available(); };
  har_hooks.wake = [this] {
    WakeTrace();
    Idle();
  };
  har_hooks.settled = [this](int context) {
    contexts_->ContinueCloseContext(context);
    Idle();
  };
  har_recording_ =
      std::make_shared<suitecut::NativeHARRecording>(*network_archive_, std::move(har_hooks));
  suitecut::NativeContextVideo::Hooks video_hooks;
  video_hooks.background = background;
  video_hooks.owner = owner;
  video_hooks.now = [] { return Now(); };
  video_hooks.wake = [this] {
    WakeTrace();
    Idle();
  };
  video_hooks.settled = [this](int context) {
    contexts_->ContinueCloseContext(context);
    Idle();
  };
  video_hooks.start = [this](int page, int context, suitecut::MediaPipelineOptions options) {
    return recordings_->StartAutomatic(page, context, std::move(options));
  };
  context_video_ = std::make_shared<suitecut::NativeContextVideo>(std::move(video_hooks));
  suitecut::NativeHARReplayFiles::Hooks replay_hooks;
  replay_hooks.background = background;
  replay_hooks.owner = owner;
  replay_hooks.now = [] { return Now(); };
  replay_hooks.wake = [this] {
    WakeTrace();
    Idle();
  };
  har_files_ = std::make_shared<suitecut::NativeHARReplayFiles>(std::move(replay_hooks));
  suitecut::HTTPTraceControls::Hooks api_trace_hooks;
  api_trace_hooks.raw_directory =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-traces-dir").ToString();
  api_trace_hooks.now = [] { return Now(); };
  api_trace_hooks.context_exists = [this](int context) { return http_->ContextExists(context); };
  api_trace_hooks.background = background;
  api_trace_hooks.owner = owner;
  api_trace_hooks.wake = [this] {
    WakeTrace();
    Idle();
  };
  http_->SetTracing(std::make_shared<suitecut::HTTPTraceControls>(std::move(api_trace_hooks)));
}
void Engine::AuthenticationFatal(int context, const std::string& error) {
  if (!authentication_.Get(context)) return;
  authentication_.Closing(context);
  auto& failure = authentication_failures_[context];
  if (!failure) {
    auto reason = std::make_shared<std::string>();
    failure = std::make_shared<suitecut::NativeNetworkFatalTeardown>(
        [alive = alive_](std::function<void()> task) {
          return CefPostTask(TID_UI, new Task([alive, task = std::move(task)] {
                               if (*alive) task();
                             }));
        },
        [reason](const std::string& error) { *reason = error; },
        [this, alive = alive_, context, reason] {
          if (!*alive) return;
          auto close = CefDictionaryValue::Create();
          close->SetInt("contextId", context);
          close->SetString("reason", *reason);
          contexts_->Handle("close", close, [](bool, Dict, const std::string&) {});
        });
  }
  failure->Fail(error);
  WakeTrace();
}
void Engine::HARPolicyFailure(const std::string& owner, const std::string& error) {
  for (const auto& [page, source] : sources_)
    if (owner == "page:" + std::to_string(page)) {
      source->NetworkFatal(error);
      return;
    }
  if (!workers_->NetworkPolicyFailure(owner, error)) {
    writer_.Error(error.c_str());
    CloseAll();
  }
}
std::vector<CefRefPtr<Source>> Engine::OwnerFrameSources(int page) const {
  std::vector<CefRefPtr<Source>> result;
  const int context = contexts_->ContextId(page);
  for (const auto& [id, source] : sources_)
    if (contexts_->ContextId(id) == context && source->browser_ && !source->closing_page_)
      result.push_back(source);
  return result;
}
bool Engine::TracePending(int context) const {
  const auto session = traces_->SessionLease(context);
  if (session.context && network_archive_->pending(context, session.session)) return true;
  if (trace_preview_->HasPendingForContext(context) || trace_sources_->Pending(context))
    return true;
  for (const auto& [page, source] : sources_)
    if (contexts_->ContextId(page) == context &&
        (!source->native_trace_protocol_.empty() ||
         (source->native_trace_phases_ && source->native_trace_phases_->Pending(context))))
      return true;
  return false;
}
void Engine::TracePaint(int page, int width, int height, double now, const void* pixels,
                        int pixel_width, int pixel_height) {
  const int context = contexts_->ContextId(page);
  const auto lease = traces_->CaptureLease(context);
  const auto config = traces_->Configuration(context);
  if (lease.context && config && config->screenshots)
    trace_preview_->Paint(lease, page, width, height, now, pixels, pixel_width, pixel_height);
}
void Engine::WakeTrace() {
  if (!deadline_timer_active_) {
    deadline_timer_active_ = true;
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
}
Engine::~Engine() {
  *alive_ = false;
#if defined(__linux__)
  if (native_work_wake_) native_work_wake_->Stop();
#endif
  for (const auto& [context, failure] : authentication_failures_) {
    (void)context;
    failure->Closed();
  }
  authentication_failures_.clear();
  context_unroute_all_->Clear();
  context_routes_->Clear();
  context_har_->Clear();
  har_files_->Clear();
  har_recording_->Clear();
  context_video_->Clear();
  video_preparations_.clear();
  screenshot_inputs_->Clear();
  traces_->Clear();
  trace_preview_->Clear();
  trace_sources_->Clear();
  workers_->Clear();
  context_console_->Clear();
  context_network_->Clear();
  worker_network_->Clear();
  network_archive_->Clear();
  network_policy_.Clear();
  network_headers_.Clear();
  authentication_.Clear();
  if (http_) http_->Clear();
  if (downloads_) downloads_->Clear();
  if (reader_source_) reader_source_->StopReader();
  sources_.clear();
  reader_source_ = nullptr;
  contexts_.reset();
}
#if defined(OS_LINUX) || defined(__APPLE__)
bool Engine::SelectStreamingSource(const std::string& stream, int page, std::string& error) {
  const auto found = sources_.find(page);
  if (found == sources_.end() || found->second->closing_page_ || !found->second->browser_ ||
      !found->second->ready_) {
    error = "Native stream source is not an active page in this Engine";
    return false;
  }
  auto source = found->second;
  try {
    streaming_->Select(stream, page, contexts_->ContextId(page), source->capture_width_,
                       source->capture_height_, source->i420_);
    source->EmitFrame();
    source->browser_->GetHost()->Invalidate(PET_VIEW);
    return true;
  } catch (const std::exception& failure) {
    error = std::string(failure.what()).substr(0, 4096);
    return false;
  }
}
#endif

}  // namespace suitecut::browser
