// SPDX-License-Identifier: MIT
#pragma once
#include "accepted_work_origin.h"
#include <atomic>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace suitecut {
// Process lifetime identity: later Engine instances cannot alias an invocation
// whose genuine asynchronous owner is still physically retained.
inline uint64_t NextNativeLocatorHandlerInvocation(uint64_t maximum)noexcept{
  static std::atomic<uint64_t> sequence{0};auto value=sequence.load(std::memory_order_relaxed);
  while(value<maximum){if(sequence.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value+1;}
  return 0;
}
// Actual asynchronous callback/captured-state lifetime, separate from CEF Task
// and Inspector tickets. Logical cancellation cannot discharge this receipt.
class NativeLocatorHandlerAsyncWork {
 public:
  NativeLocatorHandlerAsyncWork():origin_(CurrentNativeWorkOrigin()) {
    if(!origin_.locator_handler_invocation)return;
    auto& state=State();std::lock_guard lock(state.mutex);
    auto found=state.pending.find(origin_.locator_handler_invocation);
    if(found==state.pending.end()) {
      if(state.pending.size()>=64)std::terminate();
      state.pending.emplace(origin_.locator_handler_invocation,1);
    } else {
      if(found->second==std::numeric_limits<size_t>::max())std::terminate();
      ++found->second;
    }
  }
  ~NativeLocatorHandlerAsyncWork(){
    if(!origin_.locator_handler_invocation)return;
    auto& state=State();std::lock_guard lock(state.mutex);
    const auto found=state.pending.find(origin_.locator_handler_invocation);
    if(found==state.pending.end()||!found->second)std::terminate();
    if(!--found->second)state.pending.erase(found);
  }
  NativeLocatorHandlerAsyncWork(const NativeLocatorHandlerAsyncWork&)=delete;
  NativeLocatorHandlerAsyncWork& operator=(const NativeLocatorHandlerAsyncWork&)=delete;
  NativeWorkOrigin Origin()const noexcept{return origin_;}
  static bool Ready(uint64_t invocation){
    if(!invocation)return false;
    auto& state=State();std::lock_guard lock(state.mutex);return !state.pending.contains(invocation);
  }
 private:
  struct Ledger{std::mutex mutex;std::map<uint64_t,size_t>pending;};
  static Ledger& State(){static Ledger state;return state;}
  const NativeWorkOrigin origin_;
};
inline std::shared_ptr<NativeLocatorHandlerAsyncWork> RetainNativeLocatorHandlerAsync(){
  return CurrentNativeWorkOrigin().locator_handler_invocation?std::make_shared<NativeLocatorHandlerAsyncWork>():nullptr;
}
// The receipt precedes the callable, so captured native owners are destroyed
// before the final receipt is discharged. Copies share that exact ownership.
template<class R,class...Args>
std::function<R(Args...)> BindNativeLocatorHandlerAsync(std::function<R(Args...)> callback){
  if(!callback||!CurrentNativeWorkOrigin().locator_handler_invocation)return callback;
  struct Owned {
    NativeLocatorHandlerAsyncWork receipt;
    std::function<R(Args...)> callback;
    explicit Owned(std::function<R(Args...)> actual):callback(std::move(actual)){}
    ~Owned(){const NativeWorkOriginFrame origin(receipt.Origin());callback={};}
  };
  auto owned=std::make_shared<Owned>(std::move(callback));
  return [owned=std::move(owned)](Args...args)->R{
    const NativeWorkOriginFrame origin(owned->receipt.Origin());
    return owned->callback(std::forward<Args>(args)...);
  };
}
} // namespace suitecut
