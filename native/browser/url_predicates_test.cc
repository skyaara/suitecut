// SPDX-License-Identifier: MIT
#include "url_predicates.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <random>

int main() {
  SuiteCutURLPredicateRegistry registry;
  int calls=0;
  assert(!registry.Pause(0,1,"client",[](bool){}));
  assert(!registry.Pause(1,0,"client",[](bool){}));
  assert(!registry.Pause(1,1,"",[](bool){}));
  const auto first=registry.Pause(1,1,"client",[&](bool value){assert(value);++calls;});
  assert(first && !registry.Pause(1,1,"client",[](bool){}));
  assert(!registry.Take(1,2,*first,"client"));
  assert(!registry.Take(1,1,*first,"other"));
  auto continuation=registry.Take(1,1,*first,"client");
  assert(continuation && registry.Size()==0); (*continuation)(true); assert(calls==1);
  assert(!registry.Take(1,1,*first,"client"));
  for(int owner=1;owner<=64;++owner) assert(registry.Pause(owner,2,"client",[](bool){}));
  assert(!registry.Pause(65,2,"client",[](bool){}));
  registry.Cancel(32); assert(registry.Pause(65,2,"client",[](bool){}));
  registry.Clear(); assert(registry.Size()==0);
  auto resource=std::make_shared<int>(7); std::weak_ptr<int> weak=resource;
  assert(registry.Pause(1,3,"resource",[resource](bool){assert(*resource==7);}));
  resource.reset(); assert(!weak.expired()); registry.Cancel(1); assert(weak.expired());
  std::mt19937_64 random(0x5a17);
  for(std::uint64_t i=0;i<1000000;++i) {
    const int owner=static_cast<int>(random()%64)+1;
    const auto generation=i+4;
    bool observed=false;
    const auto token=registry.Pause(owner,generation,"fuzz",[&observed](bool matched){observed=matched;});
    assert(token);
    assert(!registry.Take(owner,generation-1,*token,"fuzz"));
    assert(!registry.Take(owner,generation,*token+1,"fuzz"));
    if(random()%3==0) registry.Cancel(owner);
    else { auto callback=registry.Take(owner,generation,*token,"fuzz");assert(callback);(*callback)(true);assert(observed); }
    assert(!registry.Take(owner,generation,*token,"fuzz"));
    assert(registry.Size()==0);
  }
}
