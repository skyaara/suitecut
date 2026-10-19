// SPDX-License-Identifier: MIT
#include "runner_retry_plan.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static std::vector<TestCase> Tests(){return {{1,"ordinary",{},0,false},{2,"group-first",{},1,false},{3,"group-second",{},0,false},{4,"after",{},0,false}};}
static Attempt Next(RunnerRetryPlan& plan,uint64_t pid,uint64_t index,uint64_t generation,uint64_t test,unsigned retry=0){const auto a=plan.Next(pid,generation,index);assert(a&&a->test.id==test&&a->retry==retry);return *a;}
static void Finish(RunnerRetryPlan& plan,uint64_t pid,const Attempt& a,Outcome outcome=Outcome::Passed){assert(plan.Finish(pid,a.generation,outcome,""));}
template<class F>static void Reject(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {RunnerRetryPlan p(Tests(),0,0,{{7,{2,3}}});Finish(p,101,Next(p,101,1,1,1));auto a=Next(p,101,1,2,2);assert(a.test.retries==1);Finish(p,101,a);a=Next(p,101,1,3,3);assert(a.test.retries==1);Finish(p,101,a,Outcome::Failed);
  assert(!p.Next(102,4,2)&&p.NeedsWorkerRetirement()&&p.UnexpectedTestFailures()==0);assert(!p.WorkerExited(101,2)&&!p.WorkerExited(102,1));assert(p.WorkerExited(101,1));
  Finish(p,102,Next(p,102,2,5,2,1));Finish(p,102,Next(p,102,2,8,3,1));assert(p.NeedsWorkerRetirement()&&!p.Done());p.ActualWorkerExited(102,2);
  assert(p.Finalized(2)&&p.Finalized(3)&&p.FinalExpected(2)&&p.FinalExpected(3));Finish(p,103,Next(p,103,3,9,4));assert(p.Done()&&p.Results().size()==6&&p.UnexpectedTestFailures()==0);++controls;}
 {RunnerRetryPlan p(Tests(),1,0,{{7,{2,3}}});Finish(p,101,Next(p,101,1,1,1));Finish(p,101,Next(p,101,1,2,2),Outcome::Failed);p.ActualWorkerExited(101,1);
  Finish(p,102,Next(p,102,2,3,2,1),Outcome::Failed);assert(!p.FailureLimitReached()&&!p.Done());p.ActualWorkerExited(102,2);
  assert(p.FailureLimitReached()&&p.Done()&&p.UnexpectedTestFailures()==1&&!p.Next(103,4,3));assert(p.Results().size()==3&&p.Groups().front().policy->Omissions().size()==2);++controls;}
 {RunnerRetryPlan p(Tests(),0,0,{{7,{2,3}}});Finish(p,101,Next(p,101,1,1,1));Finish(p,101,Next(p,101,1,2,2),Outcome::Failed);p.ActualWorkerExited(101,1);
  Finish(p,102,Next(p,102,2,3,2,1),Outcome::Failed);p.ActualWorkerExited(102,2);Finish(p,103,Next(p,103,3,4,4));assert(p.Done()&&p.UnexpectedTestFailures()==1);++controls;}
 // A genuine beforeAll skip can exclude the remaining selected group members
 // without generating attempts or reopening the skipped declarations on replay.
 {RunnerRetryPlan p(Tests(),0,0,{{7,{2,3}}});Finish(p,101,Next(p,101,1,1,1));Finish(p,101,Next(p,101,1,2,2),Outcome::Skipped);p.ExcludeQueued({3});
  assert(p.NeedsWorkerRetirement());p.ActualWorkerExited(101,1);Finish(p,102,Next(p,102,2,3,4));assert(p.Done()&&p.Results().size()==3&&p.Groups().front().policy->Omissions().empty());++controls;}
 // No-group calls retain the original RetryPlan scheduling and retirement path.
 {RunnerRetryPlan p(Tests());Finish(p,101,Next(p,101,1,1,1));Finish(p,101,Next(p,101,1,2,2),Outcome::Failed);assert(!p.Next(102,3,2));assert(p.WorkerExited(101,1));
  Finish(p,102,Next(p,102,2,3,2,1));Finish(p,102,Next(p,102,2,4,3));Finish(p,102,Next(p,102,2,5,4));assert(p.Done()&&p.Groups().empty());++controls;}
 // Original global stop wins over a pending serial replay and never allocates
 // a replacement host or a new attempt generation inside this policy.
 {RunnerRetryPlan p(Tests(),0,0,{{7,{2,3}}});Finish(p,101,Next(p,101,1,1,1));Finish(p,101,Next(p,101,1,2,2),Outcome::Failed);p.Stop();assert(!p.Done());p.ActualWorkerExited(101,1);
  assert(p.Done()&&!p.Next(102,3,2)&&p.Groups().front().policy->Current()==SerialGroup::State::Stopped);++controls;}
 // An actual physical exit before complete callback result is interrupted; it
 // cannot fabricate successful settlement or free a different native owner.
 {RunnerRetryPlan p(Tests(),0,0,{{7,{2,3}}});Finish(p,101,Next(p,101,1,1,1));Next(p,101,1,2,2);p.ActualWorkerExited(101,2);assert(!p.Done());p.ActualWorkerExited(101,1);
  assert(p.Done()&&p.Results().back().outcome==Outcome::Interrupted&&!p.Results().back().expected&&p.Results().size()==2);++controls;}
 // A current active attempt, stale shared generation or overlapping group
 // declaration cannot mutate another admission or silently reorder tests.
 {RunnerRetryPlan p(Tests(),0,0,{{7,{2,3}}});Finish(p,101,Next(p,101,1,10,1));Reject([&]{p.Next(101,9,1);});auto a=Next(p,101,1,11,2);assert(!p.Next(102,12,2)&&!p.Finish(102,a.generation,Outcome::Failed,""));Finish(p,101,a);assert(p.Results().size()==2);++controls;}
 {Reject([]{RunnerRetryPlan p(Tests(),0,0,{{7,{2,3}},{8,{3,4}}});});Reject([]{RunnerRetryPlan p(Tests(),0,0,{{7,{3,2}}});});Reject([]{RunnerRetryPlan p(Tests(),0,0,{{7,{2,4}}});});Reject([]{RunnerRetryPlan p(Tests(),0,0,{{7,{99}}});});Reject([]{RunnerRetryPlan p(Tests(),0,0,{{0,{2}}});});++controls;}
 std::cout<<"Native composed retry policy "<<controls<<" controls passed\n";
}
