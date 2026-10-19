// SPDX-License-Identifier: MIT
#include "hook_worker.h"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
using namespace suitecut::runner;

// A genuine pipe-held native child observes transport delivery only. It does
// not execute JS or manufacture any author CALL_RESULT/fixture settlement.
struct Child {
  pid_t pid;int input=-1,output=-1;bool killed=false,reaped=false;
  Child(){
    int in[2],out[2];assert(::pipe(in)==0);assert(::pipe(out)==0);pid=::fork();assert(pid>=0);
    if(!pid){
      ::close(in[1]);::close(out[0]);std::string packet;char byte;
      for(;;){const auto n=::read(in[0],&byte,1);if(n<0&&errno==EINTR)continue;if(n!=1)::_exit(2);
        if(byte!='\n'){packet+=byte;continue;}
        if(packet=="EXIT")::_exit(0);
        const char received=packet.empty()?'?':packet[0];if(::write(out[1],&received,1)!=1)::_exit(3);packet.clear();
      }
    }
    ::close(in[0]);::close(out[1]);input=in[1];output=out[0];
  }
  ~Child(){if(!reaped){::kill(pid,SIGKILL);int status;while(::waitpid(pid,&status,0)<0&&errno==EINTR){}}if(input>=0)::close(input);if(output>=0)::close(output);}
  void Delivered(const std::string& packet){
    const auto bytes=packet+'\n';assert(::write(input,bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size()));
    pollfd fd{output,POLLIN,0};int ready;do{ready=::poll(&fd,1,1000);}while(ready<0&&errno==EINTR);assert(ready==1);
    char byte;assert(::read(output,&byte,1)==1&&byte==packet[0]);
  }
  void Kill(){assert(!killed);assert(::kill(pid,SIGKILL)==0);killed=true;}
  void Capture(){assert(killed&&!reaped);int status;pid_t receipt;do{receipt=::waitpid(pid,&status,0);}while(receipt<0&&errno==EINTR);assert(receipt==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL);reaped=true;assert(::waitpid(pid,&status,WNOHANG)==-1&&errno==ECHILD);std::cout<<"ACTUAL_WAITPID "<<pid<<" "<<status<<" "<<WTERMSIG(status)<<std::endl;}
};

static void Baseline(){
  bool terminated=false;double now=0;HookWorker worker({},{{[&]{return now;}},
    [](const std::string&){throw std::runtime_error("actual before-dispatch exception");},
    [](const auto&){assert(false);},[&](const std::string&){terminated=true;}});
  assert(worker.Begin({1,1,1,0,100,FixtureScopeKind::Test},{{HookKind::Body,1,{}}},now));
  assert(terminated&&worker.Inspect().terminating&&worker.Inspect().callbacks==1);
  // This is a pure control; no physical child or author receipt is claimed.
}

static void Failure(bool deferred,bool partial,bool setup,bool unknown=false,bool terminationThrows=false){
  Child child;double now=0;std::vector<std::string> sent,errors;std::vector<HookPhases::Result> completed;
  HookWorker worker(deferred?std::vector<Fixture>{{"owned",Scope::Test,{}}}:std::vector<Fixture>{},
    {[&]{return now;},[&](const std::string& packet){
      sent.push_back(packet);const bool failing=packet.starts_with(setup?"SETUP ":"CALL ");
      if(!failing||partial)child.Delivered(packet);
      if(failing){if(unknown)throw 7;throw std::runtime_error(partial?"actual post-delivery exception":"actual before-delivery exception");}
    },[&](const auto& result){completed.push_back(result);},[&](const std::string& error){errors.push_back(error);if(terminationThrows)throw std::runtime_error("actual termination failure before kill");child.Kill();}});
  const std::vector<std::string> roots=deferred?std::vector<std::string>{"owned"}:std::vector<std::string>{};
  uint64_t fixture=0;
  bool escaped=false;
  try{
  assert(worker.Begin({1,1,1,0,100,FixtureScopeKind::Test},{{HookKind::Body,1,roots},{HookKind::AfterEach,2,{}}},now));
  if(deferred&&!setup){
    assert(!worker.Inspect().terminating&&worker.Inspect().fixtures==1&&worker.Inspect().callbacks==0);
    std::istringstream fields(sent.front());std::string command;uint64_t scope,generation;fields>>command>>fixture>>scope>>generation;
    assert(command=="SETUP"&&scope==1&&generation==1);worker.FixtureReady(fixture,scope,generation,++now);
  }
  }catch(const std::runtime_error& error){assert(terminationThrows&&std::string(error.what())=="actual termination failure before kill");escaped=true;}
  assert(escaped==terminationThrows);
  if(terminationThrows){int status;assert(!child.killed&&::waitpid(child.pid,&status,WNOHANG)==0);}
  assert(errors.size()==1&&errors[0]==(unknown?"Native hook callback dispatch threw an unknown exception":partial?"actual post-delivery exception":"actual before-delivery exception"));
  const auto held=worker.Inspect();assert(held.terminating&&held.scopes==1&&held.fixtures==(deferred?1u:0u)&&held.callbacks==(setup?0u:1u));
  assert(completed.empty());const auto deadline=worker.ActiveDeadline();assert(deadline&&*deadline==100);
  // An attempted late/foreign settlement or amendment cannot release the
  // reserved physical owner after a possibly-partial dispatch. No real author
  // receipt is asserted: actual kernel closure below is the retirement fence.
  const auto packets=sent.size();worker.CallbackSettled(1,1,1,true,"",++now);worker.FixtureSettled(fixture,1,1,true,"",now);
  worker.FixtureReady(fixture,1,1,now);worker.Cancel("late cancellation",now);worker.Tick(10000);worker.RefreshFixtureBudget(10000);
  assert(!worker.AmendTimeout(1,1,1,1000,now));assert(!worker.RequestSkip(1,1,1,"null",now));assert(!worker.NoteSoftFailure(1,1,1,1,now));
  assert(!worker.CanExpectFailure(1,1,1,1,now));assert(!worker.Retire(10000,now));assert(worker.FixtureExpired(fixture,1,1,"late expiry",now)==(fixture!=0));
  assert(!worker.FixtureExpired(fixture,2,1,"foreign expiry",now));
  assert(sent.size()==packets&&worker.Inspect().callbacks==held.callbacks&&worker.Inspect().fixtures==held.fixtures&&worker.Inspect().scopes==1&&completed.empty());
  // An actual coordinator, not the swallowed admission path, must now own the
  // fatal termination error and kill/waitpid this still-live child.
  if(terminationThrows)child.Kill();
  child.Capture();assert(worker.Inspect().callbacks==held.callbacks&&worker.Inspect().fixtures==held.fixtures);
  worker.WorkerClosed();assert(worker.Inspect().callbacks==0&&worker.Inspect().fixtures==0&&worker.Inspect().scopes==0);
  assert(completed.size()==1&&!completed[0].ok&&completed[0].infrastructure_failure);
  assert(completed[0].errors.size()>=2&&completed[0].errors.front()==errors[0]);
  assert(!worker.Begin({2,2,2,0,100,FixtureScopeKind::Test},{{HookKind::Body,2,{}}},now));
}
static void PriorRawError(){
  Child child;double now=0;unsigned calls=0;uint64_t token=0;std::vector<HookPhases::Result> results;
  HookWorker worker({}, {[&]{return now;},[&](const std::string& packet){
    assert(packet.starts_with("CALL "));if(++calls==2)throw std::runtime_error("actual cleanup dispatch failure");
    std::istringstream input(packet);std::string command;input>>command>>token;child.Delivered(packet);
  },[&](const auto& result){results.push_back(result);},[&](const auto& error){assert(error=="actual cleanup dispatch failure");child.Kill();}});
  assert(worker.Begin({1,1,1,0,100,FixtureScopeKind::Test},{{HookKind::Body,1,{}},{HookKind::AfterEach,2,{}}},now));
  const std::string raw="original body Error\n    original authored stack";
  // Controlled native callback input, not a claim of public author execution.
  worker.CallbackSettled(token,1,1,false,raw,++now);
  assert(worker.Inspect().terminating&&worker.Inspect().callbacks==1&&results.empty());
  child.Capture();worker.WorkerClosed();assert(results.size()==1&&results[0].infrastructure_failure);
  assert(results[0].errors.size()==3&&results[0].errors[0]==raw&&results[0].errors[1]=="actual cleanup dispatch failure"&&results[0].errors[2]=="Host worker physically exited");
}
int main(int argc,char**){
  if(argc>1){Baseline();return 0;}
  unsigned controls=0;
  for(bool deferred:{false,true})for(bool partial:{false,true}){Failure(deferred,partial,false);++controls;}
  for(bool partial:{false,true}){Failure(true,partial,true);++controls;}
  Failure(false,false,false,true);++controls;
  for(bool deferred:{false,true}){Failure(deferred,true,false,false,true);++controls;}
  PriorRawError();++controls;
  Baseline();++controls;
  std::cout<<"PASS "<<controls<<" controlled dispatch failures; ten real known children SIGKILL/waitpid retired before WorkerClosed; no public author callback receipt claimed\n";
}
