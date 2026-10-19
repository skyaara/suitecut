// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include "suitecut/transport.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace suitecut::detail {
using Deadline = std::chrono::steady_clock::time_point;
void cdp_check_active(Deadline);
// Permit only bounded input/state restoration after request cancellation.
struct CdpCleanupScope {
  CdpCleanupScope();
  ~CdpCleanupScope();
  CdpCleanupScope(const CdpCleanupScope&) = delete;
};
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
struct CdpPage {
  int id = 1, context = 0;
  std::string target, session, frame_id, isolated_loader, isolated_frame;
  std::string download_directory, download_frame;
  std::map<std::string, Json> downloads;
  std::map<std::string, std::string> frame_sessions;
  Json emulated_media = Json::object{};
  int isolated_context = 0;
  double mouse_x = 0, mouse_y = 0;
  int modifiers = 0, mouse_buttons = 0;
};
class CdpNetwork;
class CdpMedia;
// These maps and DOM state are confined to the single bounded command worker.
struct CdpRuntime {
  LaunchOptions options;
  std::unique_ptr<CdpConnection> wire;
  std::shared_ptr<CdpNetwork> network;
  std::shared_ptr<CdpMedia> media;
  std::map<int,CdpPage> pages;
  std::map<int,std::string> contexts;
  int next_page = 1, next_context = 0;
  Json call(CdpPage&, const std::string&, Json, Deadline);
  // Returns the complete Runtime.evaluate result (including exceptionDetails).
  Json evaluate(CdpPage&, const std::string&, Deadline, bool isolated = false);
  Json value(CdpPage&, const std::string&, Deadline, bool isolated = false);
  bool network_idle(CdpPage&, Deadline) const;
  Json browser_events(const std::string& method, std::uint64_t after_sequence=0) const;
};
Json cdp_dom_command(CdpRuntime&, CdpPage&, const Json&, Deadline);
Json cdp_input_command(CdpRuntime&, CdpPage&, const Json&, Deadline);
Json cdp_page_command(CdpRuntime&, CdpPage&, const Json&, Deadline);
} // namespace suitecut::detail
