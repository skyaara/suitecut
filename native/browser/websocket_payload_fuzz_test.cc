// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <random>
#include "include/cef_api_hash.h"
#include "websocket_payloads.h"
int main(int argc,char**argv){
 assert(cef_api_hash(CEF_API_VERSION,0));const unsigned iterations=argc>1?std::stoul(argv[1]):10000;std::mt19937 random(0x57a9ed01);using Payloads=suitecut::NativeWebSocketPayloads;using Dict=Payloads::Dict;double now=1;Payloads payloads([&]{return now;});
 size_t actual_commits=0;for(unsigned iteration=0;iteration<iterations;++iteration){
  const int length=1+random()%1024;auto begin=CefDictionaryValue::Create();begin->SetString("action","routePayloadBegin");begin->SetString("operation","replace");begin->SetInt("opcode",2);begin->SetInt("bytes",length);begin->SetString("targetId","actual-target");begin->SetString("routingToken",std::to_string(iteration+1));begin->SetString("requestId","actual-channel");std::string token;Payloads::Reply actual;
  auto validate=[](Dict){return true;};auto commit=[&](Dict metadata,double deadline,auto reply){assert(deadline==now+100);auto decoded=CefBase64Decode(metadata->GetString("payloadBase64"));assert(decoded&&decoded->GetSize()==size_t(length));actual=std::move(reply);++actual_commits;};
  auto command=[&](Dict input,auto reply,int context=1){payloads.Command(context,1,input,now+100,validate,commit,std::move(reply));};
  command(begin,[&](bool ok,Dict result,const auto&){assert(ok);token=result->GetString("payloadToken");});assert(!token.empty()&&payloads.Pending());
  auto input=begin->Copy(false);input->SetString("payloadToken",token);input->SetString("action","routePayloadCommit");command(input,[](bool ok,Dict,const auto&){assert(!ok);});
  input->SetString("action","routePayloadAbort");command(input,[](bool ok,Dict,const auto&){assert(!ok);},2);assert(payloads.Pending());
  input->SetString("routingToken","stale");command(input,[](bool ok,Dict,const auto&){assert(!ok);});input->SetString("routingToken",begin->GetString("routingToken"));
  input->SetString("action","routePayloadAppend");input->SetString("base64","@@@");command(input,[](bool ok,Dict,const auto&){assert(!ok);});
  std::vector<uint8_t>bytes(length);for(auto&byte:bytes)byte=uint8_t(random());
  for(size_t offset=0;offset<bytes.size();){const auto size=std::min(bytes.size()-offset,size_t(1+random()%97));input->SetString("base64",CefBase64Encode(bytes.data()+offset,size));command(input,[](bool ok,Dict,const auto&){assert(ok);});offset+=size;}
  switch(random()%4){
   case 0:input->SetString("action","routePayloadCommit");command(input,[](bool ok,Dict,const auto&){assert(ok);});assert(actual&&!payloads.Pending());actual(true,nullptr,"");actual={};break;
   case 1:input->SetString("action","routePayloadAbort");command(input,[](bool ok,Dict,const auto&){assert(ok);});break;
   case 2:now+=100;payloads.Tick();break;
   case 3:payloads.Closed(1,1);break;
  }
  assert(!payloads.Pending());now+=1;
 }
 std::printf("%u native WebSocket payload schedules passed (seed 0x57a9ed01, %zu real commits)\n",iterations,actual_commits);
}
