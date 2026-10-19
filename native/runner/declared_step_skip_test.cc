// SPDX-License-Identifier: MIT
#include "test_steps.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){const StepOwner owner{1,1,0,1,1,1,123};
 {TestSteps c;assert(c.Open(owner,100,0));const auto parent=*c.Begin(owner,0,"\"real parent\"",0,1),id=*c.DeclareSkipped(owner,parent,"\"not invoked\"",0,7,2);const auto* skipped=c.Find(id);assert(skipped->declared_skip_request==7&&skipped->status==StepStatus::Skipped&&skipped->receipt==StepReceipt::NotInvoked&&!skipped->receipt_ok&&skipped->started==2&&skipped->ended==2&&skipped->received==2);assert(!skipped->test_skip&&!skipped->step_skip&&!skipped->receipt_test_skip&&!skipped->receipt_step_skip&&c.Inspect().physical==1&&c.TakeEvents().empty());assert(!c.Settle(owner,id,true,"\"\"",3));assert(!c.Settle(owner,id,false,"\"fake raw error\"",4));assert(!c.Begin(owner,id,"\"impossible child callback\"",0,5));assert(c.Begin(owner,parent,"\"actual next sibling\"",0,6));assert(c.Find(parent)->status==StepStatus::Running);}
 {TestSteps c;assert(c.Open(owner,10,0));assert(!c.DeclareSkipped(owner,0,"\"late\"",0,1,10));assert(c.Inspect().records==0&&c.Inspect().physical==0);}
 {TestSteps c;assert(c.Open(owner,100,0));assert(!c.DeclareSkipped(owner,0,"\"no request\"",0,0,1));auto wrong=owner;wrong.scope_generation++;assert(!c.DeclareSkipped(wrong,0,"\"stale\"",0,1,2));assert(!c.DeclareSkipped(owner,0,"not JSON",0,1,3));assert(c.Inspect().records==0&&c.Inspect().physical==0);}
 {TestSteps c;assert(c.Open(owner,100,0));const auto id=*c.DeclareSkipped(owner,0,"\"preserved declaration\"",0,1,1);assert(c.Close(owner,2));assert(!c.ConfirmedWorkerExit(1,123,3));assert(c.Find(id)->receipt==StepReceipt::NotInvoked&&!c.Find(id)->receipt_ok&&c.Retire(owner));}
 std::cout<<"PASS4 native predeclared step skip actual admission/non-invocation/ownership groups\n";
}
