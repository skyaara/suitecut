// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"

// Drives actual adapter state with out-of-order native protocol receipts.
// No renderer, transport delivery or custom CEF runtime acceptance is claimed.
class RoutingLifecycle {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  struct Operations {
    std::map<int, uint64_t> live;
    std::set<int> expired;
    bool IsCurrent(int id, uint64_t generation) const {
      const auto it = live.find(id);
      return it != live.end() && it->second == generation;
    }
    bool IsExpired(int id, uint64_t, double) const { return expired.contains(id); }
  } operations_;
  struct Call {
    std::function<void(bool, const std::string&)> reply;
  };
  std::vector<Call> calls;
  std::map<int, bool> results;
  bool closing_page_ = false, retired = false;
  bool reject_dispatch = false;
  bool NativeWebSocketPolicyRootOwned() const { return false; }
  static double Now() { return 1; }
  static std::string Json(Dict value) {
    auto wrapped = CefValue::Create();
    wrapped->SetDictionary(value);
    return CefWriteJSON(wrapped, JSON_WRITER_DEFAULT);
  }
  void Complete(int id, bool ok, const std::string& = "", const std::string& = "") {
    results[id] = ok;
    operations_.live.erase(id);
  }
  void WakeDeadlines() {}
  void NetworkFatal(const std::string&) { retired = true; }
  void NativeNetworkSend(const std::string&, const std::string&, Dict,
                         std::function<void(bool, const std::string&)> reply, double) {
    if (reject_dispatch) {
      reply(false, "Native network command dispatch failed");
      return;
    }
    calls.push_back({std::move(reply)});
  }
  std::vector<Dict> commands;
  void DevTools(int, const std::string&, Dict params, std::function<void(bool, const std::string&)>,
                std::optional<std::string>, bool) {
    commands.push_back(params);
  }
#include "controllers/network/websocket_routing.inc"
  void Enable(int id) {
    operations_.live[id] = 1;
    auto input = CefDictionaryValue::Create();
    input->SetString("action", "routeEnable");
    NativeWebSocketRouteCommand(id, 1, input);
  }
  void Disable(int id) {
    operations_.live[id] = 1;
    auto input = CefDictionaryValue::Create();
    input->SetString("action", "routeDisable");
    input->SetString("routingToken", std::to_string(native_routed_epoch_));
    NativeWebSocketRouteCommand(id, 1, input);
  }
  static std::string Supported() { return "{\"revision\":2}"; }
  void Routed(const std::string& request, const std::string& stage, const std::string& message = "",
              const std::string& payload = "", int opcode = 0) {
    auto e = CefDictionaryValue::Create();
    e->SetString("requestId", request);
    e->SetString("stage", stage);
    e->SetString("url", stage == "created" ? "wss://example.test/" + request : "");
    e->SetString("messageId", message);
    e->SetBool("outgoing", false);
    e->SetInt("opcode", opcode);
    e->SetString("payloadBase64", payload);
    NativeWebSocketRouteEvent(e, "");
  }
  void Ack(size_t index, bool ok = true, const std::string& value = Supported()) {
    auto done = std::move(calls.at(index).reply);
    assert(done);
    done(ok, value);
  }
};
int main() {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  {
    RoutingLifecycle h;
    h.Enable(1);
    auto late = h.calls.at(0).reply;
    h.operations_.live.erase(1);
    h.Enable(2);
    assert(!h.results.at(2) && h.calls.size() == 1);
    h.Ack(0);
    assert(h.calls.size() == 2 && h.native_routed_configuring_ && !h.native_routed_active_);
    h.Enable(3);
    assert(!h.results.at(3) && h.calls.size() == 2);
    h.Ack(1);
    assert(!h.native_routed_configuring_);
    h.Enable(4);
    assert(h.native_routed_epoch_ == 2);
    h.Ack(2);
    assert(h.results.at(4) && h.native_routed_active_);
    late(true, RoutingLifecycle::Supported());
    assert(h.native_routed_epoch_ == 2 && h.native_routed_active_ && h.calls.size() == 3);
  }
  {
    RoutingLifecycle h;
    h.Enable(1);
    h.NativeWebSocketRouteFail("queue failure while enabling");
    assert(h.calls.size() == 2 && h.native_routed_configuring_);
    h.Ack(1);
    assert(h.native_routed_configuring_ && h.native_routed_enable_pending_);
    h.Enable(2);
    assert(!h.results.at(2));
    h.Ack(0);
    assert(!h.native_routed_active_ && !h.native_routed_configuring_ && !h.results.at(1));
    h.Enable(3);
    h.Ack(2);
    assert(h.results.at(3) && h.native_routed_active_);
  }
  {
    RoutingLifecycle h;
    h.Enable(1);
    h.Ack(0);
    h.Disable(2);
    assert(h.native_routed_configuring_ && !h.native_routed_active_);
    h.Ack(1, false, "{\"code\":-32000,\"message\":\"uncertain cleanup\"}");
    assert(h.retired && h.native_routed_retiring_ && !h.results.at(2));
    h.Enable(3);
    assert(!h.results.at(3) && h.calls.size() == 2);
  }
  {
    RoutingLifecycle h;
    h.Enable(1);
    h.Ack(0);
    h.Disable(2);
    auto late = h.calls.at(1).reply;
    h.operations_.expired.insert(2);
    h.Ack(1);
    assert(!h.results.at(2) && !h.native_routed_configuring_ && !h.retired);
    h.Enable(3);
    h.Ack(2);
    assert(h.results.at(3) && h.native_routed_active_);
    late(true, RoutingLifecycle::Supported());
    assert(h.native_routed_active_ && h.native_routed_epoch_ == 2);
  }
  {
    RoutingLifecycle h;
    h.reject_dispatch = true;
    h.Enable(1);
    assert(!h.results.at(1) && !h.native_routed_configuring_ && !h.native_routed_enable_pending_);
    h.reject_dispatch = false;
    h.Enable(2);
    h.Ack(0);
    assert(h.results.at(2));
  }
  {
    RoutingLifecycle h;
    h.Enable(1);
    h.operations_.live.clear();
    h.closing_page_ = true;
    h.Ack(0, false, "actual channel closed");
    assert(h.calls.size() == 1 && h.native_routed_retiring_);
  }
  {
    RoutingLifecycle h;
    h.Enable(1);
    h.Ack(0, false, "{\"code\":-32601,\"message\":\"Method not found\"}");
    assert(!h.retired && !h.native_routed_configuring_ && !h.results.at(1) && h.calls.size() == 1);
  }
  {
    // Revision 1 is the capped extension; it is never activated, and its
    // cleanup reply cannot confirm that the incompatible backend disabled.
    RoutingLifecycle h;
    h.Enable(1);
    h.Ack(0, true,
          "{\"revision\":1,\"maxSockets\":128,\"maxPendingMessages\":64,\"maxPendingBytes\":"
          "2097152}");
    assert(!h.results.at(1) && !h.native_routed_active_ && h.calls.size() == 2 &&
           h.native_routed_configuring_);
    h.Ack(1, true, "{\"revision\":1}");
    assert(h.retired && !h.native_routed_active_);
    RoutingLifecycle bare;
    bare.Enable(1);
    bare.Ack(0, true, "{\"revision\":1}");
    assert(!bare.results.at(1) && !bare.native_routed_active_);
  }
  {
    // Socket counts, queue depth, id/URL/payload/reason sizes are not ours to bound.
    RoutingLifecycle h;
    h.Enable(1);
    h.Ack(0);
    assert(h.results.at(1) && h.native_routed_active_);
    for (int i = 0; i < 200; ++i)
      h.Routed("socket-" + std::to_string(i) + std::string(200, 'r'), "created");
    std::vector<uint8_t> big(3 * 1048576 + 1, 0x5a);
    const std::string encoded = CefBase64Encode(big.data(), big.size()).ToString();
    h.Routed("socket-0" + std::string(200, 'r'), "message", std::string(64, 'm'), encoded, 2);
    assert(h.native_routed_failure_.empty() && h.native_routed_active_ &&
           h.native_routed_sockets_.size() == 200 && h.native_routed_events_.size() == 201);
    assert(h.native_routed_events_.back().data->GetString("payloadBase64").ToString() == encoded);
    h.operations_.live[2] = 1;
    auto command = CefDictionaryValue::Create();
    command->SetString("action", "routeCommand");
    command->SetString("routingToken", std::to_string(h.native_routed_epoch_));
    command->SetString("requestId", "socket-0" + std::string(200, 'r'));
    command->SetString("operation", "close");
    command->SetString("messageId", std::string(64, 'm'));
    command->SetString("payloadBase64", encoded);
    command->SetString("reason", std::string(200, 'x'));
    h.NativeWebSocketRouteCommand(2, 1, command);
    assert(!h.results.contains(2) && h.commands.size() == 1);
    assert(h.commands.back()->GetString("reason").ToString().size() == 200 &&
           h.commands.back()->GetString("payloadBase64").ToString() == encoded);
  }
}
