// SPDX-License-Identifier: MIT
#include "fixture_timeout_clocks.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace suitecut::runner;
static FixtureClockOwner Owner(uint64_t token = 1, bool worker = false) {
  return {token, 10, 20, token + 100, 30, 1234, worker};
}
int main() {
  unsigned controls = 0;
  { FixtureChargedClock c(100, 0); assert(c.Charged(20)==20 && c.Wall(20)==20); ++controls; }
  { FixtureChargedClock c(100, 0); assert(c.Exclude(1,20)); assert(c.Charged(200)==20); assert(c.Resume(1,200)); assert(c.Deadline(210)==280); assert(c.Wall(210)==210); ++controls; }
  { FixtureChargedClock c(100, 0); assert(c.Exclude(1,20)); assert(!c.Resume(2,30)); assert(c.Excluded()); assert(c.Resume(1,40)); ++controls; }
  { FixtureChargedClock c(100, 0); assert(!c.Exclude(1,100)); assert(c.Expired(100)); assert(!c.Amend(0,101)); ++controls; }
  { FixtureChargedClock c(100, 0); assert(c.Amend(50,20)); assert(c.Deadline(30)==50); assert(c.Expired(50)); ++controls; }
  { FixtureChargedClock c(0, 0); assert(!c.Deadline(1000)); assert(c.Amend(20,1000)); assert(c.Expired(1000)); ++controls; }
  { FixtureChargedClock c(100, 0); assert(c.Exclude(1,20)); assert(c.Amend(30,200)); assert(c.Resume(1,300)); assert(c.Deadline(301)==310); ++controls; }
  { FixtureChargedClock c(100, 0); assert(!c.Observe(-1)); assert(!c.Observe(std::numeric_limits<double>::infinity())); assert(c.Charged(1)==1); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(f.Begin(o,500,100,10,&c)); assert(f.Deadline(o,20)==510); assert(!c.Deadline(20)); assert(f.Ready(o,200)); assert(f.Physical()==1 && !f.IdleReady()); assert(c.Charged(210)==20); assert(f.Settled(o,220)); assert(f.Physical()==0); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(f.Begin(o,std::nullopt,100,10,&c)); assert(f.Deadline(o,20)==100); assert(!c.Excluded()); assert(f.Ready(o,30)); assert(c.Charged(40)==40); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(1,true); assert(f.Begin(o,std::nullopt,200,10,&c)); assert(f.Deadline(o,20)==210); assert(f.Ready(o,150)); assert(f.IdleReady() && f.Physical()==1); assert(c.Charged(160)==20); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock main(100,0), cleanup(100,200); auto o=Owner(); assert(f.Begin(o,500,100,0,&main)); assert(f.Ready(o,100)); assert(main.Expired(200)); assert(f.Teardown(o,100,200,&cleanup)); assert(f.Deadline(o,201)==700); assert(f.Settled(o,600)); assert(cleanup.Charged(610)==10); assert(f.Records().size()==2); ++controls; }
  { FixtureTimeoutClocks f; auto o=Owner(); assert(f.Begin(o,100,500,10,nullptr)); assert(f.Ready(o,50)); assert(f.Teardown(o,500,1000,nullptr)); assert(f.Deadline(o,1001)==1100); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(f.Begin(o,0,100,0,&c)); assert(!f.Deadline(o,1000)); assert(f.Physical()==1); assert(f.Cancel(o,1001)); assert(c.Excluded()); assert(f.Settled(o,1010)); assert(!c.Excluded()); ++controls; }
  { FixtureTimeoutClocks f; auto o=Owner(); assert(f.Begin(o,500,100,0,nullptr)); assert(f.Amend(o,100,50)); assert(f.Deadline(o,60)==100); assert(!f.Ready(o,100)); assert(f.Physical()==1); assert(f.Records()[0].expired); assert(!f.Amend(o,0,101)); assert(f.Settled(o,110)); ++controls; }
  { FixtureTimeoutClocks f; auto o=Owner(); assert(f.Begin(o,0,100,0,nullptr)); assert(f.Amend(o,100,50)); assert(f.Ready(o,60)); assert(f.Teardown(o,100,200,nullptr)); assert(f.Deadline(o,201)==300); ++controls; }
  { FixtureTimeoutClocks f; auto o=Owner(); assert(f.Begin(o,100,100,0,nullptr)); auto wrong=o; ++wrong.generation; assert(!f.Ready(wrong,1)); assert(!f.Settled(wrong,1)); assert(f.Physical()==1); ++controls; }
  { FixtureTimeoutClocks f; auto o=Owner(); assert(f.Begin(o,100,100,0,nullptr)); assert(!f.Ready(o,-1)); assert(!f.Teardown(o,100,1,nullptr)); assert(!f.Begin(o,100,100,1,nullptr)); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(f.Begin(o,100,100,0,&c)); assert(!f.ActualWorkerExited(30,9999,20)); assert(f.Physical()==1); assert(f.Cancel(o,30)); assert(f.Physical()==1); assert(f.ActualWorkerExited(30,1234,40)); assert(f.Physical()==0 && !c.Excluded()); assert(!f.Records()[0].raw_settled); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto first=Owner(), second=Owner(2); assert(f.Begin(first,100,100,0,&c)); assert(!f.Begin(second,100,100,1,&c)); assert(f.Ready(first,10)); assert(f.Begin(second,100,100,20,&c)); assert(f.Settled(first,30)); assert(c.Excluded()); assert(f.Ready(second,40)); assert(!c.Excluded()); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(f.Begin(o,100,100,0,&c)); assert(!f.Ready(o,100)); assert(f.Physical()==1 && c.Excluded()); assert(f.Cancel(o,101)); assert(f.Settled(o,110)); assert(!c.Excluded()); ++controls; }
  { FixtureTimeoutClocks f; auto o=Owner(); assert(!f.Begin(o,120001,100,0,nullptr)); o.declaration=0; assert(!f.Begin(o,100,100,0,nullptr)); assert(f.Physical()==0); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(f.Begin(o,0,100,0,&c)); assert(f.NextDeadline(o,200,500)==500); assert(f.Cancel(o,500)); assert(f.Physical()==1 && c.Excluded()); assert(f.Settled(o,510)); assert(!c.Excluded()); ++controls; }
  { FixtureTimeoutClocks f; auto o=Owner(1,true); assert(f.Begin(o,2000,100,0,nullptr)); assert(f.Ready(o,10)); assert(f.Teardown(o,100,20,nullptr)); assert(f.NextDeadline(o,21,5000)==2020); assert(f.Physical()==1); assert(f.Settled(o,1500)); assert(!f.Records().back().expired); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(f.Begin(o,std::nullopt,100,10,&c)); assert(!f.Ready(o,100)); assert(f.Records()[0].expired); assert(f.Physical()==1); assert(f.Settled(o,110)); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock c(100,0); auto o=Owner(); assert(c.Observe(30)); assert(!f.Begin(o,100,100,20,&c)); assert(f.Physical()==0); assert(f.Begin(o,100,100,30,&c)); assert(c.Observe(50)); assert(!f.Ready(o,40)); assert(f.Records()[0].elapsed==0); assert(f.Ready(o,60)); ++controls; }
  { FixtureTimeoutClocks f; FixtureChargedClock main(100,0), cleanup(100,200); auto o=Owner(); assert(f.Begin(o,500,100,0,&main)); assert(f.Ready(o,10)); assert(cleanup.Expired(300)); assert(f.Teardown(o,100,300,&cleanup)); assert(f.Deadline(o,301)==800); assert(cleanup.Expired(310)); assert(f.Settled(o,600)); assert(cleanup.Expired(610)); assert(cleanup.Charged(610)==110); ++controls; }
  assert(controls==27);
  std::cout << "PASS " << controls << " controlled native fixture-clock cases; no host/process/original-test execution claim\n";
}
