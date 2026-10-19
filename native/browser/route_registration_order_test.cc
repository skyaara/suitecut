// SPDX-License-Identifier: MIT
#include "route_registry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using Order=suitecut::NativeRouteRegistrationOrder;
using Registry=suitecut::NativeRouteRegistry;
using ContextPolicy=suitecut::NativeContextRouteRegistration;
Order Reserve(){auto order=Order::Reserve();assert(order);return *order;}
void Expect(Registry&registry,const std::vector<std::string>&ids){
  auto plan=registry.Match("https://native.test/original");
  for(const auto&id:ids){auto selected=registry.Next(plan);assert(selected&&selected->registration==id);}
  assert(!registry.Next(plan));
}
int main(){
  assert(!Order{});const auto first=Reserve(),second=Reserve();assert(first<second&&first!=second);
  assert(Order::Successor(0)==1&&!Order::Successor(UINT64_MAX));
  {
    auto older=ContextPolicy::Create("normal-old","**/*",0);assert(older);const auto har_new=Reserve();
    Registry current,future;
    assert(current.RegisterContext(older));assert(current.Register("har-new","**/*",0,true,false,har_new));
    // Actual future-target bootstrap binds all HAR before ordinary policy.
    assert(future.Register("har-new","**/*",0,true,false,har_new));assert(future.RegisterContext(older));
    Expect(current,{"har-new","normal-old"});Expect(future,{"har-new","normal-old"});
  }
  {
    const auto har_old=Reserve();auto newer=ContextPolicy::Create("normal-new","**/*",0);assert(newer);
    Registry current,reverse_install;
    assert(current.Register("har-old","**/*",0,true,false,har_old));assert(current.RegisterContext(newer));
    assert(reverse_install.RegisterContext(newer));assert(reverse_install.Register("har-old","**/*",0,true,false,har_old));
    Expect(current,{"normal-new","har-old"});Expect(reverse_install,{"normal-new","har-old"});
  }
  {
    Registry registry;const auto page_old=Reserve();auto context_new=ContextPolicy::Create("context-new","**/*",0);assert(context_new);
    assert(registry.RegisterContext(context_new));assert(registry.Register("page-old","**/*",0,false,false,page_old));
    Expect(registry,{"page-old","context-new"});
  }
  {
    Registry registry;const auto before_file_read=Reserve();assert(registry.Register("later-callback","**/*",0));
    auto already_admitted=registry.Match("https://native.test/original");
    // Real file completion may bind later; it cannot replace policy age or
    // enter a snapshot admitted while that archive was not yet available.
    assert(registry.Register("earlier-archive","**/*",0,false,false,before_file_read));
    Expect(registry,{"later-callback","earlier-archive"});
    assert(registry.Next(already_admitted)->registration=="later-callback"&&!registry.Next(already_admitted));
  }
  {
    Registry registry;const auto age=Reserve();assert(registry.Register("reused","**/*",0,false,false,age));
    auto old=registry.Match("https://native.test/original");registry.Remove("reused");
    assert(registry.Register("reused","**/*",0,false,false,age));assert(!registry.Next(old));
    assert(!registry.Register("different-id-same-age","**/*",0,false,false,age));Expect(registry,{"reused"});
  }
  {
    const auto archive_age=Reserve();auto host=ContextPolicy::Create("new-host","**/*",1,true);assert(host);
    Registry one,two;assert(one.RegisterContext(host));assert(one.Register("old-har","**/*",0,true,false,archive_age));
    assert(two.Register("old-har","**/*",0,true,false,archive_age));assert(two.RegisterContext(host));
    auto plan=one.Match("https://native.test/original");const auto candidate=one.Peek(plan);assert(candidate&&candidate->host_matcher&&!one.Next(plan));
    assert(!one.Decide(plan,*candidate,false));assert(host->available());assert(one.Next(plan)->registration=="old-har");
    auto parallel=two.Match("https://native.test/original");const auto matched=two.Peek(parallel);assert(matched&&two.Decide(parallel,*matched,true)->exhausted);
    assert(!host->available()&&two.Next(parallel)->registration=="old-har");
  }
  assert(ContextPolicy::retained_count()==0);
  std::cout<<"Native route registration chronology: 7 policy/snapshot/ownership groups passed\n";
}
