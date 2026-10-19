// SPDX-License-Identifier: MIT
#pragma once
#include <vector>
#include "har_work_receipts.h"
namespace suitecut {
// One genuine Fetch pause may traverse several HAR fallback policies. Every
// selected policy remains owned until that same actual request has a successful
// terminal Fetch ACK (or its real native channel closes). This chain survives
// route IDs, logical operation cancellation and fallback into a host handler.
class NativeHARFetchWork {
 public:
  using Policy=NativeHARRouteRegistration::Policy;
  bool Add(uint64_t scope,const std::string&session,Policy policy){
    if(completed_||!scope||!policy||(!entries_.empty()&&(scope!=scope_||session!=session_)))return false;
    for(const auto&entry:entries_)if(entry.policy==policy)return true;
    if(entries_.size()>=16)return false;
    entries_.reserve(16);auto selected_session=session;
    auto admission=GlobalHARWorkLeases().Issue(scope,session,policy,NativeHARWorkLeases::Kind::ReplayFetch);if(!admission)return false;
    entries_.push_back({std::move(policy),std::move(*admission)});scope_=scope;session_.swap(selected_session);return true;
  }
  // A rejected terminal command is an actual failure observation, but leaves
  // the request paused. A subsequently sent failRequest also needs its ACK.
  void Failed(const std::string&error){for(auto&entry:entries_)entry.admission.observe_failure(error);}
  void TerminalACK(){if(completed_)return;completed_=true;for(auto&entry:entries_)entry.admission.acknowledge(true,"");entries_.clear();}
  // Call only for an unsent pause erased by actual target detach, or after
  // actual OnBeforeClose. A dispatched command's closure retains its own chain.
  void ActualChannelClosed(){if(completed_)return;completed_=true;for(auto&entry:entries_)entry.admission.acknowledge(false,"Actual native HAR Fetch channel closed");entries_.clear();}
  size_t size()const{return entries_.size();}
 private:
  struct Entry {Policy policy;NativeHARWorkLeases::Admission admission;};
  uint64_t scope_=0;std::string session_;bool completed_=false;std::vector<Entry>entries_;
};
}
