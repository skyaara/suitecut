// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include "include/cef_api_hash.h"
#include "network/websocket_routes.h"
using Policies = suitecut::NativeWebSocketRoutes;
using Dict = Policies::Dict;
struct Fixture {
  double now = 1;
  bool failed = false, accept = true;
  std::vector<Dict> events;
  std::vector<size_t> payloads;
  struct Call {
    std::string method;
    Dict params;
    Policies::Result reply;
  };
  std::vector<Call> calls;
  Policies policies{[this] { return now; }, [] {},
                    [this](Dict e, std::vector<uint8_t> b) {
                      if (!accept) return false;
                      events.push_back(e);
                      payloads.push_back(b.size());
                      return true;
                    }};
  Fixture() {
    assert(policies.Attach(
        1, 1, "actual-page-target",
        [this](const auto& m, Dict p, double, auto reply) {
          calls.push_back({m, p, std::move(reply)});
        },
        [this](const auto&) { failed = true; }, [](bool ok, const auto&) { assert(ok); }));
  }
  ~Fixture() { suitecut::GlobalRouteCallbackLeases().Clear(); }
  static std::string Supported() { return "{\"revision\":2}"; }
  void Ack(size_t n, bool ok = true, const std::string& json = Supported()) {
    auto reply = std::move(calls.at(n).reply);
    assert(reply);
    reply(ok, json);
  }
  void Install(int id, int page, const std::string& name, const std::string& url, bool host,
               Policies::Reply reply) {
    auto input = CefDictionaryValue::Create();
    input->SetString("action", "routeRegister");
    input->SetString("registrationId", name);
    input->SetString("url", url);
    input->SetBool("hostMatcher", host);
    input->SetString("baseURL", "https://example.test/base/");
    policies.Command(id, 1, page, input, 30001, std::move(reply));
  }
  void Created(const std::string& id, const std::string& url) {
    auto e = CefDictionaryValue::Create();
    e->SetString("requestId", id);
    e->SetString("stage", "created");
    e->SetString("url", url);
    policies.Event("actual-page-target", e);
  }
  void Closed(const std::string& id) {
    auto e = CefDictionaryValue::Create();
    e->SetString("requestId", id);
    e->SetString("stage", "closed");
    policies.Event("actual-page-target", e);
  }
  static uint64_t Number(Dict d, const char* key) {
    return std::strtoull(d->GetString(key).ToString().c_str(), nullptr, 10);
  }
  static void Finish(Dict event) {
    const auto lease = event->GetDictionary("callback");
    assert(lease);
    assert(suitecut::GlobalRouteCallbackLeases().Finish(
        Number(lease, "token"), Number(lease, "scope"), lease->GetString("invocation")));
  }
};
int main() {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  {
    Fixture f;
    bool installed = false;
    f.Install(1, 0, "context", "**/*", false, [&](bool ok, Dict, const auto&) { installed = ok; });
    assert(!installed && f.calls.size() == 1);
    f.Ack(0);
    assert(installed);
    f.Install(2, 1, "page", "/socket/{one,two}?q=*", false,
              [](bool ok, Dict, const auto&) { assert(ok); });
    f.Install(3, 1, "predicate", "**/*", true, [](bool ok, Dict, const auto&) { assert(ok); });
    f.Created("socket-1", "wss://example.test/socket/two?q=x");
    assert(f.events.size() == 1 && f.events.back()->GetString("stage") == "match");
    auto event = f.events.back();
    Fixture::Finish(event);
    auto decision = event->Copy(false);
    decision->SetString("action", "routePolicyCommand");
    decision->SetString("operation", "match");
    decision->SetBool("matched", false);
    f.policies.Command(4, 1, 1, decision, 30001, [](bool ok, Dict, const auto&) { assert(ok); });
    assert(f.events.size() == 2 && f.events.back()->GetString("stage") == "created" &&
           f.events.back()->GetString("registrationId") == "page");
    auto lease = f.events.back()->GetDictionary("callback");
    assert(suitecut::GlobalRouteCallbackLeases().Start(Fixture::Number(lease, "token"),
                                                       Fixture::Number(lease, "scope"),
                                                       lease->GetString("invocation")));
    auto remove = CefDictionaryValue::Create();
    remove->SetString("action", "routeUnregister");
    auto ids = CefListValue::Create();
    ids->SetString(0, "page");
    remove->SetList("registrationIds", ids);
    remove->SetString("behavior", "wait");
    bool removed = false;
    f.policies.Command(5, 1, 1, remove, 30001, [&](bool ok, Dict, const auto&) {
      assert(ok);
      removed = true;
    });
    assert(!removed && f.calls.size() == 2 &&
           f.calls.back().params->GetString("action") == "abort");
    f.Ack(1);
    assert(!removed);
    assert(suitecut::GlobalRouteCallbackLeases().Finish(Fixture::Number(lease, "token"),
                                                        Fixture::Number(lease, "scope"),
                                                        lease->GetString("invocation")));
    f.policies.Tick();
    assert(removed);
    const auto count = f.events.size();
    f.Closed("socket-1");
    assert(f.events.size() == count && !f.failed);
  }
  {
    // The capped revision 1 extension (and a non-integer revision) never configures a target.
    for (const std::string old : {"{\"revision\":1}",
                                  "{\"revision\":1,\"maxSockets\":128,\"maxPendingMessages\":64,"
                                  "\"maxPendingBytes\":2097152}",
                                  "{\"revision\":2.5}", "{}"}) {
      Fixture f;
      bool done = false, installed = true;
      f.Install(1, 1, "old", "**/*", false, [&](bool ok, Dict, const auto&) {
        done = true;
        installed = ok;
      });
      f.Ack(0, true, old);
      assert(done && !installed && f.failed && !f.policies.Owns("actual-page-target"));
    }
  }
  {
    Fixture f;
    bool completed = false;
    f.Install(1, 1, "cancel", "**/*", false, [&](bool ok, Dict, const auto&) {
      assert(!ok);
      completed = true;
    });
    f.policies.Cancel(1);
    f.Ack(0);
    assert(!completed && f.calls.size() == 2 && f.policies.Physical() == 1);
    f.Ack(1);
    assert(completed && !f.policies.Physical() && !f.policies.Owns("actual-page-target"));
  }
  {
    Fixture f;
    f.Install(1, 1, "only", "/selected", false, [](bool ok, Dict, const auto&) { assert(ok); });
    f.Ack(0);
    f.Created("unmatched", "wss://example.test/other");
    assert(f.events.empty() && f.calls.size() == 2 &&
           f.calls.back().params->GetString("action") == "continue");
    f.Ack(1);
    auto remove = CefDictionaryValue::Create();
    remove->SetString("action", "routeUnregisterAll");
    bool removed = false;
    f.policies.Command(2, 1, 1, remove, 30001, [&](bool ok, Dict, const auto&) {
      assert(ok);
      removed = true;
    });
    assert(removed && f.calls.size() == 3 && !f.calls.back().params->GetBool("enabled"));
    f.Ack(2);
    assert(!f.policies.Owns("actual-page-target") && !f.failed);
  }
  {
    Fixture f;
    f.Install(1, 1, "conflict", "**/*", false, [](bool ok, Dict, const auto&) { assert(!ok); });
    auto callback = std::move(f.calls.at(0).reply);
    callback(false, "Explicit WebSocket control and URL policy cannot own the same root target");
    assert(!f.failed && !f.policies.Physical());
  }
  {
    // Routed messages, ids, URLs and decision payloads/reasons pass through whole.
    Fixture f;
    f.Install(1, 1, "whole", "**/*", false, [](bool ok, Dict, const auto&) { assert(ok); });
    f.Ack(0);
    // 100 sockets stay under the shared callback-lease table (route_callback_leases.h).
    for (int i = 0; i < 100; ++i)
      f.Created("socket-" + std::to_string(i) + std::string(200, 'r'),
                "wss://example.test/" + std::string(9000, 'u'));
    assert(f.events.size() == 100 && !f.failed);
    std::vector<uint8_t> big(3 * 1048576 + 1, 0x33);
    const auto encoded = CefBase64Encode(big.data(), big.size()).ToString();
    auto message = CefDictionaryValue::Create();
    message->SetString("requestId", "socket-0" + std::string(200, 'r'));
    message->SetString("stage", "message");
    message->SetString("messageId", std::string(64, 'm'));
    message->SetInt("opcode", 2);
    message->SetBool("outgoing", false);
    message->SetString("payloadBase64", encoded);
    assert(f.policies.Event("actual-page-target", message) && !f.failed && f.events.size() == 101 &&
           f.payloads.back() == big.size());
    auto decision = f.events.back()->Copy(false);
    decision->SetString("action", "routePolicyCommand");
    decision->SetString("operation", "close");
    decision->SetString("payloadBase64", encoded);
    decision->SetString("reason", std::string(200, 'x'));
    f.policies.Command(2, 1, 1, decision, 30001, [](bool, Dict, const auto&) {});
    assert(f.calls.size() == 2 && f.calls.back().method == "Network.suiteCutWebSocketCommandV1" &&
           f.calls.back().params->GetString("reason").ToString().size() == 200 &&
           f.calls.back().params->GetString("payloadBase64").ToString() == encoded);
  }
  {
    double now = 1;
    suitecut::NativeWebSocketPayloads staged([&] { return now; });
    std::vector<Policies::Reply> commits;
    auto begin = CefDictionaryValue::Create();
    begin->SetString("action", "routePayloadBegin");
    begin->SetString("operation", "client");
    begin->SetInt("opcode", 2);
    begin->SetInt("bytes", 3 * 1048576);
    begin->SetString("routingToken", "1");
    begin->SetString("requestId", "socket");
    std::string token;
    auto run = [&](Dict input, Policies::Reply reply) {
      staged.Command(
          1, 1, input, 30001, [](Dict) { return true; },
          [&](Dict metadata, double deadline, Policies::Reply done) {
            assert(deadline == 30001);
            auto binary = CefBase64Decode(metadata->GetString("payloadBase64"));
            assert(binary && binary->GetSize() == 3 * 1048576);
            commits.push_back(std::move(done));
          },
          std::move(reply));
    };
    run(begin, [&](bool ok, Dict result, const auto&) {
      assert(ok);
      token = result->GetString("payloadToken");
    });
    std::vector<uint8_t> bytes(1048576 + 1, 0x7f);
    size_t written = 0;
    while (written < size_t(3 * 1048576)) {
      const auto size = std::min(bytes.size(), size_t(3 * 1048576) - written);
      auto append = begin->Copy(false);
      append->SetString("action", "routePayloadAppend");
      append->SetString("payloadToken", token);
      append->SetString("base64", CefBase64Encode(bytes.data(), size));
      run(append, [](bool ok, Dict, const auto&) { assert(ok); });
      written += size;
    }
    auto commit = begin->Copy(false);
    commit->SetString("action", "routePayloadCommit");
    commit->SetString("payloadToken", token);
    bool complete = false;
    run(commit, [&](bool ok, Dict, const auto&) {
      assert(ok);
      complete = true;
    });
    assert(!complete && !staged.Pending() && commits.size() == 1);
    commits.front()(true, nullptr, "");
    assert(complete);
    for (int i = 0; i < 8; ++i) run(begin, [](bool ok, Dict, const auto&) { assert(ok); });
    assert(staged.Pending());
    now = 30002;
    staged.Tick();
    assert(!staged.Pending());
  }
  {
    // Writer refusal is backpressure, not failure: the callback event is kept
    // in order and published on a later Tick once the writer accepts again.
    Fixture f;
    f.Install(1, 0, "context", "**/*", false, [](bool ok, Dict, const auto&) { assert(ok); });
    f.Ack(0);
    f.accept = false;
    f.Created("socket-1", "wss://example.test/one");
    f.Created("socket-2", "wss://example.test/two");
    assert(f.events.empty() && !f.failed && f.policies.NeedsTick());
    f.policies.Tick();
    assert(f.events.empty() && !f.failed);
    f.accept = true;
    f.policies.Tick();
    assert(f.events.size() == 2 && !f.failed && !f.policies.NeedsTick());
    assert(f.events[0]->GetString("requestId") == "socket-1" &&
           f.events[1]->GetString("requestId") == "socket-2");
  }
}
