// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){
 const StepOwner owner{1,1,0,1,1,1,123};std::vector<std::string> packets,failures,kills;
 RunnerSteps r({[&](const auto& p){packets.push_back(p);},[&](const auto& e){kills.push_back(e);},[&](const auto&,const auto& e){failures.push_back(e);}});
 r.WorkerStarted();r.BeginCallback(owner,1000,0);assert(r.Packet({"STEP_BEGIN","1","0","1","1","0","226122","0"},1,false));r.AcceptTestSkip(0,1,1,7,"\"real skip\"",2);assert(r.Physical()==1);assert(packets.back()=="STEP_RESULT 1 0 1 1 skipped  - 7"||packets.back()=="STEP_RESULT 1 0 1 1 skipped - 7");
 bool forwarded=false;r.Settled(0,1,1,[&]{forwarded=true;},3);assert(!forwarded);assert(r.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},4,false));assert(!forwarded);assert(r.Packet({"STEP_SETTLED_TEST_SKIP","1","0","1","1","7","-"},5,false));assert(forwarded&&r.Physical()==0&&failures.empty()&&kills.empty());
 r.BeginCallback({2,2,0,2,2,1,123},1000,6);assert(r.Packet({"STEP_BEGIN","2","0","2","2","0","226222","0"},7,false));r.AcceptTestSkip(0,2,2,8,"\"skip but cleanup failed\"",8);assert(r.Packet({"STEP_SETTLED","2","0","2","2","0","7200650061006c0020006500720072006f007200"},9,false));assert(failures.size()==1&&failures[0]==R"("\u0072\u0065\u0061\u006c\u0020\u0065\u0072\u0072\u006f\u0072")");r.Settled(0,2,2,[]{},10);assert(r.Packet({"STEP_CALLBACK_SETTLED","0","2","2"},11,false));
 std::cout<<"PASS2 native accepted skip event/raw cause and failed cleanup composition groups\n";
}
