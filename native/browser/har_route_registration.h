// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <memory>
#include "har_replay.h"
#include "route_registration_order.h"

namespace suitecut {
// One immutable, genuinely loaded archive policy. Sources, queued installers,
// admitted pauses and dispatched Fetch receipts share this exact object.
// Logical removal cannot release its archive reservation while one still owns it.
class NativeHARRouteRegistration {
 public:
  using Policy=std::shared_ptr<const NativeHARRouteRegistration>;
  static Policy Create(std::shared_ptr<const NativeHARReplay>archive,bool fallback,NativeRouteRegistrationOrder order={}){
    if(!archive||policies_>=4096)return {};
    const auto found=archives_.find(archive.get());const auto bytes=archive->bytes();
    if(found==archives_.end()&&(archives_.size()>=4096||bytes>kBytes-retained_))return {};
    if(!order){const auto reserved=NativeRouteRegistrationOrder::Reserve();if(!reserved)return {};order=*reserved;}
    if(found==archives_.end()){archives_.emplace(archive.get(),Use{bytes,1});retained_+=bytes;}else ++found->second.references;
    ++policies_;
    return Policy(new NativeHARRouteRegistration(std::move(archive),fallback,order));
  }
  NativeHARRouteRegistration(const NativeHARRouteRegistration&)=delete;
  NativeHARRouteRegistration&operator=(const NativeHARRouteRegistration&)=delete;
  ~NativeHARRouteRegistration(){
    const auto found=archives_.find(archive_.get());
    if(found!=archives_.end()&&!--found->second.references){retained_-=found->second.bytes;archives_.erase(found);}
    --policies_;
  }
  const std::shared_ptr<const NativeHARReplay>&archive()const{return archive_;}
  bool fallback()const{return fallback_;}
  NativeRouteRegistrationOrder order()const{return order_;}
  bool Claim(const std::shared_ptr<bool>&manager,int context)const{
    if(!manager||!*manager||context<1)return false;
    if(bound_)return context_==context&&manager_.lock()==manager;
    manager_=manager;context_=context;bound_=true;return true;
  }
  bool ClaimedBy(const std::shared_ptr<bool>&manager,int context)const{return manager&&*manager&&bound_&&context_==context&&manager_.lock()==manager;}
  static size_t retained_bytes(){return retained_;}
  static size_t retained_archives(){return archives_.size();}
  static size_t retained_policies(){return policies_;}
 private:
  struct Use{size_t bytes,references;};
  NativeHARRouteRegistration(std::shared_ptr<const NativeHARReplay>archive,bool fallback,NativeRouteRegistrationOrder order):archive_(std::move(archive)),fallback_(fallback),order_(order){}
  static constexpr size_t kBytes=67108864;
  inline static std::map<const NativeHARReplay*,Use>archives_;
  inline static size_t retained_=0,policies_=0;
  mutable std::weak_ptr<bool>manager_;mutable int context_=0;mutable bool bound_=false;
  std::shared_ptr<const NativeHARReplay>archive_;bool fallback_;NativeRouteRegistrationOrder order_;
};
}
