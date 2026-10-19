// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "network_glob.h"
#include "route_registration_order.h"
namespace suitecut {
// One UI-thread policy object belongs to one actual context. Every Source and
// broker references this same counter; installing a target never copies uses.
class NativeContextRouteRegistration {
 public:
  static std::shared_ptr<NativeContextRouteRegistration> Create(std::string id,std::string pattern,int times,bool host_matcher=false,NativeRouteRegistrationOrder order={}) {
    std::vector<std::string> expanded;
    if(id.empty()||id.size()>128||id.find('\0')!=std::string::npos||pattern.empty()||pattern.size()>8192||pattern.find('\0')!=std::string::npos||times<0||times>1000000||(host_matcher&&pattern!="**/*")||!NativeURLGlobExpand(pattern,expanded))return {};
    const size_t bytes=sizeof(NativeContextRouteRegistration)+id.capacity()+pattern.capacity();
    if(live_>=4096||bytes>4194304-retained_)return {};
    if(!order){const auto reserved=NativeRouteRegistrationOrder::Reserve();if(!reserved)return {};order=*reserved;}
    auto result=std::shared_ptr<NativeContextRouteRegistration>(new NativeContextRouteRegistration(std::move(id),std::move(pattern),times,host_matcher,order));
    ++live_;retained_+=bytes;return result;
  }
  NativeContextRouteRegistration(const NativeContextRouteRegistration&)=delete;
  NativeContextRouteRegistration& operator=(const NativeContextRouteRegistration&)=delete;
  ~NativeContextRouteRegistration(){--live_;retained_-=bytes();}
  static size_t retained_bytes(){return retained_;}
  static size_t retained_count(){return live_;}
  const std::string& id()const{return id_;}
  const std::string& pattern()const{return pattern_;}
  bool Claim(const std::shared_ptr<bool>&manager,int context){
    if(!manager||!*manager||context<1)return false;
    if(bound_)return context_==context&&manager_.lock()==manager;
    manager_=manager;context_=context;bound_=true;return true;
  }
  bool ClaimedBy(const std::shared_ptr<bool>&manager,int context)const{return manager&&*manager&&bound_&&context_==context&&manager_.lock()==manager;}
  bool host_matcher()const{return host_matcher_;}
  NativeRouteRegistrationOrder order()const{return order_;}
  bool available()const{return active_&&(!limited_||remaining_>0);}
  bool Consume(bool& exhausted){
    if(!available())return false;
    exhausted=limited_&&!--remaining_;return true;
  }
  void Remove(){active_=false;}
  size_t bytes()const{return sizeof(*this)+id_.capacity()+pattern_.capacity();}
 private:
  NativeContextRouteRegistration(std::string id,std::string pattern,int times,bool host_matcher,NativeRouteRegistrationOrder order):order_(order),id_(std::move(id)),pattern_(std::move(pattern)),remaining_(times),limited_(times>0),host_matcher_(host_matcher){}
  inline static size_t retained_=0,live_=0;
  NativeRouteRegistrationOrder order_;std::string id_,pattern_;int remaining_;bool limited_,host_matcher_=false,active_=true,bound_=false;int context_=0;std::weak_ptr<bool>manager_;
};
}
