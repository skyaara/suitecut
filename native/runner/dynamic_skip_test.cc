// SPDX-License-Identifier: MIT
#include "hook_phases.h"
#include "fixture_graph.h"
#include "runtime_skip.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut::runner;
struct Proof {
  std::deque<HookPhases::Acquire> acquire;
  std::deque<HookPhases::Invoke> invoke;
  std::deque<HookPhases::Release> release;
  std::vector<HookPhases::Invoke> abort;
  std::vector<HookPhases::Result> results;
  std::vector<std::string> errors;
  HookPhases phases;
  Proof() : phases({[&](const auto& c) { acquire.push_back(c); },
      [&](const auto& c) { invoke.push_back(c); }, [&](const auto& c) { abort.push_back(c); },
      [&](const auto& c) { release.push_back(c); }, [&](const auto& r) { results.push_back(r); },
      [&](const auto& e) { errors.push_back(e); }}) {}
  HookPhases::Invoke Admit(double now) {
    assert(!acquire.empty()); const auto q=acquire.front();acquire.pop_front();
    phases.Acquired(q.request,q.scope,q.generation,true,"",now);
    assert(!invoke.empty()); const auto c=invoke.front();invoke.pop_front();return c;
  }
  void Settle(const HookPhases::Invoke& c,bool ok,double now,bool skipped=false) {
    phases.Settled(c.token,c.scope,c.generation,ok,ok?"":"actual callback failure",now,skipped);
  }
  void Release(double now) {
    assert(!release.empty());const auto c=release.front();release.pop_front();
    phases.Released(c.request,c.scope,c.generation,true,"",now);
  }
};
int main(){using K=HookKind;
  assert(RuntimeSkipReason("-")=="\"\"");
  assert(RuntimeSkipReason("610000d80000")=="\"\\u0061\\ud800\\u0000\"");
  std::string maximum;for(size_t n=0;n<8192;++n)maximum+="00d8";
  assert(JsonString(RuntimeSkipReason(maximum),8192));
  for(const auto& invalid:std::vector<std::string>{"", "000", "000x", maximum+"0000"}){
    bool rejected=false;try{(void)RuntimeSkipReason(invalid);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
  }

  { // Accepted real beforeEach skip suppresses body, cleanup observes actual skipped status.
    Proof p;assert(p.phases.Begin(1,2,{{K::BeforeEach,1,{}},{K::Body,2,{}},{K::AfterEach,3,{}}},100,0));
    auto before=p.Admit(1);
    assert(!p.phases.RequestSkip(before.token,1,3,"\"wrong generation\"",2));
    assert(!p.phases.RequestSkip(before.token,1,2,"not JSON",2));
    assert(p.phases.RequestSkip(before.token,1,2,"\"reason\\ud800\\u0000\"",2));
    assert(p.abort.size()==1&&p.abort[0].cancel_reason=="Native runtime skip requested");
    assert(!p.phases.RequestSkip(before.token,1,2,"\"duplicate\"",3));
    auto cleanup=p.Admit(3);assert(cleanup.kind==K::AfterEach&&cleanup.observation.status==PhaseStatus::Skipped);
    assert(!p.phases.RequestSkip(cleanup.token,1,2,"\"cleanup cannot skip\"",4));
    p.Settle(cleanup,true,4);p.Release(5);
    assert(p.results.empty()&&p.phases.Inspect().physical==1);
    assert(!p.phases.Begin(3,4,{{K::Body,1,{}}},100,6));
    p.Settle(before,true,6,true);
    assert(p.results.size()==1&&p.results[0].ok&&p.results[0].skipped);
    assert(p.results[0].observation.status==PhaseStatus::Skipped&&p.phases.Inspect().physical==0);
    assert(p.results[0].skip_reason_json=="\"reason\\ud800\\u0000\"");
  }
  { // A genuine exception after a caught skip remains an actual failure, even during drain.
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},100,0));auto body=p.Admit(1);
    assert(p.phases.RequestSkip(body.token,1,1,"\"skip\"",2));p.Release(3);
    p.Settle(body,false,4);assert(!p.results[0].ok&&p.results[0].skipped);
    assert(p.results[0].observation.status==PhaseStatus::Failed);
  }
  { // A fabricated skipped settlement cannot create native skip state.
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},100,0));auto body=p.Admit(1);
    p.Settle(body,true,2,true);p.Release(3);assert(!p.results[0].ok&&!p.results[0].skipped);
  }
  { // Request ingress observes the original deadline without a preceding Tick.
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},10,0));auto body=p.Admit(1);
    assert(!p.phases.RequestSkip(body.token,1,1,"\"late\"",10));
    p.Settle(body,true,11);p.Release(12);assert(p.results[0].timed_out&&!p.results[0].skipped);
  }
  { // Physical failure is not a clean skip; only actual WorkerClosed releases the raw lease.
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},10000,0));auto body=p.Admit(1);
    assert(p.phases.RequestSkip(body.token,1,1,"\"hang\"",2));p.Release(3);p.phases.Tick(1003);
    assert(p.errors.size()==1&&p.results.empty()&&p.phases.Inspect().physical==1);
    p.phases.WorkerClosed(1004);assert(!p.results[0].ok&&p.results[0].skipped&&p.phases.Inspect().physical==0);
  }
  { // Original cleanup deadline beats the independent physical drain bound.
    Proof p;assert(p.phases.Begin(1,1,{{K::Body,1,{}}},10,0));auto body=p.Admit(1);
    assert(p.phases.RequestSkip(body.token,1,1,"\"short\"",2));p.Release(3);p.phases.Tick(12);
    assert(p.phases.TimeoutAt()==12);p.phases.WorkerClosed(13);assert(p.results[0].timed_out);
  }
  { // afterAll remains explicitly unsupported in this runtime-skip slice.
    Proof p;assert(p.phases.Begin(1,1,{{K::AfterAll,1,{}}},100,0));auto all=p.Admit(1);
    assert(!p.phases.RequestSkip(all.token,1,1,"\"all\"",2));p.Settle(all,true,3);p.Release(4);assert(p.results[0].ok);
  }
  std::cout<<"PASS7 native runtime skip ownership/deadline groups\n";
}
