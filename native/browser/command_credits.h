// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>

// Reserve capacity before dispatch; release after the corresponding reply is
// consumed. The producer waits, while UI callbacks never wait for an encoder.
class SuiteCutCommandCredits {
 public:
  explicit SuiteCutCommandCredits(unsigned capacity) : capacity_(capacity) {}
  bool Reserve(const std::atomic<bool>& canceled) {
    std::unique_lock lock(mutex_);
    available_.wait(lock, [&] { return stopped_ || canceled.load() || used_ < capacity_; });
    if (stopped_ || canceled.load()) return false;
    ++used_;
    return true;
  }
  void Release() {
    { std::lock_guard lock(mutex_); if (!used_) std::abort(); --used_; }
    available_.notify_one();
  }
  void Wake() {
    // Synchronize with predicate evaluation to avoid a lost cancel wake-up.
    { std::lock_guard lock(mutex_); }
    available_.notify_all();
  }
  void Stop() {
    { std::lock_guard lock(mutex_); stopped_ = true; }
    available_.notify_all();
  }
 private:
  unsigned capacity_, used_ = 0;
  bool stopped_ = false;
  std::mutex mutex_;
  std::condition_variable available_;
};
