// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "operation_registry.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
#include "locator_normalization_metadata.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "dom_query.h"
#include "locator_normalization.h"
#include "devtools_utf16.h"
struct Harness {
 using Dict=CefRefPtr<CefDictionaryValue>;using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;using Callback=std::function<void(bool,const std::string&)>;
 SuiteCutOperationRegistry operations_;double now=0;std::string handle_namespace_="actualControlledSource";
 std::map<std::string,SnapshotReferenceDocument> documents{{"main",{"main","","actualLoader","mainUnique","utilityUnique",4,5,1}}};
 std::map<int,std::string> locator_waits_;std::vector<std::string> errors;std::vector<std::string> responses;std::vector<Callback> callbacks;std::vector<Dict> submitted;
 bool transfer=true;int retries=0,query_calls=0;std::string last_query_body;
 suitecut::SelectorWaitControls selectors{[this]{return now;},[this](const auto&error){errors.push_back(error);}};
 double Now()const{return now;}
 std::optional<SnapshotReferenceDocument> ReferenceDocument(const std::string& frame)const{const auto found=documents.find(frame);return found==documents.end()?std::nullopt:std::optional(found->second);}
 void Complete(int owner,bool ok,const std::string& error,const std::string& json=""){if(ok)responses.push_back(json);else errors.push_back(error);operations_.Finish(owner);}
 bool ReferenceTemporaryGroups(int){return transfer;}
 std::string Json(Dict value){auto item=CefValue::Create();item->SetDictionary(value);return CefWriteJSON(item,JSON_WRITER_DEFAULT).ToString();}
 void DevTools(int,const std::string& method,Dict params,Callback callback,const std::string& session,bool retained,size_t limit,bool lossless){assert(method=="Runtime.callFunctionOn"&&retained&&limit==262144&&lossless&&session.empty());assert(params->GetString("objectId")=="actualSelectedObject");assert(params->GetDictionary("serializationOptions")->GetString("serialization")=="deep");submitted.push_back(params->Copy(false));callbacks.push_back(std::move(callback));}
 std::string FrameId(Dict message){return message->HasKey("frame")?message->GetString("frame").ToString():"main";}
 suitecut::SelectorWaitControls& SelectorWaits(){return selectors;}
 void ResolveFramePath(int,uint64_t,Dict,size_t,const std::string&,std::function<void(Dict)>){assert(false);}
 void WithFrame(int,const std::string& frame,std::function<void(int)> done,bool utility){assert(utility&&frame=="main");done(5);}
 void QueryInspect(int,Dict,const std::string& body,bool utility,bool by_value,Callback callback){assert(utility&&!by_value);last_query_body=body;++query_calls;callbacks.push_back(std::move(callback));}
 void RetryContext(int){++retries;}
 uint64_t Start(){const auto entry=operations_.Begin(1,now,10);assert(entry);return entry->generation;}
 Dict Message(){auto message=CefDictionaryValue::Create();auto query=CefListValue::Create();auto step=CefDictionaryValue::Create();step->SetString("engine","css");step->SetString("selector","button");query->SetDictionary(0,step);message->SetList("query",query);return message;}
 void Ack(const std::string& json,bool ok=true){assert(!callbacks.empty());auto callback=std::move(callbacks.front());callbacks.erase(callbacks.begin());callback(ok,json);}
#include "locator_normalization_controller.inc"
};
std::string Capture(){return R"JSON({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[{"type":"node","value":{"nodeType":1,"backendNodeId":913}},{"type":"string","value":"{\"kind\":\"id\",\"query\":[{\"engine\":\"css\",\"selector\":\"#actual\"}]}"}]}}})JSON";}
const char* selected=R"JSON({"result":{"subtype":"node","objectId":"actualSelectedObject"}})JSON";
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
 {Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());assert(h.query_calls==1&&h.last_query_body.find("nodes.length>1")!=std::string::npos);h.Ack(selected);assert(h.submitted.size()==1);h.Ack(Capture());assert(h.errors.empty()&&h.responses.size()==1);auto result=CefParseJSON(h.responses[0],JSON_PARSER_RFC)->GetDictionary();auto provenance=result->GetDictionary("provenance");assert(provenance->GetInt("backendNodeId")==913&&provenance->GetString("frame")=="main"&&provenance->GetString("mainIdentity")=="mainUnique"&&provenance->GetString("documentGeneration")=="1"&&result->GetString("metadataJSON").ToString().find("#actual")!=std::string::npos);}
 {Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());h.now=10;h.Ack(selected);assert(h.submitted.empty()&&h.responses.empty()&&h.errors.size()==1&&h.errors[0]=="Native operation timed out");}
 {Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());h.Ack(selected);h.now=10;h.Ack(Capture());assert(h.responses.empty()&&h.errors.size()==1&&h.errors[0]=="Native operation timed out");}
 for(const bool main:{true,false}){Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());h.Ack(selected);if(main)h.documents["main"].main_unique="changedUniqueSameNumeric";else ++h.documents["main"].context_generation;h.Ack(Capture());assert(h.responses.empty()&&h.errors.size()==1&&h.errors[0].find("document changed")!=std::string::npos);}
 {Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());h.Ack(selected);h.operations_.Finish(1);h.Start();h.Ack(Capture());assert(h.responses.empty()&&h.errors.empty()&&h.operations_.Contains(1));}
 {Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());h.Ack(R"JSON({"result":{"type":"undefined"}})JSON");assert(h.retries==1&&h.submitted.empty()&&h.errors.empty());}
 {Harness h;h.Start();h.transfer=false;h.BeginLocatorNormalization(1,h.Message());h.Ack(selected);assert(h.submitted.empty()&&h.errors.size()==1&&h.errors[0].find("temporary-group ownership")!=std::string::npos);}
 {Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());h.Ack(selected);h.Ack("{}",false);assert(h.responses.empty()&&h.errors.size()==1);}
 {Harness h;h.Start();auto message=h.Message();message->SetString("nativeQueryNodes","invented");h.BeginLocatorNormalization(1,message);assert(!h.query_calls&&h.errors.size()==1);}
 {Harness h;h.Start();h.BeginLocatorNormalization(1,h.Message());h.Ack(selected);h.Ack(R"JSON({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[]}}})JSON");assert(h.responses.empty()&&h.errors.size()==1);}
 std::cout<<"PASS11 exact private Source normalization controller original deadlines/full context pins/reused owner guards/native provenance/control failure groups; no compiled browser pipeline claim\n";
}
