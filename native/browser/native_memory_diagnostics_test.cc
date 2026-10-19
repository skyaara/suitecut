// SPDX-License-Identifier: MIT
#include "native_memory_diagnostics.h"
#include <cassert>
#include <stdexcept>
#include <thread>
#include <vector>

int main(){
  suitecut::NativeObjectLifetimes counters;
  struct Owner {
    suitecut::NativeSourceLifetimeReceipt receipt;
    explicit Owner(suitecut::NativeObjectLifetimes& counters,bool fail=false):receipt(counters){if(fail)throw std::runtime_error("construction failed");}
  };
  {
    Owner owner(counters);
    const auto sample=counters.Inspect();assert(sample.constructed==1&&sample.destroyed==0&&sample.live==1);
  }
  try {Owner failed(counters,true);assert(false);}catch(const std::runtime_error&){}
  auto sample=counters.Inspect();assert(sample.constructed==2&&sample.destroyed==2&&sample.live==0);
  std::vector<std::thread> threads;
  for(int thread=0;thread<8;++thread)threads.emplace_back([&]{for(int iteration=0;iteration<10000;++iteration){Owner owner(counters);}});
  for(auto& thread:threads)thread.join();
  sample=counters.Inspect();assert(sample.constructed==80002&&sample.destroyed==80002&&sample.live==0);
  const auto allocator=suitecut::InspectNativeGlibcMemory();
  if(allocator.available){assert(allocator.arena_bytes>=allocator.allocated_bytes);assert(allocator.arena_bytes>=allocator.free_bytes);}
}
