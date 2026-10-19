// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>
namespace suitecut {
// Actual producer admission audit. It never advertises an uninstalled physical
// lane as zero. These currently unattested native-awaited callbacks force the
// existing fresh-host path once an actual publication invokes one. Merely
// retaining JS timers/listeners or installing a definition does not enter it.
class AcceptedPublicationAudit {
 public:
  enum Reason:uint64_t {LookupCallback=1,BindingCallback=2,PredicateCallback=4,ScreenshotRestore=8,InfrastructureFailure=16,ActionCallback=32};
  void Published(std::string_view actual_type)noexcept{
    if(actual_type=="error")unattested_.fetch_or(InfrastructureFailure,std::memory_order_relaxed);
    else if(actual_type=="httplookup")unattested_.fetch_or(LookupCallback,std::memory_order_relaxed);
    else if(actual_type=="actionstage")unattested_.fetch_or(ActionCallback,std::memory_order_relaxed);
    else if(actual_type=="binding")unattested_.fetch_or(BindingCallback,std::memory_order_relaxed);
    else if(actual_type=="eventpredicate"||actual_type=="urlpredicate"||actual_type=="contextworkerpredicate")unattested_.fetch_or(PredicateCallback,std::memory_order_relaxed);
  }
  // Called at real assertion/capture admission, before any widget override.
  void ScreenshotAdmitted()noexcept{unattested_.fetch_or(ScreenshotRestore,std::memory_order_relaxed);}
  uint64_t Unattested()const noexcept{return unattested_.load(std::memory_order_relaxed);}
 private:
  std::atomic<uint64_t> unattested_{0};
};
} // namespace suitecut
