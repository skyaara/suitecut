// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
// Controlled DTO/policy receipts only. Real owned-PID and SDK adapter proofs
// are separate; report validation cannot manufacture their provenance.
static RunnerReport Example(){
  const SuiteFileToken file{1,23,47};WorkerReusePool pool({file,1},1,4);
  const auto first=*pool.Started({file,1},{file,0,1},1,101);
  assert(pool.Complete(first,WorkerReusePool::Outcome::Passed));WorkerReusePool::GroupFence fence;
  const auto idle=*pool.RequestIdle(first,fence,10,100);
  assert(pool.Acknowledge({first,idle.request,0},11));WorkerReusePool::Inventory inventory{*pool.InventoryProbe(),12,true,true};
  const std::array<WorkerReusePool::CohortFence,1> fences{{{first,fence}}};assert(pool.ConfirmInventory(inventory,fences,12));
  const auto second=*pool.Reuse(first.host,{file,1},{file,1,2});assert(pool.Complete(second,WorkerReusePool::Outcome::Passed));
  assert(pool.PrimaryExited(second.host,{0,false},100));WorkerReusePool::RetirementFence retired;retired.descendants_retired=true;assert(pool.Retired(second.host,retired,101));
  RunnerReport r;r.files={{file,"/actual/controlled/suite.mjs","passed",{1,2},{1,2},1,200}};r.workers={{1,101,false,0,"",""}};
  r.attempts={{1,1,0,1,101,"\"first\"","\"\"","passed",true,1},{2,2,0,1,101,"\"second\"","\"\"","passed",true,1}};
  r.file_work_groups={{1,1,0,1,WorkGroupMode::Parallel,"passed","idleTransferred",{1},{1}},{2,1,0,2,WorkGroupMode::Parallel,"passed","physicalRetired",{2},{1}}};
  r.worker_reuse.hosts.assign(pool.Hosts().begin(),pool.Hosts().end());r.worker_reuse.borrows.assign(pool.Borrows().begin(),pool.Borrows().end());return r;
}
template<class F>static void Reject(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {const auto r=Example();assert(report_detail::Validate(r).expected==2);++controls;}
 {auto r=Example();r.worker_reuse={};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.hosts[0].physical_retired=false;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.hosts[0].exit.reset();Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.hosts[0].host.pid=102;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.hosts[0].host.lease=2;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.hosts[0].retired=99;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.hosts[0].retired=201;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.workers[0].exit_code=1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows.pop_back();Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[1].borrow.group.index=0;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[0].borrow.group.file.inode=48;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[1].borrow.host.lease=2;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[1].borrow.generation=1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[0].receipt=WorkerReusePool::Receipt::PhysicalRetired;r.file_work_groups[0].receipt="physicalRetired";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[0].outcome=WorkerReusePool::Outcome::Failed;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[0].idle_request=0;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[0].acknowledged=13;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[0].transferred=101;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[1].workers={2};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[1].receipt="idleTransferred";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[0].mode=WorkGroupMode::Default;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[1].outcome.reset();Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts[1].worker_index=2;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.worker_reuse.borrows[0].canceled_idle_request=7;r.worker_reuse.borrows[0].canceled_acknowledged=13;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();auto& canceled=r.worker_reuse.borrows[1];canceled.canceled_idle_request=2;canceled.canceled_acknowledged=30;assert(report_detail::Validate(r).expected==2);canceled.canceled_acknowledged=101;Reject([&]{report_detail::Validate(r);});++controls;}
 assert(controls==26);std::cout<<"Native worker reuse reports: "<<controls<<" controlled ownership-chain checks passed; OS provenance remains separate\n";
}
