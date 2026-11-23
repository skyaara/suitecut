// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <iostream>
#include "include/cef_api_hash.h"
#include "coverage/coverage_dispatch.h"
int main() {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  using Dispatch = suitecut::CoverageDispatch;
  using Dict = Dispatch::Dict;
  double now = 100;
  struct Call {
    std::string session, method;
    Dict params;
    double deadline;
    Dispatch::Reply reply;
  };
  std::deque<Call> actual;
  size_t positives = 0, negatives = 0;
  Dispatch::Hooks hooks;
  hooks.now = [&] { return now; };
  hooks.wake = [] {};
  hooks.send = [&](const std::string& session, const std::string& method, Dict params,
                   double deadline, auto reply) {
    actual.push_back(
        {session, method, params ? params->Copy(false) : nullptr, deadline, std::move(reply)});
    return true;
  };
  auto dispatch = std::make_shared<Dispatch>(hooks);
  auto reply = [&](bool ok, Dict, const std::string&) {
    if (ok)
      ++positives;
    else
      ++negatives;
  };
  auto params = CefDictionaryValue::Create();
  params->SetString("scriptId", "actual-native-script");
  // No native count, byte or concurrency cap: every command reaches the backend
  // immediately, including far more than the old 272 and payloads past 8 MiB.
  for (size_t i = 0; i < 1000; ++i) {
    assert(dispatch->Send("actual-session", "Debugger.getScriptSource", params, 110, reply));
  }
  assert(actual.size() == 1000 && dispatch->Physical() == 1000 && dispatch->Pending() == 1000);
  params->SetString("scriptId", "mutated author object");
  assert(actual.front().params->GetString("scriptId") == "actual-native-script");
  auto large = CefDictionaryValue::Create();
  large->SetString("payload", std::string(9 * 1048576, 'x'));
  assert(dispatch->Send("actual-session", "Runtime.evaluate", large, 110, reply) &&
         actual.size() == 1001 &&
         actual.back().params->GetString("payload").ToString().size() == 9 * 1048576);
  // Cancellation retires only genuinely undispatched commands. It cannot turn
  // real backend calls into physical0.
  dispatch->CancelQueued("startJS", "actual native operation canceled");
  assert(negatives == 0 && dispatch->Pending() == 1001 && dispatch->Physical() == 1001 &&
         actual.size() == 1001);
  // A dispatched command is never expired by Tick; it awaits its real ACK.
  now = 111;
  dispatch->Tick(now);
  assert(negatives == 0 && dispatch->Pending() == 1001);
  while (!actual.empty()) {
    auto call = std::move(actual.front());
    actual.pop_front();
    assert(call.deadline == 110);
    call.reply(true, CefDictionaryValue::Create(), "");
  }
  assert(positives == 1001 && dispatch->Pending() == 0 && dispatch->Physical() == 0);
  // A late actual enabled-domain ACK remains true for the owning controller to
  // record/disable that actual domain. This dispatcher invents no logical ACKs.
  assert(dispatch->Send("actual-session", "DOM.enable", nullptr, 120, reply));
  now = 121;
  auto late = std::move(actual.front());
  actual.pop_front();
  late.reply(true, CefDictionaryValue::Create(), "");
  assert(positives == 1002);
  // A command sent from inside a reply is dispatched like any other.
  now = 100;
  bool nested = false;
  assert(dispatch->Send(
      "nested-session", "Profiler.enable", nullptr, 500, [&](bool ok, Dict, const std::string&) {
        assert(ok);
        nested = true;
        assert(dispatch->Send("nested-session", "Profiler.start", nullptr, 500, reply));
      }));
  actual.front().reply(true, CefDictionaryValue::Create(), "");
  actual.pop_front();
  assert(nested && actual.size() == 1 && actual.front().method == "Profiler.start" &&
         dispatch->Pending() == 1);
  actual.front().reply(true, CefDictionaryValue::Create(), "");
  actual.pop_front();
  assert(dispatch->Pending() == 0);
  // Detach retires that actual session's calls; their late replies are ignored.
  const auto before = negatives;
  for (size_t i = 0; i < 300; ++i)
    assert(dispatch->Send("held-session", "Debugger.enable", nullptr, 500, reply));
  dispatch->Detached("held-session");
  assert(negatives == before + 300 && dispatch->Pending() == 0 && dispatch->Physical() == 0);
  while (!actual.empty()) {
    auto call = std::move(actual.front());
    actual.pop_front();
    call.reply(true, CefDictionaryValue::Create(), "");
  }
  assert(dispatch->Pending() == 0 && negatives == before + 300);
  // Transaction cancellation cannot cancel another actual target's domain
  // teardown. That accepted cleanup must reach its actual backend ACK.
  assert(dispatch->Send("cleanup-session", "Profiler.disable", nullptr, 500, reply));
  dispatch->CancelQueued("startJS", "logical transaction canceled");
  assert(dispatch->Pending() == 1 && actual.size() == 1 &&
         actual.front().method == "Profiler.disable");
  actual.front().reply(true, CefDictionaryValue::Create(), "");
  actual.pop_front();
  assert(dispatch->Pending() == 0 && dispatch->Physical() == 0);
  // Closing is an actual provider-supplied channel-retirement boundary.
  for (int i = 0; i < 17; ++i) {
    assert(dispatch->Send("closing-session", "CSS.enable", nullptr, 500, reply));
  }
  dispatch->Closed();
  assert(dispatch->Pending() == 0 && dispatch->Physical() == 0);
  while (!actual.empty()) {
    auto call = std::move(actual.front());
    actual.pop_front();
    call.reply(true, CefDictionaryValue::Create(), "");
  }
  assert(!dispatch->Send("closed-session", "CSS.enable", nullptr, 500, reply));
  // Actual synchronous replies are valid; native queue pumping stays bounded.
  hooks.send = [](const auto&, const auto&, Dict, double, auto done) {
    done(true, CefDictionaryValue::Create(), "");
    return true;
  };
  auto sync = std::make_shared<Dispatch>(hooks);
  assert(sync->Send("actual-sync-session", "Profiler.enable", nullptr, 500, reply));
  assert(sync->Physical() == 0 && sync->Pending() == 0);
  std::cout << "Native coverage unbounded dispatch, ownership copies, original deadlines, "
               "cancellation, real retirement and synchronous ACKs passed\n";
}
