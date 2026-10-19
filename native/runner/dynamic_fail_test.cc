// SPDX-License-Identifier: MIT
#include "retry_plan.h"
#include "hook_phases.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut::runner;
int main(){
  {RetryPlan p({{1,"one",{},1,false}});auto a=p.Next(42);assert(a);
   assert(!p.ExpectFailure(43,a->generation)&&!p.ExpectFailure(42,a->generation+1));
   assert(p.ExpectFailure(42,a->generation));assert(p.Finish(42,a->generation,Outcome::Passed,""));
   assert(!p.Results().back().expected&&p.NeedsWorkerRetirement());assert(!p.Next(43));
   assert(p.WorkerExited(42));a=p.Next(43);assert(a&&a->retry==1&&!a->test.expectedFailure);
   assert(p.Finish(43,a->generation,Outcome::Passed,""));assert(p.Results().back().expected);
  }
  {RetryPlan p({{1,"two",{},1,false}},1);auto a=p.Next(8);assert(p.ExpectFailure(8,a->generation));
   assert(p.Finish(8,a->generation,Outcome::Failed,"actual body"));assert(p.Results().back().expected);
   assert(!p.FailureLimitReached()&&p.UnexpectedTestFailures()==0);
  }
  {RetryPlan p({{1,"three",{},0,false}},1);auto a=p.Next(8);assert(p.ExpectFailure(8,a->generation));
   assert(p.Finish(8,a->generation,Outcome::Failed,"actual cleanup",true));assert(!p.Results().back().expected&&p.FailureLimitReached());
  }
  {for(auto status:{Outcome::TimedOut,Outcome::Interrupted}){RetryPlan p({{1,"four",{},0,false}});auto a=p.Next(8);assert(p.ExpectFailure(8,a->generation));assert(p.Finish(8,a->generation,status,"real stop"));assert(!p.Results().back().expected);}}
  {std::deque<HookPhases::Acquire> acquire;std::deque<HookPhases::Invoke> invoke;std::deque<HookPhases::Release> release;std::vector<HookPhases::Result> results;
   HookPhases p({[&](const auto& q){acquire.push_back(q);},[&](const auto& q){invoke.push_back(q);},[](const auto&){},[&](const auto& q){release.push_back(q);},[&](const auto& q){results.push_back(q);},[](const auto&){assert(false);}});
   assert(p.Begin(1,2,{{HookKind::Body,1,{}},{HookKind::AfterEach,2,{}}},100,0));auto q=acquire.front();acquire.pop_front();p.Acquired(q.request,1,2,true,"",1);auto body=invoke.front();invoke.pop_front();
   assert(!p.CanExpectFailure(body.token,1,3,2)&&p.CanExpectFailure(body.token,1,2,2));
   p.Settled(body.token,1,2,false,"actual body",3);q=acquire.front();acquire.pop_front();p.Acquired(q.request,1,2,true,"",4);auto after=invoke.front();invoke.pop_front();
   assert(!p.CanExpectFailure(after.token,1,2,5));p.Settled(after.token,1,2,false,"actual cleanup",6);auto rel=release.front();p.Released(rel.request,1,2,true,"",7);
   assert(results.size()==1&&!results[0].ok&&results[0].infrastructure_failure&&results[0].errors.size()==2);
  }
  std::cout<<"PASS5 native dynamic expectation/cleanup admission groups\n";
}
