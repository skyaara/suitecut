// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "context_route_registration.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "network_glob.h"

namespace suitecut {
// UI-thread routing order. Each intercepted request retains its original URL
// match set; fallback changes to the outgoing URL never rematch later handlers.
class NativeRouteRegistry {
 public:
  struct Plan {
    std::vector<std::string> candidates;size_t next=0;std::vector<uint64_t> generations;
    Plan()=default;
    Plan(std::vector<std::string> ids,size_t cursor):candidates(std::move(ids)),next(cursor){}
    size_t bytes()const{size_t value=candidates.capacity()*sizeof(std::string)+generations.capacity()*sizeof(uint64_t);for(const auto&id:candidates)value+=id.capacity();return value;}
    Plan Single(size_t index)const{Plan result;if(index<candidates.size()&&index<generations.size()){result.candidates.push_back(candidates[index]);result.generations.push_back(generations[index]);}return result;}
  };
  struct Invocation {std::string registration;bool exhausted=false;std::shared_ptr<NativeContextRouteRegistration>policy;NativeRouteRegistrationOrder order{};};
  struct Candidate {
    size_t index=0;std::string registration;uint64_t generation=0;
    bool host_matcher=false;std::shared_ptr<NativeContextRouteRegistration>policy;
  };
  bool Register(std::string id,std::string pattern,int times,bool context=false,bool host_matcher=false,NativeRouteRegistrationOrder order={}) {
    Prune();std::vector<std::string> alternatives;
    if(id.empty() || id.size()>128 || pattern.empty() || pattern.size()>8192 || times<0 || serial_==std::numeric_limits<uint64_t>::max() || registrations_.size()>=256 ||
       (host_matcher&&pattern!="**/*") || !NativeURLGlobExpand(pattern,alternatives) || std::any_of(registrations_.begin(),registrations_.end(),[&](const Registration& route){return route.ID()==id;}))return false;
    if(!order){const auto reserved=NativeRouteRegistrationOrder::Reserve();if(!reserved)return false;order=*reserved;}
    if(std::any_of(registrations_.begin(),registrations_.end(),[&](const Registration&route){return route.order==order;}))return false;
    Insert({std::move(id),std::move(pattern),times,context,++serial_,{},host_matcher,order});return true;
  }
  bool RegisterContext(std::shared_ptr<NativeContextRouteRegistration> policy) {
    Prune();
    if(!policy||!policy->available()||serial_==std::numeric_limits<uint64_t>::max()||registrations_.size()>=256||std::any_of(registrations_.begin(),registrations_.end(),[&](const Registration&route){return route.ID()==policy->id()||route.order==policy->order();}))return false;
    const auto order=policy->order();Insert({{}, {},0,true,++serial_,std::move(policy),false,order});return true;
  }
  void Remove(const std::string& id) {
    registrations_.erase(std::remove_if(registrations_.begin(),registrations_.end(),[&](const Registration& route){return id.empty() || route.ID()==id;}),registrations_.end());
  }
  bool RemoveContext(const std::shared_ptr<NativeContextRouteRegistration>&policy){
    if(!policy)return false;
    const auto found=std::find_if(registrations_.begin(),registrations_.end(),[&](const Registration&entry){return entry.shared==policy;});
    if(found==registrations_.end())return false;
    registrations_.erase(found);return true;
  }
  Plan Match(const std::string& original_url) const {
    Plan plan;
    // Public Page handlers always take precedence over context policy;
    // each scope independently retains reverse registration order.
    for(const bool context:{false,true})for(auto route=registrations_.rbegin();route!=registrations_.rend();++route)
      if(route->context==context&&route->Available()&&(route->HostMatcher()||NativeURLGlobMatches(route->Pattern(),original_url))){plan.candidates.push_back(route->ID());plan.generations.push_back(route->generation);}
    return plan;
  }
  bool Allows(const Plan&plan,const std::string&actual_url)const {
    for(size_t index=plan.next;index<plan.candidates.size();++index)
      for(const auto&route:registrations_)if(Matches(route,plan,index)&&!route.HostMatcher()&&NativeURLGlobMatches(route.Pattern(),actual_url))return true;
    return false;
  }
  bool HostPending(const Plan&plan)const{
    for(size_t index=plan.next;index<plan.candidates.size();++index)for(const auto&route:registrations_)if(Matches(route,plan,index)&&route.HostMatcher())return true;
    return false;
  }
  bool HasNext(const Plan& plan) const {
    for(size_t index=plan.next;index<plan.candidates.size();++index)
      if(std::any_of(registrations_.begin(),registrations_.end(),[&](const Registration& route){return Matches(route,plan,index);}))return true;
    return false;
  }
  // Looking at a native snapshot never consumes a context-global use. A host
  // URL matcher must return its genuine result before Decide can admit it.
  std::optional<Candidate> Peek(Plan&plan)const{
    while(plan.next<plan.candidates.size()){
      const auto index=plan.next;
      const auto route=std::find_if(registrations_.begin(),registrations_.end(),[&](const Registration&entry){return Matches(entry,plan,index);});
      if(route==registrations_.end()){++plan.next;continue;}
      return Candidate{index,route->ID(),route->generation,route->HostMatcher(),route->shared};
    }
    return std::nullopt;
  }
  std::optional<Invocation> Decide(Plan&plan,const Candidate&candidate,bool matched){
    // A delayed result must not advance a different cursor or a reused policy.
    if(candidate.index!=plan.next||candidate.index>=plan.candidates.size()||candidate.index>=plan.generations.size()||
       candidate.registration!=plan.candidates[candidate.index]||candidate.generation!=plan.generations[candidate.index])return std::nullopt;
    const auto route=std::find_if(registrations_.begin(),registrations_.end(),[&](const Registration&entry){return Matches(entry,plan,candidate.index);});
    ++plan.next;
    if(!matched||route==registrations_.end())return std::nullopt;
    bool exhausted=false;
    if(route->shared){if(!route->shared->Consume(exhausted))return std::nullopt;}else exhausted=route->remaining>0&&--route->remaining==0;
    Invocation invocation{route->ID(),exhausted,route->shared,route->order};
    if(exhausted)registrations_.erase(route);
    return invocation;
  }
  std::optional<Invocation> Next(Plan&plan){
    while(auto candidate=Peek(plan)){
      // Existing native glob/HAR callers cannot accidentally authorize a host
      // matcher by treating its all-URLs transport placeholder as a match.
      if(candidate->host_matcher)return std::nullopt;
      if(auto invocation=Decide(plan,*candidate,true))return invocation;
    }
    return std::nullopt;
  }
  std::vector<std::string> PageIDs()const{std::vector<std::string>ids;for(const auto&route:registrations_)if(!route.context)ids.push_back(route.ID());return ids;}
  size_t size() const {return registrations_.size();}
  bool empty() const {return std::none_of(registrations_.begin(),registrations_.end(),[](const Registration&route){return route.Available();});}
  void Clear() {registrations_.clear();}
 private:
  struct Registration {
    std::string id,pattern;int remaining=0;bool context=false;uint64_t generation=0;std::shared_ptr<NativeContextRouteRegistration>shared;bool host_matcher=false;NativeRouteRegistrationOrder order;
    const std::string& ID()const{return shared?shared->id():id;}
    const std::string& Pattern()const{return shared?shared->pattern():pattern;}
    bool Available()const{return !shared||shared->available();}
    bool HostMatcher()const{return shared?shared->host_matcher():host_matcher;}
  };
  void Insert(Registration route){
    const auto position=std::lower_bound(registrations_.begin(),registrations_.end(),route.order,[](const Registration&current,NativeRouteRegistrationOrder order){return current.order<order;});
    registrations_.insert(position,std::move(route));
  }
  static bool Matches(const Registration&route,const Plan&plan,size_t index){return index<plan.generations.size()&&route.generation==plan.generations[index]&&route.ID()==plan.candidates[index]&&route.Available();}
  void Prune(){std::erase_if(registrations_,[](const Registration&route){return !route.Available();});}
  uint64_t serial_=0;
  std::vector<Registration> registrations_;
};
}  // namespace suitecut
