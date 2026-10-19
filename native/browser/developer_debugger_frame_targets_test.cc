// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "developer_debugger_frame_targets.h"
#include "developer_debugger_frame_pause.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>
static bool deny_new=false;
static size_t denied_allocations=0;
[[gnu::noinline]] void* operator new(std::size_t size){if(deny_new){++denied_allocations;throw std::bad_alloc();}if(void*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void*p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void*p,std::size_t)noexcept{std::free(p);}
[[gnu::noinline]] void* operator new[](std::size_t size){return ::operator new(size);}
[[gnu::noinline]] void operator delete[](void*p)noexcept{::operator delete(p);}
[[gnu::noinline]] void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
using Targets=suitecut::DeveloperDebuggerFrameTargets;
using Controls=suitecut::DeveloperDebuggerControls;
using Domains=suitecut::DebuggerDomains;
using Scopes=suitecut::FrameDebuggerScopes;
using Dict=Controls::Dict;
static Dict JSON(const std::string&json){auto value=CefParseJSON(json,JSON_PARSER_RFC);assert(value);return value->GetDictionary();}
struct Host {
  struct Call{std::string session,method;Domains::Reply reply;};
  struct Result{bool ok;std::string json,error;};
  double now=1;uint64_t lease=0;
  Scopes::Host identity{"observed-source",9};
  std::map<std::string,Scopes::Pin>current;
  std::map<std::pair<std::string,std::string>,suitecut::FrameDebuggerPause::Script>scripts;
  std::vector<Call>calls;
  std::map<int,Result>results;
  std::vector<std::string>close_requests;
  std::shared_ptr<Domains>domains;
  std::shared_ptr<Scopes>scopes;
  std::unique_ptr<Targets>targets;
  std::unique_ptr<Controls>root;
  Scopes::Pin root_pin;
  Host(){
    Domains::Hooks domain;domain.now=[this]{return now;};
    domain.send=[this](const std::string&session,const std::string&method,Dict,double,auto reply){calls.push_back({session,method,std::move(reply)});return true;};
    domain.close_channel=[this](const auto&,const auto&error){close_requests.push_back(error);};
    domains=std::make_shared<Domains>(std::move(domain));
    Scopes::Hooks scope;scope.now=[this]{return now;};scope.current=[this](const auto&pin){return Current(pin);};
    scope.lease_present=[this](uint64_t id){return domains->HasLease(id);};
    scope.cleanup=[this](auto,const auto&pin){if(targets)targets->Changed(pin);if(root&&pin==root_pin)root->Changed();};
    scope.close_channel=[this](const auto&,const auto&error){close_requests.push_back(error);};scope.wake=[]{};
    scopes=Scopes::Create(identity,std::move(scope),std::make_shared<Scopes::Budget>());
    Targets::Hooks hooks;hooks.current=[this](const auto&pin){return Current(pin);};hooks.create=[this](const auto&pin){return Create(pin,false);};
    targets=std::make_unique<Targets>(std::move(hooks));
  }
  ~Host(){domains->Closed();targets->Closed();if(root)root->Closed();scopes->HostClosed(identity);}
  bool Current(const Scopes::Pin&pin)const{const auto at=current.find(pin.frame);return at!=current.end()&&at->second==pin;}
  Scopes::Pin Observe(const std::string&frame,const std::string&session,int context,uint64_t document=1,uint64_t attachment=1,bool main=false){
    Scopes::Pin pin{{identity,session},attachment,document,frame,"actual-loader",frame+"-unique-"+std::to_string(document),context,main};
    current[frame]=pin;scripts[{session,"script-"+frame}]={context,false};return pin;
  }
  std::unique_ptr<Controls>Create(Scopes::Pin pin,bool agent){
    const Controls::Document document{1,pin.context,pin.unique_context};
    Controls::Hooks hooks;hooks.now=[this]{return now;};hooks.domains=domains;
    hooks.lease=[this]{return ++lease;};hooks.owner_namespace=identity.source;hooks.inspector_session=pin.channel.session;hooks.agent_controls=agent;
    hooks.scopes=scopes;hooks.scope_pin=[this,pin,document](const auto&owner,auto&output){if(owner!=document||!Current(pin))return false;output=pin;return true;};
    hooks.current=[this,pin,document]{return Current(pin)?document:Controls::Document{};};
    hooks.pause_eligibility=[this](auto token,Dict packet){return suitecut::FrameDebuggerPause::Classify(scopes,token,packet,[this](const auto&session,const auto&script)->std::optional<suitecut::FrameDebuggerPause::Script>{const auto at=scripts.find({session,script});return at==scripts.end()?std::nullopt:std::optional(at->second);});};
    hooks.owns_pause=[this,pin](Dict packet,const auto&){const auto script=packet->GetList("callFrames")->GetDictionary(0)->GetDictionary("location")->GetString("scriptId").ToString();const auto at=scripts.find({pin.channel.session,script});return Current(pin)&&at!=scripts.end()&&at->second.context==pin.context;};
    hooks.owns_script=[this,pin](const auto&script,const auto&){const auto at=scripts.find({pin.channel.session,script});return Current(pin)&&at!=scripts.end()&&at->second.context==pin.context;};hooks.owns_breakpoint_script=hooks.owns_script;
    hooks.send=[this,pin](const auto&method,Dict,double,auto reply){calls.push_back({pin.channel.session,method,std::move(reply)});return true;};
    hooks.close_channel=[this](const auto&error){close_requests.push_back(error);};hooks.wake=[]{};
    return std::make_unique<Controls>(std::move(hooks));
  }
  Controls::Reply Reply(int owner){return [this,owner](bool ok,const auto&json,const auto&error){assert(!results.contains(owner));results.emplace(owner,Result{ok,json,error});};}
  bool Has(const std::string&session,const std::string&method)const{return std::any_of(calls.begin(),calls.end(),[&](const auto&call){return call.session==session&&call.method==method;});}
  void Ack(const std::string&session,const std::string&method,bool ok=true,Dict result=nullptr){const auto at=std::find_if(calls.begin(),calls.end(),[&](const auto&call){return call.session==session&&call.method==method;});assert(at!=calls.end());auto reply=std::move(at->reply);calls.erase(at);reply(ok,result?result:CefDictionaryValue::Create(),ok?"":"actual command failure");}
  void Open(int owner,const Scopes::Pin&pin){targets->Open(owner,pin,JSON(R"({"action":"open"})"),100,Reply(owner));}
  void Ready(const std::string&session){Ack(session,"Debugger.enable");Ack(session,"Debugger.setSkipAllPauses");}
  std::string Token(int owner)const{const auto&result=results.at(owner);assert(result.ok);return JSON(result.json)->GetString("session").ToString();}
  void Run(int owner,const std::string&frame,const std::string&token,const std::string&action,double deadline=100){auto request=CefDictionaryValue::Create();request->SetString("action",action);request->SetString("session",token);targets->Handle(owner,frame,request,deadline,Reply(owner));}
  void Close(int owner,const Scopes::Pin&pin,const std::string&token){Run(owner,pin.frame,token,"close",0);if(Has(pin.channel.session,"Debugger.resume"))Ack(pin.channel.session,"Debugger.resume");if(Has(pin.channel.session,"Debugger.disable"))Ack(pin.channel.session,"Debugger.disable");assert(results.at(owner).ok);}
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
  { // A same-process root owner fences child admission through real disable.
    Host h;h.root_pin=h.Observe("root","",1,1,1,true);const auto child=h.Observe("child","",2);
    h.root=h.Create(h.root_pin,true);h.root->Handle(1,JSON(R"({"action":"open"})"),100,h.Reply(1));h.Ready("");
    h.Open(2,child);assert(!h.results.at(2).ok&&h.calls.empty());const auto token=h.Token(1);
    auto close=JSON("{\"action\":\"close\",\"session\":\""+token+"\"}");h.root->Handle(3,close,0,h.Reply(3));
    h.Open(4,child);assert(!h.results.at(4).ok&&h.Has("","Debugger.disable"));h.Ack("","Debugger.disable");assert(h.results.at(3).ok);
    h.Open(5,child);h.Ready("");assert(h.results.at(5).ok);h.Close(6,child,h.Token(5));++groups;
  }
  { // Independent actual OOPIF agents coexist and cannot exchange native tokens.
    Host h;const auto a=h.Observe("a","actual-a",11),b=h.Observe("b","actual-b",12);
    h.Open(1,a);h.Open(2,b);h.Ready("actual-b");h.Ready("actual-a");assert(h.targets->Live()==2);
    h.Run(3,b.frame,h.Token(1),"close",0);assert(!h.results.at(3).ok&&h.calls.empty()&&h.targets->Live()==2);
    h.Close(4,a,h.Token(1));assert(h.targets->Live()==1);h.Close(5,b,h.Token(2));assert(!h.targets->Live());++groups;
  }
  { // Root renderer death cannot invalidate an independent OOPIF controller.
    Host h;const auto child=h.Observe("child","",3),oopif=h.Observe("oopif","actual-child-session",4);
    h.Open(1,child);h.Ready("");h.Open(2,oopif);h.Ready("actual-child-session");
    h.current.erase(child.frame);h.targets->RootRealmDestroyed(1);assert(h.Has("","Debugger.disable")&&!h.Has("actual-child-session","Debugger.disable"));
    h.Ack("","Debugger.disable");assert(h.targets->Live()==1);h.Close(3,oopif,h.Token(2));++groups;
  }
  { // A canceled open retains the actual channel incarnation through late ACKs.
    Host h;const auto old=h.Observe("child","actual-child-session",5);h.Open(1,old);h.targets->Cancel(1);
    const auto next=h.Observe("child","actual-child-session",5,2,2);h.Open(2,next);assert(!h.results.at(2).ok);
    h.Ack(old.channel.session,"Debugger.enable");h.Ack(old.channel.session,"Debugger.disable");assert(!h.results.contains(1));
    h.Open(3,next);h.Ready(next.channel.session);h.Close(4,next,h.Token(3));++groups;
  }
  { // Eight live records are bounded independently of the retained metadata.
    Host h;std::vector<Scopes::Pin>pins;for(int i=0;i<8;++i){pins.push_back(h.Observe("frame"+std::to_string(i),"session"+std::to_string(i),10+i));h.Open(1+i,pins.back());h.Ready(pins.back().channel.session);}
    const auto extra=h.Observe("extra","another-session",99);h.Open(9,extra);assert(!h.results.at(9).ok&&h.calls.empty()&&h.targets->Live()==8);
    for(int i=0;i<8;++i){h.Close(20+i,pins[i],h.Token(1+i));}
    assert(!h.targets->Live());++groups;
  }
  { // Only real completed records may be evicted; an old token then fails.
    Host h;std::string first;Scopes::Pin pin;
    for(int i=0;i<34;++i){pin=h.Observe("child","actual-session",8,uint64_t(i+1));const int owner=10+i*2;h.Open(owner,pin);h.Ready(pin.channel.session);if(!i)first=h.Token(owner);h.Close(owner+1,pin,h.Token(owner));assert(h.targets->Records()<=32);}
    h.Run(1000,pin.frame,first,"close",0);assert(!h.results.at(1000).ok&&h.calls.empty());++groups;
  }
  { // The real controlled channel boundary has already drained its domain
    // callbacks. Manager fanout then needs no heap, even under actual new denial.
    Host h;const auto pin=h.Observe("child","actual-child-session",7);h.Open(1,pin);h.Ready(pin.channel.session);
    h.domains->Detached(pin.channel.session);assert(h.targets->Live()==1&&h.calls.empty());
    const auto before=denied_allocations;bool failed=false;deny_new=true;
    try{h.targets->ChannelClosed(pin.channel,pin.attachment);}catch(const std::bad_alloc&){failed=true;}
    deny_new=false;if(failed)std::cerr<<"actual post-admission channel snapshot allocation denied="<<(denied_allocations-before)<<"\n";assert(!failed&&denied_allocations==before&&!h.targets->Live());++groups;
  }
  { // Actual host closure snapshots all preexisting records without allocating
    // after its closed flag is set. No pending Core public callbacks are claimed.
    Host h;const auto a=h.Observe("a","actual-a",7),b=h.Observe("b","actual-b",8);h.Open(1,a);h.Ready(a.channel.session);h.Open(2,b);h.Ready(b.channel.session);
    h.domains->Closed();assert(h.targets->Live()==2&&h.calls.empty());
    const auto before=denied_allocations;bool failed=false;deny_new=true;
    try{h.targets->Closed();}catch(const std::bad_alloc&){failed=true;}
    deny_new=false;assert(!failed&&denied_allocations==before&&!h.targets->Live());h.targets->Closed();++groups;
  }
  std::cout<<"native frame debugger target component groups "<<groups<<" passed\n";
}
