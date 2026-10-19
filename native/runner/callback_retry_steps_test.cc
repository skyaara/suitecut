// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include <cassert>
#include <iostream>

namespace {
using namespace suitecut::runner;
const StepOwner owner{1,4,0,1,4,2,501,"body"};
template<class F>void Reject(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
std::vector<std::string> Raw(uint64_t id,bool ok){return{"STEP_SETTLED",std::to_string(id),"0","1","4",ok?"1":"0",ok?"-":StepWire("\"actual child failure\"")};}
RunnerSteps::Hooks Hooks(double& now,std::vector<std::string>& sent){
  RunnerSteps::Hooks h;h.send=[&](const auto& packet){sent.push_back(packet);};h.terminate=[](const auto&){assert(false);};h.now=[&]{return now;};return h;
}
}
int main(){
  unsigned controls=0;
  {
    double now=10;std::vector<std::string> sent;RunnerSteps steps(Hooks(now,sent));steps.BeginCallback(owner,1000,now);
    const auto first=steps.BeginCallbackIteration(owner,0,0,1,200,now);assert(first);
    const auto* record=steps.Find(*first);assert(record->owner==owner&&record->callback_iteration==CallbackIterationOrigin({*first,1})&&record->deadline==200&&record->timeout_ms==0);
    now=20;steps.Amend(0,1,4,5000,now);assert(steps.Find(*first)->deadline==200);++controls;
  }
  {
    double now=10;std::vector<std::string> sent;auto hooks=Hooks(now,sent);
    std::function<void(const std::optional<std::string>&)> physical;
    hooks.fileFence=[&](const auto&,uint64_t id,auto forward,double){if(id==1)physical=std::move(forward);else forward(std::nullopt);};
    RunnerSteps steps(std::move(hooks));steps.BeginCallback(owner,1000,now);
    const auto iteration=steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);assert(iteration==1);
    now=11;assert(steps.Packet({"STEP_BEGIN","1","0","1","4","1","2261637475616c20617574686f726564206368696c6422","0"},now,false));
    bool retired=false;now=12;
    steps.SettleCallbackIteration(owner,1,1,false,"\"actual invocation throw\"",0,[&](const auto& error){assert(!error);retired=true;},now);
    assert(!retired&&!physical&&steps.Physical()==2);
    now=13;assert(steps.Packet(Raw(2,true),now,false));
    assert(!retired&&physical&&steps.Physical()==1);
    assert(steps.Find(2)->receipt==StepReceipt::Settled&&steps.Find(2)->status==StepStatus::Interrupted);
    now=14;physical(std::nullopt);
    assert(retired&&!steps.Physical()&&steps.Find(1)->receipt==StepReceipt::Settled&&steps.Find(1)->status==StepStatus::Failed);
    assert(steps.Find(1)->callback_received==12&&steps.Find(1)->received==14);++controls;
  }
  {
    double now=10;std::vector<std::string> sent;RunnerSteps steps(Hooks(now,sent));steps.BeginCallback(owner,1000,now);
    const auto id=*steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);
    Reject([&]{steps.Packet(Raw(id,true),now,false);});
    Reject([&]{steps.SettleCallbackIteration(owner,id,2,true,"\"\"",0,[](const auto&){},now);});
    auto foreign=owner;++foreign.pid;
    Reject([&]{steps.SettleCallbackIteration(foreign,id,1,true,"\"\"",0,[](const auto&){},now);});
    assert(steps.Physical()==1&&!steps.Find(id)->callback_received);++controls;
  }
  {
    double now=10;std::vector<std::string> sent;RunnerSteps steps(Hooks(now,sent));steps.BeginCallback(owner,1000,now);
    const auto first=*steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);
    bool first_retired=false;now=11;
    steps.SettleCallbackIteration(owner,first,1,false,"\"real recovered authored throw\"",0,[&](const auto& error){assert(!error);first_retired=true;},now);
    assert(first_retired&&steps.Find(first)->status==StepStatus::Failed);
    now=20;const auto second=*steps.BeginCallbackIteration(owner,0,first,2,std::nullopt,now);
    now=21;steps.SettleCallbackIteration(owner,second,2,true,"\"\"",0,[](const auto& error){assert(!error);},now);
    bool outer=false;steps.ObserveRawResult(0,1,4);
    steps.SettledWithFailure(0,1,4,[&](const auto& error){assert(!error);outer=true;},now);
    assert(!outer);assert(steps.Packet({"STEP_CALLBACK_SETTLED","0","1","4"},now,false));
    assert(outer&&steps.IdleReady()&&steps.Find(first)->error_json=="\"real recovered authored throw\""&&steps.Find(second)->status==StepStatus::Passed);++controls;
  }
  {
    double now=10;std::vector<std::string> sent;RunnerSteps steps(Hooks(now,sent));steps.BeginCallback(owner,1000,now);
    const auto id=*steps.BeginCallbackIteration(owner,0,0,1,20,now);
    now=20;steps.Tick(now);assert(steps.Find(id)->status==StepStatus::TimedOut&&steps.Physical()==1);
    now=21;steps.SettleCallbackIteration(owner,id,1,true,"\"\"",0,[](const auto& error){assert(!error);},now);
    bool outer=false;steps.ObserveRawResult(0,1,4);steps.SettledWithFailure(0,1,4,[&](const auto& error){assert(!error);outer=true;},now);
    steps.Packet({"STEP_CALLBACK_SETTLED","0","1","4"},now,false);
    assert(outer&&steps.IdleReady()&&steps.Find(id)->status==StepStatus::TimedOut&&steps.Find(id)->callback_ok&&steps.Find(id)->receipt==StepReceipt::Settled);++controls;
  }
  {
    double now=10;std::vector<std::string> sent;RunnerSteps steps(Hooks(now,sent));steps.BeginCallback(owner,1000,now);
    const auto id=*steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);
    bool outer=false;steps.ObserveRawResult(0,1,4);steps.SettledWithFailure(0,1,4,[&](const auto&){outer=true;},now);
    steps.Packet({"STEP_CALLBACK_SETTLED","0","1","4"},now,false);
    assert(!outer&&steps.Physical()==1&&!steps.BeginCallbackIteration(owner,0,id,2,std::nullopt,now));
    now=11;steps.SettleCallbackIteration(owner,id,1,true,"\"\"",0,[](const auto&){},now);
    assert(outer&&steps.IdleReady()&&steps.Find(id)->status==StepStatus::Interrupted);++controls;
  }
  {
    double now=10;std::vector<std::string> sent;RunnerSteps steps(Hooks(now,sent));steps.BeginCallback(owner,1000,now);
    const auto id=*steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);
    now=11;steps.WorkerExited(owner.worker_index,owner.pid,now);
    assert(steps.IdleReady()&&steps.Find(id)->receipt==StepReceipt::WorkerExited&&!steps.Find(id)->callback_received&&!steps.Find(id)->receipt_ok);++controls;
  }
  {
    double now=10;std::vector<std::string> sent;RunnerSteps steps(Hooks(now,sent));steps.BeginCallback(owner,1000,now);
    const auto id=*steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);
    now=11;steps.CancelCallbackIteration(owner,id,"\"actual native cancellation\"",now);
    assert(steps.Find(id)->status==StepStatus::Interrupted&&steps.Physical()==1&&!steps.Find(id)->callback_received);
    assert(steps.AdmitsCallback(0,1,4)); // Other genuine work keeps its original owner.
    now=12;steps.SettleCallbackIteration(owner,id,1,true,"\"\"",0,[](const auto& error){assert(!error);},now);
    assert(!steps.Physical()&&steps.Find(id)->receipt==StepReceipt::Settled&&steps.Find(id)->callback_ok&&steps.Find(id)->status==StepStatus::Interrupted);++controls;
  }
  std::cout<<"managed callback retry step controls "<<controls<<" passed\n";
}
