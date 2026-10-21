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

// Controlled actual-CEF value/command seams cover only native preparation and
// ownership. Actual DOM backend identity and retained handles need live proof.
struct Harness {
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Frame {std::string parent,session,loader;int context=0;};
  struct Call {int owner;std::string method,session;Dict params;std::function<void(bool,const std::string&)> done;};
  struct Completion {int owner;bool ok;std::string error;};
  SuiteCutOperationRegistry operations_;
  std::string main_frame_id_="parent";
  std::map<std::string,Frame> frame_states_{{"parent",{"","parent-session","loader",17}},{"child",{"parent","child-oopif-session","child-loader",39}}};
  std::map<int,std::string> operation_sessions_;
  std::map<int,std::set<std::string>> evaluation_groups_;
  std::deque<Call> calls;
  std::vector<Completion> completions;
  std::vector<Dict> handoffs;
  double now=0;
  suitecut::SelectorWaitControls controls{[this]{return now;},[](const std::string&){assert(false);},
    [this](int owner,const std::string& group,const std::string& session,std::function<void()> settled){
      auto params=CefDictionaryValue::Create();params->SetString("objectGroup",group);
      calls.push_back({owner,"Runtime.releaseObjectGroup",session,params,[settled=std::move(settled)](bool,const std::string&){settled();}});
    }};
  suitecut::SelectorWaitControls& SelectorWaits(){return controls;}
  void Admit(int owner=1){assert(operations_.Begin(owner,now,1000));}
  std::string FrameId(Dict message){return message->HasKey("frame")?message->GetString("frame").ToString():main_frame_id_;}
  Dict Message(std::string frame="child"){auto out=CefDictionaryValue::Create();out->SetString("frame",frame);return out;}
  void Complete(int owner,bool ok,const std::string& error=""){
    if(!operations_.Finish(owner))return;
    controls.Cancel(owner);completions.push_back({owner,ok,error});
  }
  void WithFrame(int owner,const std::string& frame,std::function<void(int)> done){
    const auto found=frame_states_.find(frame);if(found==frame_states_.end()){Complete(owner,false,"Native frame detached");return;}
    assert(found->second.context);operation_sessions_[owner]=found->second.session;done(found->second.context);
  }
  void DevTools(int owner,const std::string& method,Dict params,std::function<void(bool,const std::string&)> done,std::optional<std::string> session=std::nullopt){
    const auto operation=operations_.Get(owner);assert(operation);
    const auto actual=session.value_or(operation_sessions_[owner]);
    assert(controls.Group(owner,"suitecut-automation-"+std::to_string(owner),actual));
    if(params->HasKey("objectGroup"))assert(controls.Group(owner,params->GetString("objectGroup"),actual));
    auto physical=controls.Begin(owner);assert(physical);
    calls.push_back({owner,method,actual,params->Copy(false),[this,owner,generation=operation->generation,physical,done=std::move(done)](bool ok,const std::string& json){physical->Settle();if(operations_.IsCurrent(owner,generation))done(ok,json);}});
  }
  void Handle(int owner,Dict config,bool ready){assert(ready);handoffs.push_back(config->Copy(false));Complete(owner,true);}
  Call Take(const std::string& method){for(auto i=calls.begin();i!=calls.end();++i)if(i->method==method){auto call=std::move(*i);calls.erase(i);return call;}assert(false);return {};}
  void Ack(const std::string& method,const std::string& json="{}",bool ok=true){auto call=Take(method);call.done(ok,json);}
  void Drain(){while(!calls.empty()){auto call=Take("Runtime.releaseObjectGroup");call.done(true,"{}");}assert(!controls.Size()&&!controls.PhysicalSize());}
#include "controllers/dom/frame_element_controller.inc"
};
int main(){cef_api_hash(CEF_API_VERSION,0);
 {Harness h;h.Admit();h.FrameElementHandle(1,h.Message("parent"));assert(h.calls.empty()&&h.completions.size()==1&&!h.completions[0].ok&&h.controls.Size()==0);}
 {Harness h;h.Admit();h.FrameElementHandle(1,h.Message("missing"));assert(h.calls.empty()&&h.completions.size()==1&&!h.completions[0].ok);}
 {Harness h;h.Admit();h.FrameElementHandle(1,h.Message());auto request=h.Take("DOM.getFrameOwner");assert(request.session=="parent-session"&&request.params->GetString("frameId")=="child");request.done(true,"{\"backendNodeId\":271}");auto resolve=h.Take("DOM.resolveNode");assert(resolve.session=="parent-session"&&resolve.params->GetInt("backendNodeId")==271&&resolve.params->GetInt("executionContextId")==17&&resolve.params->GetString("objectGroup")=="suitecut-automation-1");resolve.done(true,"{\"object\":{\"type\":\"object\",\"subtype\":\"node\",\"objectId\":\"actual-parent-node-wrapper\"}}");assert(h.handoffs.size()==1&&h.handoffs[0]->GetString("nativeQueryNodes")=="actual-parent-node-wrapper"&&h.handoffs[0]->GetString("frame")=="parent"&&!h.handoffs[0]->HasKey("query")&&h.handoffs[0]->GetList("handles")->GetSize()==0);h.Drain();}
 for(const std::string field:{"child","parent","context","loader","session"}){Harness h;h.Admit();h.FrameElementHandle(1,h.Message());h.Ack("DOM.getFrameOwner","{\"backendNodeId\":271}");if(field=="child")h.frame_states_.erase("child");else if(field=="parent")h.frame_states_.erase("parent");else if(field=="context")++h.frame_states_["parent"].context;else if(field=="loader")h.frame_states_["parent"].loader="new-loader";else h.frame_states_["parent"].session="new-session";h.Ack("DOM.resolveNode","{\"object\":{\"subtype\":\"node\",\"objectId\":\"stale-wrapper\"}}");assert(h.handoffs.empty()&&h.completions.size()==1&&!h.completions[0].ok);h.Drain();}
 {Harness h;h.Admit();h.FrameElementHandle(1,h.Message());assert(h.controls.Size()==1&&h.controls.PhysicalSize()==1);h.Complete(1,false,"genuine cancellation");assert(h.controls.Size()==1&&h.controls.PhysicalSize()==1);h.Admit();const auto fresh=h.operations_.Get(1)->generation;h.Ack("DOM.getFrameOwner","{\"backendNodeId\":271}");assert(h.operations_.IsCurrent(1,fresh)&&h.handoffs.empty()&&h.completions.size()==1);h.Drain();assert(h.operations_.IsCurrent(1,fresh));}
 {Harness h;for(int id=1;id<=64;++id){h.Admit(id);h.FrameElementHandle(id,h.Message());h.Complete(id,false,"genuine cancellation");}assert(h.controls.Size()==64&&h.controls.PhysicalSize()==64);h.Admit(65);h.FrameElementHandle(65,h.Message());assert(h.completions.size()==65&&h.completions.back().owner==65&&!h.completions.back().ok&&h.controls.Size()==64);for(int id=1;id<=64;++id)h.Complete(id,false,"genuine cancellation");for(int id=1;id<=64;++id)h.Ack("DOM.getFrameOwner","{\"backendNodeId\":271}");assert(h.handoffs.empty());h.Drain();}
 for(const std::string json:{"{}","{\"backendNodeId\":0}","{\"backendNodeId\":\"271\"}"}){Harness h;h.Admit();h.FrameElementHandle(1,h.Message());h.Ack("DOM.getFrameOwner",json);assert(h.handoffs.empty()&&h.completions.size()==1&&!h.completions[0].ok);h.Drain();}
 {Harness h;h.Admit();h.FrameElementHandle(1,h.Message());h.Ack("DOM.getFrameOwner","{\"backendNodeId\":271}");h.Ack("DOM.resolveNode","{\"object\":{\"subtype\":\"node\",\"objectId\":\"\"}}");assert(h.handoffs.empty()&&h.completions.size()==1&&!h.completions[0].ok);h.Drain();}
 std::cout<<"PASS8 controlled actualCEF frame-owner parent-session/backend/context routing, parent epoch/child lifetime guards, cancel/ACK ownership, stale generation isolation, physical admission bounds and malformed native protocol negatives\n";
}
