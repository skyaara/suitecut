// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){std::vector<std::string> packets,kills,failures;RunnerSteps c({[&](const auto& x){packets.push_back(x);},[&](const auto& x){kills.push_back(x);},[&](const auto&,const auto& x){failures.push_back(x);}});c.WorkerStarted();const StepOwner owner{1,1,0,1,1,1,123};c.BeginCallback(owner,1000,0);
 assert(c.Packet({"STEP_BEGIN","1","0","1","1","0","22706172656e7422","0"},1,false));assert(c.Packet({"STEP_BEGIN_SKIP","2","0","1","1","1","22736b697070656422","0"},2,false));assert(packets.back()=="STEP_ADMITTED_NOT_INVOKED 2 0 1 1 2 1 -"&&c.Physical()==1&&c.Records()[1].receipt==StepReceipt::NotInvoked&&c.Records()[1].declared_skip_request==2);bool failed=false;try{c.Packet({"STEP_SETTLED","2","0","1","1","1","-"},3,false);}catch(const std::exception&){failed=true;}assert(failed&&c.Physical()==1);assert(c.Packet({"STEP_BEGIN","3","0","1","1","1","227369626c696e6722","0"},4,false));assert(c.Packet({"STEP_SETTLED","3","0","1","1","1","-"},5,false));assert(c.Packet({"STEP_SETTLED","1","0","1","1","1","-"},6,false));bool done=false;c.Settled(0,1,1,[&]{done=true;},7);assert(c.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},8,false));assert(done&&c.Physical()==0&&kills.empty()&&failures.empty());
 c.BeginCallback({2,2,0,2,2,1,123},10,9);assert(c.Packet({"STEP_BEGIN_SKIP","4","0","2","2","0","226578706972656422","0"},10,false));assert(packets.back().find("STEP_ADMITTED_NOT_INVOKED 4 0 2 2 0 0 ")==0&&c.Records().size()==3&&c.Physical()==0);
 std::cout<<"PASS2 native predeclared step admission/explicit non-invocation/receipt protocol groups\n";
}
