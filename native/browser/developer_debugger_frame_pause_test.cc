// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "include/cef_api_hash.h"
#include "coverage_script_inventory.h"
#include "developer_debugger_frame_pause.h"
using Book=suitecut::FrameDebuggerScopes;using Pause=suitecut::FrameDebuggerPause;using Dict=suitecut::DebuggerDomains::Dict;using D=Pause::Disposition;
Dict Packet(const std::vector<std::string>&scripts){auto packet=CefDictionaryValue::Create();auto list=CefListValue::Create();for(size_t i=0;i<scripts.size();++i){auto frame=CefDictionaryValue::Create();auto location=CefDictionaryValue::Create();location->SetString("scriptId",scripts[i]);frame->SetDictionary("location",location);frame->SetString("callFrameId","actual-frame-"+std::to_string(i));list->SetDictionary(i,frame);}packet->SetList("callFrames",list);return packet;}
struct Harness{
 bool current=true;Book::Host host{"native-host",1};Book::Token token;suitecut::CoverageScriptInventory inventory;std::shared_ptr<Book>book;
 explicit Harness(std::string session="",bool ready=true){
  Book::Hooks hooks;hooks.now=[](){return 1.;};hooks.current=[this](const auto&){return current;};hooks.lease_present=[](uint64_t){return true;};hooks.cleanup=[](auto,const auto&){};hooks.close_channel=[](const auto&,const auto&){};hooks.wake=[](){};book=Book::Create(host,std::move(hooks),std::make_shared<Book::Budget>());
  Book::Pin pin{{host,std::move(session)},1,1,"child-frame","loader","unique-child-context",8,false};std::string error;token=book->Reserve(pin,1,100,error);assert(token&&book->BeginAcquire(token));if(ready)assert(book->Ready(token,true));book->AcquireReturned(token);
 }
 ~Harness(){book->HostClosed(host);}
 void Script(const std::string&id,int context,const std::string&session="",const std::string&url="author.js"){
  auto script=CefDictionaryValue::Create();script->SetString("scriptId",id);script->SetString("url",url);script->SetInt("executionContextId",context);inventory.Observe("Debugger.scriptParsed",script,session);
 }
 D Classify(Dict packet){return Pause::Classify(book,token,packet,[this](const auto&session,const auto&id)->std::optional<Pause::Script>{int context=0;bool native=false;if(!inventory.ScriptContext(session,id,context,native))return {};return Pause::Script{context,native};});}
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));size_t groups=0;
 {Harness h;h.Script("child-a",8);h.Script("child-b",8);assert(h.Classify(Packet({"child-a","child-b"}))==D::Owned);++groups;}
 {Harness h;h.Script("parent",7);assert(h.Classify(Packet({"parent"}))==D::Foreign);++groups;}
 {Harness h;h.Script("child",8);h.Script("parent",7);assert(h.Classify(Packet({"child","parent"}))==D::Unsupported);++groups;}
 {Harness h;assert(h.Classify(Packet({"unobserved"}))==D::Unsupported);++groups;}
 {Harness h;h.Script("same",8);h.current=false;assert(h.Classify(Packet({"same"}))==D::Unsupported);++groups;}
 {Harness h("actual-child-session");h.Script("same",7);h.Script("same",8,"actual-child-session");assert(h.Classify(Packet({"same"}))==D::Owned);h.inventory.Detached("actual-child-session");assert(h.Classify(Packet({"same"}))==D::Unsupported);++groups;}
 {Harness h;h.Script("attested",8,"","identical-author-metadata");h.Script("author",8,"","identical-author-metadata");assert(h.inventory.MarkNative("",8,"attested"));assert(h.Classify(Packet({"author"}))==D::Owned&&h.Classify(Packet({"attested"}))==D::Unsupported);++groups;}
 {Harness h;h.Script("child",8);assert(h.Classify(nullptr)==D::Unsupported&&h.Classify(Packet({}))==D::Unsupported);auto packet=Packet({"child"});packet->GetList("callFrames")->GetDictionary(0)->SetString("callFrameId","");assert(h.Classify(packet)==D::Unsupported);assert(h.Classify(Packet(std::vector<std::string>(65,"child")))==D::Unsupported);++groups;}
 {Harness h("",false);h.Script("child",8);assert(!h.book->Current(h.token)&&h.book->ObservedCurrent(h.token)&&h.Classify(Packet({"child"}))==D::Owned);assert(h.book->Ready(h.token,true)&&h.book->Current(h.token));++groups;}
 std::cout<<"native frame debugger pause classifier "<<groups<<" groups passed\n";
}
