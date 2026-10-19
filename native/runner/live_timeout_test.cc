// SPDX-License-Identifier: MIT
#include "hook_phases.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut::runner;
struct P {
 std::deque<HookPhases::Acquire> acquire;std::deque<HookPhases::Invoke> invoke;std::deque<HookPhases::Release> release;std::vector<HookPhases::Result> results;std::vector<HookPhases::Invoke> abort;
 HookPhases phases;
 P():phases({[&](auto c){acquire.push_back(c);},[&](auto c){invoke.push_back(c);},[&](auto c){abort.push_back(c);},[&](auto c){release.push_back(c);},[&](auto r){results.push_back(r);},[](const auto&){assert(false);}}){}
 HookPhases::Invoke Admit(double now){assert(!acquire.empty());auto c=acquire.front();acquire.pop_front();phases.Acquired(c.request,c.scope,c.generation,true,"",now);assert(!invoke.empty());auto v=invoke.front();invoke.pop_front();return v;}
 auto Amend(HookPhases::Invoke c,uint64_t timeout,double now){return phases.AmendTimeout(c.token,c.scope,c.generation,timeout,now);}
 void Done(HookPhases::Invoke c,double now){phases.Settled(c.token,c.scope,c.generation,true,"",now);}
};
int main(){using K=HookKind;
 {P p;assert(p.phases.Begin(1,1,{{K::BeforeEach,1,{}},{K::Body,2,{}},{K::AfterEach,3,{}}},100,100));auto c=p.Admit(110);assert(p.Amend(c,200,120)==300);p.Done(c,130);c=p.Admit(140);assert(c.deadline==300);assert(p.Amend(c,70,150)==170);assert(!p.phases.AmendTimeout(c.token+1,c.scope,c.generation,90,151));assert(!p.phases.AmendTimeout(c.token,c.scope,c.generation+1,90,151));p.Done(c,160);c=p.Admit(165);assert(c.deadline==230);assert(p.Amend(c,50,170)==210);assert(!p.Amend(c,120001,171));}
 {P p;assert(p.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},100,100));auto c=p.Admit(110);assert(p.Amend(c,10,120)==110);p.phases.Tick(120);assert(p.abort.size()==1&&p.phases.Inspect().physical==1);auto after=p.Admit(121);assert(after.observation.status==PhaseStatus::TimedOut);assert(p.Amend(after,40,125)==160);assert(p.phases.Inspect().physical==2);}
 {P p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},10,100));auto c=p.Admit(101);assert(!p.Amend(c,200,110));assert(p.abort.size()==1&&p.phases.Inspect().physical==1);assert(p.release.size()==1);}
 {P p;assert(p.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},100,100));auto c=p.Admit(101);p.phases.Cancel("actual global stop",102);assert(!p.Amend(c,0,103));auto after=p.Admit(104);assert(!p.Amend(after,0,105));assert(after.observation.status==PhaseStatus::Interrupted);}
 {P p;assert(p.phases.Begin(1,1,{{K::AfterAll,1,{}}},100,500));auto c=p.Admit(510);assert(p.Amend(c,20,515)==520);assert(p.Amend(c,0,516)==3153600000500.0);assert(p.phases.ActiveDeadline()==3153600000500.0);}
 {P p;assert(p.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},50,0));auto body=p.Admit(1);p.phases.Tick(50);assert(p.phases.TimeoutAt()==50);auto after=p.Admit(51);assert(p.Amend(after,2000,52)==2050);p.phases.Cancel("later actual global stop",1000,false);assert(p.phases.TimeoutAt()==50);p.Done(body,1001);p.Done(after,1002);assert(p.release.size()==1);}
 {P p;assert(p.phases.Begin(1,1,{{K::Body,1,{}},{K::AfterEach,2,{}}},1000,0));p.Admit(1);p.phases.Cancel("earlier native stop observed late",1100,true);assert(p.phases.TimeoutAt()==1000);auto after=p.Admit(1101);assert(after.observation.status==PhaseStatus::Interrupted);}
 std::cout<<"PASS native live timeout original-main/cleanup/all-hook budgets, stale and expired ingress, bounds, interruption and physical retention\n";
}
