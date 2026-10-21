// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "context_route_registration.h"
#include "route_callback_leases.h"
#include "route_callback_waits.h"
namespace suitecut {
// All calls run on the native UI actor. Owner is an actual Source/broker
// namespace; its monotonically admitted attachment generation pins callbacks.
class NativeContextRoutes {
 public:
  using Route=std::shared_ptr<NativeContextRouteRegistration>;
  using Result=std::function<void(bool,const std::string&)>;
  using RemovalReceipt=std::function<void(bool,const std::vector<std::string>&)>;
  enum class Mutation {Install,Remove};
  using Apply=std::function<void(Mutation,Route,double,Result)>;
  using Failure=std::function<void(const std::string&,const std::string&)>;
  explicit NativeContextRoutes(Failure failure,std::function<double()>now):failure_(std::move(failure)),now_(std::move(now)),waits_(now_){}
  ~NativeContextRoutes(){Clear();}
  bool Seed(int context){return !closed_&&context>0&&states_.size()<32&&states_.emplace(context,State{}).second;}
  bool Attach(int context,std::string owner,Apply apply,Result ready){
    auto state=states_.find(context);
    if(closed_||state==states_.end()||owner.empty()||owner.size()>128||owner.find('\0')!=std::string::npos||!apply||!ready||targets_.size()>=192||targets_.contains(owner)||serial_==UINT64_MAX)return false;
    Prune(state->second);
    Target target;target.context=context;target.generation=++serial_;target.apply=std::move(apply);target.ready=std::move(ready);
    for(const auto&id:state->second.order){const auto&route=state->second.routes.at(id);int batch=0;
      if(state->second.operation){auto found=batches_.find(state->second.operation);if(found!=batches_.end()&&found->second.mutation==Mutation::Install&&found->second.ids.contains(id))batch=state->second.operation;}
      target.queue.push_back({Mutation::Install,route,batch,batch?batches_.at(batch).deadline:0,batch?batches_.at(batch).generation:0});
    }
    if(state->second.operation){auto found=batches_.find(state->second.operation);if(found!=batches_.end()&&found->second.mutation==Mutation::Install)for(const auto&task:target.queue)if(task.batch)found->second.pending.insert({owner,task.route->id()});}
    targets_.emplace(owner,std::move(target));Pump(owner);return true;
  }
  bool Install(int operation,int context,Route route,double deadline,Result reply){
    auto state=states_.find(context);
    if(!Admit(operation,state,deadline,reply)||!route||!route->available())return false;
    Prune(state->second);
    if(state->second.routes.size()>=128||state->second.routes.contains(route->id())||route->bytes()>1048576-state->second.bytes||!Room(context,1)||!route->Claim(state->second.policy_owner,context))return false;
    state->second.bytes+=route->bytes();state->second.routes.emplace(route->id(),route);state->second.order.push_back(route->id());state->second.operation=operation;
    Batch batch;batch.generation=++serial_;batch.context=context;batch.mutation=Mutation::Install;batch.deadline=deadline;batch.reply=std::move(reply);batch.ids.insert(route->id());
    std::vector<std::string>owners;
    for(auto&[owner,target]:targets_)if(target.context==context){PruneClaims(target);target.queue.push_back({Mutation::Install,route,operation,deadline,batch.generation});batch.pending.insert({owner,route->id()});owners.push_back(owner);}
    batches_.emplace(operation,std::move(batch));Start(operation,owners);return true;
  }
  // Empty IDs means this family's routes only; Page/HAR registrations are never
  // part of this book. Already admitted callback leases remain independently owned.
  bool Remove(int operation,int context,const std::vector<std::string>&ids,double deadline,Result reply,std::function<void()>admitted={},std::function<bool()>prepare={}){
    auto state=states_.find(context);if(!Admit(operation,state,deadline,reply)||ids.size()>128)return false;
    std::vector<Route>selected;
    if(ids.empty()){for(const auto&id:state->second.order)selected.push_back(state->second.routes.at(id));}
    else{std::set<std::string>unique;for(const auto&id:ids){if(id.empty()||id.size()>128||!unique.insert(id).second)return false;auto found=state->second.routes.find(id);if(found!=state->second.routes.end())selected.push_back(found->second);}}
    if(!Room(context,selected.size())||(prepare&&!prepare()))return false;
    Batch batch;batch.generation=++serial_;batch.context=context;batch.mutation=Mutation::Remove;batch.deadline=deadline;batch.reply=std::move(reply);
    for(const auto&route:selected){batch.ids.insert(route->id());Erase(state->second,route);}
    state->second.operation=operation;std::vector<std::string>owners;
    for(auto&[owner,target]:targets_)if(target.context==context){bool queued=false;for(const auto&route:selected)if(Claims(target,route)){target.queue.push_back({Mutation::Remove,route,operation,deadline,batch.generation});batch.pending.insert({owner,route->id()});queued=true;}if(queued)owners.push_back(owner);}
    batches_.emplace(operation,std::move(batch));auto alive=alive_;if(admitted)admitted();if(*alive)Start(operation,owners);return true;
  }
  // Admission and actual all-target ACK are distinct native receipts. The
  // immutable callback snapshot is reserved before any policy deactivation.
  bool RemoveAllObserved(int operation,int context,uint64_t generation,bool wait,double deadline,RemovalReceipt receipt,Result reply){
    auto state=states_.find(context);if(!generation||!receipt||!Admit(operation,state,deadline,reply))return false;
    std::set<std::string>ids;for(const auto&[id,route]:state->second.routes)ids.insert(id);
    auto snapshot=GlobalRouteCallbackLeases().Capture([&](const NativeContextRouteRegistration&policy){if(!policy.ClaimedBy(state->second.policy_owner,context))return false;ids.insert(policy.id());return true;});
    auto selected=std::make_shared<std::vector<std::string>>(ids.begin(),ids.end());size_t bytes=selected->capacity()*sizeof(std::string),wire=512;for(const auto&id:*selected){bytes+=id.capacity();wire+=6*id.size()+4;}
    // Reserve for worst-case actual JSON escaping before deactivating policy.
    if(selected->size()>256||bytes>65536||wire>65536)return false;
    auto alive=alive_;
    std::function<bool()>prepare;
    if(wait)prepare=[this,operation,context,generation,snapshot=std::move(snapshot),deadline,reply=std::move(reply)]()mutable{return waits_.Reserve(operation,context,generation,std::move(snapshot),deadline,std::move(reply));};
    auto completed=[this,alive,operation,generation,wait,selected,receipt,reply=wait?Result{}:std::move(reply)](bool ok,const std::string&error){
      if(!*alive){if(reply)reply(false,error);return;}
      if(ok){receipt(true,*selected);if(!*alive){if(reply)reply(false,"Native routing closed during mutation receipt delivery");return;}if(wait)waits_.Applied(operation,generation);else reply(true,"");}
      else if(wait)waits_.Failed(operation,generation,error);else reply(false,error);
    };
    const bool accepted=Remove(operation,context,{},deadline,std::move(completed),[receipt,selected]{receipt(false,*selected);},std::move(prepare));
    return accepted;
  }
  void Cancel(int operation){
    waits_.Cancel(operation);
    auto found=batches_.find(operation);if(found==batches_.end())return;
    auto batch=std::move(found->second);batches_.erase(found);auto state=states_.find(batch.context);if(state!=states_.end()&&state->second.operation==operation)state->second.operation=0;
    // Cancel is logical only. A dispatched installer and its exact receipt stay
    // alive. Rollback is ordered after it on each actual owner channel.
    if(batch.mutation==Mutation::Install)Rollback(batch.context,batch.ids);
  }
  void Tick(double now){
    std::vector<std::pair<int,uint64_t>>expired;for(const auto&[operation,batch]:batches_)if(batch.deadline&&now>=batch.deadline)expired.emplace_back(operation,batch.generation);
    auto alive=alive_;for(const auto&[operation,generation]:expired){if(!*alive)return;Finish(operation,false,"Native context route original deadline exceeded",generation);}
    if(*alive)waits_.Tick();
  }
  void Detach(const std::string&owner){
    const auto target=targets_.find(owner);if(target==targets_.end())return;
    auto ready=std::move(target->second.ready);targets_.erase(target);
    std::vector<std::pair<int,uint64_t>>failed;for(auto&[operation,batch]:batches_)if(std::any_of(batch.pending.begin(),batch.pending.end(),[&](const auto&key){return key.first==owner;}))failed.emplace_back(operation,batch.generation);
    auto alive=alive_;for(const auto&[operation,generation]:failed){if(!*alive)break;Finish(operation,false,"Actual context route target closed before policy acknowledgement",generation);}
    if(ready)ready(false,"Actual context route owner closed before readiness acknowledgement");
  }
  void Closed(int context){
    auto state=states_.find(context);if(state==states_.end())return;
    *state->second.policy_owner=false;for(auto&[id,route]:state->second.routes)route->Remove();
    states_.erase(state);
    std::vector<Result>replies;
    for(auto it=batches_.begin();it!=batches_.end();)if(it->second.context==context){replies.push_back(std::move(it->second.reply));it=batches_.erase(it);}else ++it;
    for(auto it=targets_.begin();it!=targets_.end();)if(it->second.context==context){if(it->second.ready)replies.push_back(std::move(it->second.ready));it=targets_.erase(it);}else ++it;
    waits_.Closed(context);for(auto&reply:replies)reply(false,"Actual native routing context closed");
  }
  void Clear(){
    if(closed_)return;
    closed_=true;*alive_=false;
    std::vector<Result>replies;for(auto&[context,state]:states_){*state.policy_owner=false;for(auto&[id,route]:state.routes)route->Remove();}
    for(auto&[operation,batch]:batches_)replies.push_back(std::move(batch.reply));
    for(auto&[owner,target]:targets_)if(target.ready)replies.push_back(std::move(target.ready));
    states_.clear();batches_.clear();targets_.clear();waits_.Clear();for(auto&reply:replies)reply(false,"Native context routing is closed");
  }
  class RemovalReservation;
  std::unique_ptr<RemovalReservation> PrepareRemoveAll(int operation,int context,double deadline,Result reply);
  bool RemovalCurrent(const RemovalReservation&prepared)const;
  // The two-family coordinator validates both reservations first. Neither
  // commit dispatches callbacks; physical dispatch starts only afterward.
  void DispatchRemoval(RemovalReservation&prepared);
  bool OwnsPolicy(int context,const NativeContextRouteRegistration&policy)const{const auto state=states_.find(context);return !closed_&&state!=states_.end()&&policy.ClaimedBy(state->second.policy_owner,context);}
  NativeRouteCallbackSnapshot CaptureCallbacks(int context)const{if(closed_||!states_.contains(context))return {};return GlobalRouteCallbackLeases().Capture([this,context](const NativeContextRouteRegistration&policy){return OwnsPolicy(context,policy);});}
  size_t pending()const{return batches_.size()+waits_.pending();}
  size_t targets()const{return targets_.size();}
  size_t physical()const{return physical_->receipts.size();}
 private:
  friend class NativeContextUnrouteAll;
  bool CommitRemoval(RemovalReservation&prepared);
  struct Task{Mutation mutation;Route route;int batch;double deadline;uint64_t generation=0;};
  struct State{std::shared_ptr<bool>policy_owner=std::make_shared<bool>(true);std::map<std::string,Route>routes;std::vector<std::string>order;size_t bytes=0;int operation=0;};
  struct Target{int context=0;uint64_t generation=0;Apply apply;Result ready;std::deque<Task>queue;std::map<std::string,Route>claims;bool busy=false;};
  struct Batch{uint64_t generation=0;int context=0;Mutation mutation=Mutation::Install;double deadline=0;std::set<std::string>ids;std::set<std::pair<std::string,std::string>>pending;Result reply;};
  struct Physical{uint64_t serial=0;std::set<uint64_t>receipts;};
  bool Admit(int operation,const std::map<int,State>::iterator&state,double deadline,const Result&reply)const{
    return !closed_&&serial_!=UINT64_MAX&&operation>0&&reply&&std::isfinite(deadline)&&deadline>=0&&(!deadline||now_()<deadline)&&state!=states_.end()&&!state->second.operation&&batches_.size()<32&&!batches_.contains(operation)&&!waits_.Contains(operation);
  }
  bool Room(int context,size_t count)const{for(const auto&[owner,target]:targets_)if(target.context==context&&(target.queue.size()>128||count>128-target.queue.size()))return false;return true;}
  static void PruneClaims(Target&target){std::erase_if(target.claims,[](const auto&entry){return !entry.second->available();});}
  static bool Claims(const Target&target,const Route&route){return target.claims.contains(route->id())||std::any_of(target.queue.begin(),target.queue.end(),[&](const Task&task){return task.mutation==Mutation::Install&&task.route==route;});}
  static void Erase(State&state,Route route){route->Remove();state.bytes-=route->bytes();state.routes.erase(route->id());std::erase(state.order,route->id());}
  static void Prune(State&state){std::vector<Route>expired;for(const auto&[id,route]:state.routes)if(!route->available())expired.push_back(route);for(const auto&route:expired)Erase(state,route);}
  void Start(int operation,const std::vector<std::string>&owners){
    if(batches_.at(operation).pending.empty()){Finish(operation,true,"");return;}
    auto alive=alive_;for(const auto&owner:owners){if(!*alive)return;Pump(owner);}
  }
  void Rollback(int context,const std::set<std::string>&ids){
    auto state=states_.find(context);if(state==states_.end())return;std::vector<Route>removed;
    for(const auto&id:ids){auto found=state->second.routes.find(id);if(found!=state->second.routes.end()){removed.push_back(found->second);Erase(state->second,found->second);}}
    std::vector<std::string>owners;std::vector<std::string>overflow;
    for(auto&[owner,target]:targets_)if(target.context==context){bool queued=false;for(const auto&route:removed)if(Claims(target,route)){if(target.queue.size()>=256){overflow.push_back(owner);break;}target.queue.push_back({Mutation::Remove,route,0,0,0});queued=true;}if(queued)owners.push_back(owner);}
    auto alive=alive_;auto failure=failure_;for(const auto&owner:overflow){if(!*alive)return;failure(owner,"Native context route rollback queue exceeded capacity; actual channel must close");}
    for(const auto&owner:owners){if(!*alive)return;Pump(owner);}
  }
  void Finish(int operation,bool success,const std::string&error,uint64_t expected=0){
    auto found=batches_.find(operation);if(found==batches_.end()||(expected&&found->second.generation!=expected))return;auto batch=std::move(found->second);batches_.erase(found);
    const bool expired=batch.deadline&&now_()>=batch.deadline;const std::string reason=expired?"Native context route original deadline exceeded":error;success=success&&!expired;
    auto state=states_.find(batch.context);if(state!=states_.end()&&state->second.operation==operation)state->second.operation=0;
    auto reply=std::move(batch.reply);auto alive=alive_;if(!success&&batch.mutation==Mutation::Install)Rollback(batch.context,batch.ids);if(*alive)reply(success,reason);else reply(false,"Native context routing closed during policy rollback");
  }
  void Pump(const std::string&owner){
    auto found=targets_.find(owner);if(closed_||found==targets_.end()||found->second.busy)return;
    auto&target=found->second;
    // An exhausted/removed installation needs no new browser command. Removal
    // still follows any genuinely dispatched earlier installation.
    while(!target.queue.empty()&&target.queue.front().mutation==Mutation::Install&&!target.queue.front().route->available()){
      auto task=std::move(target.queue.front());target.queue.pop_front();if(task.batch){auto batch=batches_.find(task.batch);if(batch!=batches_.end()&&batch->second.generation==task.generation){batch->second.pending.erase({owner,task.route->id()});if(batch->second.pending.empty()){auto alive=alive_;Finish(task.batch,true,"",task.generation);if(!*alive)return;Pump(owner);return;}}}
    }
    if(target.queue.empty()){auto ready=std::move(target.ready);if(ready)ready(true,"");return;}
    auto task=std::move(target.queue.front());target.queue.pop_front();const auto generation=target.generation;
    if(physical_->receipts.size()>=1024||physical_->serial==UINT64_MAX){auto failure=failure_;auto alive=alive_;failure(owner,"Native context route physical receipt capacity exceeded; actual channel must close");if(*alive&&task.batch)Finish(task.batch,false,"Native context route physical receipt capacity exceeded",task.generation);return;}
    target.busy=true;if(task.mutation==Mutation::Install)target.claims[task.route->id()]=task.route;
    const auto token=++physical_->serial;physical_->receipts.insert(token);auto physical=physical_;auto apply=target.apply;
    apply(task.mutation,task.route,task.deadline,[this,alive=alive_,physical,token,owner,generation,task](bool ok,const std::string&error){
      if(!physical->receipts.erase(token)||!*alive)return;
      auto found=targets_.find(owner);if(found==targets_.end()||found->second.generation!=generation)return;
      auto&target=found->second;target.busy=false;
      if(task.mutation==Mutation::Remove||!ok)target.claims.erase(task.route->id());
      if(task.batch){auto batch=batches_.find(task.batch);if(batch!=batches_.end()&&batch->second.generation==task.generation){batch->second.pending.erase({owner,task.route->id()});if(!ok||batch->second.pending.empty())Finish(task.batch,ok,error,task.generation);}}
      if(!*alive)return;
      found=targets_.find(owner);if(found==targets_.end()||found->second.generation!=generation)return;
      if(!ok){auto ready=std::move(found->second.ready);auto failure=failure_;if(ready)ready(false,error);if(*alive)failure(owner,error);return;}
      Pump(owner);
    });
  }
  std::shared_ptr<bool>alive_=std::make_shared<bool>(true);std::shared_ptr<Physical>physical_=std::make_shared<Physical>();
  Failure failure_;std::function<double()>now_;NativeRouteCallbackWaits waits_;bool closed_=false;uint64_t serial_=0;
  std::map<int,State>states_;std::map<std::string,Target>targets_;std::map<int,Batch>batches_;
};
#include "controllers/network/context_route_removal.inc"
}
