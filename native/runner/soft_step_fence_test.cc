// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void RejectRaw(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;const StepOwner owner{1,1,0,1,1,1,101,"body"};
 {std::vector<std::string> packets;RunnerSteps steps({[&](const auto& p){packets.push_back(p);},[](const auto&){assert(false);}});steps.BeginCallback(owner,100,0);RejectRaw([&]{steps.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},1,false);});assert(steps.AdmitsCallback(0,1,1));++controls;}
 {std::vector<std::string> packets;RunnerSteps steps({[&](const auto& p){packets.push_back(p);},[](const auto&){assert(false);}});steps.BeginCallback(owner,100,0);steps.ObserveRawResult(0,1,1);assert(!steps.AdmitsCallback(0,1,1));assert(steps.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},1,false));assert(packets.empty());bool forwarded=false;steps.Settled(0,1,1,[&]{forwarded=true;},2);assert(forwarded&&packets.back()=="STEP_CALLBACK_RECEIVED 0 1 1");++controls;}
 {std::vector<std::string> packets;RunnerSteps steps({[&](const auto& p){packets.push_back(p);},[](const auto&){assert(false);}});steps.BeginCallback(owner,100,0);steps.ObserveRawResult(0,1,1);steps.Packet({"STEP_BEGIN","1","0","1","1","0","227469746c6522","0"},1,false);assert(steps.Records().empty()&&packets.back().starts_with("STEP_ADMITTED 1 0 1 1 0 0 "));RejectRaw([&]{steps.ObserveRawResult(0,1,1);});RejectRaw([&]{steps.SettledWithSource(0,1,1,[](const auto&,auto){},2,"\"invented later error\"");});++controls;}
 {std::vector<std::string> packets;RunnerSteps steps({[&](const auto& p){packets.push_back(p);},[](const auto&){assert(false);}});steps.BeginCallback(owner,100,0);steps.Packet({"STEP_BEGIN","1","0","1","1","0","227469746c6522","0"},1,false);steps.Packet({"STEP_SETTLED","1","0","1","1","0",StepWire("\"actual raw error\"")},2,false);const auto error=RuntimeSkipReason(StepWire("\"actual raw error\""));steps.ObserveRawResult(0,1,1,error,1);steps.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},3,false);bool forwarded=false;steps.SettledWithSource(0,1,1,[&](const auto& raw,uint64_t source){assert(raw==error&&source==1);forwarded=true;},4,error,1);assert(forwarded);++controls;}
 std::cout<<"PASS "<<controls<<" native authentic early raw-result/physical receipt/cause controls (controlled packets)\n";
}
