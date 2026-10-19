// SPDX-License-Identifier: MIT
#include "fixture_graph.h"
#include "hook_phases.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut::runner;
struct Flat {
 std::vector<FixtureOwners::Event> events;std::vector<bool> results;std::vector<std::string> errors;unsigned killed=0;
 FixtureOwners owners{FixtureGraph({{"resource",Scope::Test,{}}}),{
  [this](const auto& e){events.push_back(e);},[](auto,auto){},[this](auto,auto,bool ok,const auto& error){results.push_back(ok);errors.push_back(error);},[this](const auto&){++killed;}}};
 Flat(double deadline=1000){assert(owners.Begin(1,1,{"resource"},deadline,0));auto e=events.back();owners.Ready(e.token,e.test,e.generation,1);}
 void Released(double now){const auto e=events.back();assert(e.kind==FixtureOwners::EventKind::ResumeTeardown);owners.Settled(e.token,e.test,e.generation,true,"",now);}
};
struct Hooks {
 std::deque<HookPhases::Acquire> acquire;std::deque<HookPhases::Invoke> invoke;std::deque<HookPhases::Release> release;
 std::vector<HookPhases::Invoke> abort;std::vector<HookPhases::Result> results;unsigned killed=0;
 HookPhases phases{{[this](const auto& c){acquire.push_back(c);},[this](const auto& c){invoke.push_back(c);},[this](const auto& c){abort.push_back(c);},[this](const auto& c){release.push_back(c);},[this](const auto& r){results.push_back(r);},[this](const auto&){++killed;}}};
 HookPhases::Invoke Admit(double now){assert(!acquire.empty());const auto a=acquire.front();acquire.pop_front();phases.Acquired(a.request,a.scope,a.generation,true,"",now);assert(!invoke.empty());const auto c=invoke.front();invoke.pop_front();return c;}
 void Finish(double now){assert(!release.empty());const auto r=release.front();release.pop_front();phases.Released(r.request,r.scope,r.generation,true,"",now);}
};
int main(){unsigned groups=0;using K=HookKind;
 {Flat f;assert(f.owners.Skip(1,1,2,true));assert(f.events.size()==1&&f.results.empty());f.owners.Tick(20);assert(f.events.size()==1);f.owners.Finish(true,"",21);assert(f.events.size()==2);f.Released(22);assert(f.results==std::vector<bool>{true}&&!f.killed);++groups;}
 {Flat f;assert(f.owners.Skip(1,1,2,true));f.owners.Cancel("actual stop during file drain",3);assert(f.events.size()==1);f.owners.Finish(true,"",4);f.Released(5);assert(f.results==std::vector<bool>{false}&&f.errors[0]=="actual stop during file drain");++groups;}
 {Flat f(10);assert(f.owners.Skip(1,1,2,true));f.owners.Finish(true,"",10);assert(f.killed==1&&f.events.size()==1&&f.results.empty()&&f.owners.State().physical==1);f.owners.WorkerClosed();assert(f.results==std::vector<bool>{false}&&f.owners.State().physical==0);++groups;}
 {Flat f(5000);assert(f.owners.Skip(1,1,2,true));f.owners.Tick(1001);assert(!f.killed);f.owners.Tick(1002);assert(f.killed==1&&f.events.size()==1&&f.owners.State().physical==1);f.owners.WorkerClosed();assert(f.results==std::vector<bool>{false});++groups;}
 for(const auto kind:{K::Body,K::BeforeEach}){Hooks h;std::vector<HookCall> calls;if(kind==K::BeforeEach)calls.push_back({K::BeforeEach,1,{}});calls.push_back({K::Body,2,{}});calls.push_back({K::AfterEach,3,{}});assert(h.phases.Begin(1,1,calls,100,0));const auto c=h.Admit(1);assert(c.kind==kind);bool accepted=false;assert(h.phases.RequestSkip(c.token,1,1,"\"native cause\"",2,[&]{accepted=true;},true));assert(accepted&&h.abort.size()==1&&h.acquire.empty()&&h.release.empty()&&h.phases.Inspect().physical==1);h.phases.Tick(5);assert(h.acquire.empty());h.phases.Settled(c.token,1,1,true,"",6,true);const auto after=h.Admit(7);assert(after.kind==K::AfterEach&&after.observation.status==PhaseStatus::Skipped);h.phases.Settled(after.token,1,1,true,"",8);h.Finish(9);assert(h.results.size()==1&&h.results[0].ok&&h.results[0].skipped&&!h.killed);++groups;}
 {Hooks h;assert(h.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},100,0));const auto c=h.Admit(1);assert(h.phases.RequestSkip(c.token,1,1,"\"cause\"",2,{},true));h.phases.Cancel("actual native interrupt",3);assert(h.acquire.empty());h.phases.Settled(c.token,1,1,true,"",4,true);const auto after=h.Admit(5);assert(after.observation.status==PhaseStatus::Interrupted);h.phases.Settled(after.token,1,1,true,"",6);h.Finish(7);assert(h.results[0].observation.status==PhaseStatus::Interrupted&&!h.results[0].ok);++groups;}
 {Hooks h;assert(h.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},10,0));const auto c=h.Admit(1);assert(h.phases.RequestSkip(c.token,1,1,"\"cause\"",2,{},true));h.phases.Tick(10);assert(h.killed==1&&h.acquire.empty()&&h.release.empty()&&h.results.empty()&&h.phases.Inspect().physical==1);h.phases.WorkerClosed(11);assert(h.results[0].timed_out&&!h.results[0].ok&&h.phases.Inspect().physical==0);++groups;}
 {Hooks h;assert(h.phases.Begin(1,1,{{K::BeforeAll,1,{}}},100,0));const auto c=h.Admit(1);assert(!h.phases.RequestSkip(c.token,1,1,"\"unsupported\"",2,{},true));assert(h.abort.empty()&&h.phases.Inspect().physical==1);h.phases.Settled(c.token,1,1,true,"",3);h.Finish(4);assert(h.results[0].ok&&!h.results[0].skipped);++groups;}
 std::cout<<"PASS"<<groups<<" controlled native callback/file skip-drain ordering and original deadline groups\n";
}
