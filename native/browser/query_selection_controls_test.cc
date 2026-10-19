// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
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
#include "operation_registry.h"
#include "selector_wait_controls.h"
#include "query_node_transfer.h"
#include "devtools_utf16.h"

// Controlled actual-CEF protocol/value seams cover native routing, actual
// response ownership and epoch guards. Live renderer prototypes need live proof.
struct Harness {
  using Dict=CefRefPtr<CefDictionaryValue>;
  static constexpr size_t kMaxResponseBytes=16*1048576;
  struct Frame {int context=17,utility_context=42;std::string loader="loader",session="session";};
  struct Call {std::string method,session;Dict params;bool lossless;std::function<void(bool,const std::string&)> done;};
  SuiteCutOperationRegistry operations_;
  std::map<std::string,Frame> frame_states_{{"frame",{}}};
  std::map<int,std::string> operation_frames_,operation_sessions_;
  std::map<int,std::set<std::string>> evaluation_groups_;
  std::deque<Call> calls;
  std::string delivered,error;
  int retries=0;
  double now=0;
  suitecut::SelectorWaitControls controls{[this]{return now;},[](const std::string&){assert(false);},
    [this](int,const std::string& group,const std::string& session,std::function<void()> settled){auto params=CefDictionaryValue::Create();params->SetString("objectGroup",group);calls.push_back({"Runtime.releaseObjectGroup",session,params,false,[settled=std::move(settled)](bool,const std::string&){settled();}});}};
  bool QueryHasSnapshotReferences(Dict message){return message->GetBool("controlledSnapshotReference");}
  bool QueryNeedsMainWorld(Dict message){return message->GetBool("controlledMainFactory");}
  bool QueryNeedsIsolatedWorld(Dict message){return message->GetBool("controlledUtilityFactory");}
  std::string FrameId(Dict){return "frame";}
  std::string QueryExpression(Dict){return "actual-authored-helper-expression";}
  void Admit(){const auto entry=operations_.Begin(1,now,1000);assert(entry&&controls.Admit(1,entry->generation));}
  Dict Message(){return CefDictionaryValue::Create();}
  void WithFrame(int owner,const std::string& frame,std::function<void(int)> done,bool utility){assert(utility);operation_frames_[owner]=frame;operation_sessions_[owner]=frame_states_.at(frame).session;done(frame_states_.at(frame).utility_context);}
  void Complete(int owner,bool,const std::string& cause=""){if(!operations_.Finish(owner))return;error=cause;controls.Cancel(owner);}
  void RetryContext(int owner){++retries;controls.Pause(owner);}
  void DevTools(int owner,const std::string& method,Dict params,std::function<void(bool,const std::string&)> done,std::optional<std::string> session=std::nullopt,bool retained=false,size_t limit=kMaxResponseBytes,bool lossless=false){
    assert(!retained&&limit==(params->HasKey("serializationOptions")?16*1048576:kMaxResponseBytes));const auto entry=operations_.Get(owner);assert(entry);const auto actual=session.value_or(operation_sessions_[owner]);assert(controls.Group(owner,"suitecut-automation-1",actual));
    if(params->HasKey("objectGroup"))assert(controls.Group(owner,params->GetString("objectGroup"),actual));
    auto physical=controls.Begin(owner);assert(physical);calls.push_back({method,actual,params->Copy(false),lossless,[this,owner,generation=entry->generation,physical,done=std::move(done)](bool ok,const std::string& json){physical->Settle();if(operations_.IsCurrent(owner,generation)&&!controls.Retrying(owner))done(ok,json);}});
  }
  Call Take(const std::string& method){for(auto i=calls.begin();i!=calls.end();++i)if(i->method==method){auto call=std::move(*i);calls.erase(i);return call;}assert(false);return {};}
  void Ack(const std::string& method,const std::string& json="{}",bool ok=true){auto call=Take(method);call.done(ok,json);}
  void Drain(){while(!calls.empty()){auto call=Take("Runtime.releaseObjectGroup");call.done(true,"{}");}}
  bool Start(Dict message,bool utility=false){return QueryBuiltinNodesInWorld(1,message,utility,[this](const std::string& nodes){delivered=nodes;Complete(1,true);});}
#include "query_selection_controller.inc"
};
int main(){cef_api_hash(CEF_API_VERSION,0);
 {Harness h;h.Admit();assert(h.Start(h.Message(),true));auto call=h.Take("Runtime.evaluate");assert(call.lossless&&call.session=="session"&&call.params->GetInt("contextId")==42&&!call.params->GetBool("returnByValue")&&call.params->GetString("expression")=="actual-authored-helper-expression");call.done(true,"{\"result\":{\"type\":\"object\",\"subtype\":\"array\",\"objectId\":\"actual-isolated-array\"}}");assert(h.delivered=="actual-isolated-array"&&h.error.empty());h.Drain();assert(!h.controls.Size()&&!h.controls.PhysicalSize());}
 {Harness h;h.Admit();assert(h.Start(h.Message()));h.Ack("Runtime.evaluate","{\"result\":{\"type\":\"object\",\"subtype\":\"array\",\"objectId\":\"actual-isolated-array\"}}");auto props=h.Take("Runtime.getProperties");assert(props.params->GetString("objectId")=="actual-isolated-array"&&props.lossless);props.done(true,"{\"result\":[{\"name\":\"0\",\"value\":{\"type\":\"object\",\"subtype\":\"node\",\"objectId\":\"actual-isolated-node\"}}]}");auto array=h.Take("Runtime.evaluate");assert(array.params->GetInt("contextId")==17&&array.params->GetString("expression")=="[void 0,]");array.done(true,"{\"result\":{\"type\":\"object\",\"subtype\":\"array\",\"objectId\":\"actual-main-array\"}}");auto describe=h.Take("DOM.describeNode");assert(describe.params->GetString("objectId")=="actual-isolated-node");describe.done(true,"{\"node\":{\"backendNodeId\":381}}");auto resolve=h.Take("DOM.resolveNode");assert(resolve.params->GetInt("backendNodeId")==381&&resolve.params->GetInt("executionContextId")==17&&resolve.params->GetString("objectGroup")=="suitecut-automation-1");resolve.done(true,"{\"object\":{\"type\":\"object\",\"subtype\":\"node\",\"objectId\":\"actual-main-node\"}}");auto append=h.Take("Runtime.callFunctionOn");assert(append.params->GetString("objectId")=="actual-main-array"&&append.params->GetList("arguments")->GetDictionary(0)->GetInt("value")==0&&append.params->GetList("arguments")->GetDictionary(1)->GetString("objectId")=="actual-main-node");append.done(true,"{\"result\":{\"type\":\"undefined\"}}");assert(h.delivered=="actual-main-array"&&h.error.empty());h.Drain();assert(!h.controls.Size());}
 for(const std::string key:{"controlledSnapshotReference","controlledMainFactory","controlledUtilityFactory","nativeRootHandle","nativeSelectorWaitState"}){Harness h;h.Admit();auto message=h.Message();message->SetBool(key,true);assert(!h.Start(message)&&h.calls.empty()&&h.delivered.empty());h.Complete(1,false);assert(!h.controls.Size());}
 for(const std::string mode:{"first","strict","all","callback"}){Harness h;h.Admit();auto message=h.Message();message->SetString("action","create");if(mode=="strict")message->SetBool("strict",true);if(mode=="all")message->SetBool("all",true);if(mode=="callback")message->SetString("queryFunction","actual-user-callback");assert(h.Start(message,true));auto call=h.Take("Runtime.evaluate");const auto expression=call.params->GetString("expression").ToString();assert((expression.find("nodes[0]")!=std::string::npos)==(mode=="first"));h.Complete(1,false);assert(h.controls.PhysicalSize()==1);call.done(true,"{}");h.Drain();}
 for(const std::string field:{"context","utility","loader","session","removed"}){Harness h;h.Admit();assert(h.Start(h.Message()));if(field=="context")++h.frame_states_["frame"].context;else if(field=="utility")++h.frame_states_["frame"].utility_context;else if(field=="loader")h.frame_states_["frame"].loader="new-loader";else if(field=="session")h.frame_states_["frame"].session="new-session";else h.frame_states_.erase("frame");h.Ack("Runtime.evaluate","{\"result\":{\"subtype\":\"array\",\"objectId\":\"stale-array\"}}");assert(h.delivered.empty());if(field=="removed")assert(!h.operations_.Contains(1));else {assert(h.retries==1&&h.operations_.Contains(1));h.Complete(1,false);}h.Drain();}
 {Harness h;h.Admit();assert(h.Start(h.Message()));const auto old=h.operations_.Get(1)->generation;h.now=1001;assert(h.operations_.IsExpired(1,old,h.now));h.Complete(1,false,"genuine original deadline");assert(h.controls.Size()==1&&h.controls.PhysicalSize()==1);assert(h.operations_.Begin(1,h.now,1000));const auto fresh=h.operations_.Get(1)->generation;h.Ack("Runtime.evaluate","{\"result\":{\"subtype\":\"array\",\"objectId\":\"late-array\"}}");assert(h.delivered.empty()&&h.operations_.IsCurrent(1,fresh));h.Drain();assert(!h.controls.Size()&&!h.controls.PhysicalSize()&&h.operations_.IsCurrent(1,fresh));}
 for(const std::string json:{"{}","{\"result\":{\"type\":\"object\",\"value\":{\"__suitecutNativeUTF16Scalar\":\"d800\"}}}","{\"result\":{\"subtype\":\"array\",\"objectId\":\"\"}}"}){Harness h;h.Admit();assert(h.Start(h.Message()));h.Ack("Runtime.evaluate",json);assert(h.delivered.empty()&&!h.error.empty());h.Drain();}
 {Harness h;h.Admit();assert(h.Start(h.Message()));h.Ack("Runtime.evaluate","{\"exceptionDetails\":{\"exception\":{\"description\":\"Actual native selector syntax failure\"}}}");assert(h.error=="Actual native selector syntax failure"&&h.delivered.empty());h.Drain();}
 {Harness h;h.Admit();h.QueryBuiltinNodesInWorld(1,h.Message(),false,[&](const std::string&array){h.delivered=array;});h.Ack("Runtime.evaluate","{\"result\":{\"subtype\":\"array\",\"objectId\":\"utility-array\"}}");
  auto properties=CefDictionaryValue::Create();auto fields=CefListValue::Create();auto result=CefDictionaryValue::Create();auto remote=CefDictionaryValue::Create();auto deep=CefDictionaryValue::Create();auto nodes=CefListValue::Create();
  for(int i=0;i<128;++i){auto field=CefDictionaryValue::Create();field->SetString("name",std::to_string(i));auto node=CefDictionaryValue::Create();node->SetString("subtype","node");node->SetString("objectId","selected-"+std::to_string(i));field->SetDictionary("value",node);fields->SetDictionary(i,field);auto item=CefDictionaryValue::Create();item->SetString("type","node");auto value=CefDictionaryValue::Create();value->SetInt("backendNodeId",3000+i);value->SetInt("nodeType",1);item->SetDictionary("value",value);nodes->SetDictionary(i,item);}
  properties->SetList("result",fields);auto json=[](Harness::Dict value){auto wrapped=CefValue::Create();wrapped->SetDictionary(value);return CefWriteJSON(wrapped,JSON_WRITER_DEFAULT).ToString();};h.Ack("Runtime.getProperties",json(properties));
  auto metadata=h.Take("Runtime.callFunctionOn");assert(metadata.lossless&&metadata.params->GetDictionary("serializationOptions")->GetString("serialization")=="deep");deep->SetString("type","array");deep->SetList("value",nodes);remote->SetString("subtype","array");remote->SetDictionary("deepSerializedValue",deep);result->SetDictionary("result",remote);metadata.done(true,json(result));
  h.Ack("Runtime.evaluate","{\"result\":{\"objectId\":\"main-array\"}}");for(int i=0;i<128;++i){auto resolve=h.Take("DOM.resolveNode");assert(resolve.params->GetInt("backendNodeId")==3000+i&&resolve.params->GetInt("executionContextId")==17);resolve.done(true,"{\"object\":{\"subtype\":\"node\",\"objectId\":\"main-node-"+std::to_string(i)+"\"}}");}h.Ack("Runtime.callFunctionOn");assert(h.delivered=="main-array"&&h.controls.PhysicalSize()==0);h.Complete(1,true);h.Drain();assert(!h.controls.Size()&&!h.controls.PhysicalSize());}
 std::cout<<"PASS9 controlled actualCEF utility selection, genuine backend adoption request identity, strict/first/array preservation, custom/retained dispatch, exact document pin/retry, original deadline/late generation and native schema/error negatives\n";
}
