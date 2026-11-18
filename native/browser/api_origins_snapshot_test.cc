// SPDX-License-Identifier: MIT
#include "api_origins_snapshot.h"
#include <cassert>
#include <iostream>
#include <map>
#include <limits>
using suitecut::APIOriginsSnapshot;
using suitecut::APIOriginsBudget;
const APIOriginsSnapshot::Clock clock_now{[]{return 10.0;},20};
const std::string origin="https://example.test";
const std::string raw=R"({"origin":"https://example.test","localStorage":[{"name":"\ud800","value":"\udfff"}],"opfs":[{"path":"hello.txt","type":"file","base64":"SA=="},{"path":"empty","type":"directory"}],"indexedDB":[{"name":"db","records":[{"valueEncoded":{"id":1,"o":[{"k":"a","v":{"ref":1}}]}}]}]})";
struct Tracking final:std::pmr::memory_resource{
 size_t current=0,peak=0;std::map<void*,std::pair<size_t,size_t>> allocations;
 void*do_allocate(size_t bytes,size_t align)override{void*p=std::pmr::new_delete_resource()->allocate(bytes,align);assert(allocations.emplace(p,std::pair(bytes,align)).second);current+=bytes;peak=std::max(peak,current);return p;}
 void do_deallocate(void*p,size_t bytes,size_t align)override{assert(allocations.at(p)==std::pair(bytes,align));allocations.erase(p);current-=bytes;std::pmr::new_delete_resource()->deallocate(p,bytes,align);}
 bool do_is_equal(const std::pmr::memory_resource&other)const noexcept override{return this==&other;}
};
void Projection(){
 auto budget=std::make_shared<APIOriginsBudget>();APIOriginsSnapshot state(budget);std::string error;assert(state.AddValidated(origin,raw,clock_now,error));
 std::pmr::string output(state.Resource());assert(state.Project(0,true,true,clock_now,output,error));assert(std::string_view(output)==raw);assert(output.find("\\ud800")!=std::string::npos);assert(output.find("hello.txt")<output.find("empty"));
 assert(state.Project(0,false,false,clock_now,output,error));assert(output==R"({"origin":"https://example.test","localStorage":[{"name":"\ud800","value":"\udfff"}]})");
 assert(state.Project(0,true,false,clock_now,output,error));assert(output.find("indexedDB")!=std::string::npos&&output.find("opfs")==std::string::npos);
 assert(state.Project(0,false,true,clock_now,output,error));assert(output.find("indexedDB")==std::string::npos&&output.find("opfs")!=std::string::npos);
 assert(!state.AddValidated(origin,raw,clock_now,error));assert(state.Size()==1);
}
void LexicalBoundaries(){
 auto budget=std::make_shared<APIOriginsBudget>();APIOriginsSnapshot state(budget);std::string error;
 const std::string text=R"({"origin":"https://example.test","localStorage":[{"name":"s","value":"}\\\"[]{,"}],"op\u0066s":[{"path":"x","type":"directory"}],"ignored":{"opfs":"do not treat nested as top-level"}})";
 assert(state.AddValidated(origin,text,clock_now,error));std::pmr::string output(state.Resource());assert(state.Project(0,false,true,clock_now,output,error));assert(output.find("op\\u0066s")!=std::string::npos);assert(output.find("ignored")==std::string::npos);assert(state.Project(0,false,false,clock_now,output,error));assert(output.find("op\\u0066s")==std::string::npos);
 for(const std::string invalid:{"{}",R"({"origin":"x","localStorage":[],"opfs":[],"op\u0066s":[]})",R"({"origin":"x","localStorage":[})",R"({"origin":"x","localStorage":[]},0)",R"({"origin":"x","localStorage":[],})",R"({"origin":"x","localStorage":[],"indexedDB":01})",R"({"origin":"x","localStorage":[],"indexedDB":1e})",R"({"origin":"x","localStorage":[],"indexedDB":"\x20"})"}){APIOriginsSnapshot candidate(budget);assert(!candidate.AddValidated(origin,invalid,clock_now,error));assert(candidate.Size()==0);}
}
void AllocationAccounting(){
 Tracking tracking;auto budget=std::make_shared<APIOriginsBudget>(&tracking);std::string error;
 {APIOriginsSnapshot first(budget),second(budget);assert(first.AddValidated(origin,raw,clock_now,error));assert(budget->Bytes()==tracking.current);const std::string big=R"({"origin":"https://second.test","localStorage":[],"extra":")"+std::string(2000,'x')+"\"}";assert(second.AddValidated("https://second.test",big,clock_now,error));assert(second.Size()==1&&budget->Bytes()==tracking.current);std::pmr::string output(first.Resource());assert(first.Project(0,true,true,clock_now,output,error));assert(budget->Bytes()==tracking.current);assert(budget->Peak()==tracking.peak);}
 assert(budget->Bytes()==0&&tracking.current==0&&tracking.allocations.empty());
}
void SharedAccounting(){
 std::string error;Tracking tracking;auto budget=std::make_shared<APIOriginsBudget>(&tracking);
 auto first=std::make_unique<APIOriginsSnapshot>(budget),second=std::make_unique<APIOriginsSnapshot>(budget);APIOriginsSnapshot third(budget);
 assert(first->AddValidated(origin,raw,clock_now,error));assert(second->AddValidated(origin,raw,clock_now,error));assert(third.AddValidated(origin,raw,clock_now,error));assert(budget->Bytes()==tracking.current);
 std::pmr::string output(first->Resource());assert(first->Project(0,true,true,clock_now,output,error));assert(std::string_view(output)==raw);
 first.reset();second.reset();assert(third.Size()==1);assert(budget->Bytes()==tracking.current);
}
void OriginalDeadline(){
 auto budget=std::make_shared<APIOriginsBudget>();APIOriginsSnapshot state(budget);std::string error;
 for(const APIOriginsSnapshot::Clock&clock:std::vector<APIOriginsSnapshot::Clock>{{[]{return 20.0;},20},{[]{return -1.0;},20},{[]{return std::numeric_limits<double>::quiet_NaN();},20},{[]{return 1.0;},std::numeric_limits<double>::infinity()},{}, {[]{return 1.0;},-1}}){assert(!state.AddValidated(origin,raw,clock,error));assert(state.Size()==0&&budget->Bytes()==0);}
 size_t calls=0;APIOriginsSnapshot::Clock expires{[&]{return ++calls==1?10.0:20.0;},20};assert(!state.AddValidated(origin,raw,expires,error));assert(state.Size()==0&&budget->Bytes()==0);
 assert(state.AddValidated(origin,raw,clock_now,error));std::pmr::string output(state.Resource());calls=0;assert(!state.Project(0,true,true,expires,output,error));assert(output.empty());assert(state.Size()==1);
}
void NoCountOrSizeCap(){
 // Former 1024-origin, 8192-byte origin, 512 KiB per-origin and 8 MiB total
 // caps are gone: every value passes through whole.
 auto budget=std::make_shared<APIOriginsBudget>();APIOriginsSnapshot state(budget);std::string error;
 for(int i=0;i<1100;++i){const std::string name="https://"+std::to_string(i)+".example";const std::string value="{\"origin\":\""+name+"\",\"localStorage\":[]}";assert(state.AddValidated(name,value,clock_now,error));}
 assert(state.Size()==1100);
 const std::string long_origin="https://"+std::string(9000,'a')+".example";assert(state.AddValidated(long_origin,"{\"origin\":\""+long_origin+"\",\"localStorage\":[]}",clock_now,error));
 APIOriginsSnapshot extra(budget);assert(!extra.AddValidated("",raw,clock_now,error));
 for(int i=0;i<20;++i){const std::string name="https://large-"+std::to_string(i)+".example";const std::string value="{\"origin\":\""+name+"\",\"localStorage\":[{\"name\":\"key\",\"value\":\""+std::string(600000,'x')+"\"}]}";assert(extra.AddValidated(name,value,clock_now,error));}
 assert(extra.Size()==20);std::pmr::string output(extra.Resource());assert(extra.Project(19,false,false,clock_now,output,error));assert(output.size()>600000);
 std::string members="{\"origin\":\"x\",\"localStorage\":[]";for(int i=0;i<300;++i)members+=",\""+std::string(200,'k')+std::to_string(i)+"\":"+std::to_string(i);members+="}";assert(extra.AddValidated("https://members.example",members,clock_now,error));
}
int main(){Projection();LexicalBoundaries();AllocationAccounting();SharedAccounting();OriginalDeadline();NoCountOrSizeCap();std::cout<<"Native API constructor origin snapshot: 6 groups passed\n";}
