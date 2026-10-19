// SPDX-License-Identifier: MIT
#include "hook_worker.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
int main(){
  unsigned groups=0;std::vector<sr::HookPhases::Acquire> acquired;std::vector<sr::HookPhases::Invoke> invoked;std::vector<sr::HookPhases::Release> releases;std::vector<sr::HookPhases::Result> results;
  sr::HookPhases phases({[&](const auto& v){acquired.push_back(v);},[&](const auto& v){invoked.push_back(v);},[](const auto&){},[&](const auto& v){releases.push_back(v);},[&](const auto& v){results.push_back(v);},[](const auto&){assert(false);}});
  const std::vector<sr::HookCall> calls{{sr::HookKind::BeforeEach,1,{}},{sr::HookKind::Body,2,{}},{sr::HookKind::AfterEach,3,{}}};
  // Preparation actually consumed30ms before this test group; an independent
  // beforeAll consumed another200ms. Only native preparation consumes its budget.
  assert(phases.Begin(1,1,calls,100,1000,{sr::PhaseStatus::Passed,230},30));assert(acquired.back().deadline==1070);++groups;
  auto acquire=acquired.back();phases.Acquired(acquire.request,1,1,true,"",1001);auto invoke=invoked.back();assert(invoke.kind==sr::HookKind::BeforeEach&&invoke.observation.duration==231&&invoke.deadline==1070);++groups;
  const auto amended=phases.AmendTimeout(invoke.token,1,1,150,1010);assert(amended&&*amended==1120);++groups;
  phases.Settled(invoke.token,1,1,true,"",1020);acquire=acquired.back();assert(acquire.deadline==1120);phases.Acquired(acquire.request,1,1,true,"",1021);invoke=invoked.back();assert(invoke.kind==sr::HookKind::Body&&invoke.observation.duration==251);++groups;
  phases.Settled(invoke.token,1,1,true,"",1030);acquire=acquired.back();assert(acquire.cleanup&&acquire.deadline==1180);phases.Acquired(acquire.request,1,1,true,"",1031);invoke=invoked.back();assert(invoke.kind==sr::HookKind::AfterEach&&invoke.observation.duration==261);++groups;
  const auto cleanup=phases.AmendTimeout(invoke.token,1,1,40,1032);assert(cleanup&&*cleanup==1070);phases.Settled(invoke.token,1,1,true,"",1033);const auto release=releases.back();phases.Released(release.request,1,1,true,"",1034);assert(results.size()==1&&results.back().ok&&results.back().observation.duration==264);++groups;
  const auto before=acquired.size();assert(!phases.Begin(2,2,calls,30,2000,{sr::PhaseStatus::Passed,30},30));assert(!phases.Begin(2,2,calls,30,2000,{sr::PhaseStatus::Passed,29},30));assert(!phases.Begin(2,2,calls,30,2000,{sr::PhaseStatus::Passed,0},-1));assert(acquired.size()==before&&!phases.Inspect().physical);++groups;
  // An original70ms remaining budget expires despite the callback arriving
  // before the next Tick; cleanup keeps its own actual100ms phase budget.
  const auto physical_calls=invoked.size();assert(phases.Begin(2,2,calls,100,2000,{sr::PhaseStatus::Passed,30},30));acquire=acquired.back();phases.Acquired(acquire.request,2,2,true,"",2070);assert(invoked.size()==physical_calls); // expired main acquisition admitted no user callback
  assert(acquired.back().cleanup&&acquired.back().deadline==2170);acquire=acquired.back();phases.Acquired(acquire.request,2,2,true,"",2071);invoke=invoked.back();assert(invoke.kind==sr::HookKind::AfterEach);phases.Settled(invoke.token,2,2,true,"",2072);const auto last=releases.back();phases.Released(last.request,2,2,true,"",2073);assert(results.back().timed_out&&!results.back().ok&&results.back().observation.duration==103);++groups;
  std::cout<<"PASS"<<groups<<" native preparation budget/original amendment origin/cleanup/observation groups\n";
}
