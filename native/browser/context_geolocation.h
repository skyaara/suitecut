// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
struct NativeGeolocationValue {
  struct Coordinates {double latitude=0,longitude=0,accuracy=0;};
  bool configured=false;
  std::optional<Coordinates> coordinates;
  static bool Valid(const std::optional<Coordinates>&value){return !value||(std::isfinite(value->latitude)&&std::isfinite(value->longitude)&&std::isfinite(value->accuracy)&&value->latitude>=-90&&value->latitude<=90&&value->longitude>=-180&&value->longitude<=180&&value->accuracy>=0);}
};
// UI-thread desired state plus real target callback ownership. Logical timeout
// or cancellation does not retire a dispatched update, unlock the context, or
// invent an ACK. The sender must callback only for an actual protocol result,
// failed submission, or destruction of that exact native target/channel.
class NativeContextGeolocation {
 public:
  using Result=std::function<void(bool,const std::string&)>;
  using Apply=std::function<void(const NativeGeolocationValue&,double,Result)>;
  static constexpr size_t kContexts=32,kTargets=2048,kUpdates=64;
  ~NativeContextGeolocation(){Clear();}
  bool Seed(int context,NativeGeolocationValue value){
    if(context<1||states_.contains(context)||states_.size()>=kContexts||!NativeGeolocationValue::Valid(value.coordinates)||(!value.configured&&value.coordinates))return false;
    states_.emplace(context,State{std::move(value)});return true;
  }
  std::optional<NativeGeolocationValue> Get(int context)const{const auto row=states_.find(context);return row==states_.end()?std::nullopt:std::optional(row->second.value);}
  bool Attach(int context,const std::string&key,Apply apply,Result ready){
    const auto state=states_.find(context);
    if(state==states_.end()||state->second.closing||key.empty()||key.size()>128||!apply||!ready||targets_.contains(key)||targets_.size()>=kTargets||serial_==std::numeric_limits<uint64_t>::max())return false;
    targets_.emplace(key,Target{context,++serial_,std::move(apply),std::move(ready)});
    if(!state->second.value.configured){auto callback=std::move(targets_.at(key).ready);callback(true,"");return true;}
    const int owner=state->second.owner;const auto update=updates_.find(owner);const int joined=update!=updates_.end()&&!update->second.logical_done?owner:0;if(joined)update->second.pending.insert(key);
    Dispatch(key,joined);return true;
  }
  bool Set(int owner,int context,std::optional<NativeGeolocationValue::Coordinates>value,double deadline,Result done){
    const auto state=states_.find(context);
    if(owner<1||state==states_.end()||state->second.closing||state->second.owner||!done||updates_.contains(owner)||updates_.size()>=kUpdates||!std::isfinite(deadline)||deadline<0||!NativeGeolocationValue::Valid(value))return false;
    if(state->second.revision==std::numeric_limits<uint64_t>::max())return false;
    for(const auto&[key,target]:targets_)if(target.context==context&&target.revision==std::numeric_limits<uint64_t>::max())return false;
    state->second.value={true,std::move(value)};++state->second.revision;state->second.owner=owner;
    Update update;update.context=context;update.deadline=deadline;update.done=std::move(done);
    for(const auto&[key,target]:targets_)if(target.context==context)update.pending.insert(key);
    const auto targets=update.pending;updates_.emplace(owner,std::move(update));
    if(targets.empty()){Finish(owner);return true;}
    for(const auto&key:targets){const auto target=targets_.find(key);if(target!=targets_.end()&&!target->second.in_flight)Dispatch(key,owner);}
    return true;
  }
  void Cancel(int owner){const auto update=updates_.find(owner);if(update==updates_.end())return;update->second.logical_done=true;update->second.done={};}
  void Tick(double now){
    std::vector<int> expired;for(const auto&[owner,update]:updates_)if(!update.logical_done&&update.deadline>0&&now>=update.deadline)expired.push_back(owner);
    for(const int owner:expired)LogicalFailure(owner,"Native context geolocation deadline exceeded");
  }
  void Detach(const std::string&key){
    const auto target=targets_.find(key);if(target==targets_.end())return;
    const int context=target->second.context;auto ready=std::move(target->second.ready);targets_.erase(target);
    const auto state=states_.find(context);const int owner=state==states_.end()?0:state->second.owner;
    const auto update=updates_.find(owner);if(update!=updates_.end()){update->second.pending.erase(key);if(update->second.pending.empty())Finish(owner);}
    if(ready)ready(false,"Native geolocation target is closed");
  }
  void Closing(int context){const auto state=states_.find(context);if(state==states_.end())return;state->second.closing=true;if(state->second.owner)LogicalFailure(state->second.owner,"Native browser context is closing");}
  void Closed(int context){
    const auto state=states_.find(context);if(state==states_.end())return;
    const int owner=state->second.owner;state->second.closing=true;if(owner)LogicalFailure(owner,"Native browser context is closed");
    std::vector<std::string> targets;for(const auto&[key,target]:targets_)if(target.context==context)targets.push_back(key);
    for(const auto&key:targets)Detach(key);
    if(updates_.contains(owner))LogicalFailure(owner,"Native browser context is closed");
    states_.erase(context);
  }
  void Clear(){*alive_=false;targets_.clear();updates_.clear();states_.clear();}
  size_t contexts()const{return states_.size();}
  size_t targets()const{return targets_.size();}
  size_t pending()const{return updates_.size();}
  size_t logical()const{size_t total=0;for(const auto&[owner,update]:updates_)if(!update.logical_done)++total;return total;}
  size_t inFlight()const{size_t total=0;for(const auto&[key,target]:targets_)if(target.in_flight)++total;return total;}
 private:
  struct State{NativeGeolocationValue value;int owner=0;bool closing=false;uint64_t revision=0;};
  struct Target{int context;uint64_t generation;Apply apply;Result ready;bool in_flight=false;uint64_t revision=0;};
  struct Update{int context=0;double deadline=0;Result done;std::set<std::string>pending;bool logical_done=false,success=true;std::string error;};
  void LogicalFailure(int owner,const std::string&error){const auto update=updates_.find(owner);if(update==updates_.end()||update->second.logical_done)return;update->second.logical_done=true;auto done=std::move(update->second.done);if(done)done(false,error);}
  void Dispatch(const std::string&key,int owner){
    const auto target=targets_.find(key);if(target==targets_.end())return;
    const auto state=states_.find(target->second.context);if(state==states_.end())return;
    target->second.in_flight=true;const auto generation=target->second.generation,revision=++target->second.revision,setting_revision=state->second.revision;auto apply=target->second.apply;const auto value=state->second.value;
    const auto update=updates_.find(owner);const double deadline=update==updates_.end()?0:update->second.deadline;
    apply(value,deadline,[this,alive=alive_,key,generation,revision,setting_revision,owner](bool ok,const std::string&error){
      if(!*alive)return;
      const auto target=targets_.find(key);if(target==targets_.end()||target->second.generation!=generation||target->second.revision!=revision||!target->second.in_flight)return;
      target->second.in_flight=false;
      const auto state=states_.find(target->second.context);if(state==states_.end())return;
      // An update arriving during initial bootstrap waits behind its genuine
      // outstanding command. A stale bootstrap ACK cannot make Source ready.
      const auto active=updates_.find(state->second.owner);
      if(ok&&state->second.revision!=setting_revision&&(active==updates_.end()||!active->second.logical_done)){Dispatch(key,state->second.owner);return;}
      const int current_owner=owner?owner:state->second.owner;
      auto ready=std::move(target->second.ready);if(ready)ready(ok,error);
      const auto update=updates_.find(current_owner);if(update!=updates_.end()){
        update->second.pending.erase(key);if(!ok){update->second.success=false;if(update->second.error.empty())update->second.error=error.substr(0,4096);}
        if(update->second.pending.empty())Finish(current_owner);
      }
    });
  }
  void Finish(int owner){
    const auto found=updates_.find(owner);if(found==updates_.end())return;
    auto update=std::move(found->second);updates_.erase(found);
    const auto state=states_.find(update.context);if(state!=states_.end()&&state->second.owner==owner)state->second.owner=0;
    if(!update.logical_done&&update.done)update.done(update.success,update.error);
  }
  std::map<int,State>states_;std::map<std::string,Target>targets_;std::map<int,Update>updates_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);uint64_t serial_=0;
};
}
