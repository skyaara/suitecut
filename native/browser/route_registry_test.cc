// SPDX-License-Identifier: MIT
#include "route_registry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int main(int argc,char** argv) {
  using suitecut::NativeRouteRegistry;
  NativeRouteRegistry registry;
  assert(registry.Register("old","**/*",0));
  assert(registry.Register("new","**/original",1));
  auto first=registry.Match("https://native.test/original");
  auto parallel=registry.Match("https://native.test/original");
  const auto newest=registry.Next(first);assert(newest && newest->registration=="new" && newest->exhausted);
  const auto older_parallel=registry.Next(parallel);assert(older_parallel && older_parallel->registration=="old");
  assert(registry.Register("late","**/*",0));
  const auto older=registry.Next(first);assert(older && older->registration=="old");
  assert(!registry.Next(first));
  auto removed=registry.Match("https://native.test/original");registry.Remove("late");
  assert(registry.Next(removed)->registration=="old");assert(!registry.Next(removed));
  registry.Clear();
  for(int i=0;i<256;++i)assert(registry.Register(std::to_string(i),"**/*",0));
  assert(!registry.Register("overflow","**/*",0));assert(!registry.Register("0","**/*",0));
  registry.Clear();
  assert(registry.Register("page-old","**/*",0));assert(registry.Register("context-old","**/*",0,true));assert(registry.Register("page-new","**/*",0));assert(registry.Register("context-new","**/*",0,true));
  auto scoped=registry.Match("https://native.test/original");for(const auto*id:{"page-new","page-old","context-new","context-old"})assert(registry.Next(scoped)->registration==id);
  for(const auto&id:registry.PageIDs())registry.Remove(id);
  auto remaining=registry.Match("https://native.test/original");assert(registry.Next(remaining)->registration=="context-new");assert(registry.Next(remaining)->registration=="context-old");assert(!registry.Next(remaining));registry.Clear();
  // Reusing a registration ID cannot redirect a previously admitted plan to a
  // new registration, even after removal or Clear. IDs alone are not leases.
  assert(registry.Register("reused","**/*",0));auto stale=registry.Match("https://native.test/original");
  registry.Remove("reused");assert(registry.Register("reused","**/*",0));assert(!registry.HasNext(stale));assert(!registry.Next(stale));
  registry.Clear();assert(registry.Register("reused","**/*",0));assert(!registry.Next(stale));registry.Clear();
  {
    NativeRouteRegistry page,broker,future;
    auto policy=suitecut::NativeContextRouteRegistration::Create("shared","**/*",2);assert(policy);
    assert(page.RegisterContext(policy)&&broker.RegisterContext(policy));assert(page.Register("page","**/*",1));
    auto page_plan=page.Match("https://native.test/original"),broker_plan=broker.Match("https://native.test/original");
    assert(page.Next(page_plan)->registration=="page");auto one=broker.Next(broker_plan);assert(one&&one->registration=="shared"&&!one->exhausted);
    assert(future.RegisterContext(policy));auto future_plan=future.Match("https://native.test/original");auto two=future.Next(future_plan);assert(two&&two->exhausted);
    assert(!page.Next(page_plan)&&!broker.HasNext(broker_plan)&&!policy->available());assert(!future.RegisterContext(policy));
    auto removed=suitecut::NativeContextRouteRegistration::Create("removed","**/*",0);assert(page.RegisterContext(removed));auto pending=page.Match("https://native.test/original");removed->Remove();assert(!page.Next(pending));
    assert(page.empty()&&broker.empty()&&future.empty());
  }
  assert(suitecut::NativeContextRouteRegistration::retained_count()==0);
  const uint64_t rounds=argc>1?std::strtoull(argv[1],nullptr,10):100000;
  std::mt19937 random(0x534354);
  // Independent model: fixed 16 slots, reverse insertion order, invocation
  // counts and explicit removal; no production registry structures are reused.
  for(uint64_t round=0;round<rounds;++round) {
    struct Entry {std::string id;bool matches;int uses;bool live;};
    std::vector<Entry> entries;
    const unsigned count=random()%16+1;
    for(unsigned slot=0;slot<count;++slot) {
      Entry entry{std::to_string(round)+":"+std::to_string(slot),(random()%3)!=0,static_cast<int>(random()%4),true};
      assert(registry.Register(entry.id,entry.matches?"**/original":"**/other",entry.uses));entries.push_back(std::move(entry));
    }
    auto plan=registry.Match("https://native.test/original");
    for(auto entry=entries.rbegin();entry!=entries.rend();++entry) {
      if((random()%4)==0){registry.Remove(entry->id);entry->live=false;}
      if(!entry->matches || !entry->live)continue;
      auto invocation=registry.Next(plan);assert(invocation && invocation->registration==entry->id);
      const bool exhausted=entry->uses==1;assert(invocation->exhausted==exhausted);
      if(entry->uses>0){--entry->uses;if(exhausted)entry->live=false;}
    }
    assert(!registry.Next(plan));
    size_t live=0;for(const auto& entry:entries)if(entry.live)++live;assert(registry.size()==live);
    registry.Clear();assert(registry.empty());
  }
  std::cout<<"Native route registry: "<<rounds<<" seeded lifecycle rounds passed\n";
}
