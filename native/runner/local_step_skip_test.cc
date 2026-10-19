// SPDX-License-Identifier: MIT
#include "test_steps.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){
 const StepOwner owner{1,1,0,1,1,1,123};
 {TestSteps c;assert(c.Open(owner,100,0));const auto parent=*c.Begin(owner,0,"\"parent\"",0,1),a=*c.Begin(owner,parent,"\"selected\"",0,2),b=*c.Begin(owner,a,"\"child\"",0,3),sibling=*c.Begin(owner,parent,"\"sibling\"",0,4);assert(c.SkipStep(owner,a,7,"\"native reason\"",true,5));assert(c.Find(parent)->status==StepStatus::Running&&c.Find(sibling)->status==StepStatus::Running);assert(c.Find(a)->status==StepStatus::Skipped&&c.Find(b)->step_skip->source_step==a&&c.Inspect().physical==4);assert(!c.Begin(owner,a,"\"late child\"",0,6));assert(c.Begin(owner,parent,"\"actual next sibling\"",0,7));assert(c.Settle(owner,b,true,"\"\"",8,0,7));assert(c.Settle(owner,a,true,"\"\"",9,0,7));assert(c.Find(a)->receipt_step_skip==7&&!c.Find(a)->test_skip);}
 {TestSteps c;assert(c.Open(owner,10,0));const auto a=*c.Begin(owner,0,"\"a\"",1,1);assert(!c.SkipStep(owner,a,1,"\"r\"",true,2));assert(c.Find(a)->status==StepStatus::TimedOut&&!c.Find(a)->step_skip);}
 {TestSteps c;assert(c.Open(owner,100,0));const auto a=*c.Begin(owner,0,"\"a\"",0,1);auto wrong=owner;wrong.scope_generation++;assert(!c.SkipStep(wrong,a,1,"\"r\"",true,2));assert(!c.SkipStep(owner,a,0,"\"r\"",true,2));assert(!c.SkipStep(owner,a,1,"\"r\"",false,2));assert(!c.SkipStep(owner,a,1,"invalid",true,2));assert(c.SkipStep(owner,a,1,"\"\"",false,3));assert(!c.Settle(owner,a,true,"\"\"",4,0,2));assert(c.Inspect().physical==1);assert(c.Settle(owner,a,true,"\"\"",5,0,1));}
 {TestSteps c;assert(c.Open(owner,100,0));const auto a=*c.Begin(owner,0,"\"parent\"",0,1),b=*c.Begin(owner,a,"\"child\"",0,2);assert(c.SkipStep(owner,b,1,"\"first local cause\"",true,3));assert(c.SkipStep(owner,a,2,"\"later parent cause\"",true,4));assert(c.Find(a)->step_skip->request==2&&c.Find(b)->step_skip->request==1);assert(c.SkipTestCallback(owner,3,"\"whole test cause\"",5));assert(c.Find(b)->step_skip->request==1&&c.Find(b)->test_skip->request==3);assert(c.Settle(owner,b,true,"\"\"",6,0,1));assert(c.Settle(owner,a,true,"\"\"",7,0,2));}
 {TestSteps c;assert(c.Open(owner,100,0));const auto a=*c.Begin(owner,0,"\"a\"",0,1);assert(c.SkipStep(owner,a,1,"\"accepted\"",true,2));assert(c.Settle(owner,a,false,"\"real cleanup error\"",3));assert(c.Find(a)->status==StepStatus::Skipped&&!c.Find(a)->receipt_ok&&c.Find(a)->receipt_error_json=="\"real cleanup error\"");}
 {TestSteps c;assert(c.Open(owner,100,0));const auto a=*c.Begin(owner,0,"\"a\"",0,1);assert(c.SkipStep(owner,a,1,"\"accepted\"",true,2));assert(c.ConfirmedWorkerExit(1,123,3));assert(c.Find(a)->status==StepStatus::Skipped&&c.Find(a)->receipt==StepReceipt::WorkerExited&&!c.Find(a)->receipt_ok);}
 std::cout<<"PASS6 native local-step skip subtree/cause/deadline/physical receipt groups\n";
}
