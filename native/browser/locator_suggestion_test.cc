// SPDX-License-Identifier: MIT
#include "tests/support/cef_library.h"
#include "include/cef_api_hash.h"
#include "locator_suggestion.h"
#include "locator_suggestion_dom.h"
#include "dom_query.h"
#include "query_node_transfer.h"
#include "operation_registry.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
#include <cassert>
#include <iostream>
#include <optional>
#include <map>
using Dict=CefRefPtr<CefDictionaryValue>;
static Dict Parse(const std::string&json){const auto v=CefParseJSON(json,JSON_PARSER_RFC);assert(v);return v->GetDictionary();}
static std::string JSON(Dict value){auto v=CefValue::Create();v->SetDictionary(value);return CefWriteJSON(v,JSON_WRITER_DEFAULT).ToString();}
struct Harness {
 using DevToolsCallback=std::function<void(bool,const std::string&)>;
 using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
 struct ObjectHandle{std::string object_id,group,frame_id;int context=0;std::string session,node_id,context_identity;uint64_t renderer_epoch=0;};
 struct Frame{std::string parent,session;};
 std::map<std::string,Frame>frame_states_{{"main",{"",""}}};std::map<std::string,std::string>sessions_;std::string main_frame_id_="main";
 SuiteCutOperationRegistry operations_;double now=0;uint64_t native_pristine_renderer_epoch_=1;std::string handle_namespace_="controlledSource";
 std::map<std::string,SnapshotReferenceDocument>documents{{"main",{"main","","loader","mainUnique","utilityUnique",4,5,1}}};
 std::map<int,ObjectHandle>handles_;std::vector<std::string>errors,responses;
 struct Call{std::string method;Dict params;DevToolsCallback done;};std::vector<Call>calls;
 std::vector<std::function<void()>>releases;
 suitecut::SelectorWaitControls selectors{[this]{return now;},[this](const auto&e){errors.push_back(e);},[this](int,const auto&,const auto&,std::function<void()>done){releases.push_back(std::move(done));}};
 bool transfer=true,hold_publication=false;std::string held;int queries=0;
 double Now()const{return now;}
 auto ReferenceDocument(const std::string&frame)const{const auto at=documents.find(frame);return at==documents.end()?std::optional<SnapshotReferenceDocument>{}:std::optional(at->second);}
 void Complete(int owner,bool ok,const std::string&e,const std::string&json=""){
   if(ok){const auto found=locator_suggestion_work_.find(owner);if(found!=locator_suggestion_work_.end()&&!LocatorSuggestionCurrent(found->second))return;if(hold_publication){held=json;return;}}
   if(!operations_.Finish(owner))return;
   locator_suggestion_work_.erase(owner);selectors.Cancel(owner);
   if(ok)responses.push_back(json);else{std::cout<<"controlled failure "<<e<<std::endl;errors.push_back(e);}
 }
 auto&SelectorWaits(){return selectors;}
 bool ReferenceTemporaryGroups(int owner){return transfer&&selectors.Group(owner,"actual-operation-group","");}
 std::string Json(Dict value){return JSON(value);}
 std::string FrameId(Dict m){return m->HasKey("frame")?m->GetString("frame").ToString():"main";}
 std::string MissingHandle(int){return "Missing actual retained handle";}
 void ResolveFramePath(int,uint64_t,Dict,size_t,const std::string&,std::function<void(Dict)>){assert(false);}
 void WithFrame(int,const std::string&frame,std::function<void(int)>done,bool utility){assert(utility&&documents.contains(frame));done(documents.at(frame).utility);}
 void DevTools(int owner,const std::string&method,Dict params,DevToolsCallback done,const std::string&session,bool retained,size_t limit,bool lossless){
   assert(session==documents.at("main").session&&retained&&limit==262144&&lossless);
   auto lease=selectors.Begin(owner);assert(lease);
   calls.push_back({method,params->Copy(false),[lease,done=std::move(done)](bool ok,const auto&json){lease->Settle();done(ok,json);}});
 }
 void QueryInspect(int,Dict,const std::string&body,bool utility,bool byvalue,DevToolsCallback done){assert(utility&&!byvalue&&body.find("nodes.length!==1")!=std::string::npos);++queries;calls.push_back({"query",nullptr,std::move(done)});}
 void Start(bool handle=false){std::cout<<"begin controlled case handle="<<handle<<std::endl;assert(operations_.Begin(1,now,10));auto m=CefDictionaryValue::Create();if(handle){m->SetInt("nativeHandle",17);handles_[17]={"wrapper","actual-handle-group","main",4,"","actual-retained-node","mainUnique",1};}else m->SetList("query",CefListValue::Create());BeginLocatorSuggestion(1,m);}
 void Ack(const std::string&method,const std::string&json,bool ok=true){std::cout<<"controlled reply "<<method<<" queued="<<calls.size()<<std::endl;assert(!calls.empty()&&calls.front().method==method);auto done=std::move(calls.front().done);calls.erase(calls.begin());done(ok,json);}
 void Drain(){while(!releases.empty()){auto done=std::move(releases.back());releases.pop_back();done();}}
 ~Harness(){selectors.Close();}
#include "controllers/dom/locator_suggestion_controller.inc"
#ifdef SUITECUT_LOCATOR_PICK_TEST_INCLUDE
#include SUITECUT_LOCATOR_PICK_TEST_INCLUDE
#endif
};
static std::string Capture(const std::string&json,int backend=913){auto node=CefDictionaryValue::Create();node->SetInt("backendNodeId",backend);node->SetInt("nodeType",1);auto element=CefDictionaryValue::Create();element->SetString("type","node");element->SetDictionary("value",node);auto text=CefDictionaryValue::Create();text->SetString("type","string");text->SetString("value",json);auto values=CefListValue::Create();values->SetDictionary(0,element);values->SetDictionary(1,text);auto deep=CefDictionaryValue::Create();deep->SetString("type","array");deep->SetList("value",values);auto remote=CefDictionaryValue::Create();remote->SetString("subtype","array");remote->SetDictionary("deepSerializedValue",deep);auto result=CefDictionaryValue::Create();result->SetDictionary("result",remote);return JSON(result);}
static const char*observed=R"({"candidates":[{"kind":"path","query":[{"engine":"css","selector":"html > button"}]},{"kind":"role","query":[{"engine":"role","role":"button","name":{"text":"Go","exact":true}}]},{"kind":"testId","query":[{"engine":"testId","attribute":"data-testid","text":"go","exact":true}]}],"omitted":[{"kind":"pointPicking","reason":"No actual pipeline"}]})";
static const char*selected=R"({"result":{"subtype":"node","objectId":"actual-utility-node"}})";
[[maybe_unused]] static void Observe(Harness&h){h.Ack("query",selected);h.Ack("Runtime.callFunctionOn",Capture(observed));}
#ifndef SUITECUT_LOCATOR_SUGGESTION_TEST_NO_MAIN
int main(){if(!LoadSuiteCutCEFForTest())return 1;assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
 {Harness h;h.Start();Observe(h);const auto fn=h.calls.front().params->GetString("functionDeclaration").ToString();assert(fn.find("testId")<fn.rfind("html > button"));h.Ack("Runtime.callFunctionOn",Capture(R"({"results":[{"matches":1,"same":true},{"matches":1,"same":true},{"matches":1,"same":true}]})"));assert(h.errors.empty()&&h.responses.size()==1);const auto out=Parse(h.responses[0]);assert(out->GetList("suggestions")->GetDictionary(0)->GetString("kind")=="testId"&&out->GetDictionary("provenance")->GetInt("backendNodeId")==913&&!out->GetBool("pointPicking"));assert(h.selectors.PhysicalSize()==1&&!h.releases.empty());h.Drain();assert(!h.selectors.Size()&&!h.selectors.PhysicalSize());++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {Harness h;h.Start();Observe(h);h.Ack("Runtime.callFunctionOn",Capture(R"({"results":[{"matches":2,"same":false},{"matches":1,"same":false},{"matches":1,"same":true}]})"));const auto out=Parse(h.responses[0]);assert(out->GetList("suggestions")->GetSize()==1&&out->GetList("suggestions")->GetDictionary(0)->GetString("kind")=="path"&&out->GetList("diagnostics")->GetSize()==3);h.Drain();++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {Harness h;h.Start(true);assert(!h.queries&&h.calls.front().params->GetString("objectId")=="actual-retained-node");h.Ack("DOM.describeNode",R"({"node":{"backendNodeId":913}})");assert(h.calls.front().params->GetInt("executionContextId")==5);h.Ack("DOM.resolveNode",R"({"object":{"subtype":"node","objectId":"actual-adopted-node"}})");assert(h.calls.front().params->GetString("objectId")=="actual-adopted-node");h.Ack("Runtime.callFunctionOn",Capture(observed));h.Ack("Runtime.callFunctionOn",Capture(R"({"results":[{"matches":1,"same":true},{"matches":1,"same":true},{"matches":1,"same":true}]})"));assert(Parse(h.responses[0])->GetDictionary("provenance")->GetInt("retainedHandle")==17);h.Drain();++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 for(int change=0;change<4;++change){Harness h;h.Start();Observe(h);if(change==0)++h.documents["main"].context_generation;else if(change==1)h.documents["main"].utility_unique="replacement";else if(change==2)h.documents.erase("main");else h.now=10;h.Ack("Runtime.callFunctionOn",Capture("{}"));assert(h.responses.empty()&&h.errors.size()==1);h.Drain();++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {Harness h;h.Start(true);h.handles_.erase(17);assert(h.selectors.PhysicalSize()==1);h.Complete(1,false,"Actual author cancel");assert(h.selectors.PhysicalSize()==1);h.Ack("DOM.describeNode",R"({"node":{"backendNodeId":913}})");assert(h.calls.empty()&&h.responses.empty());h.Drain();assert(!h.selectors.PhysicalSize());++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {Harness h;h.Start();Observe(h);h.Ack("Runtime.callFunctionOn",Capture(R"({"results":[{"matches":1,"same":true},{"matches":1,"same":true},{"matches":1,"same":true}]})",914));assert(h.responses.empty()&&h.errors[0].find("backend identity")!=std::string::npos);h.Drain();++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {Harness h;h.Start();Observe(h);h.Ack("Runtime.callFunctionOn",Capture(R"({"results":[{"matches":2,"same":true}]})"));assert(h.responses.empty()&&!h.errors.empty());h.Drain();++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {Harness h;h.Start();Observe(h);h.Complete(1,false,"cancel");assert(h.selectors.PhysicalSize()==1);h.Ack("Runtime.callFunctionOn",Capture("{}"));h.Drain();assert(h.responses.empty()&&!h.selectors.PhysicalSize());++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {Harness h;h.Start();Observe(h);h.hold_publication=true;h.Ack("Runtime.callFunctionOn",Capture(R"({"results":[{"matches":1,"same":true},{"matches":1,"same":true},{"matches":1,"same":true}]})"));assert(!h.held.empty()&&h.responses.empty());++h.documents["main"].context_generation;h.hold_publication=false;h.Complete(1,true,"",h.held);assert(h.responses.empty()&&h.errors.size()==1);h.Drain();++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 {std::vector<suitecut::LocatorSuggestionPlan::Candidate>out;std::string error;assert(!suitecut::LocatorSuggestionPlan::Read(Parse(R"({"candidates":[{"kind":"role","query":[{"engine":"custom"}]}]})"),out,error));assert(suitecut::LocatorSuggestionPlan::CSS("a\"b") == "\\61 \\22 \\62 ");++groups;std::cout<<"completed controlled group "<<groups<<std::endl;}
 std::cout<<"native locator suggestion "<<groups<<" controlled groups passed\n";
}

#endif
