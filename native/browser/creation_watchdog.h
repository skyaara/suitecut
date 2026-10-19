// SPDX-License-Identifier: MIT
#pragma once
#include "accepted_task_work.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace suitecut {
// Only Source creation's UI-thread watchdogs use this owner. The delayed CEF
// wrapper retains this state, not Source. UI cancellation destroys the actual
// callback/captures before its native work receipt. A later wrapper Execute is
// then inert. This does not exempt a queued or executing Command-origin task.
class NativeCreationWatchdog {
 public:
  explicit NativeCreationWatchdog(std::function<void()> callback,
      NativeWorkOrigin origin,AcceptedTaskWork& work=NativeProcessTaskWork())
      :pending_(std::make_unique<Invocation>(std::move(callback),origin,work)){}
  NativeCreationWatchdog(const NativeCreationWatchdog&)=delete;
  NativeCreationWatchdog& operator=(const NativeCreationWatchdog&)=delete;
  bool Pending()const noexcept{return bool(pending_);}
  // UI-thread only, just like Execute. A running invocation has already taken
  // its real payload; reentrant cancellation cannot release that local owner.
  bool Cancel()noexcept{
    if(!pending_)return false;
    pending_.reset();return true;
  }
  void Execute(){
    auto actual=std::move(pending_);
    if(actual)actual->Execute();
    // Captures and receipt really die here, even when CEF retains its empty
    // wrapper. On unwind the real frame still records the original failure.
  }
 private:
  struct Invocation {
    // Declaration order is authority: callback/captures destroy BEFORE work.
    AcceptedTaskReceipt work;
    std::function<void()> callback;
    Invocation(std::function<void()> fn,NativeWorkOrigin origin,AcceptedTaskWork& owner)
        :work(AcceptedTaskWork::Kind::WorkerMonitor,owner,origin),callback(std::move(fn)){
      if(!callback)throw std::invalid_argument("Native creation watchdog callback is missing");
    }
    void Execute(){const AcceptedTaskFrame frame(work);callback();}
  };
  std::unique_ptr<Invocation> pending_;
};
} // namespace suitecut
