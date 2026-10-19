// SPDX-License-Identifier: MIT
#include "suite_worker_plan.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
using K=sr::SuiteHookKind;
using P=sr::NativeSuiteWorkerPlan;
sr::NativeSuiteTree Tree(){return sr::NativeSuiteTree({
 {1,0,0,{{K::BeforeAll,1},{K::BeforeEach,2},{K::AfterEach,3},{K::AfterAll,4}}},
 {2,1,1,{{K::BeforeAll,10},{K::BeforeEach,11},{K::AfterEach,12},{K::AfterAll,13}}}},
 {{11,1,0},{21,2,0},{12,1,2}});}
template<class F>void Reject(F callback){bool failed=false;try{callback();}catch(const std::runtime_error&){failed=true;}assert(failed);}
std::vector<std::pair<P::Kind,uint64_t>> Execute(P& plan,uint64_t fail_suite=0,P::Kind fail_kind=P::Kind::Test){
 std::vector<std::pair<P::Kind,uint64_t>> events;
 while(const auto operation=plan.Next()){
  Reject([&]{plan.Next();});
  auto stale=*operation;++stale.epoch;assert(!plan.Complete(stale,true));assert(plan.AwaitingResult());
  events.push_back({operation->kind,operation->kind==P::Kind::Test?operation->test:operation->suite});
  assert(plan.Complete(*operation,!(operation->suite==fail_suite&&operation->kind==fail_kind)));
  assert(!plan.Complete(*operation,true));
 }
 return events;
}
int main(){
 {P plan(Tree(),{11,21,12},1,getpid());auto events=Execute(plan);assert((events==std::vector<std::pair<P::Kind,uint64_t>>{
  {P::Kind::BeforeAll,1},{P::Kind::Test,11},{P::Kind::BeforeAll,2},{P::Kind::Test,21},{P::Kind::AfterAll,2},{P::Kind::Test,12},{P::Kind::AfterAll,1}}));assert(!plan.NeedsRetirement());}
 {P plan(Tree(),{21},1,getpid());auto events=Execute(plan);assert(events.size()==5);assert(events[0].first==P::Kind::BeforeAll&&events[1].second==2&&events.back().second==1);}
 {P plan(Tree(),{21},1,getpid());auto events=Execute(plan,2,P::Kind::BeforeAll);assert(events.size()==4);for(const auto& event:events)assert(event.first!=P::Kind::Test);assert(plan.NeedsRetirement());}
 {P plan(Tree(),{21,12},1,getpid());auto events=Execute(plan,2,P::Kind::Test);assert(events.size()==5);assert(events[3].first==P::Kind::AfterAll&&events.back().second==1);assert(plan.Failures().size()==1);}
 {P plan(Tree(),{21,12},1,getpid());auto events=Execute(plan,2,P::Kind::AfterAll);assert(events.size()==5);assert(events.back().first==P::Kind::AfterAll&&events.back().second==1);assert(plan.NeedsRetirement());}
 {P plan(Tree(),{},1,getpid());assert(!plan.Next());assert(!plan.NeedsRetirement());}
 {P plan(Tree(),{21,12},1,getpid());auto operation=plan.Next();assert(operation);Reject([&]{plan.RetireAfterCurrentTest();});assert(plan.Complete(*operation,true));operation=plan.Next();assert(operation&&plan.Complete(*operation,true));operation=plan.Next();assert(operation&&operation->kind==P::Kind::Test&&plan.Complete(*operation,true));assert(plan.HasClosures());plan.RetireAfterCurrentTest();auto events=Execute(plan);assert(events.size()==2&&events[0].second==2&&events[1].second==1);assert(plan.NeedsRetirement());}
 Reject([]{P plan(Tree(),{21},0,getpid());});
 {sr::DedicatedHostProcess worker("/bin/sleep",{"60"});P plan(Tree(),{21},1,worker.Pid());auto operation=plan.Next();assert(operation);assert(!plan.WorkerClosed(worker));worker.Kill();const auto end=sr::DedicatedRealMilliseconds()+2000;while(!worker.Exited()&&sr::DedicatedRealMilliseconds()<end)worker.Poll(2);assert(worker.Exited());assert(plan.WorkerClosed(worker));assert(plan.Closed()&&!plan.Next()&&!plan.Complete(*operation,true));}
 std::cout<<"PASS nine native worker suite-route lifecycle branches\n";
}
