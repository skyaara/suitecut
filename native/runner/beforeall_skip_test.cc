// SPDX-License-Identifier: MIT
#include "suite_worker_plan.h"
#include "hook_phases.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut::runner;
using P=NativeSuiteWorkerPlan;using K=SuiteHookKind;
NativeSuiteTree Tree(){return NativeSuiteTree({
 {1,0,0,{{K::BeforeAll,1},{K::BeforeEach,3},{K::AfterEach,4},{K::AfterAll,2}}},
 {2,1,0,{{K::BeforeAll,10},{K::AfterAll,11}}},
 {3,2,0,{{K::BeforeAll,30},{K::AfterAll,31}}}},
 {{31,3,0},{21,2,1},{90,1,1}});}
struct PhaseProof{
 std::deque<HookPhases::Acquire> acquire;std::deque<HookPhases::Invoke> invoke;std::deque<HookPhases::Release> release;
 std::vector<HookPhases::Result> results;std::vector<std::string> errors;HookPhases phases;
 PhaseProof():phases({[&](const auto& c){acquire.push_back(c);},[&](const auto& c){invoke.push_back(c);},[](const auto&){},[&](const auto& c){release.push_back(c);},[&](const auto& c){results.push_back(c);},[&](const auto& e){errors.push_back(e);}}){}
 HookPhases::Invoke Admit(double now){const auto q=acquire.front();acquire.pop_front();phases.Acquired(q.request,q.scope,q.generation,true,"",now);const auto c=invoke.front();invoke.pop_front();return c;}
 void Release(double now){const auto q=release.front();release.pop_front();phases.Released(q.request,q.scope,q.generation,true,"",now);}
};
int main(){
 {P p(Tree(),{31,21,90},1,getpid());auto root=p.Next();assert(root&&p.Complete(*root,true));auto entered=p.Next();assert(entered&&entered->suite==2);
  auto stale=*entered;++stale.token;assert(!p.SkipBeforeAll(stale,77)&&p.AwaitingResult());assert(!p.SkipBeforeAll(*entered,0));
  assert(p.SkipBeforeAll(*entered,77));auto skipped=p.Next();assert(skipped&&skipped->kind==P::Kind::SkippedTest&&skipped->test==31&&skipped->suite==2);
  assert(skipped->skip_cause==77&&(skipped->skipped_tests==std::vector<uint64_t>{21})&&skipped->before_each.empty()&&skipped->after_each.empty());
  stale=*skipped;++stale.skip_cause;assert(!p.Complete(stale,true));assert(p.Complete(*skipped,true));
  auto close=p.Next();assert(close&&close->kind==P::Kind::AfterAll&&close->suite==2&&(close->callbacks==std::vector<uint64_t>{11}));assert(p.Complete(*close,true));
  auto sibling=p.Next();assert(sibling&&sibling->kind==P::Kind::Test&&sibling->test==90&&(sibling->before_each==std::vector<uint64_t>{3})&&(sibling->after_each==std::vector<uint64_t>{4}));assert(p.Complete(*sibling,true));
  close=p.Next();assert(close&&close->kind==P::Kind::AfterAll&&close->suite==1);assert(p.Complete(*close,true));assert(!p.Next()&&!p.NeedsRetirement());
 }
 {P p(Tree(),{31,21,90},2,getpid());auto root=p.Next();assert(root&&p.SkipBeforeAll(*root,88));auto skipped=p.Next();assert(skipped&&skipped->kind==P::Kind::SkippedTest&&(skipped->skipped_tests==std::vector<uint64_t>{21,90}));assert(p.Complete(*skipped,true));auto close=p.Next();assert(close&&close->suite==1&&close->kind==P::Kind::AfterAll);assert(p.Complete(*close,true));assert(!p.Next());}
 {P p(Tree(),{31,21,90},3,getpid());auto q=p.Next();assert(q&&p.Complete(*q,true));q=p.Next();assert(q&&p.SkipBeforeAll(*q,99));q=p.Next();assert(q&&p.Complete(*q,true));q=p.Next();assert(q&&q->suite==2&&p.Complete(*q,false));q=p.Next();assert(q&&q->suite==1&&p.Complete(*q,true));assert(!p.Next()&&p.NeedsRetirement());}
 {PhaseProof p;assert(p.phases.Begin(10,2,{{HookKind::BeforeAll,70,{}}},100,0));auto c=p.Admit(1);assert(!p.phases.CanExpectFailure(c.token,10,2,2));assert(p.phases.RequestSkip(c.token,10,2,"\"actual cause\"",90));assert(p.release.front().deadline==100);p.Release(95);assert(p.results.empty()&&p.phases.Inspect().physical==1);p.phases.Settled(c.token,10,2,true,"",96,true);assert(p.results.size()==1&&p.results[0].ok&&p.results[0].skipped&&p.results[0].skip_kind==HookKind::BeforeAll&&p.results[0].skip_callback==70&&p.results[0].skip_token==c.token);}
 {PhaseProof p;assert(p.phases.Begin(10,2,{{HookKind::BeforeAll,70,{}}},100,0));auto c=p.Admit(1);assert(p.phases.RequestSkip(c.token,10,2,"\"actual cause\"",90));p.Release(95);p.phases.Tick(100);assert(p.results.empty()&&p.phases.Inspect().physical==1&&p.errors.size()==1);p.phases.WorkerClosed(101);assert(!p.results[0].ok&&p.results[0].timed_out&&p.results[0].skipped&&p.results[0].skip_kind==HookKind::BeforeAll);}
 {PhaseProof p;assert(p.phases.Begin(10,2,{{HookKind::AfterAll,70,{}}},100,0));auto c=p.Admit(1);assert(!p.phases.RequestSkip(c.token,10,2,"\"still unsupported\"",2));p.phases.Settled(c.token,10,2,true,"",3);p.Release(4);assert(p.results[0].ok&&!p.results[0].skipped);}
 std::cout<<"PASS6 native beforeAll skip route/physical ownership groups\n";
}
