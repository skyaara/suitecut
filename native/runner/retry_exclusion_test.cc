// SPDX-License-Identifier: MIT
#include "retry_plan.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Reject(F callback){bool rejected=false;try{callback();}catch(const std::runtime_error&){rejected=true;}assert(rejected);}
int main(){
 {RetryPlan plan({{1,"one",{}},{2,"two",{}},{3,"three",{}}});plan.ExcludeQueued({2});auto a=plan.Next(1);assert(a&&a->test.id==1);Reject([&]{plan.ExcludeQueued({3});});assert(plan.Finish(1,a->generation,Outcome::Passed,""));a=plan.Next(1);assert(a&&a->test.id==3);assert(plan.Finish(1,a->generation,Outcome::Passed,""));assert(plan.Done()&&plan.Results().size()==2);}
 {RetryPlan plan({{1,"one",{},1},{2,"two",{}},{3,"three",{}}});auto a=plan.Next(7);assert(a);assert(plan.Finish(7,a->generation,Outcome::Failed,"actual failure"));plan.ExcludeQueued({1,2});assert(plan.NeedsWorkerRetirement()&&!plan.Next(8));assert(!plan.WorkerExited(8));assert(plan.WorkerExited(7));a=plan.Next(8);assert(a&&a->test.id==3);assert(plan.Results().size()==1&&plan.Results()[0].outcome==Outcome::Failed);}
 {RetryPlan plan({{1,"one",{},1},{2,"two",{}}});auto a=plan.Next(7);assert(plan.Finish(7,a->generation,Outcome::Failed,"real"));plan.ExcludeQueued({2});assert(!plan.Next(8));assert(plan.WorkerExited(7));a=plan.Next(8);assert(a&&a->test.id==1&&a->retry==1);assert(plan.Finish(8,a->generation,Outcome::Passed,""));assert(plan.Done());}
 {RetryPlan plan({{1,"one",{}},{2,"two",{}}});Reject([&]{plan.ExcludeQueued({2,999});});auto a=plan.Next(1);assert(a&&a->test.id==1);assert(plan.Finish(1,a->generation,Outcome::Passed,""));a=plan.Next(1);assert(a&&a->test.id==2);}
 {RetryPlan plan({{1,"one",{}}});plan.ExcludeQueued({1});assert(plan.Done()&&plan.Results().empty());}
 {RetryPlan plan({{1,"one",{}},{2,"two",{}},{3,"three",{}}},1);plan.ExcludeQueued({2});auto a=plan.Next(7);assert(plan.Finish(7,a->generation,Outcome::Failed,"real limit failure"));assert(plan.FailureLimitReached()&&plan.UnexpectedTestFailures()==1);assert(!plan.Done()&&!plan.Next(8));assert(plan.WorkerExited(7)&&plan.Done());assert(plan.Results().size()==1);}
 std::cout<<"PASS six queue-exclusion branches including maxFailures and exact exit fence\n";
}
