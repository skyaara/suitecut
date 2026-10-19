// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include "developer_debugger_scripts.h"
namespace {long fail_after=-1;size_t allocations=0;}
// Fail the actual string/map allocations, including the outgoing JSON build.
void*operator new(std::size_t size){
 if(fail_after>=0&&allocations++==static_cast<size_t>(fail_after))throw std::bad_alloc();
 if(void*memory=std::malloc(size?size:1))return memory;
 throw std::bad_alloc();
}
void operator delete(void*memory)noexcept{std::free(memory);}
void operator delete(void*memory,std::size_t)noexcept{std::free(memory);}
void*operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void*memory)noexcept{::operator delete(memory);}
void operator delete[](void*memory,std::size_t)noexcept{::operator delete(memory);}
using Scripts=suitecut::DeveloperDebuggerScripts;
std::string Next(const std::string&json){const auto key=json.find("\"next\":\"");assert(key!=std::string::npos);const auto start=key+8;return json.substr(start,json.find('"',start)-start);}
Scripts::Hooks Hooks(){
 Scripts::Hooks hooks;hooks.session=std::string(90,'s');hooks.now=[](){return 1.;};hooks.current=[](){return true;};
 hooks.page=[](const std::string&after,const uint64_t*,double,Scripts::Page&page,std::string&){page.revision=1;page.json="["+std::string(1600,' ')+"0]";page.count=1;page.more=true;page.last=std::string(64,after.empty()?'a':'b');return true;};return hooks;
}
int main(){
 size_t failures=0;bool completed=false;
 for(long fault=0;fault<128&&!completed;++fault){
  assert(Scripts::ProcessRetained()==0);Scripts scripts(Hooks());std::string json,error;
  assert(scripts.Read("",100,json,error));const auto cursor=Next(json);
  assert(scripts.Retained()==1&&Scripts::ProcessRetained()==1);
  fail_after=fault;allocations=0;
  try{const bool ok=scripts.Read(cursor,100,json,error);fail_after=-1;assert(ok);completed=true;}
  catch(const std::bad_alloc&){
   fail_after=-1;++failures;
   assert(json.empty()&&scripts.Retained()==1&&Scripts::ProcessRetained()==1);
   // Every failed allocation leaves the original position usable, with no
   // hidden successor or leaked global reservation. Retry is a real read.
   assert(scripts.Read(cursor,100,json,error));
  }
  assert(scripts.Retained()==2&&Scripts::ProcessRetained()==2);
  const auto previous=json;assert(scripts.Read(cursor,100,json,error)&&json==previous&&scripts.Retained()==2);
 }
 assert(completed&&failures>=8&&Scripts::ProcessRetained()==0);
 std::cout<<"developer script allocation failure sweep "<<failures<<" points passed\n";
}
