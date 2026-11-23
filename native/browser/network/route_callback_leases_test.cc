// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "network/route_callback_leases.h"
int main() {
  suitecut::NativeRouteCallbackLeases leases;const auto scope=leases.Scope();assert(scope);
  const auto issued=leases.Issue(*scope,"physical-invocation");assert(issued);
  assert(!leases.Start(*issued,*scope+1,"physical-invocation"));assert(leases.Start(*issued,*scope,"physical-invocation"));assert(!leases.Start(*issued,*scope,"physical-invocation"));
  leases.Close(*scope);assert(leases.size()==1 && leases.running()==1);assert(!leases.Finish(*issued,*scope,"forged"));assert(leases.Finish(*issued,*scope,"physical-invocation"));
  // A closed scope's lease is released by its one Finish; a repeat is stale.
  assert(!leases.Finish(*issued,*scope,"physical-invocation"));assert(!leases.completed());
  const auto transit=leases.Issue(*scope,"not-started");assert(transit);leases.Close(*scope);assert(!leases.Start(*transit,*scope,"not-started"));assert(leases.completed()==1&&leases.Finish(*transit,*scope,"not-started"));assert(!leases.completed());
  const auto waiting=leases.Issue(*scope,"expired-queued");assert(waiting);leases.Revoke(*scope,"expired-queued");assert(!leases.Start(*waiting,*scope,"expired-queued"));assert(leases.Finish(*waiting,*scope,"expired-queued"));assert(leases.Finish(*waiting,*scope,"expired-queued"));
  const auto executing=leases.Issue(*scope,"expired-running");assert(executing && leases.Start(*executing,*scope,"expired-running"));leases.Revoke(*scope,"expired-running");assert(leases.running()==1);assert(leases.Finish(*executing,*scope,"expired-running"));
  const auto other=leases.Scope();assert(other);std::vector<uint64_t> pending;
  for(size_t i=0;i<1000;++i){const auto token=leases.Issue(*other,std::to_string(i)+std::string(200,'i'));assert(token && leases.Start(*token,*other,std::to_string(i)+std::string(200,'i')));pending.push_back(*token);}
  assert(leases.Available());leases.Close(*other);assert(leases.size()==1000 && leases.running()==1000);
  for(size_t i=0;i<pending.size();++i)assert(leases.Finish(pending[i],*other,std::to_string(i)+std::string(200,'i')));
  assert(!leases.size());
  for(size_t i=0;i<10000;++i){const auto next=leases.Scope();assert(next);const auto token=leases.Issue(*next,"turn");assert(token);leases.Close(*next);assert(leases.Finish(*token,*next,"turn"));}
  assert(!leases.size() && !leases.running());
  // Open-scope leases stay idempotent until Close; Close then releases every
  // finished one, so no scope's completions outlive it.
  assert(leases.completed()==2);leases.Close(*scope);assert(!leases.completed()&&!leases.Finish(*waiting,*scope,"expired-queued"));
  const auto unfinished=leases.Scope();assert(unfinished);const auto queued=leases.Issue(*unfinished,"queued");assert(queued);leases.Revoke(*unfinished,"queued");leases.Close(*unfinished);
  assert(leases.completed()==1&&leases.Finish(*queued,*unfinished,"queued")&&!leases.completed());leases.Clear();
}
