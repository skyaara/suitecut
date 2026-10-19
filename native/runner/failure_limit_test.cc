// SPDX-License-Identifier: MIT
#include "retry_plan.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main(){
  namespace sr=suitecut::runner;
  {
    sr::RetryPlan plan({{1,"retry",{},1,false},{2,"not admitted",{},0,false}},1);
    auto first=*plan.Next(11);
    assert(plan.Finish(11,first.generation,sr::Outcome::Failed,"first actual failure"));
    assert(plan.UnexpectedTestFailures()==0&&!plan.FailureLimitReached());
    assert(!plan.Next(12)&&!plan.Done());assert(plan.WorkerExited(11));
    auto retry=*plan.Next(12);assert(retry.test.id==1&&retry.retry==1);
    assert(plan.Finish(12,retry.generation,sr::Outcome::Failed,"final actual failure"));
    assert(plan.UnexpectedTestFailures()==1&&plan.FailureLimitReached());
    assert(!plan.Done()&&!plan.Next(13));assert(plan.WorkerExited(12));
    assert(plan.Done()&&!plan.Next(13)&&plan.Results().size()==2);
    assert(!plan.Finish(12,retry.generation,sr::Outcome::Failed,"duplicate"));
    assert(plan.UnexpectedTestFailures()==1);
  }
  {
    sr::RetryPlan plan({{1,"flaky",{},1,false},{2,"final failure",{},0,false},{3,"unrun",{},0,false}},1);
    auto test=*plan.Next(20);assert(plan.Finish(20,test.generation,sr::Outcome::Failed,"first"));
    assert(plan.WorkerExited(20));test=*plan.Next(21);
    assert(plan.Finish(21,test.generation,sr::Outcome::Passed,""));
    assert(plan.UnexpectedTestFailures()==0&&!plan.FailureLimitReached());
    test=*plan.Next(21);assert(test.test.id==2);
    assert(plan.Finish(21,test.generation,sr::Outcome::TimedOut,"actual timeout"));
    assert(plan.FailureLimitReached()&&plan.UnexpectedTestFailures()==1);
    assert(plan.WorkerExited(21)&&plan.Done());
  }
  {
    sr::RetryPlan plan({{1,"expected failure",{},0,true},{2,"unexpected pass",{},0,true},{3,"unrun",{},0,false}},1);
    auto test=*plan.Next(30);assert(plan.Finish(30,test.generation,sr::Outcome::Failed,"expected"));
    assert(plan.UnexpectedTestFailures()==0);test=*plan.Next(30);
    assert(plan.Finish(30,test.generation,sr::Outcome::Passed,""));
    assert(plan.FailureLimitReached()&&plan.UnexpectedTestFailures()==1);
    assert(plan.WorkerExited(30)&&plan.Done());
  }
  {
    sr::RetryPlan plan({{1,"actual interruption",{},2,false},{2,"not admitted by signal owner",{},0,false}},1);
    const auto test=*plan.Next(40);
    assert(plan.Finish(40,test.generation,sr::Outcome::Interrupted,"actual SIGINT"));
    assert(!plan.FailureLimitReached()&&plan.UnexpectedTestFailures()==0);
    assert(plan.WorkerExited(40));const auto next=plan.Next(41);
    assert(next&&next->test.id==2&&next->retry==0);
  }
  {
    sr::RetryPlan plan({{1,"one",{},0,false},{2,"two",{},0,false}},0);
    auto test=*plan.Next(50);assert(plan.Finish(50,test.generation,sr::Outcome::Failed,"one"));
    assert(plan.WorkerExited(50));test=*plan.Next(51);
    assert(plan.Finish(51,test.generation,sr::Outcome::Failed,"two"));
    assert(plan.WorkerExited(51)&&plan.Done());
    assert(!plan.FailureLimitReached()&&plan.UnexpectedTestFailures()==2);
  }
  bool rejected=false;try{sr::RetryPlan invalid({},4097);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
}
