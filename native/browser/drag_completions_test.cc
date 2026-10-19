// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "drag_completions.h"
#include <cstdio>
#include <stdexcept>

using Core=suitecut::DragCompletions;
struct Fixture {
  struct Call{Core::Stage stage;Core::Reply reply;};
  struct Result{bool ok;std::string error;};
  Core::Scope scope{1,1,1,1};
  double now=1;
  bool current=true,incarnation=true,synchronous=false,refuse_drop=false,refuse_disable=false,throw_send=false;
  int wakes=0,closes=0,sends=0;
  std::vector<Call>calls;
  std::vector<Core::Stage>stages;
  std::vector<Result>results;
  std::shared_ptr<Core::Budget>budget;
  std::shared_ptr<Core>core;
  explicit Fixture(std::shared_ptr<Core::Budget>b=std::make_shared<Core::Budget>()):budget(std::move(b)){
    Core::Hooks hooks;
    hooks.now=[this]{return now;};hooks.wake=[this]{++wakes;};
    hooks.send=[this](const Core::Scope&,Core::Stage stage,const Core::Payload&,Core::Reply reply){
      ++sends;stages.push_back(stage);
      if(throw_send)throw std::runtime_error("actual controlled sender exception");
      if((refuse_drop&&stage==Core::Stage::Drop)||(refuse_disable&&stage==Core::Stage::DisableIntercept))return false;
      if(synchronous)reply(Core::Outcome::Acknowledged,"");else calls.push_back({stage,std::move(reply)});
      return true;
    };
    hooks.operation_current=[this](const Core::Scope&){return current;};
    hooks.incarnation_current=[this](const Core::Scope&){return incarnation;};
    hooks.logical=[this](const Core::Scope&key,const Core::Payload&,bool ok,const std::string&error){results.push_back({ok,error});if(!ok)core->Cancel(key);};
    hooks.close_channel=[this](const Core::Scope&,const std::string&){++closes;};
    core=Core::Create(std::move(hooks),budget);assert(core);
  }
  ~Fixture(){core->ChannelClosed();}
  bool Begin(Core::Kind kind=Core::Kind::Manual,double deadline=0,size_t bytes=7){return core->Begin(scope,kind,bytes,deadline,std::make_shared<int>(42));}
  void Ack(Core::Outcome outcome=Core::Outcome::Acknowledged,const std::string&error=""){
    assert(!calls.empty());auto call=std::move(calls.front());calls.erase(calls.begin());call.reply(outcome,error);
  }
};
int main(){int groups=0;
 {Fixture f;assert(f.Begin());assert(f.core->Pending()==1&&f.budget->records==1);f.Ack();assert(f.calls[0].stage==Core::Stage::DisableIntercept&&f.results.empty());f.Ack();assert(!f.core->Busy()&&f.budget->records==0&&f.results.size()==1&&f.results[0].ok);++groups;}
 {Fixture f;assert(f.Begin(Core::Kind::Helper));f.Ack();assert(f.calls[0].stage==Core::Stage::MouseRelease);f.Ack();assert(f.calls[0].stage==Core::Stage::DisableIntercept);f.Ack();assert(f.results.size()==1&&f.results[0].ok&&f.budget->bytes==0);++groups;}
 {Fixture f;assert(f.Begin());f.current=false;f.core->Cancel(f.scope);assert(f.calls.size()==1&&f.calls[0].stage==Core::Stage::Drop&&f.core->Pending()==1);f.Ack();assert(f.calls.size()==1&&f.calls[0].stage==Core::Stage::DisableIntercept&&f.results.empty());f.Ack();assert(f.budget->records==0&&f.results.empty());++groups;}
 {Fixture f;assert(f.Begin(Core::Kind::Manual,10));f.now=10;f.Ack();assert(f.results.size()==1&&!f.results[0].ok&&f.results[0].error.find("timed out")!=std::string::npos);assert(f.stages==std::vector<Core::Stage>({Core::Stage::Drop,Core::Stage::DisableIntercept}));f.Ack();assert(f.results.size()==1&&!f.core->Busy());++groups;}
 {Fixture f;assert(f.Begin(Core::Kind::Manual,10));f.now=10;f.core->Tick();assert(f.core->Pending()==1&&f.results.size()==1&&f.closes==0);f.now=30009;f.core->Tick();assert(f.closes==0);f.now=30010;f.core->Tick();assert(f.closes==1&&f.core->Closing()==1&&f.core->Pending()==1&&!f.core->NeedsTick());f.core->Tick();assert(f.closes==1);f.Ack();assert(f.calls.empty()&&f.budget->records==1&&f.core->Pending()==0);f.core->ChannelClosed();assert(f.budget->records==0);++groups;}
 {Fixture f;assert(f.Begin(Core::Kind::Manual,100000));f.now=3;f.core->Cancel(f.scope);f.now=30002;f.core->Tick();assert(f.closes==0);f.now=30003;f.core->Tick();assert(f.closes==1);++groups;}
 {Fixture f;assert(f.Begin());assert(!f.core->NeedsTick());f.now=900000;f.core->Tick();assert(f.closes==0&&f.results.empty()&&f.core->Pending()==1);f.core->Cancel(f.scope);f.now=929999;f.core->Tick();assert(f.closes==0);f.now=930000;f.core->Tick();assert(f.closes==1);++groups;}
 {Fixture f;f.refuse_drop=true;assert(f.Begin());assert(f.calls.empty()&&f.results.size()==1&&!f.results[0].ok&&f.closes==1&&f.budget->records==1&&f.sends==1);f.core->ChannelClosed();assert(f.budget->records==0);++groups;}
 {Fixture f;assert(f.Begin());f.Ack(Core::Outcome::Error,"Actual accepted Drop error");assert(f.calls.empty()&&f.closes==1&&f.results[0].error=="Actual accepted Drop error"&&f.budget->records==1);assert(f.stages==std::vector<Core::Stage>({Core::Stage::Drop}));++groups;}
 {Fixture f;assert(f.Begin());f.Ack();f.Ack(Core::Outcome::Error,"Actual disable error");assert(f.closes==1&&f.results.size()==1&&!f.results[0].ok&&f.budget->records==1);++groups;}
 {Fixture f;f.refuse_disable=true;assert(f.Begin());f.Ack();assert(f.closes==1&&f.budget->records==1&&f.results.size()==1&&!f.results[0].ok);++groups;}
 {Fixture f;assert(f.Begin());f.incarnation=false;f.Ack();assert(f.sends==1&&f.calls.empty()&&f.results.size()==1&&!f.results[0].ok&&f.budget->records==1&&f.closes==1);++groups;}
 {Fixture f;assert(f.Begin());f.current=false;f.Ack();f.Ack();assert(f.results.empty()&&f.budget->records==0);++groups;}
 {Fixture f;assert(f.Begin());auto duplicate=f.calls[0].reply;f.Ack();duplicate(Core::Outcome::Acknowledged,"");assert(f.sends==2&&f.core->Pending()==1);f.Ack();duplicate(Core::Outcome::Acknowledged,"");assert(f.results.size()==1&&f.budget->records==0);++groups;}
 {Fixture f;f.synchronous=true;assert(f.Begin(Core::Kind::Helper));assert(f.sends==3&&f.results.size()==1&&f.results[0].ok&&f.budget->records==0);++groups;}
 {Fixture f;assert(f.Begin());const auto old=f.calls[0].reply;f.core->ChannelClosed();assert(f.budget->records==0&&f.budget->bytes==0&&!f.core->Busy());old(Core::Outcome::Acknowledged,"");assert(f.results.empty()&&f.sends==1);assert(!f.Begin());++groups;}
 {auto budget=std::make_shared<Core::Budget>();budget->max_records=2;budget->max_bytes=20;Fixture a(budget),b(budget),c(budget);assert(a.Begin(Core::Kind::Manual,0,7)&&b.Begin(Core::Kind::Manual,0,12));assert(!c.Begin(Core::Kind::Manual,0,1));a.Ack();a.Ack();assert(c.Begin(Core::Kind::Manual,0,8));assert(budget->records==2&&budget->bytes==20);++groups;}
 {Fixture f;assert(f.Begin());auto different=f.scope;++different.incarnation;assert(!f.core->Begin(different,Core::Kind::Manual,1,0,std::make_shared<int>(1)));assert(f.sends==1&&f.budget->records==1);++groups;}
 {Fixture f;f.now=10;assert(!f.Begin(Core::Kind::Manual,10));assert(f.sends==0&&f.budget->records==0);++groups;}
 {Fixture f;assert(f.Begin(Core::Kind::Helper));f.current=false;f.core->Cancel(f.scope);f.Ack();assert(f.sends==1&&f.closes==1&&f.budget->records==1&&f.results.empty());++groups;}
 {Fixture f;assert(f.Begin(Core::Kind::Helper));f.Ack();assert(f.calls[0].stage==Core::Stage::MouseRelease);f.current=false;f.core->Cancel(f.scope);f.Ack();assert(f.sends==2&&f.closes==1&&f.budget->records==1&&f.results.empty());++groups;}
 {Fixture f;assert(f.Begin(Core::Kind::Helper));auto old=f.calls[0].reply;f.incarnation=false;f.Ack();assert(f.closes==1&&f.sends==1&&f.budget->records==1);old(Core::Outcome::Acknowledged,"");assert(f.sends==1&&f.budget->records==1);f.core->ChannelClosed();assert(f.budget->records==0);++groups;}
 {Fixture f;f.throw_send=true;assert(f.Begin());assert(f.closes==1&&f.budget->records==1&&f.results.size()==1&&!f.results[0].ok&&!f.core->NeedsTick());++groups;}
 std::printf("Native drag completion receipt draft: %d controlled groups PASS\n",groups);
}
