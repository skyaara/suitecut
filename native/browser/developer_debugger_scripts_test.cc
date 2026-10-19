// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "developer_debugger_scripts.h"
using Scripts=suitecut::DeveloperDebuggerScripts;
struct Harness{
 double now=1;bool current=true;uint64_t revision=1;int calls=0;
 size_t records=130;bool malformed=false,expire=false,change=false;std::unique_ptr<Scripts>scripts;
 explicit Harness(std::string session="native-session"){
  Scripts::Hooks hooks;hooks.session=std::move(session);hooks.now=[this]{return now;};hooks.current=[this]{return current;};
  hooks.page=[this](const std::string&after,const uint64_t*expected,double,Scripts::Page&page,std::string&error){
   ++calls;if(expected&&*expected!=revision){error="inventory changed; restart enumeration";return false;}
   page.revision=revision;const auto start=after.empty()?0:size_t(std::stoul(after));
   const auto end=std::min(records,start+64);page.count=end-start;page.more=end<records;page.json="[";
   for(size_t i=start;i<end;++i){if(i>start)page.json+=',';page.json+='0';}
   page.json+=']';if(page.count)page.last=std::to_string(end+1000000);
   // IDs are lexical in production. The fixture stores its numeric position
   // with a fixed-width leading range and translates it on the next receipt.
   if(expire)now=100;
   if(change)current=false;
   if(malformed)page.count=65;
   return true;
  };
  // Replace numeric fixture adapter with lexically ordered real positions.
  const auto actual=hooks.page;
  hooks.page=[actual](const std::string&after,const uint64_t*revision,double deadline,Scripts::Page&page,std::string&error){return actual(after.empty()?"":std::to_string(std::stoul(after)-1000000),revision,deadline,page,error);};
  scripts=std::make_unique<Scripts>(std::move(hooks));
 }
 bool Read(const std::string&cursor,std::string&json,std::string&error,double deadline=100){return scripts->Read(cursor,deadline,json,error);}
};
std::string Next(const std::string&json){const auto key=json.find("\"next\":\"");if(key==std::string::npos)return {};const auto start=key+8;return json.substr(start,json.find('"',start)-start);}
int main(){size_t groups=0;
 {Harness h;std::string json,error;assert(h.Read("",json,error));const auto first=Next(json);assert(!first.empty()&&h.scripts->Retained()==1);assert(h.Read(first,json,error));const auto second=Next(json);assert(!second.empty()&&second!=first&&h.scripts->Retained()==2);const auto same=json;assert(h.Read(first,json,error)&&json==same&&h.scripts->Retained()==2);assert(h.Read(second,json,error)&&Next(json).empty());++groups;}
 {Harness h;std::string json,error;assert(h.Read("",json,error));const auto cursor=Next(json);++h.revision;assert(!h.Read(cursor,json,error)&&error.find("restart")!=std::string::npos&&h.scripts->Retained()==0);assert(h.Read("",json,error));++groups;}
 {Harness h;std::string json,error;assert(h.Read("",json,error));const auto old=Next(json);assert(h.Read("",json,error)&&Next(json)!=old);assert(!h.Read(old,json,error)&&h.scripts->Retained()==1);++groups;}
 {Harness a("a"),b("b");std::string json,error;assert(a.Read("",json,error));const auto cursor=Next(json);assert(!b.Read(cursor,json,error)&&b.calls==0);assert(!a.Read("forged",json,error));++groups;}
 {Harness h;std::string json,error;assert(h.Read("",json,error));h.current=false;assert(!h.Read(Next(json),json,error)&&h.scripts->Retained()==0);++groups;}
 {Harness h;std::string json,error;h.expire=true;assert(!h.Read("",json,error)&&error.find("deadline")!=std::string::npos&&h.scripts->Retained()==0);h.now=100;assert(!h.Read("",json,error)&&h.calls==1);++groups;}
 {Harness h;std::string json,error;h.change=true;assert(!h.Read("",json,error)&&h.scripts->Retained()==0);++groups;}
 {Harness h;std::string json,error;h.malformed=true;assert(!h.Read("",json,error)&&h.scripts->Retained()==0);++groups;}
 {Harness h;std::string json,error;h.records=0;assert(h.Read("",json,error)&&json=="{\"scripts\":[]}"&&h.scripts->Retained()==0);++groups;}
 {Harness h;h.records=100000;std::string json,error,cursor;for(size_t i=0;i<256;++i){assert(h.Read(cursor,json,error));cursor=Next(json);assert(!cursor.empty());}assert(h.scripts->Retained()==256&&!h.Read(cursor,json,error));assert(error.find("capacity")!=std::string::npos);Harness other("other");assert(!other.Read("",json,error)&&Scripts::ProcessRetained()==256);h.scripts->Clear();assert(Scripts::ProcessRetained()==0&&other.Read("",json,error));++groups;}
 assert(Scripts::ProcessRetained()==0);std::cout<<"developer script positions "<<groups<<" groups passed\n";
}
