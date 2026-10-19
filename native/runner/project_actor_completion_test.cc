// SPDX-License-Identifier: MIT
#include "project_actor_completion.h"
#include "resource_locks.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
using Result=sr::ProjectActorCompletion;
int main(){unsigned controls=0;
  const auto graph=[](){return sr::ProjectGraph({{"p",{},std::nullopt,true},{"q",{},std::nullopt,true}});};
  const auto finish=[](sr::ProjectPool& pool,const sr::ProjectPool::Admission& owner){return sr::FinishUnplannedProject(pool,owner,sr::ProjectPool::Outcome::Interrupted);};
  {
    auto g=graph();sr::ProjectPool pool(g,{g.Select(),false},2);sr::ResourceLocks locks(pool,{{std::string("A\0",2)},{std::string("B\0",2)}});
    const auto owner=*pool.Next();assert(locks.Acquire(owner));
    assert(finish(pool,owner)==Result::Rejected); // A reservation alone is no exit.
    assert(!locks.Release(owner)&&locks.Active()==1);
    pool.Stop("actual original global stop");assert(pool.CanceledBeforeStart(owner)&&!pool.Retired(owner));
    assert(finish(pool,owner)==Result::CanceledBeforeStart);
    assert(locks.Release(owner)&&!locks.Active());
    assert(locks.Records()[owner.index]->receipt==sr::ResourceLocks::Receipt::NeverStarted);
    assert(!pool.WorkerStarted(owner.generation,1,101)&&!pool.Complete(owner.generation,sr::ProjectPool::Outcome::Interrupted));++controls;
  }
  {
    auto g=graph();sr::ProjectPool pool(g,{g.Select(),false},2);const auto owner=*pool.Next();pool.Stop("stop");
    auto stale=owner;++stale.generation;assert(finish(pool,stale)==Result::Rejected);++controls;
    auto wrong=owner;++wrong.index;assert(finish(pool,wrong)==Result::Rejected);++controls;
    auto teardown=owner;teardown.teardown=!teardown.teardown;assert(finish(pool,teardown)==Result::Rejected);++controls;
    assert(finish(pool,{99,99,false})==Result::Rejected);++controls;
  }
  {
    auto g=graph();sr::ProjectPool pool(g,{g.Select(),false},2);sr::ResourceLocks locks(pool,{{std::string("A\0",2)},{std::string("B\0",2)}});
    const auto owner=*pool.Next();assert(locks.Acquire(owner));assert(pool.WorkerStarted(owner.generation,1,101));
    pool.Stop("actual stop after worker admission");assert(!pool.CanceledBeforeStart(owner)&&pool.Reserved(owner));
    assert(finish(pool,owner)==Result::Rejected&&!locks.Release(owner));
    assert(!pool.WorkerExited(owner.generation,2,101)&&!pool.WorkerExited(owner.generation,1,102));
    assert(finish(pool,owner)==Result::Rejected&&!locks.Release(owner));
    assert(pool.WorkerExited(owner.generation,1,101));
    assert(finish(pool,owner)==Result::Completed&&pool.Retired(owner)&&!pool.CanceledBeforeStart(owner));
    assert(locks.Release(owner)&&locks.Records()[owner.index]->receipt==sr::ResourceLocks::Receipt::PhysicalRetired);++controls;
    assert(finish(pool,owner)==Result::Rejected);++controls; // A merely absent retired owner is no cancellation.
  }
  {
    auto g=graph();sr::ProjectPool pool(g,{g.Select(),false},2);const auto first=*pool.Next(),second=*pool.Next();
    assert(pool.WorkerStarted(first.generation,1,101));pool.Stop("stop");
    assert(finish(pool,second)==Result::CanceledBeforeStart&&finish(pool,first)==Result::Rejected);
    auto mixed=second;mixed.generation=first.generation;assert(finish(pool,mixed)==Result::Rejected);
    assert(pool.WorkerExited(first.generation,1,101)&&finish(pool,first)==Result::Completed);++controls;
  }
  assert(controls==8);std::cout<<"Native unplanned project completion: "<<controls<<" controls passed\n";
}
