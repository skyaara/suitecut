// SPDX-License-Identifier: MIT
#include "hook_worker.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
struct Harness {
  std::vector<ScopedFixtures::Command> events;
  std::vector<ScopedFixtures::Notice> ready,closed;
  unsigned terminated=0;
  ScopedFixtures manager{{{"fixture",Scope::Test,{},false,500}},{
    [this](auto value){events.push_back(value);},[this](auto value){ready.push_back(value);},
    [this](auto value){closed.push_back(value);},[this](const auto&){++terminated;}}};
};
static constexpr FixtureAttemptClockOwner attempt{1,1,1,123};
static FixtureClockOwner Owner(const ScopedFixtures::Command& event){return {event.token,event.scope,event.generation,1,1,123,false};}
int main(){
  unsigned n=0;
  {
    Harness h;FixtureTimeoutClocks fixtures;FixtureAttemptClocks timing(attempt,100,0);
    assert(h.manager.Open(1,1,FixtureScopeKind::Test,{"fixture"},100,0,timing.Active(attempt)));
    const auto event=h.events[0];const auto owner=Owner(event);
    assert(fixtures.Begin(owner,500,100,0,timing.Active(attempt).get()));h.manager.Tick(200);assert(!h.terminated&&h.ready.empty());
    assert(fixtures.Ready(owner,250));h.manager.Ready(event.token,1,1,250);assert(h.ready.size()==1&&h.ready[0].ok);
    assert(timing.BeginCleanup(attempt,270));assert(h.manager.BeginCleanup(1,1,370,270,timing.Active(attempt)));
    h.manager.Close(1,1,370,270);assert(h.events.back().kind==ScopedFixtures::CommandKind::Release);
    assert(fixtures.Teardown(owner,100,270,timing.Active(attempt).get()));h.manager.Tick(600);assert(!h.terminated&&h.closed.empty());
    assert(fixtures.Settled(owner,700));h.manager.Settled(event.token,1,1,true,"",700);
    assert(h.closed.size()==1&&h.closed[0].ok&&h.manager.Inspect().physical==0);assert(timing.Finish(attempt,700));
    assert(timing.Observe(attempt,700).charged==20&&timing.Observe(attempt,700).wall==700);++n;
  }
  {
    Harness h;FixtureTimeoutClocks fixtures;FixtureAttemptClocks timing(attempt,100,0);
    assert(h.manager.Open(1,1,FixtureScopeKind::Test,{"fixture"},100,0,timing.Active(attempt)));
    const auto event=h.events[0];const auto owner=Owner(event);
    assert(fixtures.Begin(owner,std::nullopt,100,0,timing.Active(attempt).get()));h.manager.Tick(100);
    assert(h.events.back().kind==ScopedFixtures::CommandKind::Cancel&&h.closed.empty());h.manager.Tick(150);assert(!h.terminated);
    assert(fixtures.Settled(owner,200));h.manager.Settled(event.token,1,1,true,"",200);
    assert(h.closed.size()==1&&!h.closed[0].ok&&h.closed[0].error=="Fixture scope original deadline expired");++n;
  }
  {
    Harness h;FixtureTimeoutClocks fixtures;FixtureAttemptClocks timing(attempt,100,0);
    assert(h.manager.Open(1,1,FixtureScopeKind::Test,{"fixture"},100,0,timing.Active(attempt)));
    const auto event=h.events[0];const auto owner=Owner(event);
    assert(fixtures.Begin(owner,500,100,0,timing.Active(attempt).get()));assert(fixtures.Ready(owner,10));h.manager.Ready(event.token,1,1,10);
    assert(timing.BeginCleanup(attempt,20));assert(h.manager.BeginCleanup(1,1,120,20,timing.Active(attempt)));
    assert(timing.Active(attempt)->Expired(130));h.manager.Close(1,1,1130,130);
    assert(fixtures.Teardown(owner,100,130,timing.Active(attempt).get()));h.manager.Tick(500);assert(!h.terminated&&h.closed.empty());
    assert(fixtures.Settled(owner,600));h.manager.Settled(event.token,1,1,true,"",600);
    assert(h.closed.size()==1&&!h.closed[0].ok);assert(timing.Finish(attempt,600));
    assert(timing.Observe(attempt,600).charged==120&&timing.Active(attempt)==nullptr);++n;
  }
  assert(n==3);std::cout<<"PASS "<<n<<" controlled scoped fixture-owner charged clock composition cases; no actual author/OS receipt claim\n";
}
