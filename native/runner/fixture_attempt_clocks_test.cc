// SPDX-License-Identifier: MIT
#include "fixture_attempt_clocks.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static constexpr FixtureAttemptClockOwner owner{10,20,30,1234};
int main() {
  unsigned n=0;
  {FixtureAttemptClocks c(owner,100,0);const auto x=c.Observe(owner,10);assert(x.charged==10&&x.wall==10&&!x.closed);++n;}
  {FixtureAttemptClocks c(owner,100,0);auto main=c.Active(owner);assert(main->Exclude(1,10));assert(main->Resume(1,110));assert(c.Observe(owner,120).charged==20);assert(c.Observe(owner,120).wall==120);++n;}
  {FixtureAttemptClocks c(owner,100,0);assert(c.BeginCleanup(owner,40));assert(c.Active(owner)->Deadline(50)==140);assert(c.Observe(owner,70).charged==70);++n;}
  {FixtureAttemptClocks c(owner,100,0);auto main=c.Active(owner);assert(c.BeginCleanup(owner,100));assert(main->Expired(101));assert(c.Active(owner)->Deadline(101)==200);assert(c.Observe(owner,110).charged==110);++n;}
  {FixtureAttemptClocks c(owner,100,0);assert(c.Amend(owner,200,30));assert(c.Active(owner)->Deadline(40)==200);assert(c.BeginCleanup(owner,50));assert(c.Active(owner)->Timeout()==200);assert(c.Active(owner)->Deadline(60)==250);++n;}
  {FixtureAttemptClocks c(owner,100,0);assert(c.BeginCleanup(owner,40));assert(c.Amend(owner,50,60));assert(c.Active(owner)->Deadline(70)==90);assert(c.Active(owner)->Expired(90));++n;}
  {FixtureAttemptClocks c(owner,100,0);auto wrong=owner;++wrong.generation;assert(!c.Active(wrong));assert(!c.BeginCleanup(wrong,10));assert(!c.Amend(wrong,0,10));assert(!c.Finish(wrong,10));++n;}
  {FixtureAttemptClocks c(owner,100,0);assert(c.BeginCleanup(owner,10));assert(!c.BeginCleanup(owner,20));assert(c.Active(owner)->Deadline(20)==110);++n;}
  {FixtureAttemptClocks c(owner,100,0);assert(c.Finish(owner,40));assert(!c.Active(owner));assert(!c.Amend(owner,200,50));assert(c.Observe(owner,100).charged==40);assert(c.Observe(owner,100).wall==100);assert(c.RetirementReady(owner));++n;}
  {FixtureAttemptClocks c(owner,100,0);auto main=c.Active(owner);assert(main->Exclude(1,20));assert(c.BeginCleanup(owner,30));assert(c.Observe(owner,40).excluded);assert(c.Finish(owner,50));assert(!c.RetirementReady(owner));assert(main->Resume(1,60));assert(c.RetirementReady(owner));assert(c.Observe(owner,70).charged==20);++n;}
  {FixtureAttemptClocks c(owner,0,0);assert(!c.Active(owner)->Deadline(1000));assert(c.BeginCleanup(owner,2000));assert(!c.Active(owner)->Deadline(3000));assert(c.Observe(owner,4000).charged==4000);++n;}
  {FixtureAttemptClocks c(owner,100,0);auto main=c.Active(owner);assert(main->Observe(30));assert(!c.BeginCleanup(owner,20));assert(c.Active(owner)==main&&!main->Frozen());++n;}
  {FixtureAttemptClocks c(owner,100,30,20);assert(c.Observe(owner,40).charged==30);assert(c.Observe(owner,40).wall==30);assert(c.Active(owner)->Deadline(40)==110);++n;}
  {FixtureAttemptClocks c(owner,100,500,20,500);assert(c.Observe(owner,510).charged==30);assert(c.Observe(owner,510).wall==510);assert(c.Active(owner)->Deadline(510)==580);++n;}
  {bool rejected=false;try{FixtureAttemptClocks c(owner,100,500,20,10);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++n;}
  {FixtureAttemptClocks c(owner,100,0);auto main=c.Active(owner);assert(main->Exclude(11,20));assert(c.BeginCleanup(owner,500));auto cleanup=c.Active(owner);assert(!cleanup->Deadline(900));assert(!main->Resume(12,950));assert(cleanup->Excluded());assert(main->Resume(11,1000));assert(cleanup->Deadline(1010)==1100);assert(c.Observe(owner,1010).charged==30);++n;}
  assert(n==16);std::cout<<"PASS "<<n<<" controlled native associated fixture attempt-clock cases; no runner/physical exit claim\n";
}
