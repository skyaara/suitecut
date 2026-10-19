// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <deque>
#include <string>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "coverage_controls.h"
using Controls=suitecut::CoverageControls;using Dict=Controls::Dict;
Dict Object(const std::string& text){const auto parsed=CefParseJSON(text,JSON_PARSER_RFC);assert(parsed&&parsed->GetType()==VTYPE_DICTIONARY);return parsed->GetDictionary();}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 double now=100;bool root=true;int closed=0,owner=0,replies=0;bool success=false;std::string result,error;
 struct Call{std::string method;Dict params;double deadline;Controls::ProtocolReply reply;};std::deque<Call> calls;
 Controls::Hooks hooks;hooks.now=[&]{return now;};hooks.root_target_only=[&]{return root;};hooks.author_context=[](int context){return context==7;};hooks.close_channel=[&](const std::string&){++closed;};hooks.send=[&](const std::string& method,Dict params,double deadline,Controls::ProtocolReply reply){calls.push_back({method,params?params->Copy(false):nullptr,deadline,std::move(reply)});return true;};
 Controls coverage(hooks);auto invoke=[&](const std::string& action,Dict params=nullptr,double deadline=1000){if(!params)params=CefDictionaryValue::Create();params->SetString("action",action);const int id=++owner;coverage.Handle(id,params,deadline,[&](bool ok,const std::string& message,const std::string& json){++replies;success=ok;error=message;result=json;});return id;};
 auto ack=[&](const char* method,Dict result=nullptr,bool ok=true){assert(!calls.empty());auto found=std::find_if(calls.begin(),calls.end(),[&](const Call& call){return call.method==method;});assert(found!=calls.end());auto call=std::move(*found);calls.erase(found);call.reply(ok,result?result:CefDictionaryValue::Create(),ok?"":"actual protocol failure");};
 auto jsStart=[&]{ack("Debugger.enable");ack("Debugger.setSkipAllPauses");ack("Profiler.enable");ack("Profiler.startPreciseCoverage");};
 auto jsEnd=[&]{ack("Profiler.stopPreciseCoverage");ack("Profiler.disable");ack("Debugger.disable");};
 invoke("stopJS");assert(replies==1&&!success);invoke("startJS");assert(coverage.PhysicalCount()==1);jsStart();assert(success&&replies==2&&coverage.PhysicalCount()==0);
 coverage.Event("Debugger.scriptParsed",Object(R"({"scriptId":"utility","url":"http://same/author.js","executionContextId":99})"));assert(calls.empty());
 coverage.Event("Debugger.scriptParsed",Object(R"({"scriptId":"1","url":"http://actual/author.js","executionContextId":7})"));assert(coverage.PhysicalCount()==1);invoke("stopJS");assert(coverage.PhysicalCount()==2);
 ack("Debugger.getScriptSource",Object(R"({"scriptSource":"function foo(){return 1;} foo();"})"));
 ack("Profiler.takePreciseCoverage",Object(R"({"result":[{"scriptId":"1","url":"http://actual/author.js","functions":[{"functionName":"foo","isBlockCoverage":true,"ranges":[{"startOffset":0,"endOffset":25,"count":1}]}]},{"scriptId":"utility","url":"http://same/author.js","functions":[]}]})"));
 assert(coverage.PhysicalCount()==1&&replies==2);jsEnd();assert(success&&replies==3&&coverage.PhysicalCount()==0);
 auto parsed=CefParseJSON(result,JSON_PARSER_RFC);assert(parsed&&parsed->GetList()->GetSize()==1);auto entry=parsed->GetList()->GetDictionary(0);assert(entry->GetString("source")=="function foo(){return 1;} foo();");assert(entry->GetString("scriptId")=="1");assert(entry->GetList("functions")->GetDictionary(0)->GetList("ranges")->GetDictionary(0)->GetDouble("count")==1);
 // Canceling a startup logically retires its owner but retains the real pending
 // debugger command. Its late ACK must trigger actual domain disable, not start.
 const int canceled=invoke("startJS");const int prior=replies;coverage.Cancel(canceled);assert(replies==prior+1&&!success&&coverage.PhysicalCount()==1);ack("Debugger.enable");assert(calls.size()==1&&calls.front().method=="Debugger.disable");ack("Debugger.disable");assert(coverage.PhysicalCount()==0&&replies==prior+1);
 // CSS owns DOM.enable too. Cancel before its real ACK still retains and
 // disables that actual domain; physical0 cannot precede DOM.disable ACK.
 const int canceled_css=invoke("startCSS");const int prior_css=replies;coverage.Cancel(canceled_css);assert(replies==prior_css+1&&!success&&coverage.PhysicalCount()==1);ack("DOM.enable");assert(calls.size()==1&&calls.front().method=="DOM.disable"&&coverage.PhysicalCount()==1);ack("DOM.disable");assert(calls.empty()&&coverage.PhysicalCount()==0&&replies==prior_css+1);
 // One original deadline, no renewed callback budget and no false physical0.
 const int timed=invoke("startJS",nullptr,110);assert(calls.front().deadline==110);now=111;coverage.Tick(now);assert(!success&&coverage.PhysicalCount()==1&&closed==0);const auto timed_replies=replies;ack("Debugger.enable");ack("Debugger.disable");assert(coverage.PhysicalCount()==0&&replies==timed_replies);(void)timed;
 // ACK arrival itself enforces the original deadline even when no Tick ran.
 now=100;invoke("startJS",nullptr,110);const int late_start=replies;now=111;ack("Debugger.enable");assert(replies==late_start+1&&!success&&error.find("timed out")!=std::string::npos);assert(calls.size()==1&&calls.front().method=="Debugger.disable"&&coverage.PhysicalCount()==1);ack("Debugger.disable");assert(calls.empty()&&coverage.PhysicalCount()==0);
 // Retain a COPY of the measured list while a real source callback is pending.
 // Mutation and destruction of the protocol reply's parent cannot affect it.
 now=100;invoke("startJS");jsStart();coverage.Event("Debugger.scriptParsed",Object(R"({"scriptId":"retained","url":"http://actual/retained.js","executionContextId":7})"));invoke("stopJS");auto borrowed=Object(R"({"result":[{"scriptId":"retained","functions":[{"functionName":"","isBlockCoverage":true,"ranges":[{"startOffset":0,"endOffset":9,"count":1}]}]}]})");ack("Profiler.takePreciseCoverage",borrowed);assert(coverage.PhysicalCount()==1);borrowed->GetList("result")->Clear();borrowed=nullptr;ack("Debugger.getScriptSource",Object(R"({"scriptSource":"actual();"})"));jsEnd();assert(success);parsed=CefParseJSON(result,JSON_PARSER_RFC);assert(parsed->GetList()->GetSize()==1&&parsed->GetList()->GetDictionary(0)->GetList("functions")->GetSize()==1);
 // Logical stop cannot become successful at a late final cleanup ACK.
 now=100;invoke("startJS");jsStart();invoke("stopJS",nullptr,110);ack("Profiler.takePreciseCoverage",Object(R"({"result":[]})"));ack("Profiler.stopPreciseCoverage");ack("Profiler.disable");const int late_stop=replies;now=111;ack("Debugger.disable");assert(replies==late_stop+1&&!success&&coverage.PhysicalCount()==0&&calls.empty());
 // A late CSS stop ACK has already physically stopped tracking; do not issue
 // a duplicate stop when cleaning up the expired logical operation.
 now=100;invoke("startCSS");ack("DOM.enable");ack("CSS.enable");ack("CSS.startRuleUsageTracking");invoke("stopCSS",nullptr,110);const int late_css=replies;now=111;ack("CSS.stopRuleUsageTracking",Object(R"({"ruleUsage":[]})"));assert(replies==late_css+1&&!success&&calls.size()==1&&calls.front().method=="CSS.disable");ack("CSS.disable");assert(coverage.PhysicalCount()==1&&calls.front().method=="DOM.disable");ack("DOM.disable");assert(calls.empty());
 // A source read's own native deadline is checked on ACK, before Tick too.
 now=100;invoke("startJS");jsStart();coverage.Event("Debugger.scriptParsed",Object(R"({"scriptId":"expired-source","url":"http://actual/source.js","executionContextId":7})"));now=30101;ack("Debugger.getScriptSource",Object(R"({"scriptSource":"late();"})"));invoke("stopJS",nullptr,31000);ack("Profiler.takePreciseCoverage",Object(R"({"result":[{"scriptId":"expired-source","functions":[]}]})"));assert(!success&&error.find("timed out")!=std::string::npos);jsEnd();assert(calls.empty());
 // Navigation reset drops only obsolete document data, retaining original source
 // read physical ownership and refusing its late source bytes in the new epoch.
 now=100;invoke("startJS");jsStart();coverage.Event("Debugger.scriptParsed",Object(R"({"scriptId":"old","url":"http://actual/old.js","executionContextId":7})"));coverage.Navigated();ack("Debugger.getScriptSource",Object(R"({"scriptSource":"actual old bytes"})"));invoke("stopJS");ack("Profiler.takePreciseCoverage",Object(R"({"result":[{"scriptId":"old","functions":[]}]})"));jsEnd();assert(success&&result=="[]");
 // Native stylesheet text and genuinely unused CSS ranges remain represented.
 invoke("startCSS");ack("DOM.enable");ack("CSS.enable");ack("CSS.startRuleUsageTracking");coverage.Event("CSS.styleSheetAdded",Object(R"({"header":{"styleSheetId":"actual","origin":"regular","sourceURL":"nicename.css"}})"));ack("CSS.getStyleSheetText",Object(R"({"text":"div { color: green; } span { color:red; }"})"));invoke("stopCSS");ack("CSS.stopRuleUsageTracking",Object(R"({"ruleUsage":[{"styleSheetId":"actual","startOffset":0,"endOffset":21,"used":true},{"styleSheetId":"actual","startOffset":22,"endOffset":40,"used":false}]})"));ack("CSS.disable");ack("DOM.disable");assert(success);parsed=CefParseJSON(result,JSON_PARSER_RFC);assert(parsed->GetList()->GetSize()==1);assert(parsed->GetList()->GetDictionary(0)->GetList("ranges")->GetSize()==1);
 root=false;invoke("startJS");assert(!success&&error.find("out-of-process")!=std::string::npos&&coverage.PhysicalCount()==0);root=true;
 invoke("startJS");jsStart();coverage.OutOfProcessFrameAttached();invoke("stopJS");ack("Profiler.takePreciseCoverage",Object(R"({"result":[]})"));assert(!success&&error.find("out-of-process")!=std::string::npos);jsEnd();
 auto diagnostics=CefDictionaryValue::Create();coverage.Diagnostics(diagnostics);assert(diagnostics->GetInt("nativeCoverageRecordings")==0&&diagnostics->GetInt("nativeCoveragePhysical")==0&&diagnostics->GetDouble("nativeCoverageSourceBytes")==0);invoke("startJS",Object(R"({"resetOnNavigation":false})"));ack("Profiler.getNavigationCoverageCapabilities");assert(!success&&error.find("retention across navigation")!=std::string::npos&&calls.empty());coverage.Closed();assert(calls.empty()&&closed==0);
}
