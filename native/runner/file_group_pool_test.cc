// SPDX-License-Identifier: MIT
#include "file_group_pool.h"
#include <cassert>
#include <iostream>
#include <type_traits>
#include "work_group_pool.h"
using namespace suitecut::runner;
using O=FileGroupPool::Outcome;using S=FileGroupPool::State;
static FileGroupPool Pool(size_t limit=2){
  NativeTestGroupTree tree({3,1,2},{},{},{});
  return {FileWorkGroups({11,23,47},tree,{1,2,3},true),limit};
}
template<class F>void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 static_assert(!std::is_convertible_v<FileGroupPool::Admission,WorkGroupPool::Admission>);
 {auto p=Pool();const auto a=*p.Next(),b=*p.Next();assert(a.file==SuiteFileToken({11,23,47})&&a.index==0&&b.index==1&&a.generation==1&&b.generation==2&&p.Active()==2&&!p.Next());++controls;}
 {auto p=Pool();const auto a=*p.Next();assert(!p.Complete(a,O::Passed)&&p.ReservedBeforeStart(a)&&!p.Retired(a));assert(p.WorkerStarted(a,1,101)&&p.Complete(a,O::Passed));assert(p.Physical()==1&&p.Active()==1&&!p.Retired(a));assert(p.WorkerExited(a,1,101)&&p.Retired(a)&&p.Group(0)==S::Passed);++controls;}
 {auto p=Pool();const auto a=*p.Next();assert(p.WorkerStarted(a,1,101)&&p.WorkerExited(a,1,101)&&p.StartedWorkerExited(a)&&!p.Retired(a));assert(p.Complete(a,O::Failed)&&p.Retired(a)&&p.Group(0)==S::Failed);++controls;}
 {auto p=Pool();const auto a=*p.Next();auto file=a,device=a,inode=a,index=a,generation=a;++file.file.id;++device.file.device;++inode.file.inode;++index.index;++generation.generation;
  for(const auto wrong:{file,device,inode,index,generation}){assert(!p.Reserved(wrong)&&!p.WorkerStarted(wrong,1,101)&&!p.Complete(wrong,O::Passed)&&!p.Retired(wrong)&&!p.CanceledBeforeStart(wrong));}
  ++controls;}
 {auto p=Pool();const auto a=*p.Next(),b=*p.Next();assert(p.WorkerStarted(a,1,101));assert(!p.WorkerStarted(b,1,102)&&!p.WorkerStarted(b,2,101));assert(p.WorkerStarted(b,2,102));assert(!p.WorkerExited(a,2,102)&&!p.WorkerExited(b,1,101)&&p.Physical()==2);++controls;}
 {auto p=Pool();const auto a=*p.Next();assert(p.WorkerStarted(a,1,101)&&p.WorkerExited(a,1,101));assert(p.WorkerStarted(a,3,103)&&!p.WorkerExited(a,1,101));assert(p.Complete(a,O::Passed)&&!p.Retired(a));assert(p.WorkerExited(a,3,103)&&p.Retired(a));assert(!p.WorkerStarted(a,4,104)&&!p.WorkerExited(a,3,103)&&!p.Complete(a,O::Passed));++controls;}
 {auto p=Pool();const auto a=*p.Next(),b=*p.Next();assert(p.WorkerStarted(a,1,101));p.Stop();p.Stop();assert(p.Reserved(a)&&!p.CanceledBeforeStart(a)&&p.CanceledBeforeStart(b)&&!p.Retired(b));assert(!p.Done()&&!p.Next()&&p.Group(2)==S::NotRun);assert(p.WorkerExited(a,1,101)&&p.Complete(a,O::Interrupted)&&p.Done());++controls;}
 {auto p=Pool();const auto a=*p.Next();p.Stop();auto wrong=a;++wrong.file.inode;assert(p.Done()&&p.CanceledBeforeStart(a)&&!p.CanceledBeforeStart(wrong)&&!p.Retired(a)&&!p.WorkerStarted(a,1,101));++controls;}
 {auto p=Pool();const auto a=p.Next([](size_t i){return i!=0;});assert(a&&a->index==1&&a->generation==1&&p.Active()==1);const auto b=p.Next([](size_t i){return i>1;});assert(b&&b->index==2&&p.Active()==2);++controls;}
 {auto p=Pool();Reject([&]{p.Next([](size_t)->bool{throw std::runtime_error("real readiness failure");});});assert(p.Active()==0&&p.Physical()==0&&p.Next()->generation==1);++controls;}
 {auto p=Pool();const auto a=*p.Next();assert(p.WorkerStarted(a,1,101)&&!p.Complete(a,static_cast<O>(99)));assert(!p.WorkerExited(a,1,102)&&p.Physical()==1&&p.WorkerExited(a,1,101)&&p.Complete(a,O::Passed));++controls;}
 {NativeTestGroupTree tree({1,2,3},{},{},{});FileWorkGroups groups({11,23,47},tree,{3,1},false);assert(groups.Groups().size()==1&&groups.Groups()[0].tests==std::vector<uint64_t>({1,3}));++controls;}
 {NativeTestGroupTree tree({},{{1,0,0,{}}},{},{});FileGroupPool p(FileWorkGroups({1,2,3},tree,{},true),1);const auto a=*p.Next();assert(p.Plan().Groups()[0].tests.empty()&&p.WorkerStarted(a,1,101)&&p.Complete(a,O::Passed)&&!p.Done()&&p.WorkerExited(a,1,101)&&p.Done());++controls;}
 Reject([]{(void)Pool(0);});++controls;
 Reject([]{(void)Pool(9);});++controls;
 Reject([]{NativeTestGroupTree tree({1},{},{},{});(void)FileWorkGroups({0,1,2},tree,{1},true);});++controls;
 Reject([]{NativeTestGroupTree tree({1},{},{},{});(void)FileWorkGroups({1,1,2},tree,{1,1},true);});++controls;
 assert(controls==17);std::cout<<"Native anonymous file group policy: "<<controls<<" controls passed\n";
}
