// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "developer_debugger_breakpoints.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <utility>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
using Store=suitecut::DeveloperDebuggerBreakpoints;using Dict=Store::Dict;
static Dict JSON(const std::string&text){auto value=CefParseJSON(text,JSON_PARSER_RFC);assert(value);auto dict=value->GetDictionary();assert(dict);return dict;}
struct Fixture{
  struct Call{std::string method;Dict params;double deadline; suitecut::DebuggerDomains::Reply reply;};
  struct Result{bool ok;std::string json,error;};
  double now=1;bool current=true,refuse=false,synchronous_refusal=false,synchronous_unknown=false;size_t changed=0;
  std::vector<Call>calls;std::map<int,Result>results;std::vector<std::string>fatal;
  std::unique_ptr<Store>store;
  Fixture(){static size_t owner=0;Store::Hooks hooks;hooks.session="actual-native-owner-"+std::to_string(++owner);
    hooks.now=[this]{return now;};hooks.current=[this]{return current;};hooks.owns_script=[](const std::string&id){return id=="actual-script";};
    hooks.send=[this](const std::string&method,Dict params,double deadline,auto reply){if(refuse)return false;if(synchronous_refusal||synchronous_unknown){reply(false,nullptr,"actual synchronous failure");return !synchronous_refusal;}calls.push_back({method,params->Copy(false),deadline,std::move(reply)});return true;};
    hooks.close_channel=[this](const std::string&error){fatal.push_back(error);};hooks.changed=[this]{++changed;};store=std::make_unique<Store>(std::move(hooks));}
  ~Fixture(){Dead();}
  void Dead(){if(store)store->Closed();calls.clear();}
  Store::Reply Reply(int owner){return [this,owner](bool ok,const std::string&json,const std::string&error){assert(!results.contains(owner));results.emplace(owner,Result{ok,json,error});return ok;};}
  void Set(int owner,double deadline=100){auto location=JSON(R"({"scriptId":"actual-script","lineNumber":3,"columnNumber":2})");store->Set(owner,location,deadline,Reply(owner));location->Clear();}
  void Ack(const std::string&method,bool ok=true,Dict result=nullptr){auto found=std::find_if(calls.begin(),calls.end(),[&](const auto&call){return call.method==method;});assert(found!=calls.end());auto reply=std::move(found->reply);calls.erase(found);reply(ok,result?result:CefDictionaryValue::Create(),ok?"":"actual Inspector error");}
  Dict Response(const std::string&id="real-inspector-breakpoint",const std::string&script="actual-script"){return JSON("{\"breakpointId\":\""+id+"\",\"actualLocation\":{\"scriptId\":\""+script+"\",\"lineNumber\":4,\"columnNumber\":1}}");}
  std::string Token(int owner){assert(results.at(owner).ok);return JSON(results.at(owner).json)->GetString("breakpoint").ToString();}
  void Remove(int owner,const std::string&token,double deadline=100){store->Remove(owner,token,deadline,Reply(owner));}
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));size_t groups=0;
  {Fixture f;f.Set(1);assert(f.calls.size()==1&&f.store->Retained()==1);assert(f.calls.front().params->GetDictionary("location")->GetInt("lineNumber")==3);f.Ack("Debugger.setBreakpoint",true,f.Response());auto result=JSON(f.results.at(1).json);assert(result->GetDictionary("location")->GetInt("lineNumber")==4);f.Remove(2,f.Token(1));assert(f.calls.front().params->GetString("breakpointId")=="real-inspector-breakpoint"&&f.store->Retained()==1&&!f.results.contains(2));f.Ack("Debugger.removeBreakpoint");assert(f.results.at(2).ok&&f.store->Retained()==0);++groups;}
  {Fixture f;f.store->Set(1,JSON(R"({"scriptId":"foreign","lineNumber":0})"),100,f.Reply(1));f.store->Set(2,JSON(R"({"scriptId":"actual-script","lineNumber":-1})"),100,f.Reply(2));f.store->Set(3,JSON(R"({"scriptId":"actual-script","lineNumber":0.5})"),100,f.Reply(3));f.Remove(4,"forged-token");assert(f.calls.empty()&&!f.results.at(1).ok&&!f.results.at(2).ok&&!f.results.at(3).ok&&!f.results.at(4).ok);++groups;}
  {Fixture f,other;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());other.Remove(1,f.Token(1));assert(!other.results.at(1).ok&&other.calls.empty());f.Remove(2,f.Token(1));f.Remove(3,f.Token(1));assert(!f.results.at(3).ok&&f.calls.size()==1);f.Ack("Debugger.removeBreakpoint");f.Remove(4,f.Token(1));assert(!f.results.at(4).ok);++groups;}
  {Fixture f;f.Set(1);f.store->Cancel(1);assert(f.store->Retained()==1&&f.calls.size()==1);f.Ack("Debugger.setBreakpoint",true,f.Response());assert(!f.results.contains(1)&&f.store->Retained()==1&&f.calls.size()==1&&f.calls.front().method=="Debugger.removeBreakpoint");assert(f.calls.front().deadline==30001);f.Ack("Debugger.removeBreakpoint");assert(f.store->Retained()==0&&f.results.empty());++groups;}
  {Fixture f;f.Set(1);f.store->Close();assert(f.store->Retained()==1);f.Ack("Debugger.setBreakpoint",true,f.Response());assert(f.store->Retained()==1&&!f.results.contains(1));f.Ack("Debugger.removeBreakpoint");assert(f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1,5);f.now=5;f.Ack("Debugger.setBreakpoint",true,f.Response());assert(!f.results.at(1).ok&&f.results.at(1).error.find("original deadline")!=std::string::npos);assert(f.calls.front().deadline==30005&&f.store->Retained()==1);f.Ack("Debugger.removeBreakpoint");assert(f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());f.Remove(2,f.Token(1),5);assert(f.calls.front().deadline==5);f.now=5;f.Ack("Debugger.removeBreakpoint");assert(!f.results.at(2).ok&&f.results.at(2).error.find("original deadline")!=std::string::npos&&f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,JSON(R"({"actualLocation":{"scriptId":"actual-script","lineNumber":4}})"));assert(!f.results.at(1).ok&&f.fatal.size()==1&&f.store->Retained()==1);f.store->Close();assert(f.calls.empty()&&f.store->Retained()==1);f.Dead();assert(f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response("actual-id","foreign-script"));assert(!f.results.at(1).ok&&f.fatal.size()==1&&f.store->Retained()==1);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());f.Remove(2,f.Token(1));f.Ack("Debugger.removeBreakpoint",false);assert(!f.results.at(2).ok&&f.fatal.size()==1&&f.store->Retained()==1);f.store->Close();assert(f.calls.empty()&&f.store->Retained()==1);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());f.Remove(2,f.Token(1));f.store->Cancel(2);f.store->Close();assert(f.store->Retained()==1&&f.calls.size()==1);f.Ack("Debugger.removeBreakpoint");assert(!f.results.contains(2)&&f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1);f.current=false;f.Ack("Debugger.setBreakpoint",true,f.Response());assert(!f.results.at(1).ok&&f.calls.front().method=="Debugger.removeBreakpoint");f.Ack("Debugger.removeBreakpoint");assert(f.store->Retained()==0);++groups;}
  {Fixture f;f.refuse=true;f.Set(1);assert(!f.results.at(1).ok&&f.store->Retained()==0&&f.fatal.empty());f.refuse=false;f.Set(2);f.Ack("Debugger.setBreakpoint",false,JSON(R"({"code":-32000,"message":"actual Inspector error"})"));assert(!f.results.at(2).ok&&f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());f.Set(2);f.Ack("Debugger.setBreakpoint",true,f.Response());assert(f.fatal.size()==1&&f.store->Retained()==2&&!f.results.at(2).ok);++groups;}
  {Fixture f;for(int i=1;i<=16;++i)f.Set(i);f.Set(17);assert(f.calls.size()==16&&f.store->Retained()==16&&!f.results.at(17).ok);f.store->Close();assert(f.store->Retained()==16);++groups;}
  {std::vector<std::unique_ptr<Fixture>>fixtures;for(int owner=0;owner<8;++owner){fixtures.push_back(std::make_unique<Fixture>());for(int i=1;i<=16;++i)fixtures.back()->Set(i);}assert(Store::ProcessRetained()==128);Fixture extra;extra.Set(1);assert(!extra.results.at(1).ok&&extra.calls.empty());for(auto&f:fixtures)f->Dead();assert(Store::ProcessRetained()==0);extra.Set(2);assert(extra.calls.size()==1);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());const auto token=f.Token(1);f.store->Remove(2,token,100,[&](bool ok,const std::string&,const std::string&){assert(ok&&f.store->Retained()==0);f.Set(3);return true;});f.Ack("Debugger.removeBreakpoint");assert(f.calls.size()==1&&f.store->Retained()==1);f.Ack("Debugger.setBreakpoint",true,f.Response("new-real-id"));assert(f.Token(3)!=token);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());f.store.reset();assert(f.calls.size()==1&&f.calls.front().method=="Debugger.removeBreakpoint");f.Ack("Debugger.removeBreakpoint");assert(Store::ProcessRetained()==0);++groups;}
  {Fixture f;f.Set(1,0);f.store->Cancel(1);f.store->Close();assert(f.store->NeedsTick()&&f.store->Retained()==1);f.now=30001;f.store->Tick(f.now);assert(f.fatal.size()==1&&!f.store->NeedsTick()&&f.store->Retained()==1);f.now=40000;f.store->Tick(f.now);assert(f.fatal.size()==1);f.Ack("Debugger.setBreakpoint",true,f.Response());assert(f.calls.front().deadline==30001&&f.store->Retained()==1);f.Ack("Debugger.removeBreakpoint");assert(f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",true,f.Response());assert(!f.store->NeedsTick());f.now=100000;f.store->Close();assert(f.calls.front().deadline==130000&&f.store->NeedsTick());f.store->Tick(129999);assert(f.fatal.empty()&&f.store->Retained()==1);f.Ack("Debugger.removeBreakpoint");assert(!f.store->NeedsTick()&&f.store->Retained()==0);++groups;}
  {Fixture f;f.store->Set(1,JSON(R"({"scriptId":"actual-script","lineNumber":0})"),100,[](bool ok,const std::string&,const std::string&){assert(ok);return false;});f.Ack("Debugger.setBreakpoint",true,f.Response());assert(f.calls.front().method=="Debugger.removeBreakpoint"&&f.store->Retained()==1);f.Ack("Debugger.removeBreakpoint");assert(f.store->Retained()==0);++groups;}
  {Fixture f;f.Set(1);f.Ack("Debugger.setBreakpoint",false);assert(!f.results.at(1).ok&&f.fatal.size()==1&&f.store->Retained()==1);f.store->Close();assert(f.store->Retained()==1);++groups;}
  {Fixture f;f.synchronous_refusal=true;f.Set(1);assert(!f.results.at(1).ok&&f.fatal.empty()&&f.store->Retained()==0&&f.calls.empty());++groups;}
  {Fixture f;f.synchronous_unknown=true;f.Set(1);assert(!f.results.at(1).ok&&f.fatal.size()==1&&f.store->Retained()==1&&f.calls.empty());++groups;}
  assert(Store::ProcessRetained()==0);std::cout<<"developer breakpoint store "<<groups<<" groups passed\n";
}
