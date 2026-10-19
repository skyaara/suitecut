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
#include <limits>
int main() {
  using K=HookKind; using S=PhaseStatus;
  {
    Proof p;
    assert(!p.phases.Begin(1,1,{{K::Body,1,{}}},100,0,{S::Passed,std::numeric_limits<double>::quiet_NaN()}));
    assert(!p.phases.Begin(1,1,{{K::Body,1,{}}},100,0,{S::Passed,-1}));
    assert(p.phases.Begin(1,1,{{K::BeforeEach,1,{}},{K::Body,2,{}},{K::AfterEach,3,{}},{K::AfterEach,4,{}}},100,20));
    auto c=p.Admit(21);assert(c.observation.status==S::Passed&&c.observation.duration==1);p.Settle(c,true,22);
    c=p.Admit(24);assert(c.observation.duration==4);p.Settle(c,false,25);
    c=p.Admit(26);assert(c.observation.status==S::Failed&&c.observation.duration==6);p.Settle(c,true,27);
    c=p.Admit(28);assert(c.observation.status==S::Failed&&c.observation.duration==8);p.Settle(c,true,29);p.Release(30);
    assert(p.results[0].observation.status==S::Failed&&p.results[0].observation.duration==10);
  }
  {
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},10,0));auto body=p.Admit(1);
    p.Settle(body,true,10);auto cleanup=p.Admit(11);assert(cleanup.observation.status==S::TimedOut&&cleanup.observation.duration==11);
    p.Settle(cleanup,true,12);p.Release(13);assert(p.results[0].observation.status==S::TimedOut&&p.phases.Inspect().physical==0);
  }
  {
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},100,0));auto body=p.Admit(1);
    p.phases.Cancel("actual signal",2);auto cleanup=p.Admit(3);assert(cleanup.observation.status==S::Interrupted&&cleanup.observation.duration==3);
    p.Settle(body,true,4);assert(p.phases.Inspect().physical==1);p.Settle(cleanup,true,5);p.Release(6);
    assert(p.results[0].observation.status==S::Interrupted);
  }
  {
    Proof p;assert(p.phases.Begin(1,1,{{K::AfterAll,1,{}}},100,40,{S::Failed,30}));auto c=p.Admit(42);
    assert(c.observation.status==S::Failed&&c.observation.duration==32);p.Settle(c,true,45);p.Release(48);
    assert(p.results[0].ok&&p.results[0].observation.status==S::Failed&&p.results[0].observation.duration==38);
  }
  {
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},100,50));p.Admit(52);p.phases.Tick(55);p.phases.WorkerClosed(57);
    assert(p.results[0].observation.status==S::Failed&&p.results[0].observation.duration==7&&p.phases.Inspect().physical==0);
  }
  {
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},100,50));auto c=p.Admit(60);p.Settle(c,true,55);p.Release(56);
    assert(p.results[0].observation.duration==10); // A stale clock ingress never moves native duration backwards.
  }
  std::cout<<"PASS native phase observations: actual failure, timeout ingress, cancellation, inherited state, exit and monotonicity\n";
}
