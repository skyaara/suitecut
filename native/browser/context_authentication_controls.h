// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
struct NativeContextCredential {
  std::string username,password,origin;
  bool always=false;
};
using NativeContextCredentials=std::vector<NativeContextCredential>;
// UI publishes immutable, native-validated credentials. CEF's IO callback may
// retain this handle without reading an Engine map or a mutable renderer value.
class NativeContextAuthenticationSnapshot {
 public:
  explicit NativeContextAuthenticationSnapshot(std::shared_ptr<const NativeContextCredentials> value):value_(std::move(value)){}
  std::shared_ptr<const NativeContextCredentials> Get()const{std::lock_guard lock(mutex_);return value_;}
  void Set(std::shared_ptr<const NativeContextCredentials> value){std::lock_guard lock(mutex_);value_=std::move(value);}
 private:
  mutable std::mutex mutex_;std::shared_ptr<const NativeContextCredentials>value_;
};
// Bounded UI-thread policy fanout. Cancellation/timeout retires only the reply;
// real ClearHttpAuthCredentials and Fetch configuration receipts remain owned
// until their actual callback or exact native target destruction.
class NativeContextAuthenticationControls {
 public:
  using Value=std::shared_ptr<const NativeContextCredentials>;
  using Snapshot=std::shared_ptr<NativeContextAuthenticationSnapshot>;
  using Result=std::function<void(bool,const std::string&)>;
  using Apply=std::function<void(Value,Result)>;
  using Prepare=std::function<void(Result)>;
  static constexpr size_t kContexts=32,kTargets=192,kUpdates=64;
  // Receipt times must use the same clock domain as the caller's deadlines.
  // Requiring an explicit clock prevents absolute/relative epoch substitution.
  explicit NativeContextAuthenticationControls(std::function<double()> now):now_(std::move(now)){}
  struct Receipt {int owner=0;double deadline=0,at=0;};
  const Receipt& LastReceipt()const{return receipt_;}
  static const char* DeadlineError(){return "Native context HTTP credentials deadline exceeded";}
  void SetSettled(std::function<void(int)> settled){settled_=std::move(settled);}
  ~NativeContextAuthenticationControls(){Clear();}
  static bool Valid(const NativeContextCredentials&value){
    if(value.size()>64)return false;
    size_t bytes=0;
    for(const auto&item:value){if(item.username.size()>4096||item.password.size()>4096||item.origin.size()>8192||item.username.find('\0')!=std::string::npos||item.password.find('\0')!=std::string::npos||item.origin.find('\0')!=std::string::npos)return false;bytes+=item.username.size()+item.password.size()+item.origin.size();if(bytes>65536)return false;}return true;
  }
  bool Seed(int context,NativeContextCredentials value){
    if(context<1||states_.contains(context)||states_.size()>=kContexts||!Valid(value))return false;
    auto snapshot=std::make_shared<NativeContextAuthenticationSnapshot>(std::make_shared<const NativeContextCredentials>(std::move(value)));states_.emplace(context,State{std::move(snapshot)});return true;
  }
  Snapshot Get(int context)const{const auto found=states_.find(context);return found==states_.end()?nullptr:found->second.snapshot;}
  bool Attach(int context,const std::string&key,Apply apply,Result ready){
    const auto found=states_.find(context);if(found==states_.end()||found->second.closing||key.empty()||key.size()>128||!apply||!ready||targets_.size()>=kTargets||targets_.contains(key)||serial_==std::numeric_limits<uint64_t>::max())return false;
    targets_.emplace(key,Target{context,++serial_,std::move(apply),std::move(ready)});
    const int owner=found->second.owner;const auto update=updates_.find(owner);if(update!=updates_.end()){update->second.pending.insert(key);if(update->second.preparing)return true;}
    Dispatch(key);return true;
  }
  bool Set(int owner,int context,NativeContextCredentials value,double deadline,Prepare prepare,Result done){
    if(!now_)return false;
    const auto found=states_.find(context);if(owner<1||found==states_.end()||found->second.closing||found->second.owner||!Valid(value)||!std::isfinite(deadline)||deadline<0||!prepare||!done||updates_.contains(owner)||updates_.size()>=kUpdates||serial_==std::numeric_limits<uint64_t>::max()||found->second.revision==std::numeric_limits<uint64_t>::max())return false;
    for(const auto&[key,target]:targets_){(void)key;if(target.context==context&&target.revision==std::numeric_limits<uint64_t>::max())return false;}
    auto&state=found->second;state.snapshot->Set(std::make_shared<const NativeContextCredentials>(std::move(value)));++state.revision;state.owner=owner;
    Update update;update.context=context;update.generation=++serial_;update.deadline=deadline;update.done=std::move(done);update.preparing=true;
    for(const auto&[key,target]:targets_)if(target.context==context)update.pending.insert(key);
    const auto generation=update.generation;updates_.emplace(owner,std::move(update));
    prepare([this,alive=alive_,owner,generation](bool ok,const std::string&error){
      if(!*alive)return;
      const auto found=updates_.find(owner);if(found==updates_.end()||found->second.generation!=generation||!found->second.preparing)return;
      found->second.preparing=false;if(!ok){
        found->second.success=false;found->second.error=error.substr(0,4096);
        const auto state=states_.find(found->second.context);if(state!=states_.end()){state->second.closing=true;state->second.snapshot->Set(nullptr);}
        std::vector<Result>ready;for(auto&[key,target]:targets_){(void)key;if(target.context==found->second.context&&target.ready)ready.push_back(std::move(target.ready));}
        for(auto&callback:ready)callback(false,error);
        if(*alive){Finish(owner);}return;
      }
      const auto pending=found->second.pending;if(pending.empty()){Finish(owner);return;}
      for(const auto&key:pending){const auto target=targets_.find(key);if(target!=targets_.end()&&!target->second.in_flight)Dispatch(key);}
    });return true;
  }
  void Cancel(int owner){const auto found=updates_.find(owner);if(found!=updates_.end()){found->second.logical_done=true;found->second.done={};}}
  void Tick(double now){if(!std::isfinite(now))return;std::vector<int>expired;for(const auto&[owner,update]:updates_)if(!update.logical_done&&update.deadline>0&&now>=update.deadline)expired.push_back(owner);for(const int owner:expired)FailLogical(owner,DeadlineError());}
  void Detach(const std::string&key){
    const auto found=targets_.find(key);if(found==targets_.end())return;const int context=found->second.context;auto ready=std::move(found->second.ready);targets_.erase(found);
    const auto state=states_.find(context);if(state!=states_.end()){const int owner=state->second.owner;const auto update=updates_.find(owner);if(update!=updates_.end()){update->second.pending.erase(key);if(!update->second.preparing&&update->second.pending.empty())Finish(owner);}}
    if(ready)ready(false,"Native authentication target is closed");
    Notify(context);
  }
  void Closing(int context){const auto found=states_.find(context);if(found==states_.end())return;found->second.closing=true;found->second.snapshot->Set(nullptr);if(found->second.owner)FailLogical(found->second.owner,"Native browser context is closing");}
  void Closed(int context){
    Closing(context);const auto found=states_.find(context);if(found==states_.end())return;++retiring_;std::vector<std::string>keys;for(const auto&[key,target]:targets_)if(target.context==context)keys.push_back(key);for(const auto&key:keys)Detach(key);const auto retained=states_.find(context);if(retained==states_.end()){--retiring_;return;}const int owner=retained->second.owner;
    // This entry point is called only after the actual request context and all
    // browser actors are closed; it may retire an abandoned CEF completion.
    updates_.erase(owner);states_.erase(context);--retiring_;
  }
  void Clear(){*alive_=false;for(const auto&[context,state]:states_){(void)context;state.snapshot->Set(nullptr);}targets_.clear();updates_.clear();states_.clear();}
  bool Ready(int context)const{
    const auto state=states_.find(context);if(state!=states_.end()&&state->second.owner)return false;
    for(const auto&[key,target]:targets_){(void)key;if(target.context==context&&target.in_flight)return false;}return true;
  }
  size_t pending()const{return updates_.size();}
  size_t physical()const{size_t total=0;for(const auto&[owner,update]:updates_){(void)owner;total+=update.preparing;}for(const auto&[key,target]:targets_){(void)key;total+=target.in_flight;}return total;}
  size_t logical()const{size_t total=0;for(const auto&[owner,update]:updates_){(void)owner;total+=!update.logical_done;}return total;}
  size_t targets()const{return targets_.size();}
 private:
  void Notify(int context){if(!retiring_&&settled_)settled_(context);}
  struct State{Snapshot snapshot;int owner=0;bool closing=false;uint64_t revision=0;};
  struct Target{int context;uint64_t generation;Apply apply;Result ready;bool in_flight=false;uint64_t revision=0;};
  struct Update{int context=0;uint64_t generation=0;double deadline=0;Result done;std::set<std::string>pending;bool preparing=false,logical_done=false,success=true;std::string error;};
  void FailLogical(int owner,const std::string&error){const auto found=updates_.find(owner);if(found==updates_.end()||found->second.logical_done)return;found->second.logical_done=true;auto done=std::move(found->second.done);if(done)done(false,error);}
  void Dispatch(const std::string&key){
    const auto target=targets_.find(key);if(target==targets_.end()||target->second.in_flight)return;const auto state=states_.find(target->second.context);if(state==states_.end()||state->second.closing)return;
    const auto update=updates_.find(state->second.owner);if(update!=updates_.end()&&update->second.preparing)return;
    target->second.in_flight=true;const auto generation=target->second.generation,revision=++target->second.revision,setting=state->second.revision;auto apply=target->second.apply;const auto value=state->second.snapshot->Get();
    apply(value,[this,alive=alive_,key,generation,revision,setting](bool ok,const std::string&error){
      if(!*alive)return;
      const auto target=targets_.find(key);if(target==targets_.end()||target->second.generation!=generation||target->second.revision!=revision||!target->second.in_flight)return;target->second.in_flight=false;
      const int context=target->second.context;const auto state=states_.find(context);if(state==states_.end())return;const int owner=state->second.owner;const auto update=updates_.find(owner);
      if(ok&&state->second.revision!=setting&&update!=updates_.end()){if(!update->second.preparing)Dispatch(key);return;}
      const uint64_t update_generation=update==updates_.end()?0:update->second.generation;
      auto ready=std::move(target->second.ready);if(ready)ready(ok,error);
      if(!*alive)return;
      const auto current=updates_.find(owner);
      if(current!=updates_.end()&&current->second.generation==update_generation){current->second.pending.erase(key);if(!ok){current->second.success=false;if(current->second.error.empty())current->second.error=error.substr(0,4096);}if(!current->second.preparing&&current->second.pending.empty())Finish(owner);}
      Notify(context);
    });
  }
  void Finish(int owner){
    const auto found=updates_.find(owner);if(found==updates_.end())return;
    auto update=std::move(found->second);updates_.erase(found);
    const auto state=states_.find(update.context);if(state!=states_.end()&&state->second.owner==owner)state->second.owner=0;
    // A genuine completion may arrive between native deadline ticks. A late
    // successful configuration remains accepted, but cannot produce success
    // for an expired caller. Its physical receipts have actually settled here.
    const double received=now_();receipt_={owner,update.deadline,received};
    if(!update.logical_done&&update.success&&update.deadline>0){if(!std::isfinite(received)||received>=update.deadline){update.success=false;update.error=DeadlineError();}}
    if(!update.logical_done&&update.done)update.done(update.success,update.error);
    Notify(update.context);
  }
  std::map<int,State>states_;std::map<std::string,Target>targets_;std::map<int,Update>updates_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);uint64_t serial_=0;size_t retiring_=0;std::function<void(int)>settled_;std::function<double()>now_;Receipt receipt_;
};
}
