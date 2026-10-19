// SPDX-License-Identifier: MIT
#include "project_pool.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Rejected(F f){bool yes=false;try{f();}catch(const std::invalid_argument&){yes=true;}assert(yes);}
ProjectPool::Admission Admit(ProjectPool& pool,size_t index){const auto next=pool.Next();assert(next&&next->index==index);return *next;}
int main(){
  ProjectGraph graph({{"bodyA",{"setup"}},{"bodyB",{"setup"}},{"setup",{},"cleanup",false},{"independent"},{"cleanup",{},std::nullopt,false}});
  for(size_t limit:{0U,9U})Rejected([&]{ProjectPool pool(graph,{graph.Select(),false},limit);});
  Rejected([&]{ProjectPool pool(graph,{{true,false,false,false,false},false},2);});
  {
    ProjectPool pool(graph,{graph.Select(),false},2);const auto setup=Admit(pool,2),independent=Admit(pool,3);assert(pool.Active()==2&&!pool.Next());
    assert(!pool.Complete(setup.generation,ProjectPool::Outcome::Passed));assert(!pool.WorkerStarted(setup.generation,0,900));
    assert(pool.WorkerStarted(setup.generation,1,901));assert(pool.WorkerStarted(independent.generation,2,902));
    assert(!pool.WorkerStarted(setup.generation,3,903));assert(pool.Physical()==2);
    assert(pool.Complete(setup.generation,ProjectPool::Outcome::Passed));assert(!pool.Next());
    assert(!pool.WorkerExited(setup.generation,1,902));assert(!pool.WorkerExited(setup.generation+10,1,901));assert(!pool.WorkerExited(setup.generation,2,901));
    assert(pool.WorkerExited(setup.generation,1,901));assert(!pool.WorkerExited(setup.generation,1,901));
    const auto a=Admit(pool,0);assert(pool.WorkerStarted(a.generation,3,903));assert(!pool.Next());
    assert(pool.WorkerExited(independent.generation,2,902));assert(!pool.Next()); // logical result is still missing
    assert(pool.Complete(independent.generation,ProjectPool::Outcome::Passed));const auto b=Admit(pool,1);
    assert(!pool.WorkerStarted(b.generation,3,904));assert(!pool.WorkerStarted(b.generation,4,903));assert(!pool.WorkerStarted(b.generation,2,904)); // indices never reused
    assert(pool.WorkerStarted(b.generation,4,904));assert(pool.Complete(a.generation,ProjectPool::Outcome::Passed));assert(!pool.Next());
    assert(pool.WorkerExited(a.generation,3,903));assert(!pool.Next());assert(pool.WorkerExited(b.generation,4,904));assert(!pool.Next());assert(pool.Complete(b.generation,ProjectPool::Outcome::Failed));
    const auto cleanup=Admit(pool,4);assert(cleanup.teardown);assert(pool.WorkerStarted(cleanup.generation,5,905));assert(pool.WorkerExited(cleanup.generation,5,905));assert(pool.Complete(cleanup.generation,ProjectPool::Outcome::Passed));assert(pool.Done());
  }
  {
    ProjectPool pool(graph,{graph.Select(),false},2);const auto setup=Admit(pool,2),independent=Admit(pool,3);
    assert(pool.WorkerStarted(setup.generation,1,911));assert(pool.WorkerExited(setup.generation,1,911));
    assert(pool.WorkerStarted(setup.generation,2,912));assert(!pool.WorkerExited(setup.generation,1,911)); // real retry replacement
    assert(pool.Complete(setup.generation,ProjectPool::Outcome::Failed));assert(pool.WorkerExited(setup.generation,2,912));assert(!pool.Next());
    assert(pool.Results()[0].state==ProjectPool::State::Blocked&&pool.Results()[1].state==ProjectPool::State::Blocked);
    assert(pool.WorkerStarted(independent.generation,3,913));pool.Stop("original global deadline");assert(!pool.Next()&&pool.Physical()==1&&!pool.Done());
    assert(pool.Complete(independent.generation,ProjectPool::Outcome::Interrupted));assert(!pool.Done());assert(pool.WorkerExited(independent.generation,3,913));assert(pool.Done());assert(pool.Results()[4].state==ProjectPool::State::NotRun);
  }
  {
    ProjectPool pool(graph,{graph.Select({"bodyA","setup"},true),true},2);const auto a=Admit(pool,0),setup=Admit(pool,2);assert(!a.teardown&&!setup.teardown);
    assert(pool.WorkerStarted(a.generation,1,921));pool.Stop("actual SIGINT");assert(pool.Results()[2].state==ProjectPool::State::NotRun);assert(!pool.WorkerStarted(setup.generation,2,922));assert(pool.Complete(a.generation,ProjectPool::Outcome::Interrupted));assert(pool.WorkerExited(a.generation,1,921));assert(pool.Done());
  }
  std::cout<<"Native parallel project pool controls passed\n";
}
