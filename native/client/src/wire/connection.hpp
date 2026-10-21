// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include "suitecut/transport.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace suitecut::detail {
using Deadline = std::chrono::steady_clock::time_point;
// Owns Chrome and its private CDP pipe. Exactly one persistent I/O thread;
// no network listener, Node process, or per-command thread.
class CdpConnection {
 public:
  static std::unique_ptr<CdpConnection> launch(const LaunchOptions&);
  // Generic JSON-RPC WebSocket transport (e.g. WebDriver BiDi), with no CDP
  // Browser.getVersion probe or browser-specific URL path requirement.
  static std::unique_ptr<CdpConnection> connect_websocket(const LaunchOptions&);
  ~CdpConnection();
  Json call(const std::string& method, Json params, const std::string& session, Deadline);
  std::optional<Json> poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
  void close();
  ClientStats stats() const;
  void set_event_handler(std::function<void(const Json&)>);
  // True marks only an actively consumed Page.screencastFrame as disposable
  // public telemetry; other lifecycle/control events remain protected.
  void set_event_handler_with_disposition(std::function<bool(const Json&)>);
 private:
  struct Impl;
  explicit CdpConnection(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace suitecut::detail
