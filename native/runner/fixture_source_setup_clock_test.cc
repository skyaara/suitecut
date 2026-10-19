// SPDX-License-Identifier: MIT
#include "fixture_source_setup_clock.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
const FixtureClockOwner owner{1,2,3,4,5,1234,true};
int main(){unsigned n=0;
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,0,30000,nullptr,10));const auto slot=InspectFixtureSourceSetupClock(r,owner,11,std::nullopt);assert(slot&&!slot->deadline&&slot->started==10&&FixtureSourceSetupDeadline(*slot)==10+3153600000000.0);++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,0,30000,nullptr,10));const auto slot=InspectFixtureSourceSetupClock(r,owner,100,std::nullopt);const auto again=InspectFixtureSourceSetupClock(r,owner,1000,std::nullopt);assert(slot&&again&&FixtureSourceSetupDeadline(*slot)==FixtureSourceSetupDeadline(*again));++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,0,30000,nullptr,10));const auto slot=InspectFixtureSourceSetupClock(r,owner,11,100);assert(slot&&slot->deadline==100);++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,100,30000,nullptr,10));const auto slot=InspectFixtureSourceSetupClock(r,owner,11,std::nullopt);assert(slot&&slot->deadline==110);++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,100,30000,nullptr,10));auto wrong=owner;++wrong.generation;assert(!InspectFixtureSourceSetupClock(r,wrong,11,std::nullopt));++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,0,30000,nullptr,10));assert(r.Ready(owner,11));assert(!InspectFixtureSourceSetupClock(r,owner,12,std::nullopt));++n;}
 {unsigned expired=0;FixtureTimeoutRuntime r({[&](const auto&,double){++expired;},[](const auto&){}});assert(r.Begin(owner,100,30000,nullptr,10));assert(!InspectFixtureSourceSetupClock(r,owner,110,std::nullopt));assert(r.Physical()==1&&expired==0);r.Tick(110);assert(expired==1&&r.Physical()==1);++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,0,30000,nullptr,10));assert(r.Cancel(owner,11,100));assert(!InspectFixtureSourceSetupClock(r,owner,12,std::nullopt));++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,0,30000,nullptr,10));assert(r.Settled(owner,11));assert(!InspectFixtureSourceSetupClock(r,owner,12,std::nullopt));++n;}
 {FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(owner,0,30000,nullptr,10));assert(!InspectFixtureSourceSetupClock(r,owner,100,100));++n;}
 {auto test=owner;test.worker_fixture=false;auto clock=std::make_shared<FixtureChargedClock>(0,10);FixtureTimeoutRuntime r({[](const auto&,double){},[](const auto&){}});assert(r.Begin(test,std::nullopt,30000,clock,10));const auto slot=InspectFixtureSourceSetupClock(r,test,11,std::nullopt);assert(slot&&!slot->deadline);++n;}
 assert(n==11);std::cout<<"PASS "<<n<<" native live-vs-unbounded fixture Source setup clock controls; no CEF claim\n";
}
