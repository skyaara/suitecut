// SPDX-License-Identifier: MIT
#include "hook_phases.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut::runner;
struct Proof {
  std::deque<HookPhases::Acquire> acquire;
  std::deque<HookPhases::Invoke> invoke;
  std::deque<HookPhases::Release> release;
  std::vector<HookPhases::Invoke> abort;
  std::vector<HookPhases::Result> results;
  std::vector<std::string> errors;
  HookPhases phases;
  Proof() : phases({[&](const auto& c) { acquire.push_back(c); },
      [&](const auto& c) { invoke.push_back(c); }, [&](const auto& c) { abort.push_back(c); },
      [&](const auto& c) { release.push_back(c); }, [&](const auto& r) { results.push_back(r); },
      [&](const auto& e) { errors.push_back(e); }}) {}
  HookPhases::Invoke Admit(double now) {
    assert(!acquire.empty()); auto q = acquire.front(); acquire.pop_front();
    phases.Acquired(q.request, q.scope, q.generation, true, "", now);
    assert(!invoke.empty()); auto c = invoke.front(); invoke.pop_front(); return c;
  }
  void Settle(const HookPhases::Invoke& c, bool ok, double now) {
    phases.Settled(c.token, c.scope, c.generation, ok, ok ? "" : "actual callback error", now);
  }
  void Release(double now) {
    assert(!release.empty()); const auto c = release.front(); release.pop_front();
    phases.Released(c.request, c.scope, c.generation, true, "", now);
  }
};
int main() {
  using K = HookKind;
  {
    Proof p;
    assert(p.phases.Begin(1, 1, {{K::BeforeEach, 1, {"test"}}, {K::Body, 2, {"test"}},
        {K::AfterEach, 3, {"test"}}, {K::AfterEach, 4, {"test"}}}, 100, 0));
    auto before = p.Admit(1); assert(before.callback == 1 && before.deadline == 100);
    p.Settle(before, true, 2);
    auto body = p.Admit(3); assert(body.scope == before.scope && body.callback == 2);
    p.Settle(body, true, 4);
    assert(p.acquire.front().cleanup && p.acquire.front().deadline == 104);
    auto after1 = p.Admit(5); p.Settle(after1, false, 6);
    auto after2 = p.Admit(7); assert(after2.callback == 4 && after2.deadline == 104);
    p.Settle(after2, true, 8); p.Release(9);
    assert(p.results.size() == 1 && !p.results[0].ok && p.results[0].errors.size() == 1);
    assert(p.phases.Inspect().physical == 0 && p.abort.empty());
  }
  {
    Proof p;
    assert(p.phases.Begin(1, 1, {{K::BeforeEach, 1, {}}, {K::Body, 2, {}},
        {K::AfterEach, 3, {}}, {K::AfterEach, 4, {}}}, 100, 0));
    p.Settle(p.Admit(1), false, 2);
    auto after = p.Admit(3); assert(after.callback == 3); p.Settle(after, true, 4);
    after = p.Admit(5); assert(after.callback == 4); p.Settle(after, true, 6);
    p.Release(7);
    assert(!p.results[0].ok && p.abort.empty()); // Actual beforeEach failure, no body admission.
  }
  {
    Proof p;
    assert(p.phases.Begin(1, 1, {{K::Body, 1, {}}, {K::AfterEach, 2, {}}}, 10, 0));
    auto body = p.Admit(1);
    p.phases.Tick(10);
    assert(p.abort.size() == 1 && p.abort[0].token == body.token && p.phases.Inspect().physical == 1);
    auto cleanup = p.Admit(11); assert(cleanup.kind == K::AfterEach && cleanup.deadline == 20);
    p.Settle(cleanup, true, 12); p.Release(13);
    assert(!p.results[0].ok && p.phases.Inspect().physical == 1);
    assert(p.phases.Begin(2, 2, {{K::AfterAll, 3, {}}}, 10, 14));
    auto all = p.Admit(15); assert(all.deadline == 24);
    p.Settle(body, true, 16); // Stale callback settles physically, cannot finish afterAll.
    assert(p.phases.Inspect().physical == 1 && p.results.size() == 1);
    p.Settle(all, true, 17); p.Release(18);
    assert(p.results.size() == 2 && p.results[1].ok && p.phases.Inspect().physical == 0);
  }
  {
    Proof p;
    assert(p.phases.Begin(1, 1, {{K::Body, 1, {}}}, 10, 0));
    auto body = p.Admit(1);
    p.Settle(body, true, 10); // Actual ingress, no preceding Tick, late success rejected.
    assert(p.abort.size() == 1 && p.release.size() == 1 && p.phases.Inspect().physical == 0);
    p.Release(11); assert(!p.results[0].ok);
  }
  {
    Proof p;
    assert(p.phases.Begin(1, 1, {{K::BeforeAll, 1, {}}}, 10, 0));
    auto query = p.acquire.front(); p.acquire.pop_front();
    p.phases.Acquired(query.request, query.scope, query.generation, true, "", 10);
    assert(p.invoke.empty() && p.release.size() == 1);
    p.Release(11); assert(!p.results[0].ok);
    assert(p.phases.Begin(2, 2, {{K::AfterAll, 2, {}}}, 10, 12));
    auto after = p.Admit(13); assert(after.deadline == 22);
    p.Settle(after, true, 14); p.Release(15); assert(p.results[1].ok);
  }
  {
    Proof p;
    assert(p.phases.Begin(1, 1, {{K::Body, 1, {}}, {K::AfterEach, 2, {}}}, 10, 0));
    auto body = p.Admit(1); p.phases.Cancel("actual caller abort", 2);
    p.phases.Cancel("repeated caller abort", 3);
    auto cleanup = p.Admit(4); p.Settle(cleanup, true, 5); p.Release(6);
    assert(p.abort.size() == 1 && p.results[0].errors.size() == 1 && p.phases.Inspect().physical == 1);
    p.phases.WorkerClosed(); assert(p.phases.Inspect().physical == 0);
    (void)body;
  }
  {
    Proof p;
    for (uint64_t id = 1; id <= 64; ++id) {
      double now = double(id * 20);
      assert(p.phases.Begin(id, id, {{K::BeforeAll, 1, {}}}, 10, now));
      p.Admit(now + 1); p.phases.Tick(now + 10); p.Release(now + 11);
    }
    assert(p.phases.Inspect().physical == 64);
    assert(p.phases.Begin(65, 65, {{K::AfterAll, 1, {}}}, 10, 2000));
    auto query = p.acquire.front(); p.acquire.pop_front();
    p.phases.Acquired(query.request, query.scope, query.generation, true, "", 2001);
    assert(p.errors.size() == 1 && p.phases.Inspect().physical == 64);
    p.phases.WorkerClosed(); assert(p.phases.Inspect().physical == 0 && !p.results.back().ok);
  }
  {
    Proof p;
    assert(!p.phases.Begin(1, 1, {{K::Body, 1, {}}}, NAN, 0));
    bool invalid = false;
    try { p.phases.Begin(1, 1, {{K::BeforeAll, 1, {}}, {K::AfterAll, 2, {}}}, 100, 0); }
    catch (const std::invalid_argument&) { invalid = true; }
    assert(invalid && !p.phases.Inspect().active);
    assert(p.phases.Begin(1, 1, {{K::Body, 1, {}}}, 100, 0));
    p.Settle(p.Admit(1), true, 2);
    auto close = p.release.front();
    p.phases.Released(close.request, close.scope, close.generation, true, "", close.deadline);
    assert(p.errors.size() == 1 && p.results.empty());
    p.phases.WorkerClosed(); assert(!p.results[0].ok);
  }
  {
    Proof p;
    assert(p.phases.Begin(1, 1, {{K::Body, 1, {}}}, 10, 0));
    p.Admit(1); p.phases.Tick(10); p.Release(11);
    assert(p.phases.Inspect().physical == 1 && p.phases.Retire(20, 12));
    assert(!p.phases.Begin(2, 2, {{K::Body, 1, {}}}, 10, 13));
    p.phases.Tick(20);
    assert(p.errors.size() == 1 && p.phases.Inspect().physical == 1);
    p.phases.WorkerClosed(); assert(p.phases.Inspect().physical == 0);
  }
  std::cout << "Native hook phase component: 9 groups passed\n";
}
