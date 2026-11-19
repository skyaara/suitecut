// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <pthread.h>
#include <stdexcept>
#include <vector>
namespace suitecut::runner {
// Owned by one mutable matching state, never by an immutable compiled pattern.
struct GrepNFAScratch {
  std::vector<size_t> current,next,stack;
  std::vector<uint32_t> seen;
  bool active=false;
};
// Pattern nesting is bounded only by this thread's native stack, the way V8
// bounds RegExp parsing by its stack limit. The reserve leaves room to unwind
// the error instead of overflowing the stack.
inline void GrepStackCheck(){
  thread_local const uintptr_t stack_floor=[]{
    uintptr_t low=0;
#if defined(__APPLE__)
    const auto self=pthread_self();low=reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(self))-pthread_get_stacksize_np(self);
#else
    pthread_attr_t attributes;void* address=nullptr;size_t size=0;
    if(!pthread_getattr_np(pthread_self(),&attributes)){if(pthread_attr_getstack(&attributes,&address,&size))address=nullptr;pthread_attr_destroy(&attributes);}
    low=reinterpret_cast<uintptr_t>(address);
#endif
    return low?low+256*1024:0;
  }();
  char here=0;
  if(stack_floor&&reinterpret_cast<uintptr_t>(&here)<stack_floor)throw std::invalid_argument("Native grep: maximum call stack size exceeded");
}
struct GrepWork {
  // Runner installs the original suite deadline/interrupt check. Pure policy
  // callers may leave it empty; no clock, timeout or cancellation is invented.
  std::function<void()> check_stopped={};
  uint64_t until_poll=4096;
  GrepNFAScratch nfa_scratch={};
  void Charge(uint64_t amount=1){
    if(check_stopped){
      if(amount>=until_poll){until_poll=4096-((amount-until_poll)%4096);check_stopped();}
      else until_poll-=amount;
    }
  }
};
}
