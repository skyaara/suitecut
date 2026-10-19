// SPDX-License-Identifier: MIT
#include "resource_lock_declarations.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static std::string Name(std::string_view text){std::string result;for(const auto c:text){result+=c;result+='\0';}return result;}
static ProjectPool Pool(size_t count,size_t workers=2){std::vector<ProjectDeclaration> declarations;for(size_t i=0;i<count;++i)declarations.push_back({"p"+std::to_string(i),{},std::nullopt,true});ProjectGraph graph(std::move(declarations));return ProjectPool(std::move(graph),{std::vector<bool>(count,true),false},workers);}
static ProjectPool::Admission Admit(ProjectPool& pool,ResourceLocks& locks,size_t project){const auto a=pool.Next([&](size_t index){return locks.CanAcquire(index);});assert(a&&a->index==project&&locks.Acquire(*a));return *a;}
static void Exit(ProjectPool& pool,const ProjectPool::Admission& owner,unsigned worker,int pid){assert(pool.WorkerExited(owner.generation,worker,pid));}
static void Logical(ProjectPool& pool,const ProjectPool::Admission& owner,ProjectPool::Outcome outcome=ProjectPool::Outcome::Passed){assert(pool.Complete(owner.generation,outcome));}
template<class F>static void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 // Same-name waiting keeps the declaration Pending without consuming a slot.
 {auto p=Pool(2);ResourceLocks l(p,{{Name("shared")},{Name("shared")}});const auto a=Admit(p,l,0);assert(p.Active()==1&&!p.Next([&](size_t i){return l.CanAcquire(i);})&&p.Active()==1);
  assert(p.WorkerStarted(a.generation,1,101));Logical(p,a);assert(!l.Release(a)&&!p.Retired(a));Exit(p,a,1,101);assert(p.Retired(a)&&l.Release(a));Admit(p,l,1);++controls;}
 // Actual physical exit alone cannot release a logically unsettled group.
 {auto p=Pool(2);ResourceLocks l(p,{{Name("s")},{Name("s")}});const auto a=Admit(p,l,0);assert(p.WorkerStarted(a.generation,1,101));Exit(p,a,1,101);
  assert(!l.Release(a)&&!p.Retired(a));Logical(p,a);assert(l.Release(a)&&l.CanAcquire(1));++controls;}
 // Disjoint names admit independent ready projects concurrently.
 {auto p=Pool(2);ResourceLocks l(p,{{Name("a")},{Name("b")}});Admit(p,l,0);Admit(p,l,1);assert(p.Active()==2&&l.Active()==2);++controls;}
 // Atomic all-lock acquisition never keeps the free half of a blocked set.
 // The ready scan skips that waiter and admits a disjoint eligible project.
 {auto p=Pool(3,3);ResourceLocks l(p,{{Name("a")},{Name("a"),Name("b")},{Name("b")}});const auto a=Admit(p,l,0);const auto c=Admit(p,l,2);assert(p.Active()==2&&!l.CanAcquire(1));
  assert(p.WorkerStarted(a.generation,1,101)&&p.WorkerStarted(c.generation,2,102));Logical(p,a);Exit(p,a,1,101);assert(l.Release(a)&&!l.CanAcquire(1));Logical(p,c);Exit(p,c,2,102);assert(l.Release(c));Admit(p,l,1);++controls;}
 // Duplicate names collapse rather than self-deadlocking; empty name is a real
 // canonical resource, distinct from the empty set that declares no resources.
 {auto p=Pool(3,3);ResourceLocks l(p,{{Name("x"),Name("x"),{}},{},{std::string{}}});const auto a=Admit(p,l,0);assert(l.Names(0).size()==2);Admit(p,l,1);assert(!l.CanAcquire(2)&&!l.Release(a));++controls;}
 // Only the actual bound pool reservation can acquire; wrong index, generation
 // or teardown marker cannot reserve or release another project's names.
 {auto p=Pool(2);ResourceLocks l(p,{{Name("x")},{Name("x")}});assert(!l.Acquire({0,99,false}));const auto a=Admit(p,l,0);
  auto stale=a;++stale.generation;assert(!l.Release(stale));stale=a;stale.index=1;assert(!l.Release(stale));stale=a;stale.teardown=!stale.teardown;assert(!l.Release(stale));
  assert(p.WorkerStarted(a.generation,1,101));Logical(p,a);Exit(p,a,1,101);assert(l.Release(a)&&!l.Release(a)&&!l.Acquire(a));++controls;}
 // Acquiring after real worker admission is rejected, making the native
 // before-callback reservation order explicit rather than an elapsed-time guess.
 {auto p=Pool(1);ResourceLocks l(p,{{Name("x")}});const auto a=p.Next();assert(a&&p.WorkerStarted(a->generation,1,101));assert(!l.Acquire(*a));++controls;}
 // Stop before actual worker admission produces a distinct native cancellation
 // receipt, never a fabricated OS exit or successful callback receipt.
 {auto p=Pool(1);ResourceLocks l(p,{{Name("x")}});const auto a=Admit(p,l,0);p.Stop("original global stop");assert(p.CanceledBeforeStart(a)&&!p.Retired(a)&&l.Release(a));assert(l.Records()[0]->receipt==ResourceLocks::Receipt::NeverStarted);++controls;}
 // Stop retains a genuine live owner and the lock through actual logical/OS
 // retirement; pending declarations stay unadmitted and create no lock records.
 {auto p=Pool(2);ResourceLocks l(p,{{Name("x")},{Name("x")}});const auto a=Admit(p,l,0);assert(p.WorkerStarted(a.generation,1,101));p.Stop("SIGINT");assert(!p.CanceledBeforeStart(a)&&!l.Release(a)&&!l.Records()[1]);
  Logical(p,a,ProjectPool::Outcome::Interrupted);Exit(p,a,1,101);assert(l.Release(a)&&l.Records()[0]->receipt==ResourceLocks::Receipt::PhysicalRetired&&p.Done());++controls;}
 // Logical failure still keeps locks charged while a retry worker is genuinely
 // alive inside the same project admission. Release follows final project exit.
 {auto p=Pool(2);ResourceLocks l(p,{{Name("x")},{Name("x")}});const auto a=Admit(p,l,0);assert(p.WorkerStarted(a.generation,1,101));Exit(p,a,1,101);assert(!l.Release(a));
  assert(p.WorkerStarted(a.generation,2,102)&&!l.CanAcquire(1));Logical(p,a,ProjectPool::Outcome::Failed);Exit(p,a,2,102);assert(l.Release(a)&&l.CanAcquire(1));++controls;}
 // Dependency gating remains native even when resource readiness permits all.
 {ProjectGraph graph({{"setup",{},std::nullopt,true},{"body",{"setup"},std::nullopt,true},{"other",{},std::nullopt,true}});ProjectPool p(std::move(graph),{{true,true,true},false},3);ResourceLocks l(p,{{Name("s")},{Name("b")},{Name("o")}});
  const auto a=Admit(p,l,0);Admit(p,l,2);assert(p.WorkerStarted(a.generation,1,101));Logical(p,a);assert(!p.Next([&](size_t i){return l.CanAcquire(i);}));Exit(p,a,1,101);assert(l.Release(a));Admit(p,l,1);++controls;}
 // Canonical UTF16 equality handles case-insensitive wire hex, embedded NUL,
 // non-BMP pairs and isolated units without Unicode folding or replacements.
 {assert(ResourceLockName("4100")==Name("A")&&ResourceLockName("00D8")==ResourceLockName("00d8"));assert(ResourceLockName("-").empty());
  assert(ResourceLockJSON(ResourceLockName("0000"))=="\"\\u0000\""&&ResourceLockJSON(ResourceLockName("00d8"))=="\"\\ud800\"");
  assert(ResourceLockJSON(ResourceLockName("3dd800de"))=="\"\\ud83d\\ude00\"");
  auto p=Pool(2);ResourceLocks l(p,{{ResourceLockName("00D8")},{ResourceLockName("00d8")}});Admit(p,l,0);assert(!l.CanAcquire(1));++controls;}
 // Native inheritance includes only selected tests and actual ancestor suites;
 // unrelated sibling declarations do not acquire names for this file group.
 {ResourceLockDeclarations d({1,2},{{1,0,0,{}},{2,1,0,{}},{3,1,1,{}}},{{1,2,0},{2,3,0}},{{1,{Name("test")}}, {2,{Name("sibling-test")}}},{{1,{Name("root")}},{2,{Name("suite")}},{3,{Name("other")}}},{Name("file")});
  const auto a=d.Resolve({1});assert(std::set<std::string>(a.begin(),a.end())==std::set<std::string>({Name("file"),Name("root"),Name("suite"),Name("test")}));assert(d.Resolve({}).empty());Reject([&]{d.Resolve({99});});++controls;}
 // Whole-group union holds a member's lock across the group's other members.
 {ResourceLockDeclarations d({1,2},{},{},{{1,{Name("shared")}}},{},{});const auto names=d.Resolve({1,2});assert(names==std::vector<std::string>{Name("shared")});
  auto p=Pool(2);ResourceLocks l(p,{names,{Name("shared")}});Admit(p,l,0);assert(!l.CanAcquire(1));++controls;}
 // Pool capacity stays bounded independently of whether the admitted group
 // requests any names. Waiting entries neither reserve records nor grow slots.
 {auto p=Pool(9,8);std::vector<std::vector<std::string>> names(9);ResourceLocks l(p,names);for(size_t i=0;i<8;++i)Admit(p,l,i);
  assert(p.Active()==8&&l.Active()==8&&!p.Next([&](size_t i){return l.CanAcquire(i);})&&!l.Records()[8]);++controls;}
 // Teardown readiness preserves the ordinary-project physical barrier, then
 // scans past an occupied teardown lock to admit a disjoint teardown group.
 {ProjectGraph graph({{"p0",{},"t0",true},{"p1",{},"t1",true},{"p2",{},"t2",true},{"t0",{},std::nullopt,false},{"t1",{},std::nullopt,false},{"t2",{},std::nullopt,false}});
  ProjectPool p(std::move(graph),{{true,true,true,true,true,true},false},3);ResourceLocks l(p,{{},{},{},{Name("a")},{Name("a")},{Name("b")}});
  const auto a=Admit(p,l,0),b=Admit(p,l,1),c=Admit(p,l,2);unsigned index=0;for(const auto owner:{a,b,c}){++index;assert(p.WorkerStarted(owner.generation,index,100+index));Logical(p,owner);Exit(p,owner,index,100+index);assert(l.Release(owner));}
  assert(Admit(p,l,3).teardown&&Admit(p,l,5).teardown&&!l.CanAcquire(4));++controls;}
 // Empty string, NUL, isolated UTF16 and normalized-looking Unicode strings
 // remain different keys whenever their genuine code-unit sequences differ.
 {auto p=Pool(4,4);ResourceLocks l(p,{{ResourceLockName("-")},{ResourceLockName("0000")},{ResourceLockName("e900")},{ResourceLockName("65000103")}});
  for(size_t i=0;i<4;++i)Admit(p,l,i);
  assert(l.Active()==4);++controls;}
 // Native locks cannot be acquired with a stale teardown flag or after logical
 // admission has already created the physical worker owner.
 {auto p=Pool(1);ResourceLocks l(p,{{Name("x")}});auto a=p.Next();assert(a);auto bad=*a;bad.teardown=true;assert(!l.Acquire(bad)&&!l.Records()[0]);assert(l.Acquire(*a)&&!l.Acquire(*a));++controls;}
 // Unknown declaration owner, malformed UTF16 and excessive metadata are hard
 // native errors before any resource or callback reservation.
 {Reject([]{ResourceLockName("");});Reject([]{ResourceLockName("00");});Reject([]{ResourceLockName("00zz");});Reject([]{ResourceLockName(std::string(4100,'0'));});
  Reject([]{ResourceLockDeclarations d({1},{},{},{{2,{Name("x")}}},{});});Reject([]{ResourceLockDeclarations d({1},{},{},{},{{1,{Name("x")}}});});
  Reject([]{ResourceLockDeclarations d({1},{},{},{{1,std::vector<std::string>(65,Name("x"))}},{});});
  Reject([]{auto p=Pool(1);ResourceLocks l(p,{{std::string(1,'x')}});});Reject([]{auto p=Pool(1);ResourceLocks l(p,{});});++controls;}
 std::cout<<"Native resource-lock policy "<<controls<<" controls passed; physical process adapter proof is separate\n";
}
