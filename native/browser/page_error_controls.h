// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "devtools_utf16.h"

namespace suitecut {
// Unsolicited Inspector exceptions have no ordinary automation owner. This
// registry separately pins the actual raw object and a private alias group until
// their physical release acknowledgments or actual target destruction. Logical
// expiry never frees admission while a renderer call remains outstanding.
class PageErrorControls {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Allocate=std::function<int()>;
  using Submit=std::function<bool(int,const std::string&,Dict,const std::string&)>;
  using Emit=std::function<void(Dict,Dict,const std::string&)>;
  using Fail=std::function<void(const std::string&)>;
  using Preview=std::function<std::string(Dict)>;
  using Clock=std::function<double()>;
  static constexpr size_t kCaptures=64,kCalls=128,kBytes=8*1048576,kResponse=1048576;
  PageErrorControls(Allocate allocate,Submit submit,Emit emit,Fail fail,Preview preview,Clock now)
      :allocate_(std::move(allocate)),submit_(std::move(submit)),emit_(std::move(emit)),fail_(std::move(fail)),preview_(std::move(preview)),now_(std::move(now)){}
  void Start(Dict details,const std::string& session){
    if(!details)return;
    const auto exception=details->GetDictionary("exception");const auto bytes=JSON(details).size();
    if(states_.size()>=kCaptures||bytes>kResponse||bytes>kBytes-bytes_||next_==std::numeric_limits<uint64_t>::max()){
      fail_("Native page error capture capacity exceeded");return;
    }
    auto state=std::make_shared<State>();state->id=++next_;state->details=details->Copy(false);state->session=session;state->deadline=now_()+30000;state->bytes=bytes;
    state->raw=exception?exception->GetString("objectId").ToString():"";state->group="suitecut-page-error-"+std::to_string(state->id);
    states_.emplace(state->id,state);bytes_+=bytes;
    if(exception&&exception->GetString("subtype")=="error"&&!state->raw.empty()){
      auto params=CefDictionaryValue::Create();params->SetString("objectId",state->raw);params->SetString("objectGroup",state->group);params->SetBool("returnByValue",false);params->SetString("functionDeclaration","function(){return this;}");
      state->group_owned=true;
      Request(state,"Runtime.callFunctionOn",params,[this,state](bool ok,Dict result){
        auto remote=result?result->GetDictionary("result"):nullptr;
        if(ok&&remote&&!result->HasKey("exceptionDetails")&&remote->GetType("objectId")==VTYPE_STRING&&state->group_owned)state->group_installed=true;
        if(state->canceled){Cleanup(state);return;}
        if(!ok||!remote||result->HasKey("exceptionDetails")||remote->GetType("objectId")!=VTYPE_STRING){Reject(state,"Native page error alias unavailable");return;}
        auto params=CefDictionaryValue::Create();params->SetString("objectId",remote->GetString("objectId"));params->SetBool("ownProperties",false);params->SetBool("generatePreview",false);
        Request(state,"Runtime.getProperties",params,[this,state](bool ok,Dict result){
          if(state->canceled){Cleanup(state);return;}
          auto properties=result?result->GetList("result"):nullptr;auto metadata=CefDictionaryValue::Create();
          if(!ok||!properties||properties->GetSize()>4096){Reject(state,"Native page error descriptors unavailable or excessive");return;}
          for(size_t i=0;i<properties->GetSize();++i){auto property=properties->GetDictionary(i);if(!property)continue;const auto name=property->GetString("name").ToString();
            if(name!="name"&&name!="message"&&name!="stack")continue;
            const auto value=property->GetDictionary("value");
            // Never execute author accessors or decode arbitrary author objects.
            if(!value||value->GetString("type")!="string"||!value->HasKey("value")){Reject(state,"Native page error field is not a genuine string descriptor");return;}
            if(metadata->HasKey(name)){Reject(state,"Duplicate native page error field descriptor");return;}
            DevToolsSetString(metadata,name,DevToolsString(value->GetValue("value")));
          }
          if(!metadata->HasKey("name")||!metadata->HasKey("message")||!metadata->HasKey("stack")){Reject(state,"Native page error exact fields unavailable");return;}
          if(JSON(metadata).size()>kResponse){Reject(state,"Native page error exact field limit exceeded");return;}
          Publish(state,metadata);
        });
        ReleaseRaw(state);
      });
    }else{
      const auto text=exception?(exception->HasKey("description")?DevToolsString(exception->GetValue("description")):exception->GetString("type")=="string"?DevToolsString(exception->GetValue("value")):preview_(exception)):DevToolsString(details->GetValue("text"));
      auto metadata=CefDictionaryValue::Create();metadata->SetString("name","Error");DevToolsSetString(metadata,"message",text);DevToolsSetString(metadata,"stack",text);
      Publish(state,metadata);
    }
  }
  bool Accepts(int id,const std::string& session)const{const auto found=calls_.find(id);return found!=calls_.end()&&found->second.state->session==session;}
  bool Complete(int id,bool ok,const std::string& json,const std::string& session){
    const auto found=calls_.find(id);if(found==calls_.end())return false;
    if(found->second.state->session!=session){fail_("Native page error session mismatch");return true;}
    auto call=std::move(found->second);calls_.erase(found);--call.state->physical;
    auto parsed=json.size()<=kResponse?CefParseJSON(json,JSON_PARSER_RFC):nullptr;auto result=parsed?parsed->GetDictionary():nullptr;
    call.done(ok&&result,result);Retire(call.state);return true;
  }
  void Tick(){for(auto& [id,state]:states_)if(!state->expired&&now_()>=state->deadline){state->expired=true;state->canceled=true;fail_("Native page error capture deadline exceeded");}}
  bool NeedsTick()const{for(const auto& [id,state]:states_)if(!state->expired)return true;return false;}
  void ContextDestroyed(const std::string& session,int context=0){
    std::vector<std::shared_ptr<State>> gone;for(const auto& [id,state]:states_)if(state->session==session&&(!context||state->details->GetInt("executionContextId")==context))gone.push_back(state);
    for(const auto& state:gone){state->canceled=true;state->metadata=nullptr;state->raw.clear();state->group_owned=false;state->group_installed=false;Retire(state);}
    Flush();
  }
  void Detached(const std::string& session){
    for(auto it=calls_.begin();it!=calls_.end();)if(it->second.state->session==session)it=calls_.erase(it);else ++it;
    for(auto it=states_.begin();it!=states_.end();)if(it->second->session==session){bytes_-=it->second->bytes;it=states_.erase(it);}else ++it;
    Flush();
  }
  void Close(){calls_.clear();states_.clear();bytes_=0;}
  size_t Captures()const{return states_.size();}size_t Physical()const{return calls_.size();}size_t Bytes()const{return bytes_;}
  size_t Groups()const{size_t n=0;for(const auto& [id,state]:states_)n+=state->group_installed;return n;}
  size_t GroupReservations()const{size_t n=0;for(const auto& [id,state]:states_)n+=state->group_owned;return n;}
 private:
  struct State{uint64_t id=0;Dict details;std::string session,raw,group;size_t bytes=0,physical=0;double deadline=0;bool expired=false,canceled=false,group_owned=false,group_installed=false,raw_releasing=false,group_releasing=false,ready=false,delivered=false;Dict metadata;};
  using Done=std::function<void(bool,Dict)>;
  struct Call{std::shared_ptr<State> state;Done done;};
  static std::string JSON(Dict value){auto wrapper=CefValue::Create();wrapper->SetDictionary(value);return CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString();}
  void Publish(const std::shared_ptr<State>& state,Dict metadata){
    const auto bytes=JSON(metadata).size();if(bytes>kResponse||bytes>kBytes-bytes_){Reject(state,"Native page error metadata capacity exceeded");return;}
    bytes_+=bytes;state->bytes+=bytes;state->metadata=metadata;state->ready=true;Flush();
  }
  void Flush(){
    std::vector<std::shared_ptr<State>> ready;
    for(auto& [id,item]:states_){if(item->delivered||item->canceled)continue;if(!item->ready)break;item->delivered=true;ready.push_back(item);}
    for(const auto& item:ready){emit_(item->metadata,item->details,item->session);item->metadata=nullptr;item->canceled=true;Cleanup(item);}
  }
  void Reject(const std::shared_ptr<State>& state,const std::string& error){state->canceled=true;fail_(error);Cleanup(state);}
  void Request(const std::shared_ptr<State>& state,const std::string& method,Dict params,Done done){
    if(calls_.size()>=kCalls){done(false,nullptr);return;}const int id=allocate_();if(!id){done(false,nullptr);return;}
    ++state->physical;calls_.emplace(id,Call{state,std::move(done)});
    if(!submit_(id,method,params,state->session)){auto call=std::move(calls_.at(id));calls_.erase(id);--state->physical;call.done(false,nullptr);Retire(state);}
  }
  void ReleaseRaw(const std::shared_ptr<State>& state){
    if(state->raw.empty()||state->raw_releasing)return;
    state->raw_releasing=true;auto params=CefDictionaryValue::Create();params->SetString("objectId",state->raw);
    Request(state,"Runtime.releaseObject",params,[this,state](bool ok,Dict){if(ok)state->raw.clear();else if(!state->raw.empty())fail_("Native page error raw object release failed");Retire(state);});
  }
  void Cleanup(const std::shared_ptr<State>& state){
    ReleaseRaw(state);
    if(state->group_owned&&!state->group_releasing){state->group_releasing=true;auto params=CefDictionaryValue::Create();params->SetString("objectGroup",state->group);
      Request(state,"Runtime.releaseObjectGroup",params,[this,state](bool ok,Dict){if(ok){state->group_owned=false;state->group_installed=false;}else if(state->group_owned)fail_("Native page error group release failed");Retire(state);});
    }Retire(state);
  }
  void Retire(const std::shared_ptr<State>& state){if(state->canceled&&!state->physical&&state->raw.empty()&&!state->group_owned){const auto found=states_.find(state->id);if(found!=states_.end()){bytes_-=state->bytes;states_.erase(found);}}}
  Allocate allocate_;Submit submit_;Emit emit_;Fail fail_;Preview preview_;Clock now_;uint64_t next_=0;size_t bytes_=0;
  std::map<uint64_t,std::shared_ptr<State>> states_;std::map<int,Call> calls_;
};
}
