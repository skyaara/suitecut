// SPDX-License-Identifier: MIT
#include "http_lookup_controls.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
int main(){
  using Lookup=suitecut::HTTPLookupControls;std::vector<Lookup::Event>events;Lookup controls([&](const auto&event){events.push_back(event);});size_t completed=0;
  assert(controls.Begin(1,7,"original","one.example",443,100,[&](auto addresses,const auto&error){assert(error.empty());assert(addresses.size()==2);++completed;}));
  for(const double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),-1.0}){
    const auto before=events.size();assert(!controls.Begin(9,7,"invalid-clock","actual.example",80,invalid,[](auto,const auto&){assert(false);}));assert(controls.Size()==1&&events.size()==before);
    assert(!controls.Resolve(1,7,events.front().generation,invalid,{{"127.0.0.1",4}},""));assert(completed==0&&controls.Size()==1&&events.size()==before);
  }
  const auto original=events.back();assert(original.hostname=="one.example"&&original.port==443&&controls.Size()==1);
  assert(!controls.Resolve(1,8,original.generation,50,{{"127.0.0.1",4}},""));assert(!controls.Resolve(1,7,original.generation+1,50,{{"127.0.0.1",4}},""));assert(completed==0);
  assert(controls.Resolve(1,7,original.generation,50,{{"127.0.0.1",4},{"::1",6}},""));assert(completed==1&&controls.Size()==0);
  assert(!controls.Resolve(1,7,original.generation,51,{{"127.0.0.1",4}},""));
  assert(controls.Begin(1,7,"redirect","two.example",80,200,[&](auto addresses,const auto&error){assert(addresses.empty());assert(error.find("deadline")!=std::string::npos);++completed;}));
  const auto redirect=events.back();assert(redirect.generation!=original.generation);assert(!controls.Resolve(1,7,redirect.generation,200,{{"127.0.0.1",4}},""));assert(completed==2&&events.back().canceled&&controls.Size()==0);
  assert(controls.Begin(2,7,"invalid","actual.example",80,300,[&](auto addresses,const auto&error){assert(addresses.empty());assert(error.find("invalid IP")!=std::string::npos);++completed;}));assert(controls.Resolve(2,7,events.back().generation,250,{{"127.0.0.1:80",4}},""));assert(completed==3);
  assert(controls.Begin(4,9,"infinite","actual.example",80,0,[&](auto addresses,const auto&error){assert(error.empty()&&addresses.size()==1);++completed;}));assert(controls.Resolve(4,9,events.back().generation,1000000000,{{"127.0.0.1",4}},""));
  for(int cycle=0;cycle<100000;++cycle){assert(controls.Begin(3,9,"cancel","actual.example",80,300,[&](auto addresses,const auto&error){assert(addresses.empty());assert(error=="original canceled");++completed;}));const auto generation=events.back().generation;controls.Cancel(3,"original canceled");assert(!controls.Resolve(3,9,generation,1,{{"127.0.0.1",4}},""));events.clear();}
  for(int owner=1;owner<=64;++owner)assert(controls.Begin(owner,9,"capacity","actual.example",80,300,[](auto,const auto&error){assert(error=="closed");}));
  assert(!controls.Begin(65,9,"capacity","actual.example",80,300,[](auto,const auto&){}));controls.Clear("closed");assert(controls.Size()==0);
  assert(!Lookup::Validate({{"127.0.0.1",6}}));assert(!Lookup::Validate({{"::1",4}}));assert(!Lookup::Validate({{"domain.example",4}}));assert(!Lookup::Validate({}));
  std::cout<<"Native HTTP lookup ownership: exact identity, original deadline, malformed addresses,64 capacity and100000 canceled late answers passed\n";
}
