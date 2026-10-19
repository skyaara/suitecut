// SPDX-License-Identifier: MIT
#include "fixture_transition_time.h"
#include "fixture_timeout_runtime.h"
#include "fixture_graph.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
using namespace suitecut::runner;
static double ActualNow(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static double LaterThan(double before){std::this_thread::sleep_for(std::chrono::milliseconds(2));const auto now=ActualNow();assert(now>before);return now;}
struct Harness {
 FixtureTransitionTime time;
 bool pinned,expired=false,finished=false;
 unsigned cancels=0;
 double event_time=0,later_sample=0;
 FixtureClockOwner owner{};
 std::shared_ptr<FixtureAttemptClocks> timing;
 FixtureTimeoutRuntime runtime;
 FixtureOwners manager;
 explicit Harness(bool pin):pinned(pin),runtime({[this](const auto& record,double now){
   event_time=now;later_sample=LaterThan(now);
   if(pinned){FixtureTransitionTime::Scope event(time,now);assert(manager.FixtureExpired(record.owner.token,1,1,"actual slot expired",now));}
   else assert(manager.FixtureExpired(record.owner.token,1,1,"actual slot expired",later_sample));
 },[](const auto&){assert(false);}}),manager(FixtureGraph({{"fixture",Scope::Test,{},false,1}}),{
  [this](const auto& event){
   const auto sampled=ActualNow();const auto now=time.Current().value_or(sampled);
   if(event.kind==FixtureOwners::EventKind::Setup){owner={event.token,1,1,1,1,123,false};assert(runtime.Begin(owner,1,10000,manager.ActiveChargedClock(),now));}
   else {++cancels;assert(runtime.Cancel(owner,now,now+1000));(void)manager.ActiveDeadline(now);}
  },[](auto,auto){assert(false);},[this](auto,auto,bool ok,const auto& error){assert(!ok&&error=="actual slot expired");finished=true;},[](const auto&){assert(false);}}){}
 void Begin(){const auto now=ActualNow();timing=std::make_shared<FixtureAttemptClocks>(FixtureAttemptClockOwner{1,1,1,123},10000,now);FixtureTransitionTime::Scope event(time,now);assert(manager.Begin(1,1,{"fixture"},now+10000,now,timing));assert(runtime.Physical()==1&&manager.State().physical==1);}
 void Tick(double now){if(pinned){FixtureTransitionTime::Scope event(time,now);runtime.Tick(now);(void)manager.ActiveDeadline(now);}else {runtime.Tick(now);(void)manager.ActiveDeadline(now);}expired=true;}
};
int main(){unsigned n=0;
 {FixtureTransitionTime t;assert(!t.Current());{FixtureTransitionTime::Scope a(t,10);assert(t.Current()==10);{FixtureTransitionTime::Scope b(t,10);assert(t.Current()==10);}assert(t.Current()==10);}assert(!t.Current());++n;}
 {FixtureTransitionTime t;FixtureTransitionTime::Scope a(t,10);bool threw=false;try{FixtureTransitionTime::Scope b(t,11);}catch(const std::invalid_argument&){threw=true;}assert(threw&&t.Current()==10);++n;}
 {FixtureTransitionTime t;bool threw=false;try{FixtureTransitionTime::Scope a(t,std::numeric_limits<double>::quiet_NaN());}catch(const std::invalid_argument&){threw=true;}assert(threw&&!t.Current());++n;}
 {FixtureTransitionTime t;bool threw=false;try{FixtureTransitionTime::Scope a(t,-1);}catch(const std::invalid_argument&){threw=true;}assert(threw&&!t.Current());++n;}
 {FixtureTransitionTime t;try{FixtureTransitionTime::Scope a(t,10);throw 1;}catch(int){}assert(!t.Current());++n;}
 // Preserve a real later callback observation reproducing the production bug.
 {Harness h(false);h.Begin();const auto now=LaterThan(h.runtime.Records()[0].started);bool threw=false;try{h.Tick(now);}catch(const std::invalid_argument& error){threw=std::string(error.what())=="Native charged clock time rejected";}assert(threw&&h.later_sample>now&&h.cancels==1&&!h.finished);++n;}
 // The same real FixtureOwners Fail -> cleanup carry -> Cancel -> outer refresh
 // transaction uses the Runtime event observation. No receipt is synthesized.
 {Harness h(true);h.Begin();const auto now=LaterThan(h.runtime.Records()[0].started);h.Tick(now);assert(h.expired&&h.event_time==now&&h.later_sample>now&&h.cancels==1&&!h.finished&&!h.time.Current());assert(h.runtime.Physical()==1&&h.manager.State().physical==1);assert(h.runtime.Records()[0].canceled&&!h.runtime.Records()[0].raw_settled);const auto settled=ActualNow();{FixtureTransitionTime::Scope event(h.time,settled);assert(h.runtime.Settled(h.owner,settled));h.manager.Settled(h.owner.token,1,1,true,"",settled);}assert(h.finished&&!h.runtime.Physical()&&!h.manager.State().physical);++n;}
 {FixtureChargedClock c(100,10);(void)c.Deadline(20);bool threw=false;try{(void)c.Deadline(19);}catch(const std::invalid_argument&){threw=true;}assert(threw);++n;}
 assert(n==8);std::cout<<"PASS "<<n<<" native synchronous fixture event-time controls; real later clock sample and retained model raw lease; no host/waitpid claim\n";
}
