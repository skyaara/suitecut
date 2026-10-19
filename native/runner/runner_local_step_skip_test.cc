// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){
 std::vector<std::string> packets,failures,kills;RunnerSteps c({[&](const auto& x){packets.push_back(x);},[&](const auto& x){kills.push_back(x);},[&](const auto&,const auto& x){failures.push_back(x);}});const StepOwner owner{1,1,0,1,1,1,123};c.WorkerStarted();c.BeginCallback(owner,1000,0);
 assert(c.Packet({"STEP_BEGIN","1","0","1","1","0","22706172656e7422","0"},1,false));assert(c.Packet({"STEP_BEGIN","2","0","1","1","1","226368696c6422","0"},2,false));assert(c.Packet({"STEP_LOCAL_SKIP_REQUEST","1","2","0","1","1","72006500610073006f006e00","1"},3,false));assert(packets.back()=="STEP_LOCAL_SKIP_ACK 1 2 0 1 1 1 -");assert(c.Records()[0].status==StepStatus::Running&&c.Records()[1].status==StepStatus::Skipped&&c.Physical()==2);bool bad=false;try{c.Packet({"STEP_SETTLED_LOCAL_SKIP","2","0","1","1","0","-"},4,false);}catch(const std::exception&){bad=true;}assert(bad&&c.Physical()==2);assert(c.Packet({"STEP_SETTLED_LOCAL_SKIP","2","0","1","1","1","-"},5,false));assert(c.Packet({"STEP_BEGIN","3","0","1","1","1","227369626c696e6722","0"},6,false));assert(c.Packet({"STEP_SETTLED","3","0","1","1","1","-"},7,false));assert(c.Packet({"STEP_SETTLED","1","0","1","1","1","-"},8,false));bool forwarded=false;c.Settled(0,1,1,[&]{forwarded=true;},9);assert(c.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},10,false));assert(forwarded&&failures.empty()&&kills.empty());
 c.BeginCallback({2,2,0,2,2,1,123},1000,11);assert(c.Packet({"STEP_BEGIN","4","0","2","2","0","226122","0"},12,false));assert(c.Packet({"STEP_LOCAL_SKIP_REQUEST","2","4","0","2","2","-","0"},13,false));assert(c.Packet({"STEP_SETTLED","4","0","2","2","0","7200650061006c0020006500720072006f007200"},14,false));assert(failures.size()==1&&c.Records().back().receipt_ok==false);bool saw=false;c.SettledWithFailure(0,2,2,[&](const auto& error){saw=error.has_value();},15);assert(c.Packet({"STEP_CALLBACK_SETTLED","0","2","2"},16,false));assert(saw&&c.Physical()==0);
 std::cout<<"PASS2 native local-step skip/continuation and genuine failed cleanup protocol groups\n";
}
