// SPDX-License-Identifier: MIT
#include "hook_phases.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
struct Harness {
  std::vector<HookPhases::Acquire> acquire;
  std::vector<HookPhases::Invoke> invoke, abort;
  std::vector<HookPhases::Release> release;
  std::vector<HookPhases::Result> results;
  std::vector<std::string> terminated;
  HookPhases phases{{[this](auto value){acquire.push_back(value);},[this](auto value){invoke.push_back(value);},[this](auto value){abort.push_back(value);},[this](auto value){release.push_back(value);},[this](auto value){results.push_back(value);},[this](auto value){terminated.push_back(value);}}};
  void Acquired(double now) {const auto value=acquire.back();phases.Acquired(value.request,value.scope,value.generation,true,"",now);}
  void Settled(const HookPhases::Invoke& value,double now) {phases.Settled(value.token,value.scope,value.generation,true,"",now);}
  void Released(double now) {const auto value=release.back();phases.Released(value.request,value.scope,value.generation,true,"",now);}
};
static constexpr FixtureAttemptClockOwner owner{1,1,1,123};
static constexpr FixtureClockOwner fixture{10,1,1,5,1,123,false};
int main() {
  unsigned controls=0;
  {
    Harness h;FixtureTimeoutClocks fixtures;auto timing=std::make_shared<FixtureAttemptClocks>(owner,100,0);
    assert(h.phases.Begin(1,1,{{HookKind::Body,1,{"fixture"}}},100,0,{},0,timing));
    assert(fixtures.Begin(fixture,500,100,10,h.phases.ActiveChargedClock(1,1).get()));
    h.phases.Tick(150);assert(h.abort.empty()&&h.terminated.empty()&&h.invoke.empty());
    assert(fixtures.Ready(fixture,200));h.Acquired(200);assert(h.invoke.size()==1&&h.invoke[0].deadline==290&&h.invoke[0].observation.duration==10);
    h.Settled(h.invoke[0],220);assert(h.release.size()==1&&h.release[0].deadline==320);
    assert(fixtures.Teardown(fixture,100,220,h.phases.ActiveChargedClock(1,1).get()));
    assert(fixtures.Settled(fixture,230));h.Released(230);
    assert(h.results.size()==1&&h.results[0].ok&&h.results[0].observation.duration==30);
    assert(timing->Observe(owner,230).wall==230&&timing->RetirementReady(owner));++controls;
  }
  {
    Harness h;FixtureTimeoutClocks fixtures;auto timing=std::make_shared<FixtureAttemptClocks>(owner,100,0);
    assert(h.phases.Begin(1,1,{{HookKind::Body,1,{"fixture"}},{HookKind::AfterEach,2,{}}},100,0,{},0,timing));
    assert(fixtures.Begin(fixture,500,100,0,h.phases.ActiveChargedClock(1,1).get()));assert(fixtures.Ready(fixture,10));h.Acquired(10);
    const auto body=h.invoke[0];h.phases.Tick(110);assert(h.abort.size()==1&&h.acquire.size()==2);h.Acquired(110);
    h.Settled(body,115);assert(h.invoke.back().kind==HookKind::AfterEach);h.Settled(h.invoke.back(),120);
    assert(h.release.size()==1);assert(fixtures.Teardown(fixture,100,120,h.phases.ActiveChargedClock(1,1).get()));
    h.phases.Tick(400);assert(h.terminated.empty()&&h.results.empty());
    assert(fixtures.Settled(fixture,500));h.Released(500);assert(h.results.size()==1&&h.results[0].timed_out);
    assert(h.results[0].observation.duration==110&&timing->Observe(owner,500).wall==500);++controls;
  }
  {
    Harness h;auto wrong=owner;++wrong.scope;auto timing=std::make_shared<FixtureAttemptClocks>(wrong,100,0);
    assert(!h.phases.Begin(1,1,{{HookKind::Body,1,{}}},100,0,{},0,timing));assert(h.acquire.empty()&&h.invoke.empty());
    Harness all;auto real=std::make_shared<FixtureAttemptClocks>(owner,100,0);
    assert(all.phases.Begin(1,1,{{HookKind::BeforeAll,1,{}}},100,0,{},0,real));all.Acquired(0);
    const auto call=all.invoke[0];all.phases.Tick(100);assert(all.release.size()==1&&all.release[0].deadline==1100);
    all.phases.Tick(105);assert(all.terminated.empty());all.Settled(call,105);all.Released(110);
    assert(all.results.size()==1&&all.results[0].timed_out);++controls;
  }
  assert(controls==3);std::cout<<"PASS "<<controls<<" controlled HookPhases charged fixture clock composition cases; no actual callback/OS receipt claim\n";
}
