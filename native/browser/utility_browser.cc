// SPDX-License-Identifier: MIT
#include "utility_browser.h"
#include "locator_handler_async_work.h"
#include "accepted_task_work.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>
#include <utility>
#include <vector>
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_resource_handler.h"
#include "include/cef_task.h"
namespace {
class UtilityTask : public CefTask {
 public:
  explicit UtilityTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override {const suitecut::AcceptedTaskFrame native_frame(native_work_);fn_();}
 private:
  suitecut::AcceptedTaskReceipt native_work_; // Captures below destroy first.
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(UtilityTask);
};
class EmptyOriginDocument : public CefResourceHandler {
 public:
  bool Open(CefRefPtr<CefRequest>, bool& handle_request, CefRefPtr<CefCallback>) override { handle_request = true; return true; }
  void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t& length, CefString&) override {
    response->SetStatus(200); response->SetStatusText("OK"); response->SetMimeType("text/html");
    CefResponse::HeaderMap headers; headers.emplace("Cache-Control", "no-store");
    headers.emplace("Content-Security-Policy", "default-src 'none'"); response->SetHeaderMap(headers); length = body_.size();
  }
  bool Read(void* data, int capacity, int& read, CefRefPtr<CefResourceReadCallback>) override {
    read = static_cast<int>(std::min<size_t>(std::max(capacity, 0), body_.size() - offset_));
    if (!read) return false;
    std::memcpy(data, body_.data() + offset_, read); offset_ += read; return true;
  }
  void Cancel() override {}
 private:
  const std::string body_ = "<!doctype html><html><head><meta charset=utf-8></head><body></body></html>";
  size_t offset_ = 0;
  IMPLEMENT_REFCOUNTING(EmptyOriginDocument);
};
}
struct SuiteCutUtilityBrowsers::Lease {
  CefRefPtr<Client> client; Ready ready; double deadline = 0;
};
class SuiteCutUtilityBrowsers::Client : public CefClient, public CefLifeSpanHandler,
    public CefRenderHandler, public CefLoadHandler, public CefRequestHandler,
    public CefResourceRequestHandler, public CefDevToolsMessageObserver {
 public:
  Client(std::weak_ptr<SuiteCutUtilityBrowsers> owner, int id, CefRefPtr<CefRequestContext> context,
      std::string origin, HTTPObserver observer)
      : owner_(std::move(owner)), id_(id), context_(context), origin_(std::move(origin)), observer_(std::move(observer)) {if(auto owner=owner_.lock())inspector_work_=owner->inspector_work_;}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, 1, 1); }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void*, int, int) override {}
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;NativeInspectorOpened(inspector_work_,browser->GetIdentifier(),suitecut::AcceptedInspectorWork::Kind::Utility);
    const auto native_inspector_callback=NativeInspectorCallback();
    registration_ = browser->GetHost()->AddDevToolsMessageObserver(this);
    if (closing_) { Close(); return; }
    const int call = ++next_call_;
    calls_.emplace(call, Call{0, Now() + 30000, [weak = owner_, client_id = id_](bool ok, Dict, const std::string&) {
      const auto owner = weak.lock(); if (!owner) return;
      const auto found = owner->clients_.find(client_id); if (found == owner->clients_.end()) return;
      const auto client = found->second; if (client->closing_ || !client->browser_) return;
      if (!ok) { client->Close(); return; }
      client->BypassServiceWorkers();
    }});
    if (!NativeUtilityExecute(call,"Network.enable",nullptr)) { calls_.erase(call); Close(); }
    if (const auto owner = owner_.lock()) owner->Wake();
  }
  void BypassServiceWorkers() {
    auto params = CefDictionaryValue::Create(); params->SetBool("bypass", true);
    const int call = ++next_call_;
    calls_.emplace(call, Call{0, Now() + 30000, [weak = owner_, client_id = id_](bool ok, Dict, const std::string&) {
      const auto owner = weak.lock(); if (!owner) return;
      const auto found = owner->clients_.find(client_id); if (found == owner->clients_.end()) return;
      const auto client = found->second; if (client->closing_ || !client->browser_) return;
      if (!ok) { client->Close(); return; }
      client->bypass_ready_ = true;
      if (client->origin_ != "about:blank") client->browser_->GetMainFrame()->LoadURL(client->origin_);
      else if (client->document_loaded_) { client->ready_ = true; owner->Loaded(client.get()); }
    }});
    if (!NativeUtilityExecute(call,"Network.setBypassServiceWorker",params)) { calls_.erase(call); Close(); }
    if (const auto owner = owner_.lock()) owner->Wake();
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
      CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
      CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnBeforeClose(CefRefPtr<CefBrowser> actual_browser) override {
    const auto native_inspector_callback=NativeInspectorCallback();
    closing_ = true; std::vector<Result> results;
    for (auto& [id, call] : calls_) results.push_back(std::move(call.result));
    calls_.clear(); ClearResults(); registration_ = nullptr; browser_ = nullptr;
    if (const auto owner = owner_.lock()) owner->BrowserClosed(this);
    for (auto& result : results) result(false, nullptr, "Native utility browser closed during evaluation");
    NativeInspectorClosed(actual_browser->GetIdentifier());
  }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int) override {
    if (closing_ || !frame->IsMain()) return;
    const auto url = frame->GetURL().ToString();
    const auto canonical = origin_ == "about:blank" ? origin_ : origin_.ends_with('/') ? origin_ : origin_ + "/";
    if (url != origin_ && url != canonical) return;
    document_loaded_ = true;
    if (!bypass_ready_) return;
    ready_ = true; if (const auto owner = owner_.lock()) owner->Loaded(this);
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString& text, const CefString&) override {
    if (closing_ || !frame->IsMain() || code == ERR_ABORTED) return;
    const auto owner = owner_.lock(); if (!owner) return;
    std::vector<std::pair<int, Ready>> replies;
    for (auto& [id, lease] : owner->leases_) if (lease->client.get() == this && lease->ready) replies.emplace_back(id, std::move(lease->ready));
    for (auto& [id, reply] : replies) { owner->Release(id); reply(nullptr, "Native utility origin document failed: " + text.ToString()); }
  }
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefRequest> request, bool, bool, const CefString&, bool& disable_default) override {
    // Correlated API requests use actual CEF transport and its raw header
    // observer. Utility document requests are always intercepted locally.
    if (observer_) if (auto observer = observer_(request)) return observer;
    disable_default = true; return this;
  }
  CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>) override {
    return new EmptyOriginDocument();
  }
  bool OnDevToolsMessage(CefRefPtr<CefBrowser>, const void*, size_t) override { return false; }
  struct DeferredResult { bool success; std::string json, error; };
  std::map<int, DeferredResult> completed_;
  bool completion_task_ = false;
  void DropResult(int id) {
    const auto found = completed_.find(id); if (found == completed_.end()) return;
    if (const auto owner = owner_.lock()) owner->deferred_result_bytes_ -= found->second.json.size();
    completed_.erase(found);
  }
  void ClearResults() { while (!completed_.empty()) DropResult(completed_.begin()->first); }
  void WakeCompletions() {
    if (completion_task_ || completed_.empty() || closing_) return;
    completion_task_ = true;
    CefPostTask(TID_UI, new UtilityTask([self = CefRefPtr<Client>(this)] {
      // Result consumers may navigate or close this browser. Chromium notifies
      // DevTools observers synchronously; deliver after that iteration returns.
      std::vector<int> ready; for (const auto& [id, result] : self->completed_) ready.push_back(id);
      for (const int id : ready) {
        const auto pending = self->completed_.find(id); if (pending == self->completed_.end()) continue;
        auto response = std::move(pending->second); self->completed_.erase(pending);
        if (const auto owner = self->owner_.lock()) owner->deferred_result_bytes_ -= response.json.size();
        const auto found = self->calls_.find(id);
        if (found == self->calls_.end() || self->closing_) continue;
        auto callback = std::move(found->second.result); self->calls_.erase(found);
        if (!response.error.empty()) { callback(false, nullptr, response.error); continue; }
        const auto parsed = CefParseJSON(CefString(response.json), JSON_PARSER_RFC);
        auto output = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
        if (!response.success || !output) { callback(false, nullptr, "Native utility evaluation failed"); continue; }
        const auto exception = output->GetDictionary("exceptionDetails");
        if (exception) { callback(false, nullptr, CefWriteJSON(parsed, JSON_WRITER_DEFAULT).ToString().substr(0, 4096)); continue; }
        callback(true, output, "");
      }
      self->completion_task_ = false; self->WakeCompletions();
    }));
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* result, size_t size) override {
    const auto native_inspector_callback=NativeInspectorCallback();
    const suitecut::NativeWorkOriginFrame native_origin(NativeInspectorReplyOrigin(id,""));NativeInspectorReply(id,"");
    if (closing_ || !calls_.contains(id) || completed_.contains(id)) return;
    const auto owner = owner_.lock(); if (!owner) return;
    DeferredResult output{success, {}, {}};
    if (size > 4 * 1048576) output.error = "Native utility evaluation response exceeds 4 MiB";
    else if (size > 32 * 1048576 - owner->deferred_result_bytes_) output.error = "Native utility deferred result bytes capacity exceeded";
    else { output.json.assign(static_cast<const char*>(result), size); owner->deferred_result_bytes_ += size; }
    completed_.emplace(id, std::move(output)); WakeCompletions();
  }
  #include "controllers/work/accepted_inspector_owner.inc"
  bool NativeUtilityExecute(int id,const char* method,Dict params){
    const auto actual_browser=browser_;if(!actual_browser)return false;NativeInspectorReserve(id,"");
    return NativeInspectorCall(id,"",[actual_browser,id,method,params]{return actual_browser->GetHost()->ExecuteDevToolsMethod(id,method,params)>0;});
  }
  void Close() {
    closing_ = true; calls_.clear(); ClearResults();
    if (!browser_ || close_posted_) return;
    close_posted_ = true;
    // Cancellation can win before OnAfterCreated or inside a CEF callback.
    // Return from that callback before mutating Chromium's browser lifetime;
    // retain the real client/browser until the posted close and OnBeforeClose.
    CefPostTask(TID_UI, new UtilityTask([self = CefRefPtr<Client>(this)] {
      if (self->browser_) self->browser_->GetHost()->CloseBrowser(true);
    }));
  }
  bool close_posted_ = false;
  struct Call { int lease; double deadline; Result result; };
  std::weak_ptr<SuiteCutUtilityBrowsers> owner_; int id_;
  CefRefPtr<CefRequestContext> context_; std::string origin_; HTTPObserver observer_;
  CefRefPtr<CefBrowser> browser_; CefRefPtr<CefRegistration> registration_;
  bool ready_ = false, closing_ = false, bypass_ready_ = false, document_loaded_ = false; int next_call_ = 0;
  std::map<int, Call> calls_; std::vector<Closed> closed_;
  IMPLEMENT_REFCOUNTING(Client);
};
SuiteCutUtilityBrowsers::SuiteCutUtilityBrowsers(HTTPObserver observer, Closed idle,std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work) : observer_(std::move(observer)), idle_(std::move(idle)),inspector_work_(std::move(inspector_work)) {}
SuiteCutUtilityBrowsers::~SuiteCutUtilityBrowsers() = default;
double SuiteCutUtilityBrowsers::Now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool SuiteCutUtilityBrowsers::OriginURL(const std::string& origin) {
  if (origin == "about:blank") return true;
  CefURLParts parts; if (!CefParseURL(origin, parts)) return false;
  const auto scheme = CefString(&parts.scheme).ToString();
  return (scheme == "http" || scheme == "https") && !CefString(&parts.host).empty() &&
      CefString(&parts.username).empty() && CefString(&parts.password).empty() && origin.size() <= 8192;
}
int SuiteCutUtilityBrowsers::Acquire(CefRefPtr<CefRequestContext> context, const std::string& origin, Ready ready, int timeout_ms) {
  ready=suitecut::BindNativeLocatorHandlerAsync(std::move(ready));
  if (stopping_ || !context || leases_.size() >= 128 || next_lease_ == std::numeric_limits<int>::max() || !OriginURL(origin) || timeout_ms < 1 || timeout_ms > 120000) {
    ready(nullptr, "Invalid native utility browser acquisition or capacity exceeded"); return 0;
  }
  CefRefPtr<Client> client;
  if (origin == "about:blank") for (const auto& [id, candidate] : clients_) if (!candidate->closing_ && candidate->origin_ == origin && candidate->context_->IsSame(context)) { client = candidate; break; }
  bool create = !client;
  if (create) {
    if (clients_.size() >= 32 || next_client_ == std::numeric_limits<int>::max()) { ready(nullptr, "Native utility browser capacity exceeded"); return 0; }
    client = new Client(weak_from_this(), ++next_client_, context, origin, observer_); clients_[client->id_] = client;
  }
  const int id = ++next_lease_; auto lease = std::make_shared<Lease>(); lease->client = client; lease->deadline = Now() + timeout_ms;
  if (!client->ready_) lease->ready = std::move(ready);
  leases_[id] = lease;
  if (create) {
    CefWindowInfo window; window.SetAsWindowless(0); CefBrowserSettings settings; settings.windowless_frame_rate = 1;
    if (!CefBrowserHost::CreateBrowser(window, client, "about:blank", settings, nullptr, context)) {
      auto reply = std::move(lease->ready); leases_.erase(id); clients_.erase(client->id_);
      reply(nullptr, "CEF utility browser creation failed"); return 0;
    }
  }
  if (client->ready_) ready(client->browser_->GetMainFrame(), "");
  Wake(); return id;
}
void SuiteCutUtilityBrowsers::Loaded(Client* client) {
  std::vector<Ready> replies;
  for (auto& [id, lease] : leases_) if (lease->client.get() == client && lease->ready) replies.push_back(std::move(lease->ready));
  const auto frame = client->browser_->GetMainFrame(); for (auto& reply : replies) reply(frame, "");
}
void SuiteCutUtilityBrowsers::Navigate(int id, const std::string& origin, Ready ready, int timeout_ms) {
  ready=suitecut::BindNativeLocatorHandlerAsync(std::move(ready));
  const auto found = leases_.find(id);
  if (found == leases_.end() || found->second->ready || !OriginURL(origin) || timeout_ms < 1 || timeout_ms > 120000) { ready(nullptr, "Invalid native utility navigation"); return; }
  const auto client = found->second->client;
  if (client->closing_ || !client->browser_) { ready(nullptr, "Native utility browser is not ready"); return; }
  for (const auto& [other, lease] : leases_) if (other != id && lease->client == client) { ready(nullptr, "Cannot navigate a shared API utility browser"); return; }
  client->ready_ = false; client->document_loaded_ = false; client->origin_ = origin; found->second->ready = std::move(ready); found->second->deadline = Now() + timeout_ms;
  client->browser_->GetMainFrame()->LoadURL(origin); Wake();
}
void SuiteCutUtilityBrowsers::Evaluate(int lease_id, const std::string& expression, Result result, int timeout_ms) {
  result=suitecut::BindNativeLocatorHandlerAsync(std::move(result));
  const auto found = leases_.find(lease_id);
  if (found == leases_.end() || expression.size() > 1048576 || timeout_ms < 1 || timeout_ms > 120000) { result(false, nullptr, "Invalid native utility evaluation"); return; }
  const auto client = found->second->client;
  if (!client->ready_ || client->closing_ || !client->browser_ || client->calls_.size() >= 16 || client->next_call_ == std::numeric_limits<int>::max()) { result(false, nullptr, "Native utility evaluation capacity exceeded or browser unavailable"); return; }
  auto params = CefDictionaryValue::Create(); params->SetString("expression", expression); params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true);
  params->SetDouble("timeout", timeout_ms); const int id = ++client->next_call_;
  client->calls_.emplace(id, Client::Call{lease_id, Now() + timeout_ms, std::move(result)});
  if (!client->NativeUtilityExecute(id,"Runtime.evaluate",params)) {
    auto reply = std::move(client->calls_.at(id).result); client->calls_.erase(id); reply(false, nullptr, "CEF rejected native utility evaluation");
  }
  Wake();
}
void SuiteCutUtilityBrowsers::Release(int id, Closed closed) {
  closed=suitecut::BindNativeLocatorHandlerAsync(std::move(closed));
  const auto found = leases_.find(id); if (found == leases_.end()) { if (closed) closed(); return; }
  const auto client = found->second->client; leases_.erase(found);
  for (auto it = client->calls_.begin(); it != client->calls_.end();) if (it->second.lease == id) { client->DropResult(it->first); it = client->calls_.erase(it); } else ++it;
  for (const auto& [other, lease] : leases_) if (lease->client == client) { if (closed) closed(); return; }
  if (closed) client->closed_.push_back(std::move(closed));
  client->Close();
}
void SuiteCutUtilityBrowsers::BrowserClosed(Client* client) {
  std::vector<Ready> replies;
  for (auto it = leases_.begin(); it != leases_.end();) {
    if (it->second->client.get() == client) { if (it->second->ready) replies.push_back(std::move(it->second->ready)); it = leases_.erase(it); } else ++it;
  }
  auto closed = std::move(client->closed_); clients_.erase(client->id_);
  for (auto& reply : replies) reply(nullptr, "Native utility browser closed before becoming ready");
  for (auto& callback : closed) callback();
  if (clients_.empty() && idle_) idle_();
}
void SuiteCutUtilityBrowsers::Clear() {
  stopping_ = true; leases_.clear(); std::vector<CefRefPtr<Client>> clients;
  for (const auto& [id, client] : clients_) clients.push_back(client);
  for (const auto& client : clients) client->Close();
}
void SuiteCutUtilityBrowsers::Wake() {
  if (timer_ || stopping_) return;
  bool pending = false;
  for (const auto& [id, lease] : leases_) pending |= static_cast<bool>(lease->ready);
  for (const auto& [id, client] : clients_) pending |= !client->calls_.empty();
  if (!pending) return;
  timer_ = true; CefPostDelayedTask(TID_UI, new UtilityTask([weak = weak_from_this()] { if (const auto owner = weak.lock()) owner->Tick(); }), 10);
}
void SuiteCutUtilityBrowsers::Tick() {
  timer_ = false; const double now = Now(); std::vector<std::pair<int, Ready>> expired;
  for (auto& [id, lease] : leases_) if (lease->ready && lease->deadline <= now) expired.emplace_back(id, std::move(lease->ready));
  for (auto& [id, reply] : expired) { Release(id); reply(nullptr, "Native utility browser acquisition timed out"); }
  std::vector<Result> results;
  for (const auto& [id, client] : clients_) for (auto it = client->calls_.begin(); it != client->calls_.end();) {
    if (it->second.deadline <= now) { results.push_back(std::move(it->second.result)); client->DropResult(it->first); it = client->calls_.erase(it); } else ++it;
  }
  for (auto& reply : results) reply(false, nullptr, "Native utility evaluation timed out");
  Wake();
}
void SuiteCutUtilityBrowsers::Diagnostics(Dict output) const {
  output->SetInt("utilityBrowsers", static_cast<int>(clients_.size())); output->SetInt("utilityBrowserLeases", static_cast<int>(leases_.size()));
  size_t calls = 0, acquisitions = 0, deferred = 0;
  for (const auto& [id, client] : clients_) { calls += client->calls_.size(); deferred += client->completed_.size(); }
  for (const auto& [id, lease] : leases_) acquisitions += static_cast<bool>(lease->ready);
  output->SetInt("utilityDeferredResults", static_cast<int>(deferred)); output->SetDouble("utilityDeferredResultBytes", static_cast<double>(deferred_result_bytes_));
  output->SetInt("utilityEvaluations", static_cast<int>(calls)); output->SetInt("utilityAcquisitions", static_cast<int>(acquisitions));
}

bool SuiteCutUtilityBrowsers::AcceptedWorkReady()const{
  if(stopping_||deferred_result_bytes_)return false;
  for(const auto&[id,lease]:leases_){(void)id;if(lease->ready)return false;}
  for(const auto&[id,client]:clients_){(void)id;if(!client->browser_||!client->ready_||client->closing_||!client->calls_.empty()||!client->completed_.empty()||client->completion_task_||!client->closed_.empty())return false;}
  return true;
}
