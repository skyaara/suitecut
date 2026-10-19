// SPDX-License-Identifier: MIT
#pragma once
#include "accepted_work_origin.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace suitecut {
// Selected-node commands and their real object groups stay admitted after
// cancellation. Retry cannot enter a new attempt until old commands and the
// sequential group-release acknowledgments have genuinely settled.
class SelectorWaitControls {
 public:
  using Clock=std::function<double()>;
  using Fail=std::function<void(const std::string&)>;
  using Release=std::function<void(int,const std::string&,const std::string&,std::function<void()>)>;
  struct State {NativeWorkContinuation native_origin;int owner=0;uint64_t generation=0;size_t physical=0;bool canceled=false,expired=false,retrying=false,releasing=false;double release_deadline=0;std::set<std::pair<std::string,std::string>> groups;};
  struct Physical {bool settled=false;std::function<void()> release;void Settle(){if(!settled){settled=true;release();}}~Physical(){Settle();}};
  SelectorWaitControls(Clock now,Fail fail,Release release={}):now_(std::move(now)),fail_(std::move(fail)),release_(std::move(release)){}
  ~SelectorWaitControls(){life_->alive=false;}
  bool Admit(int owner,uint64_t generation){if(closed_||owner<=0||!generation)return false;const auto found=states_.find(owner);if(found!=states_.end())return found->second->generation==generation&&!found->second->canceled;if(states_.size()>=64)return false;auto state=std::make_shared<State>();state->owner=owner;state->generation=generation;states_.emplace(owner,std::move(state));return true;}
  bool Contains(int owner)const{return states_.contains(owner);}
  bool LocatorHandlerReady(uint64_t invocation)const{for(const auto&[owner,state]:states_){(void)owner;if(state->native_origin.Origin().locator_handler_invocation==invocation&&(state->physical||!state->groups.empty()))return false;}return true;}
  bool Ready(int owner)const{const auto found=states_.find(owner);return found!=states_.end()&&!found->second->physical&&found->second->groups.empty();}
  bool Retrying(int owner)const{const auto found=states_.find(owner);return found!=states_.end()&&found->second->retrying;}
  void Pause(int owner){const auto found=states_.find(owner);if(found!=states_.end()){found->second->retrying=true;ReleaseGroups(found->second);}}
  void Resume(int owner){const auto found=states_.find(owner);if(found!=states_.end()&&!found->second->canceled&&Ready(owner))found->second->retrying=false;}
  bool Group(int owner,const std::string& group,const std::string& session){const auto found=states_.find(owner);if(found==states_.end()||found->second->canceled||!release_||group.empty()||group.size()>256||session.size()>256)return false;auto& groups=found->second->groups;const auto key=std::make_pair(group,session);if(groups.contains(key))return true;if(groups.size()>=64)return false;groups.insert(key);return true;}
  void Keep(int owner,const std::string& group,const std::string& session){const auto found=states_.find(owner);if(found!=states_.end())found->second->groups.erase({group,session});}
  std::shared_ptr<Physical> Begin(int owner){
    const auto found=states_.find(owner);if(found==states_.end()||found->second->canceled||found->second->retrying||life_->commands>=64)return nullptr;
    const auto state=found->second;const auto life=life_;++state->physical;++life->physical;++life->commands;auto call=std::make_shared<Physical>();
    call->release=[this,state,life]{--state->physical;--life->physical;--life->commands;if(life->alive)ReleaseGroups(state);};return call;
  }
  void Cancel(int owner){const auto found=states_.find(owner);if(found==states_.end())return;const auto state=found->second;state->canceled=true;if(!state->release_deadline)state->release_deadline=now_()+30000;ReleaseGroups(state);}
  void Tick(){bool expired=false;for(auto&[owner,state]:states_)if(state->canceled&&!state->expired&&state->release_deadline&&now_()>=state->release_deadline){state->expired=true;expired=true;}if(expired)fail_("Native selector wait physical settlement deadline exceeded");}
  bool NeedsTick()const{for(const auto&[owner,state]:states_)if(state->canceled&&!state->expired)return true;return false;}
  void Close(){closed_=true;for(auto&[owner,state]:states_)state->canceled=true;states_.clear();}
  size_t Size()const{return states_.size();}size_t PhysicalSize()const{return life_->physical;}
 private:
  struct Life{bool alive=true;size_t physical=0,commands=0;};
  void Retire(const std::shared_ptr<State>& state){const auto found=states_.find(state->owner);if(found!=states_.end()&&found->second==state)states_.erase(found);}
  void ReleaseGroups(const std::shared_ptr<State>& state){
    if(closed_||state->physical||(!state->canceled&&!state->retrying))return;
    if(state->groups.empty()){if(state->canceled)Retire(state);return;}
    const auto group=*state->groups.begin();const auto life=life_;auto call=std::make_shared<Physical>();
    ++state->physical;++life->physical;state->releasing=true;
    call->release=[this,state,life,group]{--state->physical;--life->physical;state->releasing=false;state->groups.erase(group);if(life->alive)ReleaseGroups(state);};
    const NativeWorkOriginFrame scope(state->native_origin.Origin());release_(state->owner,group.first,group.second,[call]{call->Settle();});
  }
  bool closed_=false;Clock now_;Fail fail_;Release release_;std::map<int,std::shared_ptr<State>> states_;std::shared_ptr<Life> life_=std::make_shared<Life>();
};
}
