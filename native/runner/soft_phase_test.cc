// SPDX-License-Identifier: MIT
#include "hook_phases.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut::runner;
struct SoftPhaseFixture {
 std::deque<HookPhases::Acquire> acquire;std::deque<HookPhases::Invoke> invoke;std::deque<HookPhases::Release> release;std::vector<HookPhases::Result> results;HookPhases phase;
 SoftPhaseFixture():phase({[&](const auto& q){acquire.push_back(q);},[&](const auto& q){invoke.push_back(q);},[](const auto&){},[&](const auto& q){release.push_back(q);},[&](const auto& r){results.push_back(r);},[](const auto&){assert(false);}}){}
 HookPhases::Invoke Admit(double now){auto q=acquire.front();acquire.pop_front();phase.Acquired(q.request,q.scope,q.generation,true,"",now);auto call=invoke.front();invoke.pop_front();return call;}
 void Complete(const HookPhases::Invoke& call,bool ok,double now){phase.Settled(call.token,call.scope,call.generation,ok,ok?"":"actual raw hard error",now);}
 void Release(double now){auto q=release.front();release.pop_front();phase.Released(q.request,q.scope,q.generation,true,"",now);}
};
int main(){unsigned groups=0;using K=HookKind;
 {SoftPhaseFixture f;assert(f.phase.Begin(1,1,{{K::BeforeEach,1,{}},{K::Body,2,{}},{K::AfterEach,3,{}}},100,0));const auto before=f.Admit(1);assert(f.phase.NoteSoftFailure(before.token,1,1,2));f.Complete(before,true,3);const auto body=f.Admit(4);assert(body.kind==K::Body);f.Complete(body,true,5);const auto after=f.Admit(6);assert(after.kind==K::AfterEach);f.Complete(after,true,7);f.Release(8);assert(f.results.size()==1&&!f.results[0].ok&&!f.results[0].infrastructure_failure&&f.results[0].errors.empty());++groups;}
 {SoftPhaseFixture f;assert(f.phase.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},100,0));f.Complete(f.Admit(1),false,2);const auto after=f.Admit(3);assert(f.phase.NoteSoftFailure(after.token,1,1,4));f.Complete(after,true,5);f.Release(6);assert(!f.results[0].ok&&f.results[0].infrastructure_failure&&f.results[0].errors.size()==1&&f.results[0].errors[0]=="actual raw hard error");++groups;}
 {SoftPhaseFixture f;assert(f.phase.Begin(1,1,{{K::BeforeAll,1,{}}},100,0));const auto all=f.Admit(1);assert(!f.phase.NoteSoftFailure(all.token,1,1,2));f.Complete(all,true,3);f.Release(4);assert(f.results[0].ok);++groups;}
 {SoftPhaseFixture f;assert(f.phase.Begin(1,1,{{K::Body,1,{}}},10,0));const auto body=f.Admit(1);assert(!f.phase.NoteSoftFailure(body.token,1,2,2));assert(!f.phase.NoteSoftFailure(body.token+1,1,1,2));assert(!f.phase.NoteSoftFailure(body.token,1,1,10));f.Complete(body,true,11);if(!f.release.empty())f.Release(12);assert(f.results.size()==1&&f.results[0].timed_out&&!f.results[0].ok);++groups;}
 std::cout<<"PASS "<<groups<<" native soft phase continuation/cleanup/hard/deadline controls (controlled callbacks)\n";
}
