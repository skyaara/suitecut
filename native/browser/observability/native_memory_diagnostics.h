// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#if defined(__linux__)
#include <malloc.h>
#endif

namespace suitecut {
// Observational counters only; these never decide admission or native idle.
class NativeObjectLifetimes {
 public:
  struct Snapshot {uint64_t constructed,destroyed,live;};
  void Constructed() noexcept {constructed_.fetch_add(1,std::memory_order_relaxed);live_.fetch_add(1,std::memory_order_relaxed);}
  void Destroyed() noexcept {live_.fetch_sub(1,std::memory_order_relaxed);destroyed_.fetch_add(1,std::memory_order_relaxed);}
  // Each field is atomic. Concurrent construction/destruction can fall between
  // loads, so the three values are not a transactional identity equation.
  Snapshot Inspect() const noexcept {return {constructed_.load(std::memory_order_relaxed),destroyed_.load(std::memory_order_relaxed),live_.load(std::memory_order_relaxed)};}
 private:
  std::atomic<uint64_t> constructed_{0},destroyed_{0},live_{0};
};
inline NativeObjectLifetimes& NativeSourceLifetimes() noexcept {static NativeObjectLifetimes counters;return counters;}
class NativeSourceLifetimeReceipt {
 public:
  explicit NativeSourceLifetimeReceipt(NativeObjectLifetimes& counters=NativeSourceLifetimes()) noexcept :counters_(counters){counters_.Constructed();}
  NativeSourceLifetimeReceipt(const NativeSourceLifetimeReceipt&)=delete;
  NativeSourceLifetimeReceipt& operator=(const NativeSourceLifetimeReceipt&)=delete;
  ~NativeSourceLifetimeReceipt() noexcept {counters_.Destroyed();}
 private:
  NativeObjectLifetimes& counters_;
};

struct NativeGlibcMemorySnapshot {
  bool available=false;
  uint64_t arena_bytes=0,allocated_bytes=0,free_bytes=0,mmap_bytes=0,top_releasable_bytes=0;
};
// glibc allocator accounting only. CEF/Chromium allocator-private mappings,
// including PartitionAlloc, are not covered. Allocated is uordblks, not a
// complete process live-object count; mmap allocations are reported separately.
inline NativeGlibcMemorySnapshot InspectNativeGlibcMemory() noexcept {
#if defined(__linux__) && defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,33)
  const auto stats=mallinfo2();
  return {true,static_cast<uint64_t>(stats.arena),static_cast<uint64_t>(stats.uordblks),static_cast<uint64_t>(stats.fordblks),static_cast<uint64_t>(stats.hblkhd),static_cast<uint64_t>(stats.keepcost)};
#endif
#endif
  return {};
}
} // namespace suitecut
