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
#include <vector>
#include "har_route_registration.h"
#include "network_glob.h"
namespace suitecut {
// Actual context policy shared by public Sources and private worker brokers.
// Target installers call only NetworkControls BindReplay/UnbindReplay. No page
// identity is invented for a broker and no replay file is read by the SDK.
class NativeContextHARRoutes {
 public:
  using Result=std::function<void(bool,const std::string&)>;
  struct Route {std::string id,pattern;std::shared_ptr<const NativeHARReplay>archive;bool fallback=false;NativeRouteRegistrationOrder order{};NativeHARRouteRegistration::Policy policy{};};
  enum class Mutation {Install,Remove};
  using Apply=std::function<void(Mutation,const Route&,double,Result)>;
  using Failure=std::function<void(const std::string&,const std::string&)>;
  explicit NativeContextHARRoutes(Failure failure,std::function<double()> now={}):failure_(std::move(failure)),now_(std::move(now)){}
  ~NativeContextHARRoutes(){Clear();}
  bool Seed(int context){return !closed_&&context>0&&states_.size()<32&&states_.emplace(context,State{}).second;}
  bool Attach(int context,const std::string&owner,Apply apply,Result ready){
    const auto state=states_.find(context);
    if(closed_||serial_>=9007199254740991ULL||state==states_.end()||owner.empty()||owner.find('\0')!=std::string::npos||owner.size()>128||!apply||!ready||targets_.size()>=192||targets_.contains(owner))return false;
    Target target;target.context=context;target.generation=++serial_;target.apply=std::move(apply);target.ready=std::move(ready);target.queue={state->second.order.begin(),state->second.order.end()};
    targets_.emplace(owner,std::move(target));
    if(state->second.owner&&batches_.contains(state->second.owner))batches_.at(state->second.owner).pending.insert(owner);
    Pump(owner);return true;
  }
  bool Install(int owner,int context,Route route,double deadline,Result done){
    const auto state=states_.find(context);std::vector<std::string>patterns;
    if(closed_||serial_>=9007199254740991ULL||owner<1||!done||!std::isfinite(deadline)||deadline<0||state==states_.end()||state->second.owner||batches_.size()+removals_.size()>=32||batches_.contains(owner)||removals_.contains(owner)||state->second.routes.size()>=16||route.id.empty()||route.id.find('\0')!=std::string::npos||route.id.size()>128||route.pattern.size()>8192||!route.archive||!NativeURLGlobExpand(route.pattern,patterns)||state->second.routes.contains(route.id))return false;
    const auto bytes=route.archive->bytes();const auto archive=route.archive.get();
    if(bytes>67108864-state->second.bytes||(!archives_.contains(archive)&&bytes>67108864-policy_bytes_))return false;
    if(!route.policy)route.policy=NativeHARRouteRegistration::Create(route.archive,route.fallback,route.order);
    if(!route.policy||route.policy->archive()!=route.archive||route.policy->fallback()!=route.fallback||(route.order&&route.order!=route.policy->order())||!route.policy->Claim(state->second.policy_owner,context))return false;
    route.order=route.policy->order();
    auto use=archives_.find(archive);if(use==archives_.end()){archives_.emplace(archive,ArchiveUse{route.archive,1});policy_bytes_+=bytes;}else ++use->second.references;
    state->second.bytes+=bytes;
    const auto id=route.id;state->second.routes.emplace(id,std::move(route));state->second.order.push_back(id);state->second.owner=owner;
    Batch batch;batch.generation=++serial_;const auto batch_generation=batch.generation;batch.context=context;batch.route=id;batch.deadline=deadline;batch.reply=std::move(done);
    std::vector<std::string>targets;for(auto&[name,target]:targets_)if(target.context==context){target.queue.push_back(id);batch.pending.insert(name);targets.push_back(name);}
    batches_.emplace(owner,std::move(batch));if(targets.empty()){Finish(owner,batch_generation);return true;}
    auto alive=alive_;for(const auto&target:targets){if(!*alive)break;Pump(target);}return true;
  }
  void Detach(const std::string&owner){
    const auto target=targets_.find(owner);if(target==targets_.end())return;
    auto ready=std::move(target->second.ready);targets_.erase(target);
    std::vector<std::pair<int,uint64_t>>finished;for(auto&[id,batch]:batches_)if(batch.pending.erase(owner)){batch.success=false;batch.error="Actual HAR replay target closed during installation";if(batch.pending.empty())finished.emplace_back(id,batch.generation);}
    auto alive=alive_;for(const auto&[id,generation]:finished){if(!*alive)break;Finish(id,generation);}
    std::vector<std::pair<int,uint64_t>>removed;
    for(const auto&[id,batch]:removals_)if(std::any_of(batch.pending.begin(),batch.pending.end(),[&](const auto&key){return key.first==owner;}))removed.emplace_back(id,batch.generation);
    for(const auto&[id,generation]:removed){if(!*alive)break;FinishRemoval(id,generation,false,"Actual HAR replay target closed before removal acknowledgement");}
    if(ready)ready(false,"Actual HAR replay target closed during installation");
  }
  void Cancel(int owner){
    const auto removal=removals_.find(owner);if(removal!=removals_.end()){const int context=removal->second.context;removals_.erase(removal);const auto state=states_.find(context);if(state!=states_.end()&&state->second.owner==owner)state->second.owner=0;return;}
    const auto found=batches_.find(owner);if(found==batches_.end())return;const auto context=found->second.context;const auto route=found->second.route;batches_.erase(found);
    const auto state=states_.find(context);if(state!=states_.end()&&state->second.owner==owner)state->second.owner=0;Drop(context,route);
  }
  void Tick(double now){
    std::vector<std::pair<int,uint64_t>>expired;for(const auto&[id,batch]:batches_)if(batch.deadline&&now>=batch.deadline)expired.emplace_back(id,batch.generation);
    auto alive=alive_;for(const auto&[id,generation]:expired){if(!*alive)break;const auto found=batches_.find(id);if(found==batches_.end()||found->second.generation!=generation)continue;found->second.success=false;found->second.error="Native context HAR replay original deadline exceeded";Finish(id,generation);}
    std::vector<std::pair<int,uint64_t>>removed;for(const auto&[id,batch]:removals_)if(batch.deadline&&now>=batch.deadline)removed.emplace_back(id,batch.generation);
    for(const auto&[id,generation]:removed){if(!*alive)break;FinishRemoval(id,generation,false,"Native context HAR removal original deadline exceeded");}
  }
  void Closed(int context){
    const auto state=states_.find(context);if(state==states_.end())return;const auto owner=state->second.owner;*state->second.policy_owner=false;for(const auto&[id,route]:state->second.routes)ReleaseArchive(route.archive);states_.erase(state);
    std::vector<Result>callbacks;for(auto it=targets_.begin();it!=targets_.end();)if(it->second.context==context){if(it->second.ready)callbacks.push_back(std::move(it->second.ready));it=targets_.erase(it);}else ++it;
    const auto batch=batches_.find(owner);if(batch!=batches_.end()){callbacks.push_back(std::move(batch->second.reply));batches_.erase(batch);}
    for(auto it=removals_.begin();it!=removals_.end();)if(it->second.context==context){callbacks.push_back(std::move(it->second.reply));it=removals_.erase(it);}else ++it;
    for(auto&callback:callbacks)if(callback)callback(false,"Actual native HAR replay context closed");
  }
  void Clear(){
    if(closed_)return;
    closed_=true;*alive_=false;
    std::vector<Result>callbacks;for(auto&[id,batch]:batches_)if(batch.reply)callbacks.push_back(std::move(batch.reply));for(auto&[owner,target]:targets_)if(target.ready)callbacks.push_back(std::move(target.ready));
    for(auto&[id,batch]:removals_)if(batch.reply)callbacks.push_back(std::move(batch.reply));
    for(auto&[context,state]:states_){(void)context;*state.policy_owner=false;}
    removals_.clear();batches_.clear();targets_.clear();states_.clear();archives_.clear();policy_bytes_=0;for(auto&callback:callbacks)callback(false,"Native HAR replay policy is closed");
  }
  class RemovalReservation;
  std::unique_ptr<RemovalReservation> PrepareRemoveAll(int owner,int context,double deadline,Result done);
  bool RemovalCurrent(const RemovalReservation&prepared)const;
  void DispatchRemoval(RemovalReservation&prepared);
  bool OwnsPolicy(int context,const NativeHARRouteRegistration::Policy&policy)const{const auto state=states_.find(context);return !closed_&&state!=states_.end()&&policy&&policy->ClaimedBy(state->second.policy_owner,context);}
  size_t retained_bytes()const{return policy_bytes_;}
  size_t pending()const{return batches_.size()+removals_.size();}
  size_t targets()const{return targets_.size();}
  size_t physical()const{return physical_->leases.size();}
 private:
  friend class NativeContextUnrouteAll;
  bool CommitRemoval(RemovalReservation&prepared);
  struct ArchiveUse{std::shared_ptr<const NativeHARReplay>archive;size_t references;};
  struct State {std::shared_ptr<bool>policy_owner=std::make_shared<bool>(true);std::map<std::string,Route>routes;std::vector<std::string>order;int owner=0;size_t bytes=0;};
  struct RemoveTask {Route route;int batch=0;uint64_t generation=0;double deadline=0;};
  struct Target {int context=0;uint64_t generation=0;bool busy=false;Apply apply;Result ready;std::deque<std::string>queue;std::map<std::string,NativeHARRouteRegistration::Policy>claims;std::deque<RemoveTask>removals;};
  struct Batch {uint64_t generation=0;int context=0;std::string route,error;double deadline=0;bool success=true;std::set<std::string>pending;Result reply;};
  struct RemovalBatch {uint64_t generation=0;int context=0;double deadline=0;std::set<std::pair<std::string,std::string>>pending;Result reply;};
  struct Physical {uint64_t serial=0;std::map<uint64_t,int>leases;};
  void Call(const std::string&owner,int context,Apply apply,Mutation mutation,const Route&route,double deadline,Result reply){
    if(physical_->serial>=9007199254740991ULL||physical_->leases.size()>=1024){auto failure=failure_;reply(false,"Native context HAR physical callback capacity exceeded");failure(owner,"Native context HAR physical callback capacity exceeded");return;}
    const auto token=++physical_->serial;physical_->leases.emplace(token,context);auto physical=physical_;
    apply(mutation,route,deadline,[physical,token,policy=route.policy,reply=std::move(reply)](bool ok,const std::string&error)mutable{if(physical->leases.erase(token)){policy.reset();auto actual=std::move(reply);actual(ok,error);}});
  }
  void Pump(const std::string&owner){
    const auto found=targets_.find(owner);if(closed_||found==targets_.end()||found->second.busy)return;auto&target=found->second;const auto state=states_.find(target.context);if(state==states_.end())return;
    if(PumpRemoval(owner))return;
    while(!target.queue.empty()&&!state->second.routes.contains(target.queue.front()))target.queue.pop_front();
    if(target.queue.empty()){auto ready=std::move(target.ready);if(ready)ready(true,"");return;}
    const auto route=state->second.routes.at(target.queue.front());target.queue.pop_front();target.busy=true;target.claims[route.id]=route.policy;
    double deadline=0;int batch_owner=0;uint64_t batch_generation=0;if(state->second.owner&&batches_.contains(state->second.owner)){const auto&batch=batches_.at(state->second.owner);if(batch.route==route.id){deadline=batch.deadline;batch_owner=state->second.owner;batch_generation=batch.generation;}}
    const auto generation=target.generation;const int context=target.context;auto apply=target.apply;
    Call(owner,context,std::move(apply),Mutation::Install,route,deadline,[this,alive=alive_,owner,context,generation,batch_owner,batch_generation,id=route.id](bool ok,const std::string&error){
      if(!*alive)return;
      const auto target=targets_.find(owner);if(target==targets_.end()||target->second.generation!=generation)return;target->second.busy=false;
      if(!ok)target->second.claims.erase(id);
      const auto batch=batches_.find(batch_owner);if(batch!=batches_.end()&&batch->second.generation==batch_generation&&batch->second.context==context&&batch->second.route==id){batch->second.pending.erase(owner);if(!ok){batch->second.success=false;batch->second.error=error;}if(batch->second.pending.empty())Finish(batch_owner,batch_generation);}
      if(!*alive)return;
      const auto current=targets_.find(owner);if(current==targets_.end()||current->second.generation!=generation)return;
      if(!ok){auto ready=std::move(current->second.ready);if(ready)ready(false,error);else failure_(owner,error);return;}
      Pump(owner);
    });
  }
  void Drop(int context,const std::string&id){
    const auto state=states_.find(context);if(state==states_.end())return;const auto found=state->second.routes.find(id);if(found==state->second.routes.end())return;auto route=found->second;state->second.bytes-=route.archive->bytes();ReleaseArchive(route.archive);state->second.routes.erase(found);std::erase(state->second.order,id);
    std::vector<std::string>targets;for(const auto&[owner,target]:targets_)if(target.context==context&&target.claims.contains(id))targets.push_back(owner);
    // Unbind immediately behind any already-dispatched native enable, without
    // waiting for its ACK. Queue-only installs are removed before they can send.
    auto alive=alive_;for(const auto&owner:targets){if(!*alive)break;const auto target=targets_.find(owner);if(target==targets_.end())continue;target->second.claims.erase(id);auto apply=target->second.apply;
      Call(owner,context,std::move(apply),Mutation::Remove,route,0,[this,alive=alive_,owner](bool ok,const std::string&error){if(*alive&&!ok)failure_(owner,error);});}
  }
  bool PumpRemoval(const std::string&owner);
  void FinishRemoval(int owner,uint64_t generation,bool ok,const std::string&error);
  void ReleaseArchive(const std::shared_ptr<const NativeHARReplay>&archive){const auto found=archives_.find(archive.get());if(found!=archives_.end()&&!--found->second.references){policy_bytes_-=found->second.archive->bytes();archives_.erase(found);}}
  void Finish(int owner,uint64_t generation){
    const auto found=batches_.find(owner);if(found==batches_.end()||found->second.generation!=generation)return;auto batch=std::move(found->second);batches_.erase(found);if(now_&&batch.deadline&&now_()>=batch.deadline){batch.success=false;batch.error="Native context HAR replay original deadline exceeded";}const auto state=states_.find(batch.context);if(state!=states_.end()&&state->second.owner==owner)state->second.owner=0;
    if(!batch.success)Drop(batch.context,batch.route);
    auto reply=std::move(batch.reply);reply(batch.success,batch.error);
  }
  std::shared_ptr<bool>alive_=std::make_shared<bool>(true);std::shared_ptr<Physical>physical_=std::make_shared<Physical>();Failure failure_;std::function<double()>now_;uint64_t serial_=0;bool closed_=false;
  std::map<const NativeHARReplay*,ArchiveUse>archives_;size_t policy_bytes_=0;
  std::map<int,State>states_;std::map<std::string,Target>targets_;std::map<int,Batch>batches_;std::map<int,RemovalBatch>removals_;
};
#include "context_har_removal.inc"
}
