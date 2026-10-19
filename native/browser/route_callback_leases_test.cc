// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "route_callback_leases.h"
int main() {
  suitecut::NativeRouteCallbackLeases leases;const auto scope=leases.Scope();assert(scope);
  const auto issued=leases.Issue(*scope,"physical-invocation");assert(issued);
  assert(!leases.Start(*issued,*scope+1,"physical-invocation"));assert(leases.Start(*issued,*scope,"physical-invocation"));assert(!leases.Start(*issued,*scope,"physical-invocation"));
  leases.Close(*scope);assert(leases.size()==1 && leases.running()==1);assert(!leases.Finish(*issued,*scope,"forged"));assert(leases.Finish(*issued,*scope,"physical-invocation"));assert(leases.Finish(*issued,*scope,"physical-invocation"));
  const auto transit=leases.Issue(*scope,"not-started");assert(transit);leases.Close(*scope);assert(!leases.Start(*transit,*scope,"not-started"));assert(leases.Finish(*transit,*scope,"not-started"));
  const auto waiting=leases.Issue(*scope,"expired-queued");assert(waiting);leases.Revoke(*scope,"expired-queued");assert(!leases.Start(*waiting,*scope,"expired-queued"));assert(leases.Finish(*waiting,*scope,"expired-queued"));
  const auto executing=leases.Issue(*scope,"expired-running");assert(executing && leases.Start(*executing,*scope,"expired-running"));leases.Revoke(*scope,"expired-running");assert(leases.running()==1);assert(leases.Finish(*executing,*scope,"expired-running"));
  const auto other=leases.Scope();assert(other);std::vector<uint64_t> pending;
  for(size_t i=0;i<leases.kCapacity;++i){const auto token=leases.Issue(*other,std::to_string(i));assert(token && leases.Start(*token,*other,std::to_string(i)));pending.push_back(*token);}
  assert(!leases.Available() && !leases.Issue(*other,"overflow"));leases.Close(*other);assert(leases.size()==leases.kCapacity && leases.running()==leases.kCapacity);
  for(size_t i=0;i<pending.size();++i)assert(leases.Finish(pending[i],*other,std::to_string(i)));
  assert(!leases.size());
  for(size_t i=0;i<10000;++i){const auto next=leases.Scope();assert(next);const auto token=leases.Issue(*next,"turn");assert(token);leases.Close(*next);assert(leases.Finish(*token,*next,"turn"));}
  assert(!leases.size() && !leases.running());leases.Clear();
}
