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
void AllocationBounds(){
 Tracking tracking;auto budget=std::make_shared<APIOriginsBudget>(1800,&tracking);std::string error;
 {APIOriginsSnapshot first(budget),second(budget);assert(first.AddValidated(origin,raw,clock_now,error));const size_t before=budget->Bytes();assert(before==tracking.current);const std::string big=R"({"origin":"https://second.test","localStorage":[],"extra":")"+std::string(2000,'x')+"\"}";assert(!second.AddValidated("https://second.test",big,clock_now,error));assert(second.Size()==0&&budget->Bytes()==before&&tracking.current==before);std::pmr::string output(first.Resource());assert(first.Project(0,true,true,clock_now,output,error));assert(budget->Bytes()==tracking.current);assert(budget->Peak()==tracking.peak);}
 assert(budget->Bytes()==0&&tracking.current==0&&tracking.allocations.empty());
}
void SharedRetainedBudget(){
 Tracking tracking;auto budget=std::make_shared<APIOriginsBudget>(1100,&tracking);std::string error;
 auto first=std::make_unique<APIOriginsSnapshot>(budget),second=std::make_unique<APIOriginsSnapshot>(budget);APIOriginsSnapshot third(budget);
 assert(first->AddValidated(origin,raw,clock_now,error));assert(second->AddValidated(origin,raw,clock_now,error));const size_t retained=budget->Bytes();assert(retained==tracking.current);
 assert(!third.AddValidated(origin,raw,clock_now,error));assert(third.Size()==0&&budget->Bytes()==retained);
 std::pmr::string output(first->Resource());output.assign(150,'k');const auto publication_before=budget->Bytes();assert(!first->Project(0,true,true,clock_now,output,error));assert(std::string_view(output)==std::string(150,'k')&&budget->Bytes()==publication_before);
 first.reset();assert(third.AddValidated(origin,raw,clock_now,error));assert(third.Size()==1);assert(budget->Bytes()==tracking.current);second.reset();
}
void OriginalDeadline(){
 auto budget=std::make_shared<APIOriginsBudget>();APIOriginsSnapshot state(budget);std::string error;
 for(const APIOriginsSnapshot::Clock&clock:std::vector<APIOriginsSnapshot::Clock>{{[]{return 20.0;},20},{[]{return -1.0;},20},{[]{return std::numeric_limits<double>::quiet_NaN();},20},{[]{return 1.0;},std::numeric_limits<double>::infinity()},{}, {[]{return 1.0;},-1}}){assert(!state.AddValidated(origin,raw,clock,error));assert(state.Size()==0&&budget->Bytes()==0);}
 size_t calls=0;APIOriginsSnapshot::Clock expires{[&]{return ++calls==1?10.0:20.0;},20};assert(!state.AddValidated(origin,raw,expires,error));assert(state.Size()==0&&budget->Bytes()==0);
 assert(state.AddValidated(origin,raw,clock_now,error));std::pmr::string output(state.Resource());calls=0;assert(!state.Project(0,true,true,expires,output,error));assert(output.empty());assert(state.Size()==1);
}
void CountAndSize(){
 auto budget=std::make_shared<APIOriginsBudget>();APIOriginsSnapshot state(budget);std::string error;
 for(int i=0;i<1024;++i){const std::string name="https://"+std::to_string(i)+".example";const std::string value="{\"origin\":\""+name+"\",\"localStorage\":[]}";assert(state.AddValidated(name,value,clock_now,error));}
 assert(state.Size()==1024);assert(!state.AddValidated("https://extra.example",raw,clock_now,error));assert(state.Size()==1024);
 APIOriginsSnapshot extra(budget);assert(!extra.AddValidated(origin,std::string(524289,'x'),clock_now,error));assert(extra.Size()==0);
 for(int i=0;i<16;++i){const std::string name="https://large-"+std::to_string(i)+".example";const std::string value="{\"origin\":\""+name+"\",\"localStorage\":[{\"name\":\"key\",\"value\":\""+std::string(524000,'x')+"\"}]}";assert(extra.AddValidated(name,value,clock_now,error));}
 const size_t retained=budget->Bytes();const std::string overflow="{\"origin\":\"https://overflow.example\",\"localStorage\":[{\"name\":\"key\",\"value\":\""+std::string(524000,'x')+"\"}]}";assert(!extra.AddValidated("https://overflow.example",overflow,clock_now,error));assert(extra.Size()==16&&budget->Bytes()==retained);
}
int main(){Projection();LexicalBoundaries();AllocationBounds();SharedRetainedBudget();OriginalDeadline();CountAndSize();std::cout<<"Native API constructor origin snapshot: 6 groups passed\n";}
