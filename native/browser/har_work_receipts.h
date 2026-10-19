// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "har_route_registration.h"
namespace suitecut {
// Native HAR work has no language callback token. Only its actual native
// producer ACK or exact native channel-close boundary may settle this receipt.
class NativeHARWorkReceipt {
 public:
  ~NativeHARWorkReceipt()=default;
  NativeHARWorkReceipt(const NativeHARWorkReceipt&)=delete;
  NativeHARWorkReceipt&operator=(const NativeHARWorkReceipt&)=delete;
  bool settled()const{return settled_;}
  bool failed()const{return failed_;}
  bool unavailable()const{return unavailable_;}
  const std::string& error()const{return error_;}
 private:
  friend class NativeHARWorkLeases;
  NativeHARWorkReceipt()=default;
  bool settled_=false,failed_=false,unavailable_=false;
  std::string error_;
};
using NativeHARWorkSnapshot=std::vector<std::shared_ptr<const NativeHARWorkReceipt>>;
class NativeHARWorkLeases {
 public:
  using Policy=NativeHARRouteRegistration::Policy;
  using Ack=std::function<void(bool,const std::string&)>;
  using Failure=std::function<void(const std::string&)>;
  enum class Kind {ReplayFetch,PostRead};
  struct Admission {std::shared_ptr<const NativeHARWorkReceipt> receipt;Ack acknowledge;Failure observe_failure;};
  static constexpr size_t kCapacity=2048,kSnapshotCapacity=128;
  NativeHARWorkLeases()=default;
  NativeHARWorkLeases(const NativeHARWorkLeases&)=delete;
  NativeHARWorkLeases&operator=(const NativeHARWorkLeases&)=delete;
  // Observation loss is not physical completion. Actual callback closures keep
  // the state/archive alive even after the receiver object is destroyed.
  ~NativeHARWorkLeases(){for(const auto&[id,entry]:state_->entries){(void)id;entry.receipt->unavailable_=true;}}
  std::optional<Admission> Issue(uint64_t scope,const std::string&session,Policy policy,Kind kind){
    if(!scope||session.size()>128||!policy||state_->serial==UINT64_MAX||state_->entries.size()>=kCapacity)return {};
    const auto token=state_->serial+1;auto receipt=std::shared_ptr<NativeHARWorkReceipt>(new NativeHARWorkReceipt);receipt->error_.reserve(2048);auto state=state_;
    // Build all potentially allocating observer/callback data before admitting
    // the physical entry. Dropping an ACK closure is deliberately not an ACK.
    Ack acknowledge=[state,token,scope](bool ok,const std::string&error){
      const auto found=state->entries.find(token);if(found==state->entries.end()||found->second.scope!=scope)return;
      auto receipt=found->second.receipt;
      if(!ok&&!receipt->failed_){receipt->error_.assign(error.data(),std::min<size_t>(error.size(),2048));receipt->failed_=true;}
      receipt->settled_=true;state->entries.erase(found);
    };
    Failure observe_failure=[state,token,scope](const std::string&error){
      const auto found=state->entries.find(token);if(found==state->entries.end()||found->second.scope!=scope)return;
      auto receipt=found->second.receipt;if(receipt->failed_)return;
      receipt->error_.assign(error.data(),std::min<size_t>(error.size(),2048));receipt->failed_=true;
    };
    state_->entries.emplace(token,Entry{scope,session,std::move(policy),kind,receipt});state_->serial=token;
    return Admission{std::move(receipt),std::move(acknowledge),std::move(observe_failure)};
  }
  // Snapshot is exact policy identity, not URL/id/archive equality. The manager
  // predicate must use the native context's actual admitted policy ownership.
  std::optional<NativeHARWorkSnapshot> Capture(const std::function<bool(const Policy&)>&matches,size_t maximum=kSnapshotCapacity)const{
    if(!matches||maximum>kSnapshotCapacity)return {};
    NativeHARWorkSnapshot snapshot;
    for(const auto&[id,entry]:state_->entries){(void)id;if(!matches(entry.policy))continue;if(snapshot.size()>=maximum)return {};snapshot.push_back(entry.receipt);}
    return snapshot;
  }
  void ObserverUnavailable(uint64_t scope){for(const auto&[id,entry]:state_->entries){(void)id;if(entry.scope==scope)entry.receipt->unavailable_=true;}}
  // These APIs require a real native target-detach / OnBeforeClose receipt.
  // Logical unroute, timeout, renderer navigation or manager cancellation must
  // never call them. Main empty-session and whole-scope close are distinct.
  void SessionClosed(uint64_t scope,const std::string&session){Close(scope,&session);}
  void ScopeClosed(uint64_t scope){Close(scope,nullptr);}
  size_t physical()const{return state_->entries.size();}
  size_t physical(Kind kind)const{size_t count=0;for(const auto&[id,entry]:state_->entries){(void)id;if(entry.kind==kind)++count;}return count;}
 private:
  struct Entry {uint64_t scope;std::string session;Policy policy;Kind kind;std::shared_ptr<NativeHARWorkReceipt>receipt;};
  struct State {uint64_t serial=0;std::map<uint64_t,Entry>entries;};
  void Close(uint64_t scope,const std::string*session){
    for(auto it=state_->entries.begin();it!=state_->entries.end();){const auto&entry=it->second;
      if(entry.scope!=scope||(session&&entry.session!=*session)){++it;continue;}
      if(!entry.receipt->failed_){entry.receipt->error_="Actual native HAR work channel closed";}
      entry.receipt->failed_=true;entry.receipt->settled_=true;it=state_->entries.erase(it);
    }
  }
  std::shared_ptr<State>state_=std::make_shared<State>();
};
inline NativeHARWorkLeases& GlobalHARWorkLeases(){static NativeHARWorkLeases leases;return leases;}
}
