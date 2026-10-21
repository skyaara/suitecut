// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::NativeStartContextHeaders() -> void {
  if (closing_page_ || !browser_) return;
  if (!engine_.network_headers().Attach(
          engine_.contexts()->ContextId(page_id_), "page:" + std::to_string(page_id_),
          [self = CefRefPtr<Source>(this)](Dict headers,
                                           suitecut::NativeContextNetworkHeaders::Result done) {
            if (self->closing_page_ || !self->network_) {
              done(false, "Native HTTP headers target is closed");
              return;
            }
            auto params = CefDictionaryValue::Create();
            params->SetString("action", "setContextExtraHTTPHeaders");
            params->SetDictionary("headers", headers->Copy(false));
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
              if (self->browser_) self->browser_->GetHost()->CloseBrowser(true);
              return;
            }
            self->NativeStartContextHAR();
          })) {
    Error("Native context HTTP headers target capacity exceeded");
    browser_->GetHost()->CloseBrowser(true);
  }
}

auto Source::NativeStartContextHAR() -> void {
  if (closing_page_ || !browser_) return;
  if (!engine_.har_routes().Attach(
          engine_.contexts()->ContextId(page_id_), "page:" + std::to_string(page_id_),
          [self = CefRefPtr<Source>(this)](suitecut::NativeContextHARRoutes::Mutation mutation,
                                           const suitecut::NativeContextHARRoutes::Route& route,
                                           double deadline,
                                           suitecut::NativeContextHARRoutes::Result done) {
            if (self->closing_page_ || !self->network_) {
              done(false, "Actual Source HAR policy target is closed");
              return;
            }
            if (mutation == suitecut::NativeContextHARRoutes::Mutation::Install)
              self->network_->BindReplayPolicy(route.id, route.pattern, route.policy,
                                               std::move(done), deadline, true);
            else
              self->network_->UnbindReplayPolicy(route.id, route.policy, std::move(done), deadline);
          },
          [self = CefRefPtr<Source>(this)](bool ok, const std::string& error) {
            if (self->closing_page_) return;
            if (!ok) {
              self->NetworkFatal(error);
              return;
            }
            self->NativeStartContextRoutes();
          }))
    NetworkFatal("Native Source context HAR policy capacity exceeded");
}

auto Source::ContextWorkerNetworkEvent(Dict event) -> bool {
  auto request =
      event ? (event->GetString("event") == "routematch" ? event : event->GetDictionary("request"))
            : nullptr;
  if (!request) return false;
  const auto worker = request->GetDictionary("workerInfo");
  if (!worker || !request->GetBool("contextWorker")) return false;
  engine_.ContextWorkerNetwork(network_context_id_, "page:" + std::to_string(page_id_), worker,
                               event);
  return true;
}

auto Source::NativeStartPrivateResponseSession() -> void {
  private_response_ = std::make_unique<suitecut::NativePrivateResponseSession>(
      [this](const std::string& method, Dict params, double deadline,
             suitecut::NativePrivateResponseSession::Reply done) {
        NativeNetworkSend("", method, params, std::move(done), deadline);
      },
      [] { return Now(); }, [this] { WakeDeadlines(); },
      [this](const std::string& error) { NetworkFatal(error); });
  if (!private_response_->Start(Now() + 30000, [this](bool ok, const std::string& error) {
        if (closing_page_) return;
        private_response_ready_ = true;
        if (!ok) private_response_error_ = error.substr(0, 512);
        MaybeReady();
      })) {
    private_response_.reset();
    private_response_ready_ = true;
    private_response_error_ = "Private body session reservation capacity exceeded";
  }
}

auto Source::NativePrivateBodyFallback(bool actual_ok, const std::string& actual_json) -> bool {
  if (!actual_ok) return actual_json.size() <= 65536;
  const auto parsed = CefParseJSON(actual_json, JSON_PARSER_RFC);
  const auto value = parsed ? parsed->GetDictionary() : nullptr;
  return value && value->GetType("body") == VTYPE_STRING &&
         value->GetType("base64Encoded") == VTYPE_BOOL && value->GetString("body").empty();
}

auto Source::NativePrimaryBodyError(int owner, const std::string& json) -> std::string {
  const auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
  const auto response = parsed ? parsed->GetDictionary() : nullptr;
  std::string error = response && response->GetType("message") == VTYPE_STRING
                          ? response->GetString("message").ToString()
                          : "Native DevTools command failed";
  const auto read = body_waiters_.find(owner);
  if (read != body_waiters_.end() && !read->second.loader.empty()) {
    const auto frame = frame_states_.find(read->second.frame);
    if (frame != frame_states_.end() && !frame->second.loader.empty() &&
        frame->second.loader != read->second.loader)
      error += " (response frame has navigated away)";
  }
  return error;
}

auto Source::NativeImageStreamEvent(const std::string& method, Dict actual,
                                    const std::string& session) -> void {
  if (!image_streams_)
    image_streams_ = std::make_unique<suitecut::NativeResponseStreams>(
        [this](const std::string& session, const std::string& request, double deadline,
               suitecut::NativeResponseStreams::Result done) {
          auto params = CefDictionaryValue::Create();
          params->SetString("requestId", request);
          NativeNetworkSend(session, "Network.streamResourceContent", params, std::move(done),
                            deadline);
        },
        [] { return Now(); }, [this] { WakeDeadlines(); });
  image_streams_->Event(method, actual, session);
}

auto Source::NativeReadResponseBody(const std::string& session, const std::string& request,
                                    double deadline, suitecut::NativeResponseBodyReads::Reply reply)
    -> void {
  if (closing_page_ || !browser_) {
    reply(false, {}, "Actual native response-body Source is closed");
    return;
  }
  if (!response_reads_)
    response_reads_ = std::make_unique<suitecut::NativeResponseBodyReads>(
        [this](const std::string& target, const std::string& request, double original,
               suitecut::NativeResponseBodyReads::Result done) {
          if (closing_page_ || !browser_) {
            done(false, "Actual native response-body Source is closed");
            return;
          }
          const int id = AllocateDevToolsId();
          if (!id || network_calls_.size() >= 1024) {
            done(false, "Native response-body command capacity exceeded");
            return;
          }
          const double physical_deadline = original ? original : Now() + 30000;
          network_calls_.emplace(
              id, NetworkCall{
                      target,
                      [this, target, request, physical_deadline, done = std::move(done)](
                          bool ok, const std::string& json) {
                        const auto result = image_streams_
                                                ? image_streams_->Resolve(target, request, ok, json)
                                                : std::pair<bool, std::string>{ok, json};
                        if (target.empty() && private_response_ &&
                            NativePrivateBodyFallback(result.first, result.second) &&
                            private_response_->Read(
                                request, physical_deadline,
                                [this, done, result](bool recovered, const std::string& actual) {
                                  if (closing_page_ || !browser_) {
                                    done(false, closed_reason_.empty() ? "Native page is closed"
                                                                       : closed_reason_);
                                    return;
                                  }
                                  done(recovered ? true : result.first,
                                       recovered ? actual : result.second);
                                }))
                          return;
                        done(result.first, result.second);
                      },
                      physical_deadline});
          WakeDeadlines();
          auto params = CefDictionaryValue::Create();
          params->SetString("requestId", request);
          if (!SubmitDevTools(id, "Network.getResponseBody", params, target)) {
            const auto found = network_calls_.find(id);
            if (found != network_calls_.end()) {
              auto callback = std::move(found->second.callback);
              network_calls_.erase(found);
              callback(false, "CEF rejected actual response-body read");
            }
          }
        },
        [] { return Now(); }, [this] { WakeDeadlines(); });
  // HAR owns logical timeout. This callback settles only on actual CDP ACK
  // or real channel close, preserving native physical-read accounting.
  response_reads_->Read(session, request, deadline, std::move(reply));
}

auto Source::NativeReadRequestBody(const std::string& session, const std::string& request,
                                   double deadline, suitecut::NativeRequestBodyReads::Reply reply,
                                   suitecut::NativeRequestBodyReads::Receipt receipt) -> void {
  if (closing_page_ || !browser_) {
    if (receipt) receipt();
    return;
  }
  if (!request_reads_)
    request_reads_ = std::make_unique<suitecut::NativeRequestBodyReads>(
        [this](const std::string& session, const std::string& request, double original,
               suitecut::NativeRequestBodyReads::Result done) {
          auto params = CefDictionaryValue::Create();
          params->SetString("requestId", request);
          NativeNetworkSend(session, "Network.getRequestPostData", params, std::move(done),
                            original);
        },
        [] { return Now(); }, [this] { WakeDeadlines(); });
  request_reads_->Read(session, request, deadline, std::move(reply), std::move(receipt));
}

auto Source::Descendant(const std::string& child, const std::string& root) -> bool {
  auto id = child;
  for (size_t step = 0; step <= frame_states_.size(); ++step) {
    if (id == root) return true;
    const auto frame = frame_states_.find(id);
    if (frame == frame_states_.end() || frame->second.parent.empty()) return false;
    id = frame->second.parent;
  }
  return false;
}

auto Source::NetworkActivity(const std::string& id) -> void {
  auto current = id;
  for (size_t step = 0; step <= frame_states_.size(); ++step) {
    const auto frame = frame_states_.find(current);
    if (frame == frame_states_.end()) break;
    frame->second.network_activity = Now();
    current = frame->second.parent;
    if (current.empty()) break;
  }
}

auto Source::NativeNetworkSend(const std::string& session, const std::string& method, Dict params,
                               suitecut::NetworkControls::Result callback, double deadline)
    -> void {
  const int id = AllocateDevToolsId();
  if (!id || network_calls_.size() >= 1024) {
    callback(false, "Native network command capacity exceeded");
    return;
  }
  if (deadline && Now() >= deadline) {
    callback(false, "Native network original command deadline exceeded");
    return;
  }
  network_calls_.emplace(
      id, NetworkCall{session, suitecut::BindNativeLocatorHandlerAsync(std::move(callback)),
                      deadline ? deadline : Now() + 30000});
  WakeDeadlines();
  if (!SubmitDevTools(id, method, params, session)) {
    const auto found = network_calls_.find(id);
    if (found != network_calls_.end()) {
      auto done = std::move(found->second.callback);
      network_calls_.erase(found);
      done(false, "Native network command dispatch failed");
    }
  }
}

auto Source::NativeFrameRequestSession(const std::string& request, const std::string& previous,
                                       const std::string& session) -> void {
  if (image_streams_ && !image_streams_->Move(previous, session, request)) {
    image_streams_->Discard(previous, request);
    image_streams_->Discard(session, request);
  }
  const auto old_key = previous + "\n" + request, new_key = session + "\n" + request;
  const auto original = documents_.find(old_key);
  if (documents_.contains(new_key)) {
    NetworkFatal("Conflicting native frame response session handoff");
    return;
  }
  if (original != documents_.end()) {
    auto document = documents_.extract(original);
    document.key() = new_key;
    document.mapped().session = session;
    documents_.insert(std::move(document));
  }
  for (auto& key : document_order_)
    if (key == old_key) key = new_key;
  auto active = active_requests_.extract(old_key);
  if (!active.empty()) {
    if (active_requests_.contains(new_key)) {
      NetworkFatal("Conflicting native frame active request handoff");
      return;
    }
    active.key() = new_key;
    active_requests_.insert(std::move(active));
  }
  for (auto& [owner, wait] : body_waiters_)
    if (wait.key == old_key) wait.key = new_key;
}

auto Source::NativeWorkerInitialRequestSession(const std::string& target, const std::string& parent,
                                               const std::string& child, const std::string& url)
    -> void {
  if (target.empty() || child.empty()) return;
  const auto old_key = parent + "\n" + target, new_key = child + "\n" + target;
  const auto original = documents_.find(old_key);
  if (original == documents_.end() || original->second.finished ||
      original->second.request != target || original->second.session != parent ||
      original->second.url != url || documents_.contains(new_key))
    return;
  auto document = documents_.extract(original);
  document.key() = new_key;
  document.mapped().session = child;
  documents_.insert(std::move(document));
  for (auto& key : document_order_)
    if (key == old_key) key = new_key;
  auto active = active_requests_.extract(old_key);
  if (!active.empty()) {
    if (active_requests_.contains(new_key)) {
      Error("Conflicting native worker script request ownership");
      return;
    }
    active.key() = new_key;
    active_requests_.insert(std::move(active));
  }
}

auto Source::AttributeInitialPopupRequest(Dict data) -> void {
  if (ready_ || popup_initial_navigation_observed_ || !engine_.contexts()->PopupOpener(page_id_) ||
      data->GetString("type") != "Document")
    return;
  popup_initial_navigation_observed_ = true;
  data->SetBool("suitecutNavigationFrameUnavailable", true);
}

auto Source::NetworkEvent(const std::string& method, Dict data, const std::string& session)
    -> void {
  const auto request = data->GetString("requestId").ToString();
  if (request.empty() || request.size() > 256) return;
  const auto key = session + "\n" + request;
  if (method == "Network.requestWillBeSent") {
    const auto frame = data->GetString("frameId").ToString();
    if (active_requests_.size() >= 8192 && !active_requests_.contains(key)) {
      Error("Native active network request limit exceeded");
      return;
    }
    active_requests_[key] = {frame, Now()};
    NetworkActivity(frame);
    {
      auto info = data->GetDictionary("request");
      if (!info) return;
      if (!documents_.contains(key) && documents_.size() >= 1024) {
        auto oldest = std::find_if(document_order_.begin(), document_order_.end(),
                                   [this](const std::string& candidate) {
                                     const auto found = documents_.find(candidate);
                                     return found != documents_.end() && found->second.finished;
                                   });
        if (oldest == document_order_.end()) {
          Error("Native document response limit exceeded");
          return;
        }
        documents_.erase(*oldest);
        document_order_.erase(oldest);
      }
      if (!documents_.contains(key)) document_order_.push_back(key);
      documents_[key] = {frame,
                         request,
                         session,
                         data->GetString("loaderId").ToString(),
                         info->GetString("method").ToString(),
                         info->GetString("url").ToString(),
                         nullptr,
                         false,
                         "",
                         data->GetString("type") == "Document"};
    }
  } else if (method == "Network.responseReceived") {
    const auto document = documents_.find(key);
    if (document != documents_.end()) {
      auto response = data->GetDictionary("response");
      // This nested dictionary is owned by the event payload. Retain an
      // independent copy after the callback's parent value is destroyed.
      document->second.response = response ? response->Copy(false) : nullptr;
    }
  } else {
    const auto active = active_requests_.find(key);
    if (active != active_requests_.end()) {
      NetworkActivity(active->second.frame);
      active_requests_.erase(active);
    }
    const auto document = documents_.find(key);
    if (document != documents_.end()) {
      document->second.finished = true;
      if (method == "Network.loadingFailed") {
        document->second.error = data->GetString("errorText").ToString();
        std::vector<int> failed;
        for (const auto& [id, nav] : navigations_)
          if (document->second.document && nav.frame == document->second.frame &&
              (!nav.dispatched || nav.loader == document->second.loader))
            failed.push_back(id);
        for (const int id : failed)
          DownloadNavigationAbort(id, document->second.error, document->second.url);
      }
    }
  }
  CheckNavigations();
  CheckBodies();
}

}  // namespace suitecut::browser
