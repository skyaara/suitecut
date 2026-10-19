// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
// Fail only an actual admission allocation; inspection and real settlement run
// with the allocator restored. No production injection hook is needed.
size_t fail_after=0;
bool armed=false;
void* Allocate(size_t size){
  if(armed&&fail_after--==0)throw std::bad_alloc();
  if(void* result=std::malloc(size?size:1))return result;
  throw std::bad_alloc();
}
using namespace suitecut::runner;
const StepOwner owner{1,4,0,1,4,2,501,"body"};
void SameCharge(const TestSteps::Budget& a,const TestSteps::Budget& b){
  assert(a.records==b.records&&a.physical==b.physical&&a.reserved==b.reserved);
}
unsigned Ledger(bool retained){
  unsigned failures=0;
  for(size_t nth=0;;++nth){
    TestSteps::Budget budget;TestSteps steps(&budget);assert(steps.Open(owner,1000,10));
    std::optional<uint64_t> prior;
    if(retained)prior=steps.Begin(owner,0,"\"prior\"",0,10);
    assert(!retained||prior);
    const auto before=budget;const auto state=steps.Inspect();
    std::optional<uint64_t> admitted;bool failed=false;
    fail_after=nth;armed=true;
    try{admitted=steps.Begin(owner,0,"\"next\"",0,11);}catch(const std::bad_alloc&){failed=true;}
    armed=false;
    if(failed){
      ++failures;SameCharge(before,budget);
      assert(steps.Records().size()==state.records&&steps.Inspect().physical==state.physical&&steps.Inspect().metadata_reserved==state.metadata_reserved);
      // No index may refer to the rolled-back vector slot.
      for(uint64_t id=1;id<=budget.sequence;++id)assert((steps.Find(id)!=nullptr)==(prior&&id==*prior));
      admitted=steps.Begin(owner,0,"\"recovered\"",0,12);assert(admitted&&(!prior||*admitted>*prior));
    }
    assert(admitted&&steps.Find(*admitted)&&steps.Find(*admitted)->receipt==StepReceipt::Pending);
    if(prior)assert(steps.Find(*prior)&&steps.Find(*prior)->title_json=="\"prior\""&&steps.Find(*prior)->receipt==StepReceipt::Pending);
    assert(budget.records==before.records+1&&budget.physical==before.physical+1);
    if(!failed)break;
  }
  assert(failures>=2);return failures;
}
unsigned Managed(){
  unsigned failures=0;
  for(size_t nth=0;;++nth){
    double now=10;TestSteps::Budget budget;RunnerSteps::Hooks hooks;
    hooks.send=[](const auto&){};hooks.terminate=[](const auto&){assert(false);};hooks.now=[&]{return now;};
    RunnerSteps steps(std::move(hooks),&budget);steps.BeginCallback(owner,1000,now);
    const auto before=budget;std::optional<uint64_t> admitted;bool failed=false;
    fail_after=nth;armed=true;
    try{admitted=steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);}catch(const std::bad_alloc&){failed=true;}
    armed=false;
    if(failed){
      ++failures;SameCharge(before,budget);assert(steps.Records().empty()&&!steps.Physical());
      for(uint64_t id=1;id<=budget.sequence;++id)assert(!steps.Find(id));
      admitted=steps.BeginCallbackIteration(owner,0,0,1,std::nullopt,now);
    }
    assert(admitted&&steps.Physical()==1&&steps.Find(*admitted)->callback_iteration==CallbackIterationOrigin({*admitted,1}));
    bool retired=false;now=11;
    steps.SettleCallbackIteration(owner,*admitted,1,true,"\"\"",0,[&](const auto& error){assert(!error);retired=true;},now);
    assert(retired&&!steps.Physical()&&steps.Find(*admitted)->receipt==StepReceipt::Settled);
    bool outer=false;steps.ObserveRawResult(0,1,4);steps.SettledWithFailure(0,1,4,[&](const auto& error){assert(!error);outer=true;},now);
    assert(steps.Packet({"STEP_CALLBACK_SETTLED","0","1","4"},now,false));
    assert(outer&&steps.IdleReady());
    if(!failed)break;
  }
  assert(failures>=3);return failures;
}
}
void* operator new(size_t size){return Allocate(size);}
void* operator new[](size_t size){return Allocate(size);}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete[](void* value)noexcept{std::free(value);}
void operator delete(void* value,size_t)noexcept{std::free(value);}
void operator delete[](void* value,size_t)noexcept{std::free(value);}
int main(){
  const auto fresh=Ledger(false),retained=Ledger(true),managed=Managed();
  std::cout<<"callback admission allocation controls 3 passed; actual failure sites "<<fresh<<' '<<retained<<' '<<managed<<'\n';
}
