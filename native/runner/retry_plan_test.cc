// SPDX-License-Identifier: MIT
#include "retry_plan.h"
#include <cassert>
#include <iostream>
namespace suitecut::runner {struct RetryPlanTestAccess {static void Exhaust(RetryPlan& plan){plan.generation_=std::numeric_limits<uint64_t>::max();}};}
using namespace suitecut::runner;
int main(){
 RetryPlan plan({{1,"first",{},0,false},{2,"retry",{},1,false},{3,"expected failure",{},1,true}});
 auto a=plan.Next(10);assert(a&&a->test.id==1);assert(!plan.Finish(11,a->generation,Outcome::Passed,""));assert(!plan.Finish(10,a->generation+1,Outcome::Passed,""));assert(plan.Finish(10,a->generation,Outcome::Passed,""));
 a=plan.Next(10);assert(a&&a->test.id==2&&a->retry==0);assert(plan.Finish(10,a->generation,Outcome::Failed,"actual exception"));assert(plan.NeedsWorkerRetirement());assert(!plan.Next(20));assert(!plan.WorkerExited(20));assert(!plan.Next(20));assert(plan.WorkerExited(10));
 a=plan.Next(20);assert(a&&a->test.id==2&&a->retry==1);assert(plan.Finish(20,a->generation,Outcome::Passed,""));a=plan.Next(20);assert(a&&a->test.id==3);assert(plan.Finish(20,a->generation,Outcome::Failed,"expected actual exception"));assert(plan.Done());assert(plan.Results().size()==4);
 RetryPlan unexpected({{1,"unexpected pass",{},1,true}});a=unexpected.Next(1);assert(unexpected.Finish(1,a->generation,Outcome::Passed,""));assert(!unexpected.Next(2));unexpected.WorkerExited(1);a=unexpected.Next(2);assert(a&&a->retry==1);assert(unexpected.Finish(2,a->generation,Outcome::Failed,""));assert(unexpected.Done());
 { bool rejected=false;try{RetryPlan oversized({{1,std::string(4097,'x'),{},0,false}});}catch(const std::invalid_argument&){rejected=true;}assert(rejected); }
 { RetryPlan exhausted({{1,"test",{},0,false}});RetryPlanTestAccess::Exhaust(exhausted);bool rejected=false;try{exhausted.Next(1);}catch(const std::overflow_error&){rejected=true;}assert(rejected&&exhausted.Results().empty()); }
 std::cout<<"Native retry physical-worker fence tests passed\n";
}
