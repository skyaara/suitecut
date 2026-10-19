// SPDX-License-Identifier: MIT
#include "test_steps.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){
 const StepOwner owner{1,1,0,1,1,1,123};
 {TestSteps c;assert(c.Open(owner,100,0));const auto a=*c.Begin(owner,0,"\"outer\"",0,1),b=*c.Begin(owner,a,"\"inner\"",0,2);assert(c.SkipTestCallback(owner,7,"\"actual reason\\ud800\"",3));assert(c.Inspect().physical==2);assert(c.Find(a)->status==StepStatus::Skipped&&c.Find(b)->test_skip->request==7);assert(!c.Begin(owner,0,"\"late\"",0,4));assert(!c.Retire(owner));assert(!c.SkipTestCallback(owner,8,"\"other\"",4));assert(!c.Settle(owner,b,true,"\"\"",5,8));assert(c.Inspect().physical==2);assert(c.Settle(owner,b,true,"\"\"",5,7));assert(c.Settle(owner,a,true,"\"\"",6,7));assert(c.Retire(owner));assert(c.Find(a)->receipt_test_skip==7);}
 {TestSteps c;assert(c.Open(owner,10,0));auto a=*c.Begin(owner,0,"\"a\"",0,1);assert(!c.SkipTestCallback(owner,1,"\"reason\"",10));assert(c.Find(a)->status==StepStatus::TimedOut);assert(c.Settle(owner,a,false,"\"actual error\"",11));}
 {TestSteps c;assert(c.Open(owner,100,0));auto a=*c.Begin(owner,0,"\"a\"",0,1);assert(c.SkipTestCallback(owner,1,"\"r\"",2));assert(c.Settle(owner,a,false,"\"real cleanup failed\"",3));assert(c.Find(a)->status==StepStatus::Skipped&&!c.Find(a)->receipt_ok&&c.Find(a)->receipt_error_json=="\"real cleanup failed\"");}
 {TestSteps c;assert(c.Open(owner,100,0));auto a=*c.Begin(owner,0,"\"a\"",0,1);assert(c.SkipTestCallback(owner,1,"\"r\"",2));assert(c.ConfirmedWorkerExit(1,123,3));assert(c.Find(a)->status==StepStatus::Skipped&&c.Find(a)->receipt==StepReceipt::WorkerExited&&!c.Find(a)->receipt_ok);}
 {TestSteps c;assert(c.Open(owner,100,0));auto a=*c.Begin(owner,0,"\"a\"",0,1);assert(!c.Settle(owner,a,true,"\"\"",2,1));auto wrong=owner;wrong.scope_generation++;assert(!c.SkipTestCallback(wrong,1,"\"r\"",3));assert(!c.SkipTestCallback(owner,0,"\"r\"",3));assert(!c.SkipTestCallback(owner,1,"bad",3));assert(c.Find(a)->status==StepStatus::Running);}
 {TestSteps c;assert(c.Open(owner,100,0));const auto a=*c.Begin(owner,0,"\"actual prior timeout\"",1,1);c.Tick(2);assert(c.Find(a)->status==StepStatus::TimedOut);assert(c.SkipTestCallback(owner,7,"\"real later native cause\"",3));assert(c.Find(a)->status==StepStatus::TimedOut);assert(c.Settle(owner,a,true,"\"\"",4,7));assert(c.Find(a)->receipt_test_skip==7);}
 std::cout<<"PASS6 native accepted test-skip cause/physical receipt/deadline groups\n";
}
