// SPDX-License-Identifier: MIT
#include "work_group_pool.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
using S=WorkGroupPool::State;using O=WorkGroupPool::Outcome;
static WorkGroupPool Pool(size_t limit=2,bool dependencies=true,bool no_deps=false){
  ProjectGraph graph({{"body",dependencies?std::vector<std::string>{"setup"}:std::vector<std::string>{},std::nullopt,true},
    {"setup",{},"cleanup",true},{"cleanup",{},std::nullopt,true}});
  const auto mask=graph.Select({"body","setup"},no_deps);
  std::vector<std::vector<uint64_t>> selected{{1,2},{3,4},mask[2]?std::vector<uint64_t>{5}:std::vector<uint64_t>{}};
  TestWorkGroups groups({{1,0},{2,0},{3,1},{4,1},{5,2}},{},{},{},mask,selected,{true,true,true});
  return {std::move(graph),{mask,no_deps},std::move(groups),limit};
}
static WorkGroupPool::Admission Next(WorkGroupPool& pool,size_t project){const auto owner=pool.Next();assert(owner&&owner->project==project);return *owner;}
static void Finish(WorkGroupPool& pool,const WorkGroupPool::Admission& owner,unsigned worker,int pid,O outcome=O::Passed){assert(pool.WorkerStarted(owner,worker,pid));assert(pool.Complete(owner,outcome));assert(pool.WorkerExited(owner,worker,pid));assert(pool.Retired(owner));}
template<class F>void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {auto p=Pool();const auto a=Next(p,1),b=Next(p,1);assert(a.index!=b.index&&a.generation!=b.generation&&p.Active()==2&&!p.Next());++controls;}
 {auto p=Pool();const auto a=Next(p,1),b=Next(p,1);Finish(p,a,1,101);assert(p.Projects()[1].state==S::Running&&!p.Next());Finish(p,b,2,102);assert(p.Projects()[1].state==S::Passed);Next(p,0);++controls;}
 {auto p=Pool();const auto a=Next(p,1);assert(!p.Complete(a,O::Passed)&&!p.Retired(a));assert(p.WorkerStarted(a,1,101)&&p.Complete(a,O::Passed));assert(!p.Retired(a)&&p.Active()==1&&p.Physical()==1);assert(p.WorkerExited(a,1,101)&&p.Retired(a));++controls;}
 {auto p=Pool();const auto a=Next(p,1);assert(p.WorkerStarted(a,1,101)&&p.WorkerExited(a,1,101));assert(!p.Retired(a)&&p.StartedWorkerExited(a));assert(p.Complete(a,O::Passed)&&p.Retired(a));++controls;}
 {auto p=Pool();const auto a=Next(p,1);auto stale=a;++stale.generation;auto foreign=a;foreign.project=0;auto wrong=a;++wrong.index;auto teardown=a;teardown.teardown=!teardown.teardown;
  for(const auto owner:{stale,foreign,wrong,teardown}){assert(!p.WorkerStarted(owner,1,101)&&!p.Complete(owner,O::Passed)&&!p.Retired(owner)&&!p.CanceledBeforeStart(owner));}assert(p.ReservedBeforeStart(a));++controls;}
 {auto p=Pool();const auto a=Next(p,1),b=Next(p,1);assert(p.WorkerStarted(a,1,101));assert(!p.WorkerStarted(b,1,102)&&!p.WorkerStarted(b,2,101));assert(p.WorkerStarted(b,2,102));assert(!p.WorkerExited(a,2,102)&&!p.WorkerExited(b,1,101));++controls;}
 {auto p=Pool();const auto a=Next(p,1);assert(p.WorkerStarted(a,1,101)&&p.WorkerExited(a,1,101));assert(!p.Retired(a)&&p.WorkerStarted(a,2,102));assert(!p.WorkerExited(a,1,101)&&p.WorkerExited(a,2,102));assert(p.Complete(a,O::Passed)&&p.Retired(a));++controls;}
 {auto p=Pool();const auto a=Next(p,1),b=Next(p,1);Finish(p,a,1,101,O::Failed);assert(p.Projects()[1].state==S::Running&&!p.Next());Finish(p,b,2,102);const auto cleanup=Next(p,2);assert(p.Projects()[0].state==S::Blocked&&cleanup.teardown);++controls;}
 {auto p=Pool(3,false);const auto a=Next(p,0),b=Next(p,0),c=Next(p,1);Finish(p,a,1,101);Finish(p,b,2,102);Finish(p,c,3,103);assert(p.Projects()[0].state==S::Passed&&p.Projects()[1].state==S::Running);const auto d=Next(p,1);Finish(p,d,4,104);const auto cleanup=Next(p,2);assert(cleanup.teardown);Finish(p,cleanup,5,105);assert(p.Done());++controls;}
 {auto p=Pool();const auto a=Next(p,1),b=Next(p,1);assert(p.WorkerStarted(a,1,101));p.Stop("original global timeout");assert(p.Reserved(a)&&!p.CanceledBeforeStart(a)&&p.CanceledBeforeStart(b)&&!p.Retired(b));assert(!p.Done()&&!p.Next()&&!p.WorkerStarted(b,2,102));assert(p.WorkerExited(a,1,101)&&p.Complete(a,O::Interrupted));assert(p.Done()&&p.Projects()[1].state==S::Interrupted&&p.Projects()[0].state==S::NotRun);++controls;}
 {auto p=Pool();const auto a=Next(p,1);p.Stop("SIGINT");assert(p.CanceledBeforeStart(a)&&!p.Retired(a)&&p.Done()&&p.Projects()[1].state==S::NotRun);auto wrong=a;wrong.project=0;assert(!p.CanceledBeforeStart(wrong));++controls;}
 {auto p=Pool(2,false);const auto a=p.Next([](size_t index){return index!=0;});assert(a&&a->index==1&&a->generation==1&&p.Active()==1);const auto b=p.Next([](size_t index){return index>1;});assert(b&&b->index==2&&p.Active()==2);++controls;}
 {auto p=Pool(2,false,true);const auto a=Next(p,0),b=Next(p,0);assert(!a.teardown&&!b.teardown);Finish(p,a,1,101);Finish(p,b,2,102);Next(p,1);assert(p.Projects()[2].state==S::NotSelected);++controls;}
 Reject([]{(void)Pool(0);});++controls;
 Reject([]{(void)Pool(9);});++controls;
 assert(controls==15);std::cout<<"Native work-group pool: "<<controls<<" controls passed\n";
}
