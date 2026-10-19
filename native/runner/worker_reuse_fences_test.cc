// SPDX-License-Identifier: MIT
#include "fixture_graph.h"
#include "runner_steps.h"
#include "runner_attachments.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(){size_t controls=0;
 {std::vector<FixtureOwners::Event> events;size_t ready=0,finished=0;FixtureOwners owner(FixtureGraph({{"worker",Scope::Worker,{}},{"test",Scope::Test,{"worker"}}}),{[&](const auto& e){events.push_back(e);},[&](auto,auto){++ready;},[&](auto,auto,bool okay,const auto&){assert(okay);++finished;},[](const auto&){assert(false);}});
  assert(owner.IdleReady()&&owner.Begin(1,10,{"test"},100,1)&&!owner.IdleReady());const auto worker=events.back();owner.Ready(worker.token,1,10,2);assert(!owner.IdleReady());const auto test=events.back();owner.Ready(test.token,1,10,3);assert(ready==1&&!owner.IdleReady());owner.Finish(true,"",4);assert(!owner.IdleReady());owner.Settled(test.token,1,10,true,"",5);assert(finished==1&&owner.IdleReady()&&owner.State().physical==1);
  size_t retained=0;owner.ForEachReadyWorkerLease([&](auto token,auto id,auto generation){assert(token==worker.token&&id==1&&generation==10);++retained;});assert(retained==1);
  assert(owner.Begin(2,20,{"worker"},100,6)&&ready==2&&!owner.IdleReady());owner.Finish(true,"",7);assert(owner.IdleReady()&&finished==2&&owner.State().physical==1);owner.StopWorker(8);assert(!owner.IdleReady());owner.Settled(worker.token,1,10,true,"",9);assert(!owner.IdleReady()&&owner.State().physical==0);++controls;}
 {bool terminated=false;FixtureOwners owner(FixtureGraph({{"worker",Scope::Worker,{}}}),{[](const auto&){},[](auto,auto){},[](auto,auto,bool,const auto&){},[&](const auto&){terminated=true;}});assert(owner.Begin(1,1,{"worker"},100,1));owner.Ready(1,1,1,2);owner.Finish(true,"",3);assert(owner.IdleReady());owner.Settled(1,1,1,false,"actual controlled fixture failure",4);assert(terminated&&owner.InfrastructureFailure()&&!owner.IdleReady());++controls;}
 {RunnerSteps steps({[](const auto&){},[](const auto&){assert(false);}});const StepOwner owner{1,1,0,1,1,1,101,"body"};assert(steps.IdleReady());steps.BeginCallback(owner,100,1);assert(steps.Physical()==0&&!steps.IdleReady());bool forwarded=false;steps.Settled(0,1,1,[&]{forwarded=true;},2);assert(!forwarded&&!steps.IdleReady());assert(steps.Packet({"STEP_CALLBACK_SETTLED","0","1","1"},3,false)&&forwarded&&steps.IdleReady());++controls;}
 {std::vector<std::string> sent;RunnerAttachments files("/tmp",{[&](const auto& line){sent.push_back(line);},[](const auto&){assert(false);}});const StepOwner owner{1,1,0,1,1,1,101,"body"};assert(files.IdleReady());files.BeginCallback(owner,100,1);assert(files.Physical()==0&&!files.IdleReady());files.CloseAdmission(0,1,1);bool forwarded=false;files.Fence(0,1,1,[&]{forwarded=true;},2);assert(forwarded&&files.IdleReady()&&sent.back()=="ATTACH_CONTEXT_RETIRED 0 1 1");++controls;}
 assert(controls==4);std::cout<<"Native idle readiness: "<<controls<<" fixture/raw/step/append fence controls passed; SDK/OS observations separate\n";
}
