// SPDX-License-Identifier: MIT
#include "route_matcher_selections.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

int main(){
  using namespace suitecut;
  NativeRouteRegistry registry,other;
  NativeRouteCallbackLeases leases;const auto scope=*leases.Scope();
  std::vector<NativeRouteMatcherSelections::Ticket>tickets;
  NativeRouteMatcherSelections selections(registry,leases,scope,[&](const auto&ticket){tickets.push_back(ticket);});
  auto policy=NativeContextRouteRegistration::Create("host","**/*",1,true);assert(policy);
  assert(!NativeContextRouteRegistration::Create("bad","**/real",1,true));
  assert(registry.Register("glob","**/*",0));assert(registry.RegisterContext(policy));assert(other.RegisterContext(policy));
  // Page priority remains authoritative even when the context has a host matcher.
  bool done=false;std::string selected,error;
  auto complete=[&](auto invocation,auto,const std::string&reason,auto){done=true;selected=invocation?invocation->registration:"";error=reason;};
  assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,32,complete));
  assert(done&&selected=="glob"&&tickets.empty()&&policy->available());registry.Remove("glob");done=false;
  auto plan=registry.Match("https://real.test/a");assert(!registry.Allows(plan,"https://real.test/a"));assert(!registry.Next(plan));assert(plan.next==0);
  assert(selections.Select(std::move(plan),"https://real.test/a",100,0,32,complete));assert(!done&&tickets.size()==1);
  auto ticket=tickets.back();assert(!selections.Result(ticket,true,1)); // no real callback receipt
  assert(leases.Start(ticket.token,scope,ticket.id));assert(!selections.Result(ticket,true,1));
  assert(leases.Finish(ticket.token,scope,ticket.id));assert(selections.Result(ticket,false,1));
  assert(done&&selected.empty()&&error.empty()&&policy->available()); // false never spends global times
  NativeRouteMatcherSelections other_selections(other,leases,*leases.Scope(),[&](const auto&value){tickets.push_back(value);});
  done=false;assert(other_selections.Select(other.Match("https://real.test/b"),"https://real.test/b",100,0,0,complete));
  ticket=tickets.back();assert(leases.Start(ticket.token,ticket.scope,ticket.id));assert(leases.Finish(ticket.token,ticket.scope,ticket.id));
  assert(other_selections.Result(ticket,true,2));assert(done&&selected=="host"&&!policy->available());
  // Actual removal/id reuse during user code cannot select the replacement.
  registry.Clear();assert(registry.Register("reuse","**/*",1,false,true));done=false;
  assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));ticket=tickets.back();
  assert(leases.Start(ticket.token,scope,ticket.id));registry.Remove("reuse");assert(registry.Register("reuse","**/*",1,false,true));
  assert(leases.Finish(ticket.token,scope,ticket.id));assert(selections.Result(ticket,true,1));assert(done&&selected.empty());
  // A queued callback delivered after actual removal skips the unavailable
  // original policy; it must not abort the request as an invalid author result.
  registry.Clear();assert(registry.Register("remaining","**/*",0));assert(registry.Register("removed","**/*",0,false,true));done=false;
  assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));ticket=tickets.back();registry.Remove("removed");
  bool skipped=false;assert(leases.Finish(ticket.token,scope,ticket.id));assert(selections.Result(ticket,std::nullopt,1,&skipped));assert(skipped&&done&&selected=="remaining"&&error.empty());
  for(bool running:{false,true}){
    registry.Clear();assert(registry.Register("remaining","**/*",0));assert(registry.Register("removed","**/*",0,false,true));done=false;
    assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));ticket=tickets.back();
    if(running){assert(leases.Start(ticket.token,scope,ticket.id));}registry.Remove("removed");
    bool skipped_early=false;assert(selections.Result(ticket,std::nullopt,1,&skipped_early));assert(skipped_early&&done&&selected=="remaining");
    assert(leases.size()==size_t(running)); // Issued retires; genuinely Running never does.
    if(running)assert(leases.Finish(ticket.token,scope,ticket.id));
  }
  // Replaying an older false receipt cannot decide a subsequent candidate.
  registry.Clear();assert(registry.Register("older","**/*",1,false,true));assert(registry.Register("newer","**/*",1,false,true));done=false;
  assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));auto first=tickets.back();
  assert(leases.Start(first.token,scope,first.id));assert(leases.Finish(first.token,scope,first.id));assert(selections.Result(first,false,1));
  ticket=tickets.back();assert(ticket.id==first.id&&ticket.token!=first.token&&ticket.registration=="older");assert(!selections.Result(first,true,1));
  assert(leases.Start(ticket.token,scope,ticket.id));assert(leases.Finish(ticket.token,scope,ticket.id));assert(selections.Result(ticket,true,1));assert(done&&selected=="older");
  // Invalid async results retire logically but retain genuine running work.
  done=false;assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));ticket=tickets.back();assert(leases.Start(ticket.token,scope,ticket.id));
  assert(selections.Result(ticket,std::nullopt,1));assert(done&&!error.empty()&&leases.running()==1&&selections.pending()==0);
  assert(leases.Finish(ticket.token,scope,ticket.id));assert(leases.size()==0);
  // Timeout/owner close preserve physical callbacks; original deadline is fixed.
  done=false;assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",5,0,0,complete));ticket=tickets.back();assert(leases.Start(ticket.token,scope,ticket.id));selections.Tick(5);
  assert(done&&error.find("deadline")!=std::string::npos&&leases.running()==1);assert(!selections.Result(ticket,true,6));assert(leases.Finish(ticket.token,scope,ticket.id));
  done=false;assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));ticket=tickets.back();assert(leases.Start(ticket.token,scope,ticket.id));selections.Clear();assert(!done&&leases.running()==1);assert(leases.Finish(ticket.token,scope,ticket.id));
  // Per-owner publication does not borrow another channel, and actual owner
  // closure retires only its logical continuation, retaining real running work.
  done=false;bool owner_done=false;NativeRouteMatcherSelections::Failure owner_failure{};
  assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,
    [&](auto,auto,const auto&,auto failure){owner_done=true;owner_failure=failure;},
    [&](const auto&actual){tickets.push_back(actual);},"actual-owner"));
  ticket=tickets.back();assert(leases.Start(ticket.token,scope,ticket.id));selections.CancelOwner("different-owner");assert(!owner_done);
  selections.CancelOwner("actual-owner");assert(owner_done&&owner_failure==NativeRouteMatcherSelections::Failure::OwnerClosed&&leases.running()==1);
  assert(!selections.Result(ticket,true,1));assert(leases.Finish(ticket.token,scope,ticket.id));
  assert(!selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,SIZE_MAX,complete));
  // Global admission bounds logical snapshots, separately from physical leases.
  for(size_t i=0;i<128;++i)assert(selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));
  assert(!selections.Select(registry.Match("https://real.test/a"),"https://real.test/a",100,0,0,complete));assert(leases.size()==128);selections.Clear();assert(leases.size()==0);
  assert(!NativeRouteMatcherSelections::retained_count()&&!NativeRouteMatcherSelections::retained_bytes());
  // Completion may synchronously close the owner without iterating freed state.
  auto reentrant=std::make_unique<NativeRouteMatcherSelections>(registry,leases,scope,[&](const auto&ticket){tickets.push_back(ticket);});
  assert(reentrant->Select(registry.Match("https://real.test/a"),"https://real.test/a",1,0,0,[&](auto,auto,const auto&,auto){reentrant.reset();}));reentrant->Tick(1);assert(!reentrant);
  std::cout<<"Native route matcher selection: 13 ownership/order groups passed\n";
}
