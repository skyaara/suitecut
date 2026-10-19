// SPDX-License-Identifier: MIT
// Fixed native serializer controller, controlled Inspector receipts and genuine
// production SelectorWait physical/group leases; no browser parity claim.
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
#include "native_aria_json.h"
#include "operation_registry.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
struct Harness {
 using Dict=CefRefPtr<CefDictionaryValue>;using DevToolsCallback=std::function<void(bool,const std::string&)>;
 using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
 struct Call{std::string method;Dict params;DevToolsCallback done;};
 SuiteCutOperationRegistry operations_;double now=0;
 SnapshotReferenceDocument document{"main","","loader","mainUnique","utilityUnique",4,5,1};
 std::vector<Call> calls;std::vector<std::function<void()>> releases;
 std::map<int,std::set<std::string>> evaluation_groups_;
 std::vector<std::string> responses,errors;int retries=0,untracked_releases=0;bool hold_publication=false;std::string held;
 suitecut::SelectorWaitControls selectors{[this]{return now;},[this](const std::string& error){errors.push_back(error);},[this](int,const std::string&,const std::string&,std::function<void()> done){releases.push_back(std::move(done));}};
 static constexpr size_t kMaxResponseBytes=1048576;
 double Now()const{return now;}
 std::optional<SnapshotReferenceDocument> ReferenceDocument(const std::string& frame)const{return frame==document.frame?std::optional(document):std::nullopt;}
 std::string FrameId(Dict config){return config->HasKey("frame")?config->GetString("frame").ToString():"main";}
 std::string Json(Dict dict){auto value=CefValue::Create();value->SetDictionary(dict);return CefWriteJSON(value,JSON_WRITER_DEFAULT);}
 suitecut::SelectorWaitControls& SelectorWaits(){return selectors;}
 void Complete(int id,bool ok,const std::string& error="",const std::string& json=""){
  if(ok)if(const auto found=aria_json_work_.find(id);found!=aria_json_work_.end())if(!AriaJSONCurrent(found->second))return;
  if(!operations_.Contains(id))return;
  if(ok&&hold_publication){held=json;return;}
  if(ok)responses.push_back(json);else errors.push_back(error);
  operations_.Finish(id);selectors.Cancel(id);aria_json_work_.erase(id);aria_json_operations_.erase(id);
  if(const auto found=evaluation_groups_.find(id);found!=evaluation_groups_.end()){untracked_releases+=static_cast<int>(found->second.size());evaluation_groups_.erase(found);}
 }
 bool PrepareSnapshotReferenceScope(int,Dict){return true;}
 void ResolveFramePath(int,uint64_t,Dict,size_t,const std::string&,std::function<void(Dict)>){assert(false);}
 void WithFrame(int,const std::string&,std::function<void(int)> done,bool utility){assert(utility);done(document.utility);}
 bool ReferenceTemporaryGroups(int owner){const auto groups=evaluation_groups_.find(owner);if(groups==evaluation_groups_.end())return true;for(const auto& session:groups->second)if(!selectors.Group(owner,"suitecut-automation-"+std::to_string(owner),session))return false;evaluation_groups_.erase(groups);return true;}
 void DevTools(int owner,const std::string& method,Dict params,DevToolsCallback done,const std::string& session="",bool=false,size_t=1048576,bool=false){
  const auto found=aria_json_work_.find(owner);if(found!=aria_json_work_.end()&&!AriaJSONCurrent(found->second))return;
  const auto entry=operations_.Get(owner);assert(entry);assert(selectors.Group(owner,"suitecut-automation-"+std::to_string(owner),session));assert(ReferenceTemporaryGroups(owner));auto physical=selectors.Begin(owner);assert(physical);
  calls.push_back({method,params->Copy(false),[this,owner,generation=entry->generation,physical,done=std::move(done)](bool ok,const std::string& json)mutable{physical->Settle();if(operations_.IsCurrent(owner,generation)&&!selectors.Retrying(owner))done(ok,json);}});
 }
 void CaptureDOMSemantic(int owner,Dict,std::function<void(const std::string&,const std::string&)>,std::function<void(const std::string&)> done){evaluation_groups_[owner].insert(document.session);DevTools(owner,"Runtime.evaluate",CefDictionaryValue::Create(),[done](bool,const std::string&){done("actual-native-semantic-capture");});}
 void RetryContext(int owner){++retries;selectors.Pause(owner);}
 uint64_t Start(){const auto entry=operations_.Begin(1,now,10);assert(entry);return entry->generation;}
 Dict Config(){auto config=CefDictionaryValue::Create();config->SetString("frame","main");return config;}
 void Begin(Dict config=nullptr){AriaJSONAction(1,Start(),config?config:Config());}
 void Ack(const std::string& method,const std::string& json,bool ok=true){assert(!calls.empty()&&calls.front().method==method);auto done=std::move(calls.front().done);calls.erase(calls.begin());done(ok,json);}
 void Captured(){Ack("Runtime.evaluate","{}");}
 void Drain(){while(!releases.empty()){auto done=std::move(releases.front());releases.erase(releases.begin());done();}}
#include "aria_json.inc"
};
constexpr auto actual=R"({"result":{"value":{"count":1,"json":"[{\"role\":\"button\",\"name\":\"Actual\"}]"}}})";
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int checks=0;
 {Harness h;h.Begin();h.Captured();assert(h.calls.front().params->GetString("objectId")=="actual-native-semantic-capture");h.Ack("Runtime.callFunctionOn",actual);assert(h.errors.empty()&&h.responses.size()==1&&h.aria_json_work_.empty());h.Drain();assert(!h.selectors.PhysicalSize()&&!h.selectors.Size()&&h.untracked_releases==0);++checks;}
 {Harness h;auto config=h.Config();config->SetInt("snapshotDepth",1);config->SetBool("accessibilityBoxes",true);h.Begin(config);h.Captured();const auto body=h.calls.front().params->GetString("functionDeclaration").ToString();assert(body.find("boxes:true,depth:1")!=std::string::npos);h.Ack("Runtime.callFunctionOn",actual);h.Drain();++checks;}
 {Harness h;h.Begin();h.Captured();h.Ack("Runtime.callFunctionOn",R"({"result":{"value":{"count":0}}})");assert(h.responses.empty()&&h.errors.empty()&&h.retries==1&&h.aria_json_work_.empty());h.Drain();assert(h.selectors.Ready(1));h.Complete(1,false,"actual caller cancellation");++checks;}
 {Harness h;h.Begin();assert(h.evaluation_groups_.empty()&&h.selectors.PhysicalSize()==1);h.Complete(1,false,"actual first-capture abort");assert(h.releases.empty()&&h.selectors.PhysicalSize()==1&&h.untracked_releases==0);h.Captured();h.Drain();assert(h.calls.empty()&&h.responses.empty()&&!h.selectors.PhysicalSize());++checks;}
 for(int changed=0;changed<4;++changed){Harness h;h.Begin();h.Captured();if(changed==0)h.now=10;else if(changed==1)h.document.main_unique="replacement-same-numeric";else if(changed==2)h.document.utility_unique="replacement-same-numeric";else ++h.document.context_generation;h.Ack("Runtime.callFunctionOn",actual);assert(h.responses.empty()&&h.errors.size()==1);h.Drain();}++checks;
 for(bool expired:{false,true}){Harness h;h.Begin();h.Captured();h.hold_publication=true;h.Ack("Runtime.callFunctionOn",actual);assert(h.aria_json_work_.size()==1&&h.responses.empty());if(expired)h.now=10;else ++h.document.context_generation;h.hold_publication=false;h.Complete(1,true,"",h.held);assert(h.responses.empty()&&h.errors.size()==1&&h.aria_json_work_.empty());h.Drain();}++checks;
 {Harness h;h.Begin();h.Captured();h.hold_publication=true;h.Ack("Runtime.callFunctionOn",actual);h.hold_publication=false;h.Complete(1,true,"",h.held);h.Drain();assert(h.responses.size()==1&&h.errors.empty());++checks;}
 for(const auto invalid:{R"({"result":{"value":{"count":2,"json":"[]"}}})",R"({"result":{"value":{"count":1,"json":"{\"__suitecutNativeUTF16Scalar\":\"d800\"}"}}})",R"({"exceptionDetails":{"exception":{"description":"Actual selector failure"}}})"}){Harness h;h.Begin();h.Captured();h.Ack("Runtime.callFunctionOn",invalid);assert(h.responses.empty()&&h.errors.size()==1&&h.retries==0);h.Drain();}++checks;
 for(int invalid=0;invalid<3;++invalid){Harness h;auto config=h.Config();if(invalid==0)config->SetInt("snapshotDepth",65);else if(invalid==1)config->SetString("accessibilityBoxes","author-string");else config->SetInt("nativeHandle",17);h.Begin(config);assert(h.calls.empty()&&h.errors.size()==1);h.Drain();}++checks;
 {Harness h;h.Begin();h.Captured();h.Ack("Runtime.callFunctionOn",R"({"result":{"value":{"count":1,"json":"[{\"role\":\"textbox\",\"name\":\"\\ud800\",\"text\":\"{\\\"__suitecutNativeUTF16Scalar\\\":\\\"d800\\\"}\"}]"}}})");assert(h.responses.size()==1&&h.errors.empty());auto result=CefParseJSON(h.responses[0],JSON_PARSER_RFC)->GetDictionary();assert(result->GetString("snapshotJSON").ToString().find("\\ud800")!=std::string::npos);h.Drain();++checks;}
 std::cout<<"Native structured accessibility Source include "<<checks<<" controlled groups PASS; actual compiled browser pending\n";
}
