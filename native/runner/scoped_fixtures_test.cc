// SPDX-License-Identifier: MIT
#include "scoped_fixtures.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;

struct Proof {
  std::deque<ScopedFixtures::Command> commands;
  std::vector<ScopedFixtures::Notice> ready, closed;
  std::vector<std::string> terminate;
  ScopedFixtures pool;
  explicit Proof(std::vector<Fixture> definitions)
      : pool(std::move(definitions), {
          [this](const auto& c) { commands.push_back(c); },
          [this](const auto& n) { ready.push_back(n); },
          [this](const auto& n) { closed.push_back(n); },
          [this](const auto& e) { terminate.push_back(e); }}) {}
  ScopedFixtures::Command Pop(ScopedFixtures::CommandKind kind, const std::string& name) {
    assert(!commands.empty());
    auto command = commands.front(); commands.pop_front();
    assert(command.kind == kind && command.name == name);
    return command;
  }
  void Ready(const ScopedFixtures::Command& c, double now) {
    pool.Ready(c.token, c.scope, c.generation, now);
  }
  void Settle(const ScopedFixtures::Command& c, double now, bool ok = true) {
    pool.Settled(c.token, c.scope, c.generation, ok, ok ? "" : "actual error", now);
  }
};
int main() {
  using K = ScopedFixtures::CommandKind;
  using S = FixtureScopeKind;
  {
    Proof p({{"worker", Scope::Worker, {}}, {"base", Scope::Test, {"worker"}},
             {"test", Scope::Test, {"base"}}, {"automatic", Scope::Test, {}, true}});
    assert(p.pool.Open(1, 11, S::BeforeAll, {"test"}, 100, 0));
    auto worker = p.Pop(K::Setup, "worker"); p.Ready(worker, 1);
    auto base = p.Pop(K::Setup, "base"); p.Ready(base, 2);
    auto test = p.Pop(K::Setup, "test"); p.Ready(test, 3);
    assert(p.ready.size() == 1 && p.commands.empty()); // No automatic test fixture in beforeAll.
    p.pool.Close(1, 11, 110, 4);
    p.Settle(p.Pop(K::Release, "test"), 5);
    p.Settle(p.Pop(K::Release, "base"), 6);
    assert(p.closed.size() == 1 && p.closed[0].ok && p.pool.Inspect().workers == 1);
    assert(p.pool.Open(2, 12, S::AfterAll, {"test"}, 200, 7));
    auto nextBase = p.Pop(K::Setup, "base");
    assert(nextBase.scope == 2 && nextBase.token != base.token);
    p.Ready(nextBase, 8);
    auto nextTest = p.Pop(K::Setup, "test"); p.Ready(nextTest, 9);
    assert(nextTest.scope != test.scope && nextTest.token != test.token);
    p.pool.Close(2, 12, 210, 10);
    p.Settle(p.Pop(K::Release, "test"), 11);
    p.Settle(p.Pop(K::Release, "base"), 12);
    p.pool.Retire(220, 13);
    p.Settle(p.Pop(K::Release, "worker"), 14);
    assert(p.pool.Inspect().physical == 0 && p.pool.Inspect().scopes == 0);
  }
  {
    Proof p({{"test", Scope::Test, {}}, {"body", Scope::Test, {"test"}}});
    assert(p.pool.Open(1, 1, S::Test, {"test"}, 100, 0));
    const auto test = p.Pop(K::Setup, "test"); p.Ready(test, 1);
    assert(p.pool.AddRoots(1, 1, {"body"}, 2));
    p.Ready(p.Pop(K::Setup, "body"), 3);
    assert(p.pool.AddRoots(1, 1, {"test"}, 4));
    assert(p.commands.empty() && p.ready.size() == 3); // Same fixture through before/body/after.
    p.pool.Close(1, 1, 110, 5);
    p.Settle(p.Pop(K::Release, "body"), 6);
    p.Settle(p.Pop(K::Release, "test"), 7);
    assert(p.closed[0].ok);
  }
  {
    Proof p({{"test", Scope::Test, {}}});
    assert(p.pool.Open(1, 1, S::BeforeAll, {"test"}, 10, 0));
    const auto old = p.Pop(K::Setup, "test");
    p.Ready(old, 10); // No Tick needed: actual late readiness cannot admit a hook.
    const auto cancel = p.Pop(K::Cancel, "test");
    assert(p.ready.empty() && p.pool.Inspect().physical == 1);
    assert(p.pool.Open(2, 2, S::AfterAll, {"test"}, 20, 11));
    auto newer = p.Pop(K::Setup, "test"); p.Ready(newer, 12);
    p.Settle(cancel, 13); // Old callback cannot erase the new scope's fixture.
    assert(p.pool.Inspect().physical == 1 && !p.closed[0].ok && p.ready.back().scope == 2);
    p.pool.Close(2, 2, 30, 14); p.Settle(p.Pop(K::Release, "test"), 15);
    assert(p.closed.back().ok && p.pool.Inspect().physical == 0);
  }
  {
    Proof p({{"test", Scope::Test, {}}});
    assert(p.pool.Open(1, 9, S::Test, {"test"}, 10, 0));
    const auto fixture = p.Pop(K::Setup, "test"); p.Ready(fixture, 1);
    p.pool.Close(1, 9, 100, 10); // Completion exactly at deadline fails honestly.
    p.Settle(p.Pop(K::Release, "test"), 11);
    assert(p.closed.size() == 1 && !p.closed[0].ok);
    assert(p.closed[0].error == "Fixture scope original deadline expired");
  }
  {
    Proof p({{"test", Scope::Test, {}}});
    assert(p.pool.Open(1, 1, S::Test, {"test"}, 100, 0));
    const auto fixture = p.Pop(K::Setup, "test");
    p.pool.Ready(fixture.token, 1, 2, 1);
    p.pool.Settled(fixture.token, 2, 1, true, "", 2);
    assert(p.ready.empty() && p.pool.Inspect().physical == 1);
    p.pool.Cancel(1, 1, "caller abort", 10, 3);
    p.Pop(K::Cancel, "test");
    p.pool.Settled(fixture.token, 1, 1, true, "", 10);
    assert(p.terminate.size() == 1); // Late physical ack cannot defeat cleanup deadline.
    assert(p.closed.empty());
    p.pool.WorkerClosed(); assert(p.closed.size() == 1 && !p.closed[0].ok);
  }
  {
    Proof p({{"test", Scope::Test, {}}});
    assert(!p.pool.Open(1, 1, S::Test, {}, NAN, 0));
    assert(!p.pool.Open(1, 1, S::Test, {}, 10, INFINITY));
    for (uint64_t id = 1; id <= 16; ++id) assert(p.pool.Open(id, id, S::AfterAll, {"test"}, 100, 0));
    assert(!p.pool.Open(17, 17, S::AfterAll, {"test"}, 100, 0));
    assert(p.pool.Inspect().physical == 16);
    p.pool.Retire(10, 1); p.pool.Tick(10);
    assert(p.terminate.size() == 1 && p.pool.Inspect().physical == 16);
    p.pool.WorkerClosed(); assert(p.closed.size() == 16 && p.pool.Inspect().physical == 0);
  }
  {
    Proof p({{"worker", Scope::Worker, {}}});
    assert(p.pool.Open(1, 1, S::BeforeAll, {"worker"}, 100, 0));
    auto fixture = p.Pop(K::Setup, "worker"); p.Ready(fixture, 1);
    p.Settle(fixture, 2, false); // Raw worker factory failed while its value was in use.
    assert(p.terminate.size() == 1 && p.pool.Inspect().terminating);
    assert(p.closed.empty()); p.pool.WorkerClosed(); assert(!p.closed[0].ok);
  }
  {
    std::vector<Fixture> definitions;
    for (int i = 0; i < 65; ++i) definitions.push_back({"f" + std::to_string(i), Scope::Test,
        i ? std::vector<std::string>{"f" + std::to_string(i - 1)} : std::vector<std::string>{}});
    Proof p(std::move(definitions));
    assert(p.pool.Open(1, 1, S::Test, {"f64"}, 100, 0));
    for (int i = 0; i < 64; ++i) p.Ready(p.Pop(K::Setup, "f" + std::to_string(i)), 1);
    assert(p.terminate.size() == 1 && p.pool.Inspect().physical == 64 && p.commands.empty());
    p.pool.WorkerClosed(); assert(p.pool.Inspect().physical == 0 && !p.closed[0].ok);
  }
  {
    Proof p({{"test", Scope::Test, {}}, {"cleanup", Scope::Test, {"test"}}});
    assert(p.pool.Open(1, 1, S::Test, {"test"}, 10, 0));
    p.Ready(p.Pop(K::Setup, "test"), 1);
    assert(p.pool.BeginCleanup(1, 1, 20, 10));
    assert(!p.pool.BeginCleanup(1, 1, 1000, 11)); // Repeated hooks cannot reset teardown time.
    assert(p.pool.AddRoots(1, 1, {"cleanup"}, 12));
    p.Ready(p.Pop(K::Setup, "cleanup"), 13);
    p.pool.Close(1, 1, 30, 14);
    p.Settle(p.Pop(K::Release, "cleanup"), 15);
    p.Settle(p.Pop(K::Release, "test"), 16);
    assert(!p.closed[0].ok && p.closed[0].error == "Fixture scope original deadline expired");
  }
  std::cout << "Scoped fixture native component: 9 groups passed\n";
}
