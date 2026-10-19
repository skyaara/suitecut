// SPDX-License-Identifier: MIT
#include "test_steps.h"
#include "runner_steps.h"
#include "runner_attachments.h"
#include "retry_plan.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){
  TestSteps::Budget shared;TestSteps a(&shared),b(&shared);const StepOwner oa{1,1,0,1,1,1,111,"body"},ob{2,2,0,2,2,2,222,"body"};
  assert(a.Open(oa,100,0)&&b.Open(ob,100,0));const auto first=a.Begin(oa,0,"\"first\"",0,1),other=b.Begin(ob,0,"\"other\"",0,1),child=a.Begin(oa,*first,"\"child\"",0,2);
  assert(first==1&&other==2&&child==3);assert(!a.Find(2)&&!b.Find(1));assert(!a.Begin(oa,*other,"\"foreign\"",0,3));assert(!b.Begin(ob,*first,"\"foreign\"",0,3));
  assert(a.UpdateOwnerDeadline(oa,80,4));assert(a.Find(*child)->deadline==80);assert(!b.Settle(oa,*child,true,"\"\"",5));assert(shared.physical==3);
  assert(!a.ConfirmedWorkerExit(2,222,5));assert(shared.physical==3);assert(a.ConfirmedWorkerExit(1,111,6));assert(shared.physical==1);assert(b.Settle(ob,*other,true,"\"\"",7));assert(shared.physical==0&&shared.records==3);
  {TestSteps::Budget budget;TestSteps left(&budget),right(&budget);assert(left.Open(oa,1000,0)&&right.Open(ob,1000,0));
    for(size_t i=0;i<32;++i){assert(left.Begin(oa,0,"\"a\"",0,1));assert(right.Begin(ob,0,"\"b\"",0,1));}
    assert(!left.Begin(oa,0,"\"capacity\"",0,2));assert(!right.Begin(ob,0,"\"capacity\"",0,2));assert(left.ConfirmedWorkerExit(1,111,3));assert(right.Begin(ob,0,"\"released\"",0,4));assert(budget.physical==33);assert(right.ConfirmedWorkerExit(2,222,5));assert(budget.physical==0);
  }
  {TestSteps::Budget budget;TestSteps steps(&budget);assert(steps.Open(oa,10000,0));size_t count=0;while(const auto id=steps.Begin(oa,0,"\"quota\"",0,1)){assert(steps.Settle(oa,*id,true,"\"\"",1));++count;}assert(count>100&&budget.reserved<=TestSteps::kMetadata&&budget.physical==0);TestSteps other_steps(&budget);assert(other_steps.Open(ob,10000,1));assert(!other_steps.Begin(ob,0,"\"shared quota\"",0,1));}
  {RetryPlan first_plan({{1,"a",{},0,false},{3,"c",{},0,false}}),second_plan({{2,"b",{},0,false}});const auto x=first_plan.Next(111,10),y=second_plan.Next(222,11);assert(x->generation==10&&y->generation==11);assert(first_plan.Finish(111,10,Outcome::Passed,""));bool rejected=false;try{first_plan.Next(111,9);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);const auto z=first_plan.Next(111,12);assert(z->test.id==3&&z->generation==12);}
  std::cout<<"Native parallel identity and shared step budget controls passed\n";
}
