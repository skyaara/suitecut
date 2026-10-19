// SPDX-License-Identifier: MIT
#include "fixture_graph.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
struct Harness {
  std::vector<FixtureOwners::Event> events;
  unsigned ready=0,finished=0,terminated=0;
  bool result=false;
  FixtureOwners manager;
  Harness():manager(FixtureGraph({{"fixture",Scope::Test,{},false,500}}),{
    [this](auto event){events.push_back(event);},[this](auto,auto){++ready;},
    [this](auto,auto,bool ok,const auto&){++finished;result=ok;},[this](const auto&){++terminated;}}){}
};
static FixtureClockOwner Owner(const FixtureOwners::Event& event){return {event.token,event.test,event.generation,1,1,123,false};}
static constexpr FixtureAttemptClockOwner attempt{1,1,1,123};
int main(){
  unsigned n=0;
  {
    Harness h;FixtureTimeoutClocks fixtures;auto timing=std::make_shared<FixtureAttemptClocks>(attempt,100,0);
    assert(h.manager.Begin(1,1,{"fixture"},100,0,timing));const auto event=h.events[0];const auto owner=Owner(event);
    assert(fixtures.Begin(owner,500,100,0,h.manager.ActiveChargedClock().get()));h.manager.Tick(200);assert(!h.finished&&!h.terminated);
    assert(fixtures.Ready(owner,250));h.manager.Ready(event.token,1,1,250);assert(h.ready==1&&h.manager.ActiveDeadline(250)==350);
    h.manager.Finish(true,"",270);assert(h.events.back().kind==FixtureOwners::EventKind::ResumeTeardown);
    assert(fixtures.Teardown(owner,100,270,h.manager.ActiveChargedClock().get()));h.manager.Tick(600);assert(!h.finished&&!h.terminated);
    assert(fixtures.Settled(owner,700));h.manager.Settled(event.token,1,1,true,"",700);
    const auto observed=h.manager.ClockObservation(1,1);assert(h.finished==1&&h.result&&observed&&observed->charged==20&&observed->wall==700);
    assert(!h.manager.State().physical&&!h.manager.State().active&&timing->RetirementReady(attempt));++n;
  }
  {
    Harness h;FixtureTimeoutClocks fixtures;auto timing=std::make_shared<FixtureAttemptClocks>(attempt,100,30,20);
    assert(h.manager.Begin(1,1,{"fixture"},110,30,timing));const auto event=h.events[0];const auto owner=Owner(event);
    assert(fixtures.Begin(owner,500,100,30,h.manager.ActiveChargedClock().get()));assert(fixtures.Ready(owner,130));h.manager.Ready(event.token,1,1,130);
    assert(h.manager.ActiveDeadline(130)==210);h.manager.Finish(true,"",150);
    assert(fixtures.Teardown(owner,100,150,h.manager.ActiveChargedClock().get()));assert(fixtures.Settled(owner,160));h.manager.Settled(event.token,1,1,true,"",160);
    const auto observed=h.manager.ClockObservation(1,1);assert(observed&&observed->charged==40&&observed->wall==150);++n;
  }
  {
    Harness h;auto wrong=attempt;++wrong.generation;auto timing=std::make_shared<FixtureAttemptClocks>(wrong,100,0);
    assert(!h.manager.Begin(1,1,{"fixture"},100,0,timing));assert(h.events.empty()&&!h.ready&&!h.finished);++n;
  }
  assert(n==3);std::cout<<"PASS "<<n<<" controlled flat fixture-owner charged clock composition cases; no actual author/OS receipt claim\n";
}
