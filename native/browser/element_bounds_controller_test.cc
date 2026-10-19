// SPDX-License-Identifier: MIT
// Controlled receipts exercise the actual Source include, not a second geometry
// implementation. Physical/group lease semantics use the production controller.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "dictionary_number.h"
#include "devtools_utf16.h"
#include "element_bounds_geometry.h"
#include "element_bounds_reply.h"
#include "operation_registry.h"
#include "query_node_transfer.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
struct Harness {
 using Dict=CefRefPtr<CefDictionaryValue>;using DevToolsCallback=std::function<void(bool,const std::string&)>;
 using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
 struct Frame{std::string parent,session;SnapshotReferenceDocument document;};
 struct Handle{std::string node_id,frame_id;};
 struct Call{std::string method,session;Dict params;DevToolsCallback done;};
 SuiteCutOperationRegistry operations_;double now=0;std::string main_frame_id_="main";
 std::map<std::string,Frame> frame_states_{{"main",{"","",{"main","","loader","mainUnique","utilityUnique",4,5,1}}}};
 std::map<std::string,std::string> sessions_;
 std::map<int,Handle> handles_;
 std::map<int,std::set<std::string>> evaluation_groups_;
 std::map<int,std::string> operation_frames_,operation_sessions_,locator_waits_;
 std::map<int,std::function<void()>> context_retries_;
 std::vector<Call> calls;std::vector<DevToolsCallback> queries;
 std::vector<std::function<void()>> releases;
 bool hold_publication=false;std::string held_json;
 std::vector<std::string> errors,responses;int retries=0,transfers=0,query_count=0,untracked_releases=0;std::string body;
 suitecut::SelectorWaitControls selectors{[this]{return now;},[this](const std::string& error){errors.push_back(error);},[this](int,const std::string&,const std::string&,std::function<void()> done){releases.push_back(std::move(done));}};
 double Now()const{return now;}
 std::optional<SnapshotReferenceDocument> ReferenceDocument(const std::string& frame)const{const auto it=frame_states_.find(frame);return it==frame_states_.end()?std::nullopt:std::optional(it->second.document);}
 void Complete(int id,bool ok,const std::string& error,const std::string& json=""){if(ok)if(const auto bounds=element_bounds_work_.find(id);bounds!=element_bounds_work_.end()){const auto work=bounds->second;if(!ElementBoundsCurrent(work))return;}if(!operations_.Contains(id))return;if(ok&&hold_publication){held_json=json;return;}if(ok)responses.push_back(json);else errors.push_back(error);operations_.Finish(id);selectors.Cancel(id);element_bounds_operations_.erase(id);element_bounds_work_.erase(id);if(const auto groups=evaluation_groups_.find(id);groups!=evaluation_groups_.end()){untracked_releases+=static_cast<int>(groups->second.size());evaluation_groups_.erase(groups);}}
 suitecut::SelectorWaitControls& SelectorWaits(){return selectors;}
 std::string FrameId(Dict config){return config->HasKey("frame")?config->GetString("frame").ToString():main_frame_id_;}
 std::string MissingHandle(int){return "Actual missing retained handle";}
 std::string Json(Dict value){auto wrapper=CefValue::Create();wrapper->SetDictionary(value);return CefWriteJSON(wrapper,JSON_WRITER_DEFAULT);}
 bool PrepareSnapshotReferenceScope(int,Dict){return true;}
 bool ReferenceTemporaryGroups(int owner){
  ++transfers;if(!selectors.Contains(owner))return false;
  const auto groups=evaluation_groups_.find(owner);if(groups==evaluation_groups_.end())return true;
  for(const auto& session:groups->second)if(!selectors.Group(owner,"suitecut-automation-"+std::to_string(owner),session))return false;
  evaluation_groups_.erase(groups);return true;
 }
 void WithFrame(int,const std::string& frame,std::function<void(int)> done,bool utility){assert(utility);done(frame_states_.at(frame).document.utility);}
 void ResolveFramePath(int,uint64_t,Dict,size_t,const std::string&,std::function<void(Dict)>){assert(false);}
 std::string QueryDiagnosticExpression(Dict,const std::string&){return "'controlled actual duplicate diagnostics'";}
 void QueryInspect(int owner,Dict,const std::string& code,bool utility,bool by_value,DevToolsCallback callback){
  assert(utility&&!by_value);body=code;++query_count;
  const auto session=operation_sessions_.at(owner);evaluation_groups_[owner].insert(session);
  auto params=CefDictionaryValue::Create();params->SetString("objectGroup","suitecut-automation-"+std::to_string(owner));
  DevTools(owner,"Runtime.evaluate",params,std::move(callback),session);
 }
 void RetryContext(int){++retries;}
 void DevTools(int owner,const std::string& method,Dict params,DevToolsCallback callback,const std::string& session){
  const auto entry=operations_.Get(owner);assert(entry);
  assert(selectors.Group(owner,"suitecut-automation-"+std::to_string(owner),session));
  if(element_bounds_operations_.contains(owner)){assert(ReferenceTemporaryGroups(owner));}
  auto physical=selectors.Begin(owner);assert(physical);
  calls.push_back({method,session,params->Copy(false),[this,owner,generation=entry->generation,physical,callback=std::move(callback)](bool ok,const std::string& json)mutable{physical->Settle();if(operations_.IsCurrent(owner,generation)&&!selectors.Retrying(owner))callback(ok,json);}});
 }
 uint64_t Start(){const auto entry=operations_.Begin(1,now,10);assert(entry);return entry->generation;}
 Dict Config(const std::string& frame="main"){auto config=CefDictionaryValue::Create();config->SetString("frame",frame);return config;}
 void Begin(const std::string& frame="main"){ElementBoundsAction(1,Start(),Config(frame));}
 void Selected(){Ack("Runtime.evaluate",R"({"result":{"type":"object","subtype":"node","objectId":"actual-utility-selected-node"}})");}
 void Ack(const std::string& method,const std::string& json,bool ok=true){assert(!calls.empty()&&calls.front().method==method);auto done=std::move(calls.front().done);calls.erase(calls.begin());done(ok,json);}
 void Drain(){while(!releases.empty()){auto done=std::move(releases.front());releases.erase(releases.begin());done();}}
 void Child(){frame_states_["child"]={"main","actualOOPIF",{"child","actualOOPIF","childLoader","childMainUnique","childUtilityUnique",8,9,2}};sessions_["actualOOPIF"]="child";}
#include "element_geometry.inc"
};
const char* visible=R"({"result":{"type":"boolean","value":true}})";
const char* hidden=R"({"result":{"type":"boolean","value":false}})";
const char* model=R"({"model":{"border":[20,30,80,30,80,55,20,55]}})";
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int checks=0;
 {Harness h;h.Begin();assert(h.body.find("throw new Error")!=std::string::npos);h.Selected();assert(h.calls.front().params->GetString("objectId")=="actual-utility-selected-node");h.Ack("Runtime.callFunctionOn",visible);h.Ack("DOM.getBoxModel",model);assert(h.errors.empty()&&h.responses.size()==1);auto result=CefParseJSON(h.responses[0],JSON_PARSER_RFC)->GetDictionary();assert(result->GetDouble("x")==20&&result->GetDouble("y")==30&&result->GetDouble("width")==60&&result->GetDouble("height")==25);assert(h.selectors.PhysicalSize()==1&&h.releases.size()==1);h.Drain();assert(!h.selectors.Size()&&!h.selectors.PhysicalSize());++checks;}
 {Harness h;h.Child();h.Begin("child");h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.Ack("DOM.getBoxModel",model);assert(h.calls.front().session=="actualOOPIF"&&h.calls.front().params->GetInt("contextId")==9);h.Ack("Runtime.evaluate",R"({"result":{"value":{"width":160,"height":140}}})");assert(h.calls.front().session.empty()&&h.calls.front().params->GetString("frameId")=="child");h.Ack("DOM.getFrameOwner",R"({"backendNodeId":782})");assert(h.calls.front().params->GetInt("backendNodeId")==782);h.Ack("DOM.getBoxModel",R"({"model":{"content":[107,57,267,57,267,197,107,197]}})");assert(h.errors.empty());auto result=CefParseJSON(h.responses.at(0),JSON_PARSER_RFC)->GetDictionary();assert(result->GetDouble("x")==127&&result->GetDouble("y")==87);h.Drain();++checks;}
 {Harness h;h.handles_[17]={"actual-original-main-node","main"};auto config=h.Config();config->SetInt("nativeHandle",17);h.ElementBoundsAction(1,h.Start(),config);assert(!h.query_count&&h.calls.front().params->GetString("objectId")=="actual-original-main-node");h.Ack("DOM.describeNode",R"({"node":{"backendNodeId":654}})");assert(h.calls.front().params->GetInt("backendNodeId")==654&&h.calls.front().params->GetInt("executionContextId")==5);h.Ack("DOM.resolveNode",R"({"object":{"subtype":"node","objectId":"actual-adopted-utility-node"}})");assert(h.calls.front().params->GetString("objectId")=="actual-adopted-utility-node");h.Ack("Runtime.callFunctionOn",visible);h.Ack("DOM.getBoxModel",model);assert(h.errors.empty()&&h.responses.size()==1&&!h.query_count);h.Drain();++checks;}
 {Harness h;h.Begin();h.now=10;h.Selected();assert(h.calls.empty()&&h.errors.size()==1&&h.errors[0].find("timed out")!=std::string::npos);h.Drain();++checks;}
 {Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.now=10;h.Ack("DOM.getBoxModel",model);assert(h.responses.empty()&&h.errors.size()==1&&h.errors[0].find("timed out")!=std::string::npos);h.Drain();++checks;}
 for(int changed=0;changed<3;++changed){Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",visible);auto& document=h.frame_states_["main"].document;if(changed==0)document.main_unique="replacement-same-numeric";else if(changed==1)document.utility_unique="replacement-utility-same-numeric";else ++document.context_generation;h.Ack("DOM.getBoxModel",model);assert(h.responses.empty()&&h.errors.size()==1&&h.errors[0].find("document changed")!=std::string::npos);h.Drain();}++checks;
 {Harness h;h.Child();h.Begin("child");h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.Ack("DOM.getBoxModel",model);++h.frame_states_["main"].document.context_generation;h.Ack("Runtime.evaluate",R"({"result":{"value":{"width":160,"height":140}}})");assert(h.responses.empty()&&h.calls.empty()&&h.errors.size()==1);h.Drain();++checks;}
 {Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",hidden);assert(h.responses==std::vector<std::string>{"null"}&&h.calls.empty());h.Drain();++checks;}
 {Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.Ack("DOM.getBoxModel",R"({"message":"Actual unrelated protocol failure","code":-32601})",false);assert(h.responses.empty()&&h.errors.size()==1&&h.calls.empty());h.Drain();++checks;}
 for(bool now_visible:{false,true}){Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.Ack("DOM.getBoxModel",R"({"message":"Could not compute box model.","code":-32000})",false);h.Ack("Runtime.callFunctionOn",now_visible?visible:hidden);if(now_visible)assert(h.responses.empty()&&h.errors.size()==1);else assert(h.errors.empty()&&h.responses==std::vector<std::string>{"null"});h.Drain();}++checks;
 {Harness h;h.Begin();h.Selected();h.Complete(1,false,"actual caller cancellation");assert(h.selectors.PhysicalSize()==1&&h.releases.empty());h.Start();h.Ack("Runtime.callFunctionOn",visible);assert(h.errors.size()==1&&h.responses.empty()&&h.operations_.Contains(1));assert(h.selectors.PhysicalSize()==1&&h.releases.size()==1);h.Drain();assert(!h.selectors.PhysicalSize());++checks;}
 {Harness h;h.Begin();h.Ack("Runtime.evaluate",R"({"result":{"type":"undefined"}})");assert(h.retries==1&&h.calls.empty()&&h.errors.empty());++checks;}
 {Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.Ack("DOM.getBoxModel",R"({"model":{"border":[0,0,1,"author-string",1,1,0,1]}})");assert(h.responses.empty()&&h.errors.size()==1);h.Drain();++checks;}
 {Harness h;h.Begin();assert(h.evaluation_groups_.empty()&&h.selectors.PhysicalSize()==1);h.Complete(1,false,"actual cancellation while first selection is physically held");assert(h.untracked_releases==0&&h.releases.empty()&&h.selectors.PhysicalSize()==1&&h.selectors.Size()==1);h.Selected();assert(h.responses.empty()&&h.calls.empty()&&h.errors.size()==1&&h.untracked_releases==0);assert(h.selectors.PhysicalSize()==1&&h.releases.size()==1);h.Drain();assert(!h.selectors.Size()&&!h.selectors.PhysicalSize());++checks;}
 for(const bool expired:{false,true}){Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.hold_publication=true;h.Ack("DOM.getBoxModel",model);assert(h.responses.empty()&&h.errors.empty()&&!h.held_json.empty()&&h.element_bounds_work_.size()==1&&h.calls.empty());if(expired)h.now=10;else h.frame_states_["main"].document.utility_unique="replacement-while-native-publication-held";h.hold_publication=false;h.Complete(1,true,"",h.held_json);assert(h.responses.empty()&&h.errors.size()==1&&h.errors[0].find(expired?"timed out":"document changed")!=std::string::npos&&h.element_bounds_work_.empty());h.Drain();assert(!h.selectors.PhysicalSize()&&h.untracked_releases==0);}++checks;
 {Harness h;h.Begin();h.Selected();h.Ack("Runtime.callFunctionOn",visible);h.hold_publication=true;h.Ack("DOM.getBoxModel",model);assert(h.responses.empty()&&h.element_bounds_work_.size()==1);h.hold_publication=false;h.Complete(1,true,"",h.held_json);assert(h.errors.empty()&&h.responses==std::vector<std::string>{h.held_json}&&h.element_bounds_work_.empty());h.Drain();++checks;}
 std::cout<<"Native selected/fixed geometry Source controller "<<checks<<" controlled groups PASS; actual compiled browser pipeline pending\n";
}
