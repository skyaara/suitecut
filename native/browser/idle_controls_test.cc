// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "idle_controls.h"
#include <cassert>
#include <iostream>
#include <limits>
using Controls=suitecut::NativeIdleControls;
struct Harness{
  struct Sent{int owner;std::string session;std::optional<Controls::State>state;Controls::Result reply;};
  std::vector<std::string>sessions{"child"};std::vector<Sent>sent;double now=1;bool root=true;unsigned successes=0,failures=0;
  std::shared_ptr<Controls>control;
  Harness(){control=std::make_shared<Controls>([this](int owner,const std::string&session,auto state,auto done){sent.push_back({owner,session,state,std::move(done)});},[this]{return sessions;},[this](const std::string&session){return session.empty()?root:std::find(sessions.begin(),sessions.end(),session)!=sessions.end();},[this]{return now;});}
  Controls::Result Done(){return [this](bool ok,const std::string&){if(ok)++successes;else ++failures;};}
  bool Apply(int id,std::optional<Controls::State>state,double deadline=100){return control->Apply(id,1,state,deadline,Done());}
  void Reply(size_t index,bool ok=true){auto done=std::move(sent.at(index).reply);assert(done);done(ok,ok?"":"actual CDP failure");}
};
int main(){
 unsigned groups=0;const Controls::State idle{false,false},active{true,true};
 {Harness h;assert(h.Apply(1,idle));assert(h.sent.size()==1&&h.sent[0].session.empty()&&!h.control->Acknowledged());h.Reply(0);assert(h.control->Acknowledged()==idle&&h.sent.size()==2&&h.sent[1].session=="child"&&h.successes==0);h.Reply(1);assert(h.successes==1&&!h.control->Pending());++groups;}
 {Harness h;assert(h.Apply(1,idle)&&h.Apply(2,std::nullopt));h.control->Cancel(1,1);assert(h.sent.size()==1&&h.control->Physical()&&!h.control->DispatchCurrent());h.Reply(0);assert(h.control->Acknowledged()==idle&&h.sent.size()==2&&h.sent[1].owner==2&&!h.sent[1].state);h.Reply(1);assert(!h.control->Acknowledged()&&h.sent.size()==3&&!h.sent[2].state);h.Reply(2);assert(h.successes==1&&h.failures==0&&!h.control->Pending());++groups;}
 {Harness h;assert(h.Apply(1,idle)&&h.Apply(2,active));h.control->Cancel(2,1);h.Reply(0);h.Reply(1);assert(h.sent.size()==2&&h.successes==1&&!h.control->Pending());++groups;}
 {Harness h;assert(h.Apply(1,idle));assert(h.control->Child("child",100,h.Done()));h.Reply(0);h.Reply(1);assert(h.sent.size()==3&&h.sent[2].state==idle);h.Reply(2);assert(h.successes==2);++groups;}
 {Harness h;assert(h.Apply(1,std::nullopt));h.Reply(0);h.Reply(1);assert(h.control->Child("child",100,h.Done()));assert(!h.sent[2].state);h.Reply(2);assert(h.successes==2);++groups;}
 {Harness h;assert(h.Apply(1,idle));h.sessions.clear();h.Reply(0);assert(h.sent.size()==1&&h.successes==1);++groups;}
 {Harness h;assert(h.Apply(1,idle));h.Reply(0);h.sessions.clear();h.Reply(1,false);assert(h.successes==1&&h.failures==0);++groups;}
 {Harness h;assert(h.Apply(1,idle));h.Reply(0);h.Reply(1,false);assert(h.failures==1&&h.control->Acknowledged()==idle&&!h.control->Pending());++groups;}
 {Harness h;assert(h.Apply(1,idle));h.Reply(0,false);assert(h.failures==1&&!h.control->Acknowledged()&&h.sent.size()==1);++groups;}
 {Harness h;assert(h.Apply(1,idle,5));h.now=5;assert(!h.control->DispatchCurrent());h.Reply(0);assert(h.failures==1&&h.control->Acknowledged()==idle&&h.sent.size()==1);++groups;}
 {Harness h;assert(h.Apply(1,idle));assert(h.Apply(2,active,5));h.now=5;h.Reply(0);h.Reply(1);assert(h.sent.size()==2&&h.failures==1&&h.successes==1);++groups;}
 {Harness h;assert(h.Apply(1,idle,0));h.now=1e12;h.Reply(0);h.Reply(1);assert(h.successes==1);++groups;}
 {Harness h;assert(h.Apply(1,idle));h.control->Close();assert(h.control->Physical()&&h.control->Pending());h.Reply(0);assert(!h.control->Pending()&&h.successes==0&&h.failures==0&&!h.control->Acknowledged());++groups;}
 {Harness h;assert(!h.control->Apply(0,1,idle,100,h.Done()));assert(!h.control->Apply(1,0,idle,100,h.Done()));assert(!h.Apply(1,idle,-1));assert(!h.Apply(1,idle,std::numeric_limits<double>::infinity()));assert(!h.control->Child("",100,h.Done()));++groups;}
 {Harness h;for(int id=1;id<=64;++id)assert(h.Apply(id,idle));assert(!h.Apply(65,idle)&&!h.Apply(1,active));for(int id=1;id<=64;++id)h.control->Cancel(id,1);assert(h.control->Physical()&&h.sent.size()==1);h.Reply(0);assert(!h.control->Pending());++groups;}
 {Harness h;assert(h.Apply(1,idle));h.control->Cancel(1,2);h.Reply(0);h.Reply(1);assert(h.successes==1);++groups;}
 {double now=1;unsigned sent=0,done=0;auto control=std::make_shared<Controls>([&](int,const auto&,auto,auto reply){++sent;reply(true,"");},[]{return std::vector<std::string>{"one","two"};},[](const auto&){return true;},[&]{return now;});assert(control->Apply(1,1,idle,100,[&](bool ok,const auto&){assert(ok);++done;assert(control->Apply(2,1,std::nullopt,100,[&](bool accepted,const auto&){assert(accepted);++done;}));}));assert(done==2&&sent==6&&!control->Pending());++groups;}
 {Harness h;assert(h.Apply(1,idle));auto callback=std::move(h.sent[0].reply);auto receipt=std::make_shared<Controls::Receipt>(std::move(callback));assert(h.control->Physical());receipt.reset();assert(h.failures==1&&!h.control->Pending()&&!h.control->Acknowledged());++groups;}
 std::cout<<"native idle controls: "<<groups<<" controlled retained-CDP lifecycle groups\n";
}
