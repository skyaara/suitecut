// SPDX-License-Identifier: MIT
#include "file_group_locks.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static std::string Name(std::string_view text){std::string raw;for(const auto c:text){raw+=c;raw+='\0';}return raw;}
static FileGroupPool Pool(size_t count){
  std::vector<uint64_t> ids;for(size_t i=0;i<count;++i)ids.push_back(i+1);
  NativeTestGroupTree tree(ids,{},{},{});
  return {FileWorkGroups({7,11,13},tree,ids,true),8};
}
static FileGroupPool::Admission Admit(FileGroupPool& pool,FileGroupLocks& locks,size_t index){const auto owner=pool.Next([&](size_t group){return locks.CanAcquire(group);});assert(owner&&owner->index==index&&locks.Acquire(*owner));return *owner;}
static void Retire(FileGroupPool& pool,const FileGroupPool::Admission& owner,unsigned worker,int pid){assert(pool.WorkerStarted(owner,worker,pid)&&pool.Complete(owner,FileGroupPool::Outcome::Passed)&&pool.WorkerExited(owner,worker,pid));}
template<class F>void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {auto p=Pool(2);FileGroupLocks locks(p,{{Name("same")},{Name("same")}});const auto a=Admit(p,locks,0);assert(!p.Next([&](size_t i){return locks.CanAcquire(i);})&&p.Active()==1&&!locks.Release(a));Retire(p,a,1,101);assert(locks.Release(a));const auto b=Admit(p,locks,1);assert(b.file==a.file&&b.generation!=a.generation);++controls;}
 {auto p=Pool(3);FileGroupLocks locks(p,{{Name("A")},{Name("A"),Name("B")},{Name("B")}});const auto a=Admit(p,locks,0),c=Admit(p,locks,2);assert(c.generation==2&&p.Active()==2&&!locks.CanAcquire(1));Retire(p,a,1,101);assert(locks.Release(a)&&!locks.CanAcquire(1));Retire(p,c,2,102);assert(locks.Release(c));Admit(p,locks,1);++controls;}
 {auto p=Pool(2);FileGroupLocks locks(p,{{Name("same")},{Name("same")}});const auto a=Admit(p,locks,0);auto wrong=a;++wrong.file.inode;assert(!locks.Acquire(wrong)&&!locks.Release(wrong));auto stale=a;++stale.generation;assert(!locks.Release(stale));auto foreign=a;++foreign.file.device;assert(!locks.Release(foreign));Retire(p,a,1,101);assert(!locks.Release(wrong)&&locks.Release(a));++controls;}
 {auto p=Pool(2);FileGroupLocks locks(p,{{Name("same")},{Name("same")}});const auto a=Admit(p,locks,0);assert(p.WorkerStarted(a,1,101)&&p.WorkerExited(a,1,101)&&!locks.Release(a));assert(p.WorkerStarted(a,2,102)&&!locks.Release(a));assert(p.Complete(a,FileGroupPool::Outcome::Passed)&&!locks.Release(a));assert(p.WorkerExited(a,2,102)&&locks.Release(a));++controls;}
 {auto p=Pool(2);FileGroupLocks locks(p,{{Name("A")},{Name("B")}});const auto a=Admit(p,locks,0),b=Admit(p,locks,1);assert(p.WorkerStarted(a,1,101));p.Stop();assert(p.CanceledBeforeStart(b)&&locks.Release(b)&&locks.Records()[1]->receipt==FileGroupLocks::Receipt::NeverStarted);assert(!locks.Release(a));assert(p.WorkerExited(a,1,101)&&p.Complete(a,FileGroupPool::Outcome::Interrupted)&&locks.Release(a));++controls;}
 {auto p=Pool(3);const auto lone=ResourceLockName("00d8"),other=ResourceLockName("01d8");FileGroupLocks locks(p,{{lone,lone},{other},{lone}});Admit(p,locks,0);Admit(p,locks,1);assert(!locks.CanAcquire(2)&&locks.Names(0).size()==1);++controls;}
 {auto p=Pool(3);FileGroupLocks locks(p,{{std::string{}},{},{std::string{}}});Admit(p,locks,0);Admit(p,locks,1);assert(!locks.CanAcquire(2));++controls;}
 {std::vector<SuiteDeclaration> suites{{1,0,0,{}},{2,1,0,{}},{3,1,1,{}}};std::vector<SuiteTestDeclaration> members{{1,2,0},{2,2,1},{3,3,0}};std::vector<uint64_t> tests{1,2,3};
  const ResourceLockDeclarations declarations({1,2,3},suites,members,{{1,{Name("one")}},{2,{Name("unselected")}}},{{2,{Name("suite")}}},{Name("file")});
  const NativeTestGroupTree tree(tests,suites,members,{{2,WorkGroupMode::Serial}});const FileWorkGroups groups({1,2,3},tree,{1,3},true);const auto names=FileGroupLockNames(groups,declarations);assert(names.size()==2&&std::set<std::string>(names[0].begin(),names[0].end())==std::set<std::string>({Name("file"),Name("suite"),Name("one")})&&names[1]==std::vector<std::string>{Name("file")});++controls;
  const NativeTestGroupTree default_tree(tests,suites,members,{});const FileWorkGroups default_groups({1,2,3},default_tree,{1,3},false);const auto defaults=FileGroupLockNames(default_groups,declarations);assert(defaults.size()==1&&defaults[0]==names[0]);++controls;}
 Reject([]{auto p=Pool(2);FileGroupLocks locks(p,{{Name("A")}});});++controls;
 Reject([]{auto p=Pool(1);FileGroupLocks locks(p,{{std::string("x")}});});++controls;
 Reject([]{
   std::vector<uint64_t> ids;for(uint64_t i=1;i<=100;++i)ids.push_back(i);
   std::vector<std::string> inherited;for(unsigned i=0;i<64;++i)inherited.push_back(Name(std::string(1024,static_cast<char>('A'+i))));
   const ResourceLockDeclarations declarations(ids,{},{},{},{},inherited);
   const NativeTestGroupTree tree(ids,{},{},{});const FileWorkGroups groups({1,2,3},tree,ids,true);(void)FileGroupLockNames(groups,declarations);
 });++controls;
 assert(controls==12);std::cout<<"Native typed file work-group locks: "<<controls<<" controls passed\n";
}
