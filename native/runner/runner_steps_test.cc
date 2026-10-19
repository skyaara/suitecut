// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include "fixture_transition_time.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){
 std::vector<std::string> sent,killed;
 RunnerSteps steps({[&](const auto& line){sent.push_back(line);},[&](const auto& error){killed.push_back(error);}});
 const StepOwner first{1,1,0,1,1,1,100};steps.WorkerStarted();steps.BeginCallback(first,1000,0);
 assert(steps.Packet({"STEP_BEGIN","1","0","1","1","0","2261637475616c22","10"},1,false));assert(steps.Physical()==1&&sent.back()=="STEP_ADMITTED 1 0 1 1 1 1 -");
 steps.Tick(11);assert(steps.Physical()==1&&sent.size()==3&&sent[1].starts_with("STEP_ABORT 1 ")&&sent[2].starts_with("STEP_RESULT 1 "));
 bool forwarded=false;steps.Settled(0,1,1,[&]{forwarded=true;},12);assert(!forwarded&&steps.Physical()==1);
 assert(steps.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},13,false));steps.Tick(1012);assert(killed.size()==1&&!forwarded&&steps.Physical()==1);steps.Tick(1013);assert(killed.size()==1);
 steps.WorkerExited(1,100,1014);assert(!forwarded&&steps.Physical()==0&&steps.Records()[0].receipt==StepReceipt::WorkerExited);
 steps.WorkerStarted();const StepOwner hook{1,2,15,20,2,2,101};steps.BeginCallback(hook,5000,1100);
 assert(steps.Packet({"STEP_BEGIN","1","15","20","2","0","22686f6f6b22","0"},1101,false));
 forwarded=false;steps.Settled(15,20,2,[&]{forwarded=true;},1102);assert(!forwarded&&steps.Records()[1].status==StepStatus::Interrupted);
 assert(steps.Packet({"STEP_SETTLED","2","15","20","2","1","-"},1103,false));assert(!forwarded&&steps.Physical()==0&&sent.back()=="STEP_RECEIVED 2 15 20 2");assert(steps.Packet({"STEP_CALLBACK_SETTLED","15","20","2"},1103,false));assert(forwarded&&sent.back()=="STEP_CALLBACK_RECEIVED 15 20 2");
 steps.WorkerExited(2,101,1104);steps.WorkerStarted();
 const StepOwner empty{2,3,0,2,3,3,102};steps.BeginCallback(empty,1105,1106); // Genuine already-issued callback, original budget expired before step admission.
 assert(steps.Packet({"STEP_BEGIN","1","0","2","3","0","226c61746522","0"},1107,false));assert(sent.back().starts_with("STEP_ADMITTED 1 0 2 3 0 0 "));
 forwarded=false;steps.Settled(0,2,3,[&]{forwarded=true;},1108);assert(!forwarded);assert(steps.Packet({"STEP_CALLBACK_SETTLED","0","2","3"},1108,false));assert(forwarded);steps.WorkerExited(3,102,1109);
 // The physical receipt can synchronously forward into the next real phase.
 // Its step ledger must share the native ingress observation, even when the
 // real steady clock has advanced by the time the file fence calls back.
 for(const bool deferred:{false,true}){
  FixtureTransitionTime event_time;double packet_now=DedicatedRealMilliseconds();
  auto observation=[&]{return event_time.Current().value_or(DedicatedRealMilliseconds());};
  std::function<void(const std::optional<std::string>&)> file_done;
  RunnerSteps::Hooks hooks{[](const auto&){},[](const auto&){assert(false);}};
  hooks.now=observation;hooks.fileFence=[&](const auto&,uint64_t,auto done,double){if(deferred)file_done=std::move(done);else done(std::nullopt);};
  RunnerSteps ledger(std::move(hooks));const StepOwner body{7,9,2,1,1,1,123},cleanup{7,9,4,1,1,1,123,"afterEach"};
  ledger.WorkerStarted();ledger.BeginCallback(body,packet_now+1000,packet_now);
  assert(ledger.Packet({"STEP_BEGIN","1","2","1","1","0","2266696c6522","0"},packet_now,false));
  ledger.Settled(2,1,1,[&]{const auto admitted=observation();ledger.BeginCallback(cleanup,admitted+1000,admitted);},packet_now);
  assert(ledger.Packet({"STEP_CALLBACK_SETTLED","2","1","1"},packet_now,false));
  {
   FixtureTransitionTime::Scope packet(event_time,packet_now);
   assert(ledger.Packet({"STEP_SETTLED","1","2","1","1","1","-"},packet_now,false));
   assert(deferred||ledger.HasCallback(4,1,1));
  }
  if(deferred){const auto physical=DedicatedRealMilliseconds();FixtureTransitionTime::Scope receipt(event_time,physical);file_done(std::nullopt);assert(ledger.HasCallback(4,1,1));assert(*ledger.Records()[0].received==physical);}
  else assert(*ledger.Records()[0].received==packet_now);
  bool backwards=false;try{ledger.Tick(packet_now-1);}catch(const std::invalid_argument&){backwards=true;}assert(backwards);
 }
 std::cout<<"PASS6 native step callback composition/drain/kill/expired-admission groups\n";
}
