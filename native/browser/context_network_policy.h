// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace suitecut {
// UI-thread policy and actual target ACK ownership. Workers inherit their real
// ancestor's network state; unsupported worker emulation is never dispatched.
class NativeContextNetworkPolicy {
 public:
  using Result=std::function<void(bool,const std::string&)>;
  using Apply=std::function<void(bool,Result)>;
  ~NativeContextNetworkPolicy(){Clear();}
  bool Seed(int context,bool offline) {
    if(context<1 || (states_.size()>=32 && !states_.contains(context)))return false;
    auto [entry,inserted]=states_.emplace(context,State{offline,0});
    return inserted || entry->second.offline==offline;
  }
  bool Attach(int context,const std::string& page,Apply apply,Result ready) {
    if(!states_.contains(context) || page.empty() || page.size()>128 || !apply || !ready || targets_.size()>=192 || targets_.contains(page))return false;
    targets_.emplace(page,Target{context,++serial_,0,std::move(apply),std::move(ready)});
    const auto& state=states_.at(context);
    if(!state.offline && !state.owner){auto callback=std::move(targets_.at(page).ready);callback(true,"");return true;}
    if(state.owner)batches_.at(state.owner).pending.insert(page);
    Dispatch(page,state.offline,state.owner);return true;
  }
  bool Set(int owner,int context,bool offline,double deadline,Result done) {
    const auto state=states_.find(context);
    if(owner<1 || !std::isfinite(deadline) || deadline<0 || state==states_.end() || state->second.owner || !done || batches_.size()>=64 || batches_.contains(owner))return false;
    state->second.offline=offline;state->second.owner=owner;
    Batch batch;batch.context=context;batch.generation=++serial_;batch.deadline=deadline;batch.done=std::move(done);
    for(const auto& [page,target]:targets_)if(target.context==context)batch.pending.insert(page);
    const auto pages=batch.pending;batches_.emplace(owner,std::move(batch));
    if(pages.empty()){Finish(owner);return true;}
    for(const auto& page:pages)Dispatch(page,offline,owner);
    return true;
  }
  void Detach(const std::string& page) {
    const auto target=targets_.find(page);if(target==targets_.end())return;
    const auto context=target->second.context;targets_.erase(target);
    const auto state=states_.find(context);if(state==states_.end() || !state->second.owner)return;
    const auto owner=state->second.owner;const auto batch=batches_.find(owner);if(batch==batches_.end())return;
    batch->second.pending.erase(page);if(batch->second.pending.empty())Finish(owner);
  }
  void Cancel(int owner) {
    const auto batch=batches_.find(owner);if(batch==batches_.end())return;
    auto state=states_.find(batch->second.context);if(state!=states_.end() && state->second.owner==owner)state->second.owner=0;
    batches_.erase(batch);
  }
  void Closed(int context) {
    auto state=states_.find(context);if(state==states_.end())return;
    const auto owner=state->second.owner;states_.erase(state);
    std::vector<std::string> pages;for(const auto& [page,target]:targets_)if(target.context==context)pages.push_back(page);
    for(const auto& page:pages)targets_.erase(page);
    const auto batch=batches_.find(owner);if(batch!=batches_.end()){auto done=std::move(batch->second.done);batches_.erase(batch);done(false,"Native browser context is closed");}
  }
  void Tick(double now) {
    std::vector<int> expired;for(const auto& [owner,batch]:batches_)if(batch.deadline>0 && now>=batch.deadline)expired.push_back(owner);
    for(const auto owner:expired){auto found=batches_.find(owner);if(found==batches_.end())continue;found->second.success=false;found->second.error="Native context offline policy deadline exceeded";Finish(owner);}
  }
  void Clear(){*alive_=false;batches_.clear();targets_.clear();states_.clear();}
  size_t pending()const{return batches_.size();}
  size_t targets()const{return targets_.size();}
 private:
  struct State {bool offline=false;int owner=0;};
  struct Target {int context;uint64_t generation,revision;Apply apply;Result ready;};
  struct Batch {int context=0;uint64_t generation=0;double deadline=0;bool success=true;std::string error;std::set<std::string> pending;Result done;};
  void Dispatch(const std::string& page,bool offline,int owner) {
    const auto target=targets_.find(page);if(target==targets_.end())return;
    const auto generation=target->second.generation,revision=++target->second.revision;
    const auto batch=batches_.find(owner);const auto epoch=batch==batches_.end()?0:batch->second.generation;
    auto apply=target->second.apply;
    apply(offline,[this,alive=alive_,page,generation,revision,owner,epoch](bool ok,const std::string& error){
      if(!*alive)return;
      auto target=targets_.find(page);if(target==targets_.end() || target->second.generation!=generation)return;
      Result ready;if(target->second.revision==revision)ready=std::move(target->second.ready);
      const auto batch=batches_.find(owner);
      if(batch!=batches_.end() && batch->second.generation==epoch){batch->second.pending.erase(page);if(!ok){batch->second.success=false;batch->second.error=error;}if(batch->second.pending.empty())Finish(owner);}
      if(ready)ready(ok,error);
    });
  }
  void Finish(int owner) {
    const auto batch=batches_.find(owner);if(batch==batches_.end())return;
    auto state=states_.find(batch->second.context);if(state!=states_.end() && state->second.owner==owner)state->second.owner=0;
    auto done=std::move(batch->second.done);const auto success=batch->second.success;const auto error=batch->second.error;batches_.erase(batch);done(success,error);
  }
  std::shared_ptr<bool> alive_=std::make_shared<bool>(true);uint64_t serial_=0;
  std::map<int,State> states_;std::map<std::string,Target> targets_;std::map<int,Batch> batches_;
};
}
