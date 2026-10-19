// SPDX-License-Identifier: MIT
#include "fixture_timeout_runtime.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static FixtureClockOwner Owner(uint64_t token = 1, bool worker = false) {
  return {token, 10, 20, 100 + token, 30, 1234, worker};
}
int main() {
  unsigned controls = 0;
  std::vector<FixtureClockRecord> expired;
  std::vector<FixtureClockOwner> physical;
  const auto callbacks = [&] {
    return FixtureTimeoutRuntime::Callbacks{
      [&](const auto& record, double) { expired.push_back(record); },
      [&](const auto& owner) { physical.push_back(owner); }};
  };
  {
    FixtureTimeoutRuntime f(callbacks()); auto c = std::make_shared<FixtureChargedClock>(100, 0);
    const auto owner = Owner(1, true);
    assert(f.Begin(owner, std::nullopt, 1000, c, 0));
    assert(f.SlotDeadline(owner, 1, std::nullopt) == 1000);
    assert(f.Ready(owner, 300)); assert(c->Charged(308) == 8);
    assert(f.IdleReady() && f.Physical() == 1);
    assert(!f.SlotDeadline(owner, 308, 5000));
    assert(f.Teardown(owner, 100, {}, 400));
    assert(f.SlotDeadline(owner, 401, std::nullopt) == 1400);
    assert(f.Settled(owner, 700)); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); auto c = std::make_shared<FixtureChargedClock>(100, 0);
    std::weak_ptr<FixtureChargedClock> retained = c;
    const auto owner = Owner(); assert(f.Begin(owner, 500, 1000, c, 0));
    c.reset(); assert(!retained.expired()); assert(f.Ready(owner, 400));
    assert(retained.expired()); assert(f.Settled(owner, 450)); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); auto c = std::make_shared<FixtureChargedClock>(100, 0);
    const auto owner = Owner(); assert(f.Begin(owner, 0, 1000, c, 0));
    assert(!f.SlotDeadline(owner, 1000, std::nullopt));
    assert(f.SlotDeadline(owner, 1000, 1500) == 1500);
    assert(f.Physical() == 1 && c->Excluded());
    assert(f.Cancel(owner, 1100, 1200)); f.Tick(1200);
    assert(physical.back() == owner && f.Physical() == 1 && c->Excluded());
    assert(f.Settled(owner, 1201)); assert(!c->Excluded()); ++controls;
  }
  {
    expired.clear(); physical.clear(); FixtureTimeoutRuntime f(callbacks());
    const auto owner = Owner(); assert(f.Begin(owner, 100, 1000, {}, 10));
    f.Tick(1050); assert(expired.size() == 1 && physical.empty());
    assert(expired[0].expired && expired[0].canceled && !expired[0].raw_settled);
    f.Tick(1110); assert(physical.size() == 1 && f.Physical() == 1);
    f.Tick(1200); assert(physical.size() == 1 && expired.size() == 1);
    assert(f.Settled(owner, 1201)); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); const auto owner = Owner();
    assert(f.Begin(owner, 1000, 1000, {}, 0));
    const auto result = f.Amend(owner, 1, 100, 10);
    assert(result && result->independent && !result->expired && result->timeout == 100);
    assert(!f.Amend(owner, 1, 500, 11)); assert(f.Ready(owner, 20));
    assert(!f.Amend(owner, 2, 500, 21)); assert(f.Teardown(owner, 1000, {}, 300));
    assert(f.SlotDeadline(owner, 301, std::nullopt) == 400);
    assert(f.Amend(owner, 3, 0, 310)); assert(!f.SlotDeadline(owner, 1000, std::nullopt));
    assert(f.Settled(owner, 1100)); ++controls;
  }
  {
    expired.clear(); FixtureTimeoutRuntime f(callbacks()); const auto owner = Owner();
    assert(f.Begin(owner, 0, 1000, {}, 0));
    const auto result = f.Amend(owner, 1, 100, 300);
    assert(result && result->expired && expired.size() == 1);
    assert(!f.Ready(owner, 301)); assert(!f.Amend(owner, 2, 0, 301));
    assert(f.Physical() == 1 && f.Settled(owner, 302)); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); const auto owner = Owner();
    auto c = std::make_shared<FixtureChargedClock>(100, 0);
    assert(f.Begin(owner, std::nullopt, 1000, c, 0));
    const auto result = f.Amend(owner, 1, 200, 20);
    assert(result && !result->independent && c->Timeout() == 200);
    assert(f.SlotDeadline(owner, 21, std::nullopt) == 200);
    assert(f.Ready(owner, 40)); assert(f.Settled(owner, 50)); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); const auto owner = Owner();
    assert(f.Begin(owner, 100, 1000, {}, 0)); auto foreign = owner; ++foreign.generation;
    assert(!f.Ready(foreign, 10)); assert(!f.Cancel(foreign, 10, 100));
    assert(!f.Amend(foreign, 1, 0, 10)); assert(!f.Settled(foreign, 10));
    assert(!f.SlotDeadline(foreign, 10, std::nullopt)); assert(f.Physical() == 1);
    assert(f.Settled(owner, 20)); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); auto c = std::make_shared<FixtureChargedClock>(100, 0);
    const auto owner = Owner(); assert(f.Begin(owner, 100, 1000, c, 0));
    assert(!f.ActualWorkerExited(30, 9999, 20)); assert(f.Physical() == 1);
    assert(f.ActualWorkerExited(30, 1234, 30));
    assert(f.Physical() == 0 && !c->Excluded());
    assert(!f.Records()[0].raw_settled && f.Records()[0].phase == FixtureClockPhase::WorkerExited);
    ++controls;
  }
  {
    unsigned notes = 0; FixtureTimeoutRuntime* actual = nullptr;
    FixtureTimeoutRuntime f({[&](const auto& record, double) { ++notes; assert(actual->Settled(record.owner, 100)); },
      [&](const auto&) { assert(false); }}); actual = &f;
    assert(f.Begin(Owner(), 100, 1000, {}, 0)); f.Tick(100);
    assert(notes == 1 && f.Physical() == 0); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); const auto owner = Owner();
    auto main = std::make_shared<FixtureChargedClock>(100, 0);
    auto cleanup = std::make_shared<FixtureChargedClock>(100, 200);
    assert(f.Begin(owner, 500, 1000, main, 0)); assert(f.Ready(owner, 10));
    assert(cleanup->Expired(300)); assert(f.Teardown(owner, 100, cleanup, 300));
    assert(f.SlotDeadline(owner, 301, std::nullopt) == 800);
    assert(f.Settled(owner, 600)); assert(cleanup->Expired(610)); ++controls;
  }
  {
    FixtureTimeoutRuntime f(callbacks()); const auto owner = Owner();
    assert(f.Begin(owner, 0, 1000, {}, 0)); assert(f.Cancel(owner, 1, 100));
    assert(f.Cancel(owner, 2, 500)); assert(f.DrainDeadline(owner) == 100);
    assert(!f.SlotDeadline(owner, 3, std::nullopt));
    assert(f.Settled(owner, 4)); ++controls;
  }
  assert(controls == 12);
  std::cout << "PASS " << controls << " controlled native fixture runtime timing cases; no actual host/helper/original oracle claim\n";
}
