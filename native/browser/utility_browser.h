// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include "accepted_inspector_work.h"
#include <map>
#include <memory>
#include <string>
#include "include/cef_browser.h"
#include "include/cef_request.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_values.h"

// Private UI-thread CEF browser leases. These browsers have no public page
// events, capture buffers, author documents, or shared disk profile.
class SuiteCutUtilityBrowsers : public std::enable_shared_from_this<SuiteCutUtilityBrowsers> {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Ready = std::function<void(CefRefPtr<CefFrame>, const std::string&)>;
  using Result = std::function<void(bool, Dict, const std::string&)>;
  using Closed = std::function<void()>;
  // Called on CEF's IO thread; the supplied observer must be thread-safe.
  using HTTPObserver = std::function<CefRefPtr<CefResourceRequestHandler>(CefRefPtr<CefRequest>)>;
  explicit SuiteCutUtilityBrowsers(HTTPObserver observer = {}, Closed idle = {},std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work = {});
  ~SuiteCutUtilityBrowsers();
  int Acquire(CefRefPtr<CefRequestContext> context, const std::string& origin, Ready ready, int timeout_ms = 30000);
  void Navigate(int lease, const std::string& origin, Ready ready, int timeout_ms = 30000);
  void Evaluate(int lease, const std::string& expression, Result result, int timeout_ms = 30000);
  void Release(int lease, Closed closed = {});
  void Clear();
  void Diagnostics(Dict output) const;
  bool AcceptedWorkReady()const;
  size_t BrowserCount() const { return clients_.size(); }
 private:
  class Client;
  struct Lease;
  void Loaded(Client* client);
  void BrowserClosed(Client* client);
  void Wake();
  void Tick();
  static double Now();
  static bool OriginURL(const std::string& origin);
  HTTPObserver observer_; Closed idle_;std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work_;
  std::map<int, std::shared_ptr<Lease>> leases_;
  std::map<int, CefRefPtr<Client>> clients_;
  size_t deferred_result_bytes_ = 0;
  int next_lease_ = 0, next_client_ = 0;
  bool timer_ = false, stopping_ = false;
};
