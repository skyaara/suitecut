// SPDX-License-Identifier: MIT
#include "worker_reuse_pool.h"
#include "runner_soft_assertions.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
// Controlled native ownership receipts only: no actual matcher, SDK or OS
// verdict is claimed here. Actual public/protocol/browser waves are separate.
struct Fixture {
 WorkerReusePool pool{{{1,2,3},1},1,4};StepOwner owner{1,1,0,1,1,1,101,"body"};std::vector<std::string> packets;
 RunnerSoftAssertions soft{{[this](const auto& p){packets.push_back(p);},[](const auto&,double){},[](const auto&,const auto&,bool){assert(false);},[](const auto&){},[](uint64_t)->const StepRecord*{return nullptr;}}};
 WorkerReusePool::Borrow borrow=*pool.Started({{1,2,3},1},{{1,2,3},0,1},1,101);
 WorkerReusePool::GroupFence Fence(){WorkerReusePool::GroupFence f;f.soft_pending=soft.Physical();f.soft_contexts=soft.IdleReady()?0:1;f.clean_outcomes=!soft.FailedAttempt(owner.attempt_generation);return f;}
 void Open(){soft.Open(owner,100,0);}
 void Begin(uint64_t request=1){assert(soft.Packet({"SOFT_BEGIN",std::to_string(request),"0",std::to_string(owner.scope),std::to_string(owner.scope_generation),"0","-","0","0","0"},1,false));}
 void End(uint64_t request=1,uint64_t id=1,std::string outcome="P"){assert(soft.Packet({"SOFT_SETTLED",std::to_string(request),"0",std::to_string(owner.scope),std::to_string(owner.scope_generation),std::to_string(id),outcome,StepWire(outcome=="P"?"\"\"":"\"controlled actual receipt mismatch\"")},3,false));}
};
int main(){size_t groups=0;
 {Fixture f;f.Open();assert(f.pool.Complete(f.borrow,WorkerReusePool::Outcome::Passed));assert(!f.pool.RequestIdle(f.borrow,f.Fence(),1,90)&&!f.soft.Physical());f.Begin();bool raw=false;f.soft.Fence(0,1,1,[&]{raw=true;},2);assert(!raw&&f.soft.Physical()==1&&!f.pool.RequestIdle(f.borrow,f.Fence(),2,90));f.End();assert(raw&&f.soft.IdleReady());const auto idle=*f.pool.RequestIdle(f.borrow,f.Fence(),4,90);assert(f.pool.Acknowledge({f.borrow,idle.request,0},5));WorkerReusePool::Inventory inventory{*f.pool.InventoryProbe(),6,true,true};const std::array<WorkerReusePool::CohortFence,1> fences{{{f.borrow,f.Fence()}}};assert(f.pool.ConfirmInventory(inventory,fences,6)&&f.pool.Physical()==1);
  const auto next=*f.pool.Reuse(f.borrow.host,{{1,2,3},1},{{1,2,3},1,2});f.owner={2,2,0,2,2,1,101,"body"};f.soft.Open(f.owner,100,7);assert(f.soft.Packet({"SOFT_BEGIN","2","0","2","2","0","-","0","0","0"},8,false));assert(f.packets.back().starts_with("SOFT_ADMITTED 2 0 2 2 2 1 ")&&f.soft.Physical()==1&&f.pool.Physical()==1);assert(f.soft.Packet({"SOFT_SETTLED","2","0","2","2","2","P","-"},9,false));f.soft.Fence(0,2,2,[]{},10);assert(f.soft.IdleReady()&&f.soft.Records().empty()&&f.pool.Complete(next,WorkerReusePool::Outcome::Passed));++groups;}
 {Fixture f;f.Open();f.Begin();f.End(1,1,"S");f.soft.Fence(0,1,1,[]{},4);assert(f.soft.IdleReady()&&!f.soft.Physical()&&f.soft.FailedAttempt(1));assert(f.pool.Complete(f.borrow,WorkerReusePool::Outcome::Passed)&&!f.pool.RequestIdle(f.borrow,f.Fence(),5,90)&&f.pool.Physical()==1&&f.soft.Records().size()==1);++groups;}
 {Fixture f;f.Open();f.Begin();f.soft.CancelAll("\"original cancellation\"",2);f.pool.Stop();assert(f.soft.Physical()==1&&!f.soft.IdleReady()&&f.pool.PrimaryExited(f.borrow.host,{9,true},3));WorkerReusePool::RetirementFence retained;retained.soft_pending=f.soft.Physical();retained.soft_contexts=f.soft.IdleReady()?0:1;retained.descendants_retired=true;assert(!f.pool.Retired(f.borrow.host,retained,4)&&f.pool.Physical()==1);f.soft.WorkerExited(1,101,5);assert(f.soft.IdleReady()&&f.soft.Records().size()==1&&f.soft.Records()[0].receipt==SoftReceipt::WorkerExited);retained.soft_pending=f.soft.Physical();retained.soft_contexts=f.soft.IdleReady()?0:1;assert(f.pool.Retired(f.borrow.host,retained,6)&&f.pool.Physical()==0);++groups;}
 assert(groups==3);std::cout<<"Native worker reuse/soft composition: "<<groups<<" controlled lease/nonce/accepted-error/real-exit-fence policy groups passed; no SDK/OS claim\n";
}
