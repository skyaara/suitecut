// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "element_state_sampler.h"
namespace suitecut {
// Native admission outlives logical cancellation until the actual synchronous
// renderer sampling call and private cleanup command acknowledge completion.
class ElementStateControls {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Allocate=std::function<int()>;
  using Submit=std::function<bool(int,const std::string&,Dict,const std::string&)>;
  using Fail=std::function<void(const std::string&)>;
  using Clock=std::function<double()>;
  struct State{int owner=0,context=0;uint64_t generation=0,serial=0;std::string frame,session,token;double deadline=0,cleanup_deadline=0;size_t physical=0;bool canceled=false,gone=false,releasing=false,expired=false;};
  struct Physical{bool settled=false;std::function<void()> release;void Settle(){if(!settled){settled=true;release();}}~Physical(){Settle();}};
  ElementStateControls(std::string key,Allocate allocate,Submit submit,Fail fail,Clock now)
      :key_(std::move(key)),allocate_(std::move(allocate)),submit_(std::move(submit)),fail_(std::move(fail)),now_(std::move(now)){}
  ~ElementStateControls(){life_->alive=false;}
  bool Admit(int owner,uint64_t generation,std::string frame,int context,std::string session,double deadline){
    const auto found=states_.find(owner);if(found!=states_.end())return found->second->generation==generation&&!found->second->canceled&&found->second->context==context&&found->second->frame==frame&&found->second->session==session;
    if(states_.size()>=64||serial_==std::numeric_limits<uint64_t>::max())return false;
    auto state=std::make_shared<State>();state->owner=owner;state->generation=generation;state->serial=++serial_;state->frame=std::move(frame);state->context=context;state->session=std::move(session);state->deadline=deadline;state->token=std::to_string(owner)+":"+std::to_string(generation)+":"+std::to_string(state->serial);states_.emplace(owner,state);return true;
  }
  std::string Expression(int owner,const std::string& observer){
    const auto found=states_.find(owner);if(found==states_.end())return "undefined";const auto& state=found->second;
    const int ttl=static_cast<int>(std::max(1.,std::min(120000.,state->deadline?state->deadline-now_():120000.)));
    return "("+std::string(kSuiteCutElementStateSampler)+")(nodes[0],"+observer+","+Quote(key_)+","+Quote(state->token)+","+std::to_string(ttl)+")";
  }
  std::shared_ptr<Physical> Begin(int owner){
    const auto found=states_.find(owner);if(found==states_.end()||found->second->canceled||life_->physical>=64)return nullptr;
    auto state=found->second;auto life=life_;++state->physical;++life->physical;auto physical=std::make_shared<Physical>();
    physical->release=[this,state,life]{--state->physical;--life->physical;if(life->alive&&state->canceled&&!state->gone&&!state->physical)Cleanup(state);};return physical;
  }
  void Cancel(int owner){const auto found=states_.find(owner);if(found==states_.end())return;auto state=found->second;state->canceled=true;if(!state->cleanup_deadline)state->cleanup_deadline=now_()+30000;if(!state->physical)Cleanup(state);}
  bool Accepts(int id,const std::string& session)const{const auto found=calls_.find(id);return found!=calls_.end()&&found->second->session==session;}
  bool Complete(int id,bool ok,const std::string& json,const std::string& session){
    const auto found=calls_.find(id);if(found==calls_.end())return false;auto state=found->second;
    if(state->session!=session){fail_("Native element-state cleanup session mismatch");return true;}calls_.erase(found);
    if(state->gone)return true;
    auto parsed=json.size()<=65536?CefParseJSON(json,JSON_PARSER_RFC):nullptr;auto result=parsed?parsed->GetDictionary():nullptr;auto value=result?result->GetDictionary("result"):nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")||!value||value->GetType("value")!=VTYPE_BOOL||!value->GetBool("value")){fail_("Native element-state renderer cleanup failed");return true;}
    Retire(state);return true;
  }
  void ContextDestroyed(const std::string& session,int context=0){
    std::vector<std::shared_ptr<State>> gone;for(const auto& [owner,state]:states_)if(state->session==session&&(!context||state->context==context))gone.push_back(state);
    for(const auto& state:gone){state->gone=true;state->canceled=true;if(!state->cleanup_deadline)state->cleanup_deadline=now_()+30000;Retire(state);}
  }
  void Detached(const std::string& session){ContextDestroyed(session);for(auto it=calls_.begin();it!=calls_.end();)if(it->second->session==session)it=calls_.erase(it);else ++it;}
  void Close(){for(auto&[owner,state]:states_){state->gone=true;state->canceled=true;}states_.clear();calls_.clear();}
  void Tick(){
    bool expired=false;
    auto check=[this,&expired](const std::shared_ptr<State>& state){if(state->cleanup_deadline&&!state->expired&&now_()>=state->cleanup_deadline){state->expired=true;expired=true;}};
    for(const auto&[owner,state]:states_)check(state);
    for(const auto&[id,state]:calls_)check(state);
    // Failure can synchronously close the source and clear both registries.
    // Never invoke that external callback while traversing their iterators.
    if(expired)fail_("Native element-state physical cleanup deadline exceeded");
  }
  bool NeedsTick()const{return !states_.empty()||!calls_.empty();}
  bool Contains(int owner)const{return states_.contains(owner);}
  void FrameRemoved(const std::string& frame){std::vector<std::shared_ptr<State>> gone;for(const auto&[owner,state]:states_)if(state->frame==frame)gone.push_back(state);for(const auto& state:gone){state->gone=true;state->canceled=true;Retire(state);}}
  size_t Size()const{return states_.size();}size_t Pending()const{return calls_.size();}size_t PhysicalSize()const{return life_->physical+calls_.size();}
 private:
  struct Life{bool alive=true;size_t physical=0;};
  static std::string Quote(const std::string& text){auto value=CefValue::Create();value->SetString(text);return CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();}
  void Retire(const std::shared_ptr<State>& state){const auto found=states_.find(state->owner);if(found!=states_.end()&&found->second==state)states_.erase(found);}
  void Cleanup(const std::shared_ptr<State>& state){
    if(state->gone){Retire(state);return;}if(state->releasing)return;state->releasing=true;
    if(calls_.size()>=64){fail_("Native element-state cleanup capacity exceeded");return;}const int id=allocate_();if(!id){fail_("Native element-state command identity exhausted");return;}
    auto params=CefDictionaryValue::Create();params->SetInt("contextId",state->context);params->SetBool("returnByValue",true);
    params->SetString("expression","("+std::string(kSuiteCutElementStateSampler)+")(null,null,"+Quote(key_)+","+Quote(state->token)+",0,true)");
    calls_.emplace(id,state);
    if(!submit_(id,"Runtime.evaluate",params,state->session)){calls_.erase(id);fail_("Native element-state cleanup dispatch failed");}
  }
  std::string key_;Allocate allocate_;Submit submit_;Fail fail_;Clock now_;uint64_t serial_=0;std::shared_ptr<Life> life_=std::make_shared<Life>();
  std::map<int,std::shared_ptr<State>> states_,calls_;
};
}
