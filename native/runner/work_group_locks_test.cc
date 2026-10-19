// SPDX-License-Identifier: MIT
#include "work_group_locks.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static std::string Name(std::string_view text){std::string raw;for(const auto c:text){raw+=c;raw+='\0';}return raw;}
static WorkGroupPool Pool(size_t count,size_t projects=1){
  std::vector<ProjectDeclaration> declarations;for(size_t i=0;i<projects;++i)declarations.push_back({"p"+std::to_string(i),{},std::nullopt,true});
  ProjectGraph graph(std::move(declarations));const auto selected=graph.Select();std::vector<WorkGroupTest> tests;std::vector<std::vector<uint64_t>> ids(projects);
  for(size_t i=0;i<count;++i){const auto project=i%projects;tests.push_back({i+1,project});ids[project].push_back(i+1);}
  TestWorkGroups groups(tests,{},{},{},selected,ids,std::vector<bool>(projects,true));return {std::move(graph),{selected,false},std::move(groups),8};
}
static WorkGroupPool::Admission Admit(WorkGroupPool& pool,WorkGroupLocks& locks,size_t index){const auto owner=pool.Next([&](size_t group){return locks.CanAcquire(group);});assert(owner&&owner->index==index&&locks.Acquire(*owner));return *owner;}
static void Retire(WorkGroupPool& pool,const WorkGroupPool::Admission& owner,unsigned worker,int pid){assert(pool.WorkerStarted(owner,worker,pid)&&pool.Complete(owner,WorkGroupPool::Outcome::Passed)&&pool.WorkerExited(owner,worker,pid));}
template<class F>void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {auto p=Pool(2);WorkGroupLocks locks(p,{{Name("same")},{Name("same")}});const auto a=Admit(p,locks,0);assert(!p.Next([&](size_t i){return locks.CanAcquire(i);})&&p.Active()==1&&!locks.Release(a));Retire(p,a,1,101);assert(locks.Release(a));const auto b=Admit(p,locks,1);assert(b.project==a.project&&b.generation!=a.generation);++controls;}
 {auto p=Pool(3);WorkGroupLocks locks(p,{{Name("A")},{Name("A"),Name("B")},{Name("B")}});const auto a=Admit(p,locks,0),c=Admit(p,locks,2);assert(c.generation==2&&p.Active()==2&&!locks.CanAcquire(1));Retire(p,a,1,101);assert(locks.Release(a)&&!locks.CanAcquire(1));Retire(p,c,2,102);assert(locks.Release(c));Admit(p,locks,1);++controls;}
 {auto p=Pool(2,2);WorkGroupLocks locks(p,{{Name("same")},{Name("same")}});const auto a=Admit(p,locks,0);auto wrong=a;wrong.project=1;assert(!locks.Acquire(wrong)&&!locks.Release(wrong));auto stale=a;++stale.generation;assert(!locks.Release(stale));auto teardown=a;teardown.teardown=!teardown.teardown;assert(!locks.Release(teardown));Retire(p,a,1,101);assert(!locks.Release(wrong)&&locks.Release(a));++controls;}
 {auto p=Pool(2);WorkGroupLocks locks(p,{{Name("same")},{Name("same")}});const auto a=Admit(p,locks,0);assert(p.WorkerStarted(a,1,101)&&p.WorkerExited(a,1,101)&&!locks.Release(a));assert(p.WorkerStarted(a,2,102)&&!locks.Release(a));assert(p.Complete(a,WorkGroupPool::Outcome::Passed)&&!locks.Release(a));assert(p.WorkerExited(a,2,102)&&locks.Release(a));++controls;}
 {auto p=Pool(2);WorkGroupLocks locks(p,{{Name("A")},{Name("B")}});const auto a=Admit(p,locks,0),b=Admit(p,locks,1);assert(p.WorkerStarted(a,1,101));p.Stop("original global stop");assert(p.CanceledBeforeStart(b)&&locks.Release(b)&&locks.Records()[1]->receipt==WorkGroupLocks::Receipt::NeverStarted);assert(!locks.Release(a));assert(p.WorkerExited(a,1,101)&&p.Complete(a,WorkGroupPool::Outcome::Interrupted)&&locks.Release(a));++controls;}
 {auto p=Pool(3);const auto lone=ResourceLockName("00d8"),other=ResourceLockName("01d8");WorkGroupLocks locks(p,{{lone,lone},{other},{lone}});Admit(p,locks,0);Admit(p,locks,1);assert(!locks.CanAcquire(2)&&locks.Names(0).size()==1);++controls;}
 {auto p=Pool(3);WorkGroupLocks locks(p,{{std::string{}},{},{std::string{}}});Admit(p,locks,0);Admit(p,locks,1);assert(!locks.CanAcquire(2));++controls;}
 {std::vector<SuiteDeclaration> suites{{1,0,0,{}},{2,1,0,{}},{3,1,1,{}}};std::vector<SuiteTestDeclaration> members{{1,2,0},{2,2,1},{3,3,0}};std::vector<WorkGroupTest> tests{{1,0},{2,0},{3,0}};
  const ResourceLockDeclarations declarations({1,2,3},suites,members,{{1,{Name("one")}},{2,{Name("unselected")}}},{{2,{Name("suite")}}},{Name("file")});
  const TestWorkGroups groups(tests,suites,members,{{2,WorkGroupMode::Serial}},{true},{{1,3}},{true});const auto names=WorkGroupLockNames(groups,declarations);assert(names.size()==2&&std::set<std::string>(names[0].begin(),names[0].end())==std::set<std::string>({Name("file"),Name("suite"),Name("one")})&&names[1]==std::vector<std::string>{Name("file")});++controls;
  const TestWorkGroups default_groups(tests,suites,members,{},{true},{{1,3}},{false});const auto defaults=WorkGroupLockNames(default_groups,declarations);assert(defaults.size()==1&&defaults[0]==names[0]);++controls;}
 Reject([]{auto p=Pool(2);WorkGroupLocks locks(p,{{Name("A")}});});++controls;
 Reject([]{auto p=Pool(1);WorkGroupLocks locks(p,{{std::string("x")}});});++controls;
 Reject([]{
   std::vector<WorkGroupTest> tests;std::vector<uint64_t> ids;for(uint64_t i=1;i<=100;++i){tests.push_back({i,0});ids.push_back(i);}
   std::vector<std::string> inherited;for(unsigned i=0;i<64;++i)inherited.push_back(Name(std::string(1024,static_cast<char>('A'+i))));
   const ResourceLockDeclarations declarations(ids,{},{},{},{},inherited);
   const TestWorkGroups groups(tests,{},{},{},{true},{ids},{true});(void)WorkGroupLockNames(groups,declarations);
 });++controls;
 assert(controls==12);std::cout<<"Native typed work-group locks: "<<controls<<" controls passed\n";
}
