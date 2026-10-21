// SPDX-License-Identifier: MIT
// Actual Source include with controlled Inspector receipts and production leases.
#include <cassert>
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
#include "devtools_utf16.h"
#include "inbound_string.h"
#include "operation_registry.h"
#include "query_node_transfer.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
struct Harness {
 using Dict=CefRefPtr<CefDictionaryValue>;using DevToolsCallback=std::function<void(bool,const std::string&)>;
 using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
 struct Frame{std::string origin="https://actual.test";SnapshotReferenceDocument document;};
 struct ObjectHandle{std::string object_id,group,frame_id;int context=0;std::string session,node_id,context_identity;uint64_t renderer_epoch=1;};
 struct Call{std::string method,session;Dict params;DevToolsCallback done;};
 SuiteCutOperationRegistry operations_;double now=0;uint64_t native_pristine_renderer_epoch_=1;
 std::map<std::string,Frame> frame_states_{{"main",{"https://actual.test",{"main","","loader","mainUnique","utilityUnique",4,5,1}}}};
 std::map<int,ObjectHandle> handles_;
 std::map<int,std::set<std::string>> evaluation_groups_;
 std::map<int,std::string> operation_frames_,operation_sessions_,locator_waits_;
 std::vector<Call> calls;std::vector<std::function<void()>> releases;
 std::vector<std::string> errors,responses;std::string body,held_json;
 int retries=0,untracked_releases=0;bool hold_publication=false;
 suitecut::SelectorWaitControls selectors{[this]{return now;},[this](const std::string& error){errors.push_back(error);},[this](int,const std::string&,const std::string&,std::function<void()> done){releases.push_back(std::move(done));}};
 double Now()const{return now;}
 std::optional<SnapshotReferenceDocument> ReferenceDocument(const std::string& frame)const{const auto it=frame_states_.find(frame);return it==frame_states_.end()?std::nullopt:std::optional(it->second.document);}
 std::string FrameId(Dict config){return config->HasKey("frame")?config->GetString("frame").ToString():"main";}
 std::string MissingHandle(int){return "Actual missing handle";}
 std::string Json(Dict dict){auto value=CefValue::Create();value->SetDictionary(dict);return CefWriteJSON(value,JSON_WRITER_DEFAULT);}
 suitecut::SelectorWaitControls& SelectorWaits(){return selectors;}
 void Complete(int id,bool ok,const std::string& error="",const std::string& json=""){
  if(ok)if(const auto found=locator_predicate_work_.find(id);found!=locator_predicate_work_.end())if(!LocatorPredicateCurrent(found->second))return;
  if(!operations_.Contains(id))return;
  if(ok&&hold_publication){held_json=json;return;}
  if(ok)responses.push_back(json);else errors.push_back(error);
  operations_.Finish(id);selectors.Cancel(id);locator_predicate_work_.erase(id);locator_predicate_operations_.erase(id);
  if(const auto groups=evaluation_groups_.find(id);groups!=evaluation_groups_.end()){untracked_releases+=static_cast<int>(groups->second.size());evaluation_groups_.erase(groups);}
 }
 bool ReferenceTemporaryGroups(int owner){if(!selectors.Contains(owner))return false;const auto found=evaluation_groups_.find(owner);if(found==evaluation_groups_.end())return true;for(const auto& session:found->second)if(!selectors.Group(owner,"suitecut-automation-"+std::to_string(owner),session))return false;evaluation_groups_.erase(found);return true;}
 void WithFrame(int owner,const std::string& frame,std::function<void(int)> done,bool utility){assert(utility);const auto found=locator_predicate_work_.find(owner);if(found!=locator_predicate_work_.end()&&!LocatorPredicateCurrent(found->second))return;done(frame_states_.at(frame).document.utility);}
 bool PrepareSnapshotReferenceScope(int,Dict){return true;}
 void ResolveFramePath(int,uint64_t,Dict,size_t,const std::string&,std::function<void(Dict)>){assert(false);}
 bool MaterializeConsoleArguments(int,Dict,std::function<void()>){return false;}
 std::string QueryDiagnosticExpression(Dict,const std::string&){return "'strict mode violation: actual duplicate nodes'";}
 void QueryInspect(int owner,Dict,const std::string& code,bool utility,bool by_value,DevToolsCallback done){assert(utility&&!by_value);body=code;evaluation_groups_[owner].insert(operation_sessions_[owner]);auto params=CefDictionaryValue::Create();params->SetString("objectGroup","suitecut-automation-"+std::to_string(owner));DevTools(owner,"Runtime.evaluate",params,std::move(done),operation_sessions_[owner]);}
 void DevTools(int owner,const std::string& method,Dict params,DevToolsCallback done,const std::string& session,bool=false,size_t=0,bool=false){
  const auto found=locator_predicate_work_.find(owner);if(found!=locator_predicate_work_.end()&&!LocatorPredicateCurrent(found->second))return;
  const auto entry=operations_.Get(owner);assert(entry);assert(selectors.Group(owner,"suitecut-automation-"+std::to_string(owner),session));assert(ReferenceTemporaryGroups(owner));auto physical=selectors.Begin(owner);assert(physical);
  calls.push_back({method,session,params->Copy(false),[this,owner,generation=entry->generation,physical,done=std::move(done)](bool ok,const std::string& json)mutable{physical->Settle();if(operations_.IsCurrent(owner,generation)&&!selectors.Retrying(owner))done(ok,json);}});
 }
 static constexpr size_t kMaxResponseBytes=1048576;
 void RetryContext(int owner){++retries;selectors.Pause(owner);}
 Dict Config(){auto config=CefDictionaryValue::Create();config->SetString("frame","main");config->SetString("function","async function(){return true;}");config->SetList("handles",CefListValue::Create());return config;}
 uint64_t Start(){const auto entry=operations_.Begin(1,now,10);assert(entry);return entry->generation;}
 void Begin(Dict config=nullptr){LocatorPredicateAction(1,Start(),config?config:Config());}
 void Ack(const std::string& method,const std::string& json,bool ok=true){assert(!calls.empty()&&calls.front().method==method);auto done=std::move(calls.front().done);calls.erase(calls.begin());done(ok,json);}
 void Selected(const std::string& object="selected-utility-node"){Ack("Runtime.evaluate","{\"result\":{\"type\":\"object\",\"subtype\":\"node\",\"objectId\":\""+object+"\"}}");}
 void Adopt(){Ack("DOM.describeNode",R"({"node":{"backendNodeId":781}})");assert(calls.front().params->GetInt("backendNodeId")==781&&calls.front().params->GetInt("executionContextId")==4);Ack("DOM.resolveNode",R"({"object":{"subtype":"node","objectId":"selected-main-node"}})");}
 void Drain(){while(!releases.empty()){auto done=std::move(releases.front());releases.erase(releases.begin());done();}}
 void AtPredicate(){Begin();Selected();Adopt();assert(calls.front().params->GetString("objectId")=="selected-main-node"&&calls.front().params->GetBool("awaitPromise")&&calls.front().params->GetBool("returnByValue"));}
#include "controllers/dom/locator_predicate.inc"
};
constexpr auto yes=R"({"result":{"type":"boolean","value":true}})";
constexpr auto no=R"({"result":{"type":"boolean","value":false}})";
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int checks=0;
 {Harness h;h.AtPredicate();h.Ack("Runtime.callFunctionOn",yes);assert(h.errors.empty()&&h.responses==std::vector<std::string>{"{}"}&&h.locator_predicate_work_.empty());h.Drain();assert(!h.selectors.PhysicalSize()&&!h.selectors.Size());++checks;}
 {Harness h;h.AtPredicate();h.Ack("Runtime.callFunctionOn",no);assert(h.responses.empty()&&h.errors.empty()&&h.retries==1&&h.locator_predicate_work_.empty()&&h.selectors.Retrying(1));h.Drain();assert(h.selectors.Ready(1));h.selectors.Resume(1);auto config=h.Config();h.LocatorPredicateAction(1,h.operations_.Get(1)->generation,config);h.Selected("genuine-rerendered-node");assert(h.calls.front().params->GetString("objectId")=="genuine-rerendered-node");h.Adopt();h.Ack("Runtime.callFunctionOn",yes);h.Drain();assert(h.responses.size()==1&&h.untracked_releases==0);++checks;}
 {Harness h;h.Begin();h.Ack("Runtime.evaluate",R"({"result":{"type":"undefined"}})");assert(h.retries==1&&h.responses.empty()&&h.errors.empty());h.Complete(1,false,"actual caller cancellation");h.Drain();++checks;}
 {Harness h;h.Begin();h.Ack("Runtime.evaluate",R"({"exceptionDetails":{"exception":{"type":"object","description":"Error: strict mode violation: actual2"}}})");assert(h.calls.empty()&&h.retries==0&&h.responses.size()==1&&h.responses[0].find("strict mode violation")!=std::string::npos);h.Drain();++checks;}
 {Harness h;h.AtPredicate();h.Ack("Runtime.callFunctionOn",R"({"exceptionDetails":{"exception":{"type":"object","description":"Error: Element is not attached; execution context destroyed"}}})");assert(h.retries==0&&h.responses.size()==1);h.Drain();++checks;}
 {Harness h;h.AtPredicate();h.Ack("Runtime.callFunctionOn",R"({"exceptionDetails":{"exception":{"type":"string","value":{"__suitecutNativeUTF16Scalar":"d800"}}}})");assert(h.retries==0&&h.responses.size()==1);auto value=CefParseJSON(h.responses[0],JSON_PARSER_RFC)->GetDictionary();assert(value->GetString("predicateErrorJSON")=="\"\\ud800\"");h.Drain();++checks;}
 {Harness h;h.AtPredicate();h.Ack("Runtime.callFunctionOn",R"({"result":{"type":"object","value":{"__suitecutNativeUTF16Scalar":"d800"}}})");assert(h.responses.empty()&&h.errors.size()==1&&h.retries==0);h.Drain();++checks;}
 for(int changed=0;changed<4;++changed){Harness h;h.AtPredicate();if(changed==0)h.now=10;else if(changed==1)h.frame_states_["main"].document.main_unique="replacement";else if(changed==2)h.frame_states_["main"].document.utility_unique="replacement";else ++h.frame_states_["main"].document.context_generation;h.Ack("Runtime.callFunctionOn",yes);assert(h.responses.empty()&&h.errors.size()==1);h.Drain();}++checks;
 {Harness h;h.Begin();assert(h.evaluation_groups_.empty()&&h.selectors.PhysicalSize()==1);h.Complete(1,false,"actual abort while first selection held");assert(h.calls.size()==1&&h.releases.empty()&&h.untracked_releases==0);h.Selected();assert(h.calls.empty()&&h.responses.empty()&&h.releases.size()==1);h.Drain();assert(!h.selectors.PhysicalSize());++checks;}
 {Harness h;h.AtPredicate();h.Complete(1,false,"actual abort while async author promise held");assert(h.selectors.PhysicalSize()==1&&h.releases.empty()&&h.locator_predicate_work_.empty());h.Ack("Runtime.callFunctionOn",yes);h.Drain();assert(h.responses.empty()&&h.errors.size()==1&&!h.selectors.PhysicalSize()&&h.untracked_releases==0);++checks;}
 for(bool expired:{false,true}){Harness h;h.AtPredicate();h.hold_publication=true;h.Ack("Runtime.callFunctionOn",yes);assert(h.locator_predicate_work_.size()==1&&h.responses.empty());if(expired)h.now=10;else ++h.frame_states_["main"].document.context_generation;h.hold_publication=false;h.Complete(1,true,"",h.held_json);assert(h.responses.empty()&&h.errors.size()==1&&h.locator_predicate_work_.empty());h.Drain();}++checks;
 {Harness h;h.AtPredicate();h.hold_publication=true;h.Ack("Runtime.callFunctionOn",yes);h.hold_publication=false;h.Complete(1,true,"",h.held_json);h.Drain();assert(h.responses.size()==1&&h.errors.empty());++checks;}
 {Harness h;auto config=h.Config();auto ids=config->GetList("handles");ids->SetInt(0,17);h.handles_[17]={"real-main-wrapper","handle-group","main",4,"","real-main-node","mainUnique",1};h.Begin(config);h.Selected();h.Adopt();assert(h.calls.front().params->GetList("arguments")->GetDictionary(0)->GetString("objectId")=="real-main-wrapper");h.Ack("Runtime.callFunctionOn",yes);h.Drain();assert(h.responses.size()==1);++checks;}
 {Harness h;h.frame_states_["sibling"]={"https://actual.test",{"sibling","","siblingLoader","siblingMain","siblingUtility",8,9,2}};auto config=h.Config();config->GetList("handles")->SetInt(0,17);h.handles_[17]={"real-sibling-wrapper","handle-group","sibling",8,"","real-sibling-node","siblingMain",1};h.Begin(config);h.Selected();h.Adopt();assert(h.calls.front().params->GetString("objectId")=="real-sibling-node");h.Ack("DOM.describeNode",R"({"node":{"backendNodeId":781}})");h.Ack("DOM.resolveNode",R"({"object":{"subtype":"node","objectId":"adopted-argument-node"}})");h.Ack("Runtime.callFunctionOn",R"({"result":{"objectId":"native-argument-wrapper"}})");assert(h.calls.front().params->GetList("arguments")->GetDictionary(0)->GetString("objectId")=="native-argument-wrapper");++h.frame_states_["sibling"].document.context_generation;h.Ack("Runtime.callFunctionOn",yes);h.Drain();assert(h.responses.empty()&&h.errors.size()==1);++checks;}
 for(int stale=0;stale<3;++stale){Harness h;auto config=h.Config();config->GetList("handles")->SetInt(0,17);h.handles_[17]={"real-main-wrapper","handle-group","main",4,"","real-main-node",stale==0?"old-unique":"mainUnique",stale==1?0u:1u};if(stale==2)h.handles_.clear();h.Begin(config);assert(h.calls.empty()&&h.errors.size()==1&&h.responses.empty());h.Drain();}++checks;
 {Harness h;h.Begin();const auto stale=h.locator_predicate_work_.at(1);h.Complete(1,false,"actual abort");h.Start();assert(!h.LocatorPredicateCurrent(stale)&&h.operations_.Contains(1));h.Selected();h.Drain();assert(h.responses.empty()&&h.errors.size()==1);++checks;}
 {Harness h;auto config=h.Config();auto ids=config->GetList("handles");for(int i=0;i<64;++i){const auto frame="argument-"+std::to_string(i),unique="unique-"+std::to_string(i);h.frame_states_[frame]={"https://actual.test",{frame,"","actualLoader",unique,"actualUtility",100+i,200+i,1}};ids->SetInt(i,i+1);h.handles_[i+1]={"actual-wrapper","actual-group",frame,100+i,"","actual-node",unique,1};}h.Begin(config);assert(h.calls.empty()&&h.responses.empty()&&h.errors.size()==1&&h.errors[0].find("document limit")!=std::string::npos);h.Drain();++checks;}
 std::cout<<"Native locator predicate actual Source include "<<checks<<" controlled groups PASS; compiled browser proof pending\n";
}
