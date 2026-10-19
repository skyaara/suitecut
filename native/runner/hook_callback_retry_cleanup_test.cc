// SPDX-License-Identifier: MIT
#include "hook_worker.h"
#include "runner_callback_retries.h"
#include "runner_steps.h"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace suitecut::runner;
namespace {
// These are controlled host/file ingress events composed with the actual native
// fixture, hook, step and retry controllers. They are not OS/JS settlement proof.
struct Harness {
  double now=10;
  TestSteps::Budget budget;
  std::vector<std::string> packets,errors;
  std::vector<HookPhases::Result> results;
  std::vector<StepOwner> owners;
  std::function<void(const std::optional<std::string>&)> file_forward;
  std::unique_ptr<RunnerSteps> steps;
  std::unique_ptr<RunnerCallbackRetries> retries;
  std::unique_ptr<HookWorker> worker;
  uint64_t setup=0,teardown=0,assertion=0;
  Harness(bool after=true,bool before=false,bool timed=true){
    RunnerSteps::Hooks sh;
    sh.send=[this](const auto& p){packets.push_back(p);};sh.now=[this]{return now;};
    sh.terminate=[this](const auto& e){errors.push_back(e);};
    sh.fileFence=[this](const auto& o,uint64_t id,auto next,double t){
      if(retries->OwnsIteration(o,id))retries->FenceIteration(o,id,std::move(next),t);
      else retries->FenceStep(o,id,std::move(next),t);
    };
    steps=std::make_unique<RunnerSteps>(std::move(sh),&budget);
    RunnerCallbackRetries::Hooks rh;
    rh.send=[this](const auto& p){packets.push_back(p);};rh.now=[this]{return now;};
    rh.next_step=[this]{return budget.sequence+1;};
    rh.begin=[this](const auto& o,uint64_t p,uint64_t a,uint64_t n,std::optional<double> cap,double t){return steps->BeginCallbackIteration(o,p,a,n,cap,t);};
    rh.settled=[this](const auto& o,uint64_t id,uint64_t n,bool ok,const auto& e,uint64_t from,auto next,double t){steps->SettleCallbackIteration(o,id,n,ok,e,from,std::move(next),t);};
    rh.cancel=[this](const auto& o,uint64_t id,const auto& e,double t){steps->CancelCallbackIteration(o,id,e,t);};
    rh.files=[this](const auto&,const auto&,auto next,double){assert(!file_forward);file_forward=std::move(next);};
    rh.source=[](const auto&,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,double,double){return RunnerCallbackRetries::SourceFence::Ready;};
    rh.step=[this](uint64_t id){return steps->Find(id);};
    rh.hard=[this](const auto&,const auto& e){errors.push_back(e);};rh.terminate=[this](const auto& e){errors.push_back(e);};
    retries=std::make_unique<RunnerCallbackRetries>(std::move(rh),budget);
    HookWorker::Callbacks wh;
    wh.now=[this]{return now;};wh.send=[this](const auto& p){Send(p);};
    wh.completed=[this](const auto& r){results.push_back(r);};wh.terminate=[this](const auto& e){errors.push_back(e);};
    wh.cleanup_ready=[this](uint64_t s,uint64_t g){return retries->CleanupReady(s,g);};
    worker=std::make_unique<HookWorker>(FixtureDefinition(),std::move(wh));
    std::vector<HookCall> calls;
    if(before)calls.push_back({HookKind::BeforeEach,10,{"owned"}});
    calls.push_back({HookKind::Body,11,{"owned"}});
    if(after)calls.push_back({HookKind::AfterEach,12,{"owned"}});
    std::shared_ptr<FixtureAttemptClocks> timing;
    if(timed)timing=std::make_shared<FixtureAttemptClocks>(FixtureAttemptClockOwner{7,9,1,501},100,now);
    assert(worker->Begin({7,9,1,0,100,FixtureScopeKind::Test,0,{},0,timing},std::move(calls),now));
    assert(setup&&!owners.size());worker->FixtureReady(setup,7,9,now);
    assert(owners.size()==1);
    const auto& o=owners.front();assertion=budget.sequence+1;
    assert(retries->Packet({"TOPASS_BEGIN","1",N(o.callback_token),"7","9","0","10","0","-","0","0","0"},now,false));
    assert(!retries->CleanupReady(7,9)&&retries->CleanupReady(8,9)&&retries->CleanupReady(7,10));
  }
  // Construct the genuine native test-scoped fixture used by HookWorker.
  static std::vector<suitecut::runner::Fixture> FixtureDefinition(){return {{"owned",Scope::Test,{}}};}
  static std::string N(uint64_t n){return std::to_string(n);}
  void Send(const std::string& p){
    packets.push_back(p);std::istringstream in(p);std::string kind;uint64_t token,scope,generation;
    in>>kind>>token>>scope>>generation;
    if(kind=="SETUP")setup=token;
    if(kind=="TEARDOWN")teardown=token;
    if(kind=="CALL"){
      std::string callback_kind;in>>callback_kind;
      StepOwner o{1,9,token,scope,generation,1,501,callback_kind};owners.push_back(o);
      steps->BeginCallback(o,*worker->ActiveDeadline(),now);retries->Open(o,*worker->ActiveDeadline(),now);
    }
  }
  void Tick(double t){now=t;steps->Tick(t);retries->Tick(t);worker->Tick(t);}
  void Stop(){
    Tick(20);assert(retries->Records().empty()&&steps->Physical()==1);
    now=21;worker->Cancel("Original native global stop",now);retries->Cancel(owners.front(),now);
    assert(worker->ActiveDeadline()==121&&owners.size()==1&&!teardown&&worker->Inspect().callbacks==1);
    const auto o=owners.front();now=22;steps->ObserveRawResult(o.callback_token,7,9);
    retries->Fence(o,[this,o](const auto& e){assert(!e);steps->SettledWithFailure(o.callback_token,7,9,[this,o](const auto& error){assert(!error);worker->CallbackSettled(o.callback_token,7,9,true,"",now);},now);},now);
    assert(steps->HasCallback(o.callback_token,7,9));
  }
  void RetireManaged(){
    now=30;const auto o=owners.front();
    assert(retries->Packet({"TOPASS_RAW","1",N(o.callback_token),"7","9",N(assertion),"1",N(assertion),"P","-","0","0","0"},now,false));
    assert(!file_forward);Tick(30);assert(file_forward&&!retries->CleanupReady(7,9)&&!teardown&&owners.size()==1);
    now=31;auto f=std::move(file_forward);f(std::nullopt);
    assert(!retries->CleanupReady(7,9));Tick(32);
    assert(retries->CleanupReady(7,9)&&retries->Records().size()==1&&steps->Find(assertion)->received==32);
    assert(retries->Records()[0].status==CallbackAssertionStatus::TimedOut);
    // The actual parent's separate raw-physical packet is still absent. The
    // managed operation's retirement must not release this native hook lease.
    assert(worker->Inspect().callbacks==1&&steps->HasCallback(o.callback_token,7,9));
    Tick(33);assert(!teardown&&owners.size()==1&&worker->ActiveDeadline()==121);
  }
  void ParentPhysical(){
    now=34;const auto o=owners.front();
    assert(steps->Packet({"STEP_CALLBACK_SETTLED",N(o.callback_token),"7","9"},now,false));
    assert(!steps->HasCallback(o.callback_token,7,9));
  }
  void Finish(bool after){
    ParentPhysical();
    if(after){
      assert(owners.size()==2&&owners.back().callback_kind=="afterEach"&&!teardown&&worker->ActiveDeadline()==121);
      const auto o=owners.back();now=35;steps->ObserveRawResult(o.callback_token,7,9);
      retries->Fence(o,[this,o](const auto& e){assert(!e);steps->Settled(o.callback_token,7,9,[this,o]{worker->CallbackSettled(o.callback_token,7,9,true,"",now);},now);},now);
      assert(steps->Packet({"STEP_CALLBACK_SETTLED",N(o.callback_token),"7","9"},now,false));
    }else assert(owners.size()==1);
    assert(teardown&&results.empty()&&worker->ActiveDeadline()==121);
    now=36;worker->FixtureSettled(setup,7,9,true,"",now);
    assert(results.size()==1&&!results[0].ok&&results[0].observation.status==PhaseStatus::Interrupted&&errors.empty());
    const auto state=worker->Inspect();assert(!state.callbacks&&!state.fixtures&&!state.scopes);
    assert(steps->IdleReady()&&retries->IdleReady());
  }
};
}
int main(){
  unsigned controls=0;
  // Keep both supported bindings: production charged clocks and clockless
  // HookWorker consumers must obey the same original cleanup fence/cap.
  for(const bool timed:{true,false}){
  for(const bool before:{false,true})for(const bool after:{false,true}){
    Harness f(after,before,timed);f.Stop();f.RetireManaged();f.Finish(after);++controls;
  }
  for(const bool retire:{false,true}){
    Harness f(true,false,timed);f.Stop();if(retire)f.RetireManaged();
    f.Tick(120);assert(f.errors.empty()&&!f.teardown&&f.owners.size()==1&&f.worker->ActiveDeadline()==121);
    f.Tick(121);assert(f.errors.size()==1&&f.worker->Inspect().terminating&&!f.teardown&&f.owners.size()==1&&f.results.empty());
    // This is controlled trusted worker-exit ingress, not an OS waitpid claim.
    f.now=122;f.steps->WorkerExited(1,501,f.now);f.retries->WorkerExited(1,501,f.now);f.worker->WorkerClosed();
    assert(f.steps->IdleReady()&&f.retries->IdleReady()&&!f.worker->Inspect().callbacks&&!f.worker->Inspect().fixtures);
    ++controls;
  }
  }
  assert(controls==12);
  std::cout<<"PASS 12 actual native HookWorker/fixtures/RunnerSteps/retry cleanup compositions (6 charged, 6 clockless); host/file/exit ingress controlled, no OS/JS proof claim\n";
}
