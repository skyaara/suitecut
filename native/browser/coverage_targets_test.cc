// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <deque>
#include <iostream>
#include <string>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "coverage_targets.h"
#include "devtools_utf16.h"
namespace {
using Controls=suitecut::CoverageTargets;using Dict=Controls::Dict;
Dict Object(const std::string& json){const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);assert(parsed);return parsed->GetDictionary();}
struct Backend{
 struct Call{std::string session,method;Dict params;double deadline; suitecut::CoverageControls::ProtocolReply reply;};
 double now=100;std::deque<Call> calls;int closed=0,owner=0,replies=0;bool ok=false;std::string output,error;std::shared_ptr<Controls> coverage;
 Backend(){Controls::Hooks hooks;hooks.now=[this]{return now;};hooks.wake=[]{};hooks.author_context=[](const std::string& session,int context){return (session.empty()&&context==7)||(session=="actual-child-session"&&context==9)||(session=="actual-future-session"&&context==11)||(session=="actual-worker-session"&&context==13);};hooks.close_channel=[this](const auto&,const auto&){++closed;};hooks.send=[this](const std::string& session,const std::string& method,Dict params,double deadline,auto reply){calls.push_back({session,method,params?params->Copy(false):nullptr,deadline,std::move(reply)});return true;};coverage=std::make_shared<Controls>(std::move(hooks));}
 int Invoke(const std::string& action,double deadline=1000,Dict params=nullptr){if(!params)params=CefDictionaryValue::Create();params->SetString("action",action);int id=++owner;coverage->Handle(id,params,deadline,[this](bool result,const std::string& reason,const std::string& json){++replies;ok=result;error=reason;output=json;});return id;}
 void Ack(const std::string& session,const std::string& method,Dict result=nullptr,bool ok=true){auto found=std::find_if(calls.begin(),calls.end(),[&](const auto& call){return call.session==session&&call.method==method;});if(found==calls.end()){std::cerr<<"Missing "<<session<<" "<<method<<" replies="<<replies<<" error="<<error<<" pending=";for(const auto& actual:calls)std::cerr<<actual.session<<":"<<actual.method<<" ";std::cerr<<"\n";}assert(found!=calls.end());auto call=std::move(*found);calls.erase(found);call.reply(ok,result?result:CefDictionaryValue::Create(),ok?"":"actual target protocol error");}
 void StartJS(const std::string& session){for(const char* method:{"Debugger.enable","Debugger.setSkipAllPauses","Profiler.enable","Profiler.startPreciseCoverage"})Ack(session,method);}
 void EndJS(const std::string& session){for(const char* method:{"Profiler.stopPreciseCoverage","Profiler.disable","Debugger.disable"})Ack(session,method);}
 void StartCSS(const std::string& session){for(const char* method:{"DOM.enable","CSS.enable","CSS.startRuleUsageTracking"})Ack(session,method);}
 void EndCSS(const std::string& session){Ack(session,"CSS.disable");Ack(session,"DOM.disable");}
 void Child(){int ready=0;coverage->Attached("actual-child-session","actual-child-target",1000,[&](bool ok,const std::string&){assert(ok);++ready;});assert(ready==1&&calls.empty());}
 void Clean(){coverage->Tick(now);auto value=CefDictionaryValue::Create();coverage->Diagnostics(value);for(const char* key:{"nativeCoverageSharedPayloadBytes","nativeCoverageBackendPhysical","nativeCoverageRecordings","nativeCoveragePhysical","nativeCoverageQueuedSources","nativeCoverageSourceBytes"}){if(value->GetType(key)==VTYPE_INT)assert(value->GetInt(key)==0);else{assert(value->GetType(key)==VTYPE_DOUBLE);assert(value->GetDouble(key)==0);}}assert(calls.empty()&&!coverage->PhysicalCount()&&!coverage->NeedsTick()&&closed==0);}
};
}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 {
  Backend b;b.Child();b.Invoke("startJS");b.StartJS("");assert(b.replies==0);b.StartJS("actual-child-session");assert(b.ok&&b.replies==1);
  // The same real target-local script ID in two sessions remains two genuine
  // source/count records. Utility contexts never enter either author journal.
  b.coverage->Event("Debugger.scriptParsed",Object(R"({"scriptId":"1","url":"http://root/author.js","executionContextId":7})"),"");
  b.coverage->Event("Debugger.scriptParsed",Object(R"({"scriptId":"1","url":"http://child/author.js","executionContextId":9})"),"actual-child-session");
  b.coverage->Event("Debugger.scriptParsed",Object(R"({"scriptId":"utility","url":"http://child/author.js","executionContextId":99})"),"actual-child-session");assert(b.calls.size()==2);
  b.Invoke("stopJS");auto root=Object(R"({"result":[{"scriptId":"1","functions":[{"functionName":"actualRoot","isBlockCoverage":true,"ranges":[{"startOffset":0,"endOffset":4,"count":2}]}]}]})");auto child=Object(R"({"result":[{"scriptId":"1","functions":[{"functionName":"actualChild","isBlockCoverage":true,"ranges":[{"startOffset":0,"endOffset":4,"count":3}]}]}]})");b.Ack("","Profiler.takePreciseCoverage",root);b.Ack("actual-child-session","Profiler.takePreciseCoverage",child);root->GetList("result")->Clear();child->GetList("result")->Clear();
  b.Ack("","Debugger.getScriptSource",Object(R"({"scriptSource":"root();"})"));auto exact=CefDictionaryValue::Create();suitecut::DevToolsSetString(exact,"scriptSource",std::string("child();\xed\xa0\x80"));b.Ack("actual-child-session","Debugger.getScriptSource",exact);b.EndJS("");assert(b.replies==1);b.EndJS("actual-child-session");assert(b.ok&&b.replies==2&&b.output.find("\\ud800")!=std::string::npos&&b.output.find("\"count\":2")!=std::string::npos&&b.output.find("\"count\":3")!=std::string::npos&&b.output.find("actualRoot")!=std::string::npos&&b.output.find("actualChild")!=std::string::npos);b.Clean();
  b.Invoke("startCSS");b.StartCSS("");b.StartCSS("actual-child-session");assert(b.ok);
  b.coverage->Event("CSS.styleSheetAdded",Object(R"({"header":{"styleSheetId":"same","origin":"regular","sourceURL":"http://root/a.css"}})"),"");b.coverage->Event("CSS.styleSheetAdded",Object(R"({"header":{"styleSheetId":"same","origin":"regular","sourceURL":"http://child/a.css"}})"),"actual-child-session");b.Ack("","CSS.getStyleSheetText",Object(R"({"text":"div{}"})"));b.Ack("actual-child-session","CSS.getStyleSheetText",Object(R"({"text":"span{}"})"));b.Invoke("stopCSS");b.Ack("","CSS.stopRuleUsageTracking",Object(R"({"ruleUsage":[{"styleSheetId":"same","startOffset":0,"endOffset":5,"used":true}]})"));b.Ack("actual-child-session","CSS.stopRuleUsageTracking",Object(R"({"ruleUsage":[{"styleSheetId":"same","startOffset":0,"endOffset":6,"used":true}]})"));b.EndCSS("");b.EndCSS("actual-child-session");assert(b.ok&&b.output.find("div{}")!=std::string::npos&&b.output.find("span{}")!=std::string::npos);b.Clean();
 }
 {
  Backend b;b.Child();b.Invoke("startJS");int resumed=0;b.coverage->Attached("actual-child-session","actual-child-target",700,[&](bool ok,const auto&){assert(ok);++resumed;});assert(resumed==0);b.StartJS("");assert(resumed==0);b.StartJS("actual-child-session");assert(resumed==1);b.Invoke("stopJS");b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.Ack("actual-child-session","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.EndJS("");b.EndJS("actual-child-session");b.Clean();
 }
 {
  Backend b;b.Invoke("startJS");b.StartJS("");int resumed=0;b.coverage->Attached("actual-future-session","actual-future-target",800,[&](bool ok,const auto&){assert(ok);++resumed;});assert(resumed==0);b.Ack("actual-future-session","Debugger.enable");b.Ack("actual-future-session","Debugger.setSkipAllPauses");b.Ack("actual-future-session","Profiler.enable");assert(resumed==0);b.Ack("actual-future-session","Profiler.startPreciseCoverage");assert(resumed==1);
  b.coverage->Detached("actual-future-session");b.Invoke("stopJS");assert(!b.ok&&b.error.find("detached before")!=std::string::npos);b.EndJS("");b.Clean();
 }
 {
  Backend b;b.Child();b.Invoke("startJS");b.StartJS("");b.StartJS("actual-child-session");b.coverage->Detached("actual-child-session");b.coverage->Navigated();b.Invoke("stopJS");b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.EndJS("");assert(b.ok&&b.output=="[]");b.Clean();
 }
 {
  Backend b;b.Child();b.Invoke("startJS");b.StartJS("");b.StartJS("actual-child-session");b.coverage->Navigated();b.coverage->Detached("actual-child-session");b.Invoke("stopJS");b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.EndJS("");assert(b.ok&&b.output=="[]");b.Clean();
 }
 {
  Backend b;b.Invoke("startJS");b.StartJS("");b.Invoke("stopJS");int resumed=0;b.coverage->Attached("actual-future-session","actual-future-target",800,[&](bool ok,const auto&){assert(ok);++resumed;});assert(resumed==1);b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.EndJS("");assert(b.ok&&b.output=="[]");b.Clean();
 }
 {
  Backend b;b.Child();const int owner=b.Invoke("startJS",110);assert(b.calls.size()==2);b.coverage->Cancel(owner);assert(b.replies==1&&!b.ok&&b.coverage->PhysicalCount()==2);b.Ack("","Debugger.enable");b.Ack("actual-child-session","Debugger.enable");assert(b.calls.size()==2);b.Ack("","Debugger.disable");b.Ack("actual-child-session","Debugger.disable");b.Clean();
  b.Invoke("startCSS",110);assert(b.calls.size()==2);for(const auto& call:b.calls)assert(call.deadline==110);b.now=111;b.Ack("","DOM.enable");assert(!b.ok&&b.error.find("timed out")!=std::string::npos);b.Ack("actual-child-session","DOM.enable");b.Ack("","DOM.disable");b.Ack("actual-child-session","DOM.disable");b.Clean();
 }
 {
  Backend b;b.Invoke("startJS");b.StartJS("");int positive=0,negative=0;b.coverage->Attached("actual-future-session","actual-future-target",800,[&](bool ok,const auto&){if(ok)++positive;else++negative;});b.coverage->Closed();assert(positive==0&&negative==1&&!b.coverage->PhysicalCount());for(auto& call:b.calls)call.reply(true,CefDictionaryValue::Create(),"");b.calls.clear();
 }
 {
  Backend b;int ready=0;for(int i=0;i<40;++i)b.coverage->Attached("idle-session-"+std::to_string(i),"idle-target-"+std::to_string(i),1000,[&](bool ok,const auto&){assert(ok);++ready;});assert(ready==40&&b.calls.empty());b.Invoke("startJS");assert(!b.ok&&b.error.find("capacity")!=std::string::npos&&b.calls.empty());b.Clean();
 }
 {
  Backend b;b.Invoke("startJS");b.StartJS("");int ready=0;
  auto drain=[&]{int calls=0;while(!b.calls.empty()){assert(++calls<1000);auto call=std::move(b.calls.front());b.calls.pop_front();call.reply(true,CefDictionaryValue::Create(),"");}};
  for(int i=0;i<31;++i){b.coverage->Attached("active-session-"+std::to_string(i),"active-target-"+std::to_string(i),1000,[&](bool ok,const auto&){assert(ok);++ready;});drain();}assert(ready==31);
  b.coverage->Attached("overflow-session","overflow-target",1000,[&](bool ok,const auto&){assert(ok);++ready;});assert(ready==32&&b.replies==1);drain();b.coverage->Tick(b.now);b.Invoke("stopJS");assert(!b.ok&&b.error.find("capacity")!=std::string::npos);b.Clean();
  // The failed recording does not permanently cap otherwise valid page frames.
  b.coverage->Attached("later-idle-session","later-idle-target",1000,[&](bool ok,const auto&){assert(ok);++ready;});assert(ready==33&&b.calls.empty());b.Clean();
 }
 {
  Backend b;b.Child();b.Invoke("startJS");b.StartJS("");b.StartJS("actual-child-session");b.coverage->Attached("actual-future-session","actual-future-target",1000,{});b.StartJS("actual-future-session");
  const std::vector<std::pair<std::string,int>> targets={{"",7},{"actual-child-session",9},{"actual-future-session",11}};
  for(const auto& [session,context]:targets){auto event=Object(R"({"scriptId":"actual","url":"http://actual/large.js"})");event->SetInt("executionContextId",context);b.coverage->Event("Debugger.scriptParsed",event,session);auto source=CefDictionaryValue::Create();source->SetString("scriptSource",std::string(800000,'x'));b.Ack(session,"Debugger.getScriptSource",source);}
  b.Invoke("stopJS");for(const auto& [session,context]:targets)b.Ack(session,"Profiler.takePreciseCoverage",Object(R"({"result":[{"scriptId":"actual","functions":[]}]})"));for(const auto& [session,context]:targets)b.EndJS(session);assert(!b.ok&&b.error.find("exceeds 2 MiB")!=std::string::npos);b.Clean();
 }
 {
  Backend b;b.Child();const int owner=b.Invoke("startJS",110);int resumed=0;b.coverage->Attached("actual-child-session","actual-child-target",800,[&](bool ok,const auto&){assert(ok);++resumed;});b.coverage->Cancel(owner);assert(!b.ok&&b.coverage->PhysicalCount()==2&&resumed==0);b.Ack("","Debugger.enable");b.Ack("actual-child-session","Debugger.enable");b.Ack("","Debugger.disable");b.Ack("actual-child-session","Debugger.disable");assert(!b.coverage->PhysicalCount()&&resumed==0);
  // No Tick has run since actual cleanup ACKs. Admission must use genuine
  // readiness now, and the old bootstrap cannot bypass this new startup.
  b.Invoke("startJS");b.StartJS("");assert(resumed==0);b.StartJS("actual-child-session");assert(b.ok&&resumed==1);b.Invoke("stopJS");b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.Ack("actual-child-session","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.EndJS("");b.EndJS("actual-child-session");assert(b.ok);b.Clean();
 }
 {
  Backend b;int ready=0;b.coverage->WorkerAttached("actual-worker-session","actual-worker-target",1000,[&](bool ok,const auto&){assert(ok);++ready;});assert(ready==1&&b.calls.empty());
  // The default remains document-only; CSS never sends unsupported worker DOM
  // commands, even when idle workers exceed the capture transaction capacity.
  for(int i=0;i<40;++i)b.coverage->WorkerAttached("idle-worker-"+std::to_string(i),"idle-worker-target-"+std::to_string(i),1000,{});
  b.Invoke("startJS");b.StartJS("");assert(b.ok&&b.calls.empty());b.Invoke("stopJS");b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.EndJS("");b.Clean();
  b.Invoke("startCSS");b.StartCSS("");assert(b.ok&&b.calls.empty());b.Invoke("stopCSS");b.Ack("","CSS.stopRuleUsageTracking",Object(R"({"ruleUsage":[]})"));b.EndCSS("");b.Clean();
  b.Invoke("startCSS",1000,Object(R"({"includeWorkers":true})"));assert(!b.ok&&b.error.find("worker CSS is unsupported")!=std::string::npos&&b.calls.empty());
  b.Invoke("startJS",1000,Object(R"({"includeWorkers":"true"})"));assert(!b.ok&&b.calls.empty());
  b.Invoke("startJS",1000,Object(R"({"includeWorkers":true})"));assert(!b.ok&&b.error.find("capacity")!=std::string::npos&&b.calls.empty());b.Clean();
 }
 {
  Backend b;b.coverage->WorkerAttached("actual-worker-session","actual-worker-target",1000,{});b.Invoke("startJS",1000,Object(R"({"includeWorkers":true})"));b.StartJS("");assert(b.replies==0);b.StartJS("actual-worker-session");assert(b.ok&&b.replies==1);
  b.coverage->Event("Debugger.scriptParsed",Object(R"({"scriptId":"3","url":"http://actual/worker.js","executionContextId":13})"),"actual-worker-session");
  b.Invoke("stopJS");b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.Ack("actual-worker-session","Profiler.takePreciseCoverage",Object(R"({"result":[{"scriptId":"3","functions":[{"functionName":"actualWorker","isBlockCoverage":true,"ranges":[{"startOffset":0,"endOffset":4,"count":3}]}]}]})"));
  b.Ack("actual-worker-session","Debugger.getScriptSource",Object(R"({"scriptSource":"work();"})"));b.EndJS("");assert(b.replies==1);b.EndJS("actual-worker-session");assert(b.ok&&b.output.find("actualWorker")!=std::string::npos&&b.output.find("\"count\":3")!=std::string::npos);b.Clean();
 }
 {
  Backend b;b.Invoke("startJS",1000,Object(R"({"includeWorkers":true})"));b.StartJS("");int ready=0;b.coverage->WorkerAttached("actual-worker-session","actual-worker-target",500,[&](bool ok,const auto&){assert(ok);++ready;});assert(ready==0);
  b.Ack("actual-worker-session","Debugger.enable");b.Ack("actual-worker-session","Debugger.setSkipAllPauses");b.Ack("actual-worker-session","Profiler.enable");assert(ready==0);b.Ack("actual-worker-session","Profiler.startPreciseCoverage");assert(ready==1);
  b.coverage->Lost("actual-worker-session","Actual worker context lost");assert(!b.calls.size());b.Invoke("stopJS");assert(!b.ok&&b.error=="Actual worker context lost");b.EndJS("");b.EndJS("actual-worker-session");b.Clean();
 }
 {
  Backend b;b.coverage->WorkerAttached("actual-worker-session","actual-worker-target",1000,{});const auto owner=b.Invoke("startJS",110,Object(R"({"includeWorkers":true})"));int ready=0;b.coverage->WorkerAttached("actual-worker-session","actual-worker-target",1000,[&](bool ok,const auto&){assert(ok);++ready;});b.coverage->Cancel(owner);assert(!b.ok&&b.coverage->PhysicalCount()==2&&ready==0);
  b.Ack("","Debugger.enable");b.Ack("actual-worker-session","Debugger.enable");b.Ack("","Debugger.disable");assert(ready==0);b.Ack("actual-worker-session","Debugger.disable");b.Clean();assert(ready==1);
 }
 {
  Backend b;b.coverage->WorkerAttached("actual-worker-session","actual-worker-target",1000,{});b.Invoke("startJS",1000,Object(R"({"includeWorkers":true})"));b.StartJS("");b.StartJS("actual-worker-session");
  // Genuine main navigation resets the old worker scope. Early worker context
  // destruction alone cannot fabricate success or prematurely retire a channel.
  b.coverage->Lost("actual-worker-session","Actual worker context lost");b.coverage->Navigated();b.coverage->Detached("actual-worker-session");b.Invoke("stopJS");b.Ack("","Profiler.takePreciseCoverage",Object(R"({"result":[]})"));b.EndJS("");assert(b.ok&&b.output=="[]");b.Clean();
 }
 {
  Backend b;b.Invoke("startJS",1000,Object(R"({"includeWorkers":true})"));b.StartJS("");int ready=0;b.coverage->WorkerAttached("actual-worker-session","actual-worker-target",110,[&](bool ok,const auto&){assert(!ok);++ready;});assert(b.coverage->PhysicalCount()==1);
  b.now=111;b.Ack("actual-worker-session","Debugger.enable");assert(ready==0&&b.coverage->PhysicalCount()==2); // real root teardown plus actual late worker cleanup
  b.Ack("actual-worker-session","Debugger.disable");b.EndJS("");b.coverage->Tick(b.now);assert(ready==1);b.Invoke("stopJS");assert(!b.ok);b.Clean();
 }
 std::cout<<"Native coverage actual-session transactions, independent counts/CSS, UTF16, startup barriers, detach/reset and original-deadline cleanup passed\n";
}
