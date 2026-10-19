// SPDX-License-Identifier: MIT
#include "hook_worker.h"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace suitecut::runner;
int main(){
 double now=0;std::vector<std::string> sent;std::vector<HookPhases::Result> results;
 HookWorker worker({},HookWorker::Callbacks{[&]{return now;},[&](const auto& line){sent.push_back(line);},[&](const auto& result){results.push_back(result);},[](const auto&){assert(false);}});
 assert(!worker.Begin({1,1,7,0,100,FixtureScopeKind::Test,8},{{HookKind::Body,7,{}}},now));assert(sent.empty());
 for(const auto kind:{FixtureScopeKind::BeforeAll,FixtureScopeKind::AfterAll}){
  const uint64_t scope=kind==FixtureScopeKind::BeforeAll?1:2;const auto hook=kind==FixtureScopeKind::BeforeAll?HookKind::BeforeAll:HookKind::AfterAll;
  assert(worker.Begin({scope,3,0,2,100,kind,42},{{hook,11,{}}},now));assert(!sent.empty());
  std::istringstream in(sent.back());std::string command,actualKind,extra,status;double duration=-1;uint64_t token,owner,generation,callback,retry,test,timeout;
  assert(in>>command>>token>>owner>>generation>>actualKind>>callback>>retry>>test>>timeout>>status>>duration);assert(!(in>>extra));
  assert(status=="passed"&&duration==0);
  assert(command=="CALL"&&owner==scope&&generation==3&&callback==11&&retry==2&&test==42&&timeout==100);
  assert(actualKind==(kind==FixtureScopeKind::BeforeAll?"beforeAll":"afterAll"));assert(worker.Inspect().callbacks==1);
  worker.CallbackSettled(token,owner,generation,true,"",now);assert(results.size()==scope&&results.back().ok);assert(worker.Inspect().callbacks==0&&worker.Inspect().fixtures==0&&worker.Inspect().scopes==0);
 }
 std::cout<<"PASS native component associated-attempt routing fields and mismatched test metadata rejection\n";
}
