// SPDX-License-Identifier: MIT
#include "serial_group.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
using State=SerialGroup::State;
using Classification=SerialGroup::Classification;
static const SerialGroup::Worker first{0,101,1},second{1,102,1},third{2,103,1};
static std::vector<TestCase> Tests(unsigned retries=0){return {{1,"first",{},retries,false},{2,"second",{},retries,false},{3,"third",{},retries,false}};}
static Attempt Admit(SerialGroup& plan,SerialGroup::Worker worker,uint64_t generation,uint64_t test,unsigned retry=0){
 const auto a=plan.Next(worker,generation);assert(a&&a->test.id==test&&a->retry==retry&&a->generation==generation);return *a;
}
static void Finish(SerialGroup& plan,SerialGroup::Worker worker,const Attempt& a,Outcome outcome=Outcome::Passed){assert(plan.Finish(worker,a.generation,outcome));}
template<typename F>static void Reject(F function){bool rejected=false;try{function();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){
 unsigned controls=0;
 // Public serial behavior: skip later members after an actual failure; replay
 // earlier passed members too, with the same group retry index.
 {SerialGroup p(Tests(1));assert(p.Begin(first));Finish(p,first,Admit(p,first,10,1));Finish(p,first,Admit(p,first,12,2),Outcome::Failed);
  assert(p.Current()==State::Retiring&&!p.Done()&&p.FinalUnexpectedTests()==0);assert(!p.Next(first,13));
  assert(p.Results().size()==2&&p.Omissions().size()==1);const auto omitted=p.Omissions().front();assert(omitted.test_id==3&&omitted.cause_test==2&&omitted.cause_generation==12&&omitted.retry==0);
  assert(!p.Begin(second));assert(p.Retired(first)&&p.Begin(second));
  Finish(p,second,Admit(p,second,15,1,1));Finish(p,second,Admit(p,second,18,2,1));Finish(p,second,Admit(p,second,20,3,1));
  assert(!p.Done()&&p.FinalUnexpectedTests()==0);assert(p.Retired(second)&&p.Current()==State::Passed&&p.Results().size()==5);
  assert(p.Classify(1)==Classification::Passed&&p.Classify(2)==Classification::Flaky&&p.Classify(3)==Classification::Passed);++controls;}
 // Exhaustion never invents attempts for declarations omitted after the failure.
 {SerialGroup p(Tests());assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1),Outcome::Failed);assert(p.Retired(first));
  assert(p.Done()&&p.Current()==State::Failed&&p.Results().size()==1&&p.Omissions().size()==2&&p.FinalUnexpectedTests()==1);
  assert(p.Classify(2)==Classification::NotRun&&!p.Begin(second));++controls;}
 // Logical completion alone cannot release worker ownership or report success.
 {SerialGroup p({{1,"only",{},0,false}});assert(p.Begin(first));const auto a=Admit(p,first,1,1);
  assert(!p.Retired(first));Finish(p,first,a);assert(!p.Done()&&!p.Next(second,2));
  for(const auto stale:std::vector<SerialGroup::Worker>{{1,101,1},{0,102,1},{0,101,2}})assert(!p.Retired(stale));
  assert(p.Retired(first)&&p.Done()&&!p.Retired(first));++controls;}
 // Exact worker identity and attempt generations fence every mutable transition.
 {SerialGroup p(Tests(1));assert(!p.Begin({0,0,1})&&!p.Begin({0,101,0}));assert(p.Begin(first));
  Reject([&]{p.Next(first,0);});const auto a=Admit(p,first,50,1);assert(!p.Finish(second,a.generation,Outcome::Failed)&&!p.Finish(first,51,Outcome::Failed));
  assert(!p.ExpectFailure(second,a.generation)&&!p.ExpectFailure(first,51));Finish(p,first,a,Outcome::Failed);assert(p.Retired(first));
  assert(!p.Begin({0,201,2}));assert(p.Begin({1,101,1}));Reject([&]{p.Next({1,101,1},49);});
  const auto retry=Admit(p,{1,101,1},60,1,1);assert(!p.Finish(first,retry.generation,Outcome::Passed));Finish(p,{1,101,1},retry);++controls;}
 // Expected failures do not poison a serial wave. Runtime amendments need the
 // genuine currently active worker/generation, just like existing RetryPlan.
 {auto tests=Tests();tests[0].expectedFailure=true;SerialGroup p(tests);assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1),Outcome::Failed);
  const auto b=Admit(p,first,2,2);assert(p.ExpectFailure(first,b.generation));Finish(p,first,b,Outcome::Failed);
  Finish(p,first,Admit(p,first,3,3));assert(p.Retired(first)&&p.Current()==State::Passed&&p.Omissions().empty());++controls;}
 // Unexpected pass triggers exactly the same whole-group retirement/retry fence.
 {auto tests=Tests(1);tests[0].expectedFailure=true;SerialGroup p(tests);assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1));assert(p.Current()==State::Retiring);
  assert(p.Retired(first)&&p.Begin(second));Finish(p,second,Admit(p,second,2,1,1),Outcome::Failed);Finish(p,second,Admit(p,second,3,2,1));Finish(p,second,Admit(p,second,4,3,1));
  assert(p.Retired(second)&&p.Classify(1)==Classification::Flaky&&p.FinalUnexpectedTests()==0);++controls;}
 // Group retry allowance is the largest effective member retry count, not the
 // failing member's individual value. Every member shares the group wave index.
 {auto tests=Tests();tests[2].retries=2;SerialGroup p(tests);assert(p.MaxRetries()==2&&p.Begin(first));
  Finish(p,first,Admit(p,first,1,1),Outcome::Failed);assert(p.Retired(first)&&p.Begin(second));Finish(p,second,Admit(p,second,2,1,1),Outcome::Failed);
  assert(p.Retired(second)&&p.Begin(third));Finish(p,third,Admit(p,third,3,1,2),Outcome::Failed);assert(p.Retired(third)&&p.Retry()==2&&p.Current()==State::Failed);++controls;}
 // The original serial oracle includes pass-then-fail on replay as flaky, and
 // failure-then-pass as flaky. Keep actual histories, rather than faking skips.
 {SerialGroup p(Tests(1));assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1));Finish(p,first,Admit(p,first,2,2),Outcome::Failed);assert(p.Retired(first)&&p.Begin(second));
  Finish(p,second,Admit(p,second,3,1,1),Outcome::Failed);assert(p.Retired(second));
  assert(p.Classify(1)==Classification::Flaky&&p.Classify(2)==Classification::Failed&&p.Classify(3)==Classification::NotRun&&p.FinalUnexpectedTests()==1);++controls;}
 // Genuine runtime skip is an expected result and permits the next declaration;
 // a later skipped retry still has the genuine earlier failure in its history.
 {SerialGroup p(Tests(1));assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1),Outcome::Failed);assert(p.Retired(first)&&p.Begin(second));
  Finish(p,second,Admit(p,second,2,1,1),Outcome::Skipped);Finish(p,second,Admit(p,second,3,2,1),Outcome::Skipped);Finish(p,second,Admit(p,second,4,3,1));assert(p.Retired(second));
  assert(p.Classify(1)==Classification::Flaky&&p.Classify(2)==Classification::Skipped&&p.Classify(3)==Classification::Passed);++controls;}
 // Hook/fixture infrastructure failure must not be swallowed by test.fail().
 // Adapter Finish includes real before/afterAll and fixture cleanup outcome.
 {SerialGroup p({{1,"hook failure",{},1,true},{2,"later",{},1,false}});assert(p.Begin(first));const auto a=Admit(p,first,1,1);
  assert(p.Finish(first,a.generation,Outcome::Failed,"actual beforeAll failure",true));assert(p.Retired(first)&&p.Begin(second));
  const auto b=Admit(p,second,2,1,1);assert(p.Finish(second,b.generation,Outcome::Failed,"actual afterAll failure",true));assert(p.Retired(second)&&p.FinalUnexpectedTests()==1&&p.Omissions().size()==2);++controls;}
 // A global stop before admission cannot create workers, callbacks or results.
 {SerialGroup p(Tests(2));p.Stop();assert(p.Done()&&p.Current()==State::Stopped&&!p.Begin(first)&&p.Results().empty()&&p.Omissions().empty());++controls;}
 // Stop between declarations retains physical ownership until actual retirement.
 {SerialGroup p(Tests(2));assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1));p.Stop();assert(p.Current()==State::Retiring&&!p.Done());
  assert(!p.Next(first,2)&&p.Retired(first)&&p.Current()==State::Stopped&&!p.Begin(second)&&p.Results().size()==1);++controls;}
 // An active callback cannot be released by stop, nor can its actual interrupt
 // be converted into a replay or hidden as a flaky earlier successful attempt.
 {SerialGroup p(Tests(2));assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1),Outcome::Failed);assert(p.Retired(first)&&p.Begin(second));
  const auto a=Admit(p,second,2,1,1);p.Stop();assert(p.Current()==State::Active&&!p.Retired(second));Finish(p,second,a,Outcome::Interrupted);
  assert(p.Retired(second)&&p.Current()==State::Stopped&&!p.Begin(third)&&p.Classify(1)==Classification::Interrupted&&p.FinalUnexpectedTests()==0);++controls;}
 // Pending retry can be stopped after the old worker has actually retired.
 {SerialGroup p(Tests(2));assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1),Outcome::Failed);assert(p.Retired(first));p.Stop();assert(p.Current()==State::Stopped&&!p.Begin(second));++controls;}
 // Native beforeAll skip propagation removes only queued group declarations.
 {SerialGroup p(Tests(1));assert(p.Begin(first));Finish(p,first,Admit(p,first,1,1),Outcome::Skipped);p.ExcludeQueued({2,3});
  assert(p.Current()==State::Retiring&&p.Results().size()==1&&p.Omissions().empty());assert(p.Retired(first)&&p.Current()==State::Passed&&p.Classify(2)==Classification::NotRun);++controls;}
 {SerialGroup p(Tests());p.ExcludeQueued({1});assert(!p.LastSelected(1)&&p.LastSelected(3)&&p.Begin(first));Finish(p,first,Admit(p,first,1,2));Finish(p,first,Admit(p,first,2,3));assert(p.Retired(first)&&p.Classify(1)==Classification::NotRun);++controls;}
 // Metadata and projected whole-group replay remain explicitly bounded.
 {Reject([]{SerialGroup p(std::vector<TestCase>{});});Reject([]{SerialGroup p({{1,"x",{},0,false},{1,"y",{},0,false}});});
  Reject([]{SerialGroup p({{1,"x",{},101,false}});});
  std::vector<TestCase> tests;for(uint64_t id=1;id<=100;++id)tests.push_back({id,"x",{},0,false});tests.back().retries=100;
  Reject([&]{SerialGroup p(tests);});SerialGroup p(Tests());Reject([&]{p.Classify(999);});++controls;}
 std::cout<<"Native serial-group policy "<<controls<<" controls passed; actual process/hook integration remains separate\n";
}
