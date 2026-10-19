// SPDX-License-Identifier: MIT
#pragma once
#include "locator_handler_async_work.h"
#include "worker_budget.h"
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace suitecut {
// Trace jobs retain their own immutable leases, deadlines and owner completion.
// CEF's best-effort file task runner may stop admitting execution during browser
// closure; this bounded process-owned executor retires accepted work first.
class TraceBackground final {
 public:
  TraceBackground():TraceBackground(AutomaticWorkerPolicy()){}
  explicit TraceBackground(size_t workers,size_t capacity)
      :capacity_(capacity),worker_limit_(std::min(workers?workers:AutomaticWorkerCount(),capacity)){
    if(!capacity_)throw std::invalid_argument("Trace background capacity must be positive");
  }
  ~TraceBackground(){Close();}
  TraceBackground(const TraceBackground&)=delete;
  TraceBackground& operator=(const TraceBackground&)=delete;
  bool Submit(std::function<void()> task){
    if(!task)return false;
    task=BindNativeLocatorHandlerAsync(std::move(task));
    {
      std::lock_guard lock(mutex_);
      if(closing_||physical_>=capacity_)return false;
      queue_.push_back(std::move(task));++physical_;
      // Existing workers remain available for reuse. Grow only when accepted
      // running/queued work needs another worker, within the automatic budget.
      if(threads_.size()<worker_limit_&&threads_.size()<physical_){
        try{threads_.emplace_back([this]{Run();});}
        catch(...){
          ++start_failures_;
          // An existing worker can drain this job even when the OS refuses
          // another thread. Refuse only if no worker can execute accepted work.
          if(threads_.empty()){
            task=std::move(queue_.back());queue_.pop_back();--physical_;return false;
          }
        }
      }
    }
    available_.notify_one();return true;
  }
  size_t PhysicalCount()const{std::lock_guard lock(mutex_);return physical_;}
  size_t Failures()const{std::lock_guard lock(mutex_);return failures_;}
  size_t WorkerLimit()const noexcept{return worker_limit_;}
  size_t Capacity()const noexcept{return capacity_;}
  bool Available()const{std::lock_guard lock(mutex_);return !closing_&&physical_<capacity_;}
  size_t WorkerCount()const{std::lock_guard lock(mutex_);return threads_.size();}
  size_t WorkerStartFailures()const{std::lock_guard lock(mutex_);return start_failures_;}
 private:
  explicit TraceBackground(const WorkerPolicy& policy):TraceBackground(policy.workers,policy.capacity){}
  void Close(){
    {std::lock_guard lock(mutex_);closing_=true;}
    available_.notify_all();for(auto& thread:threads_)if(thread.joinable())thread.join();
  }
  void Run(){
    for(;;){std::function<void()> task;
      {std::unique_lock lock(mutex_);available_.wait(lock,[this]{return closing_||!queue_.empty();});if(queue_.empty())return;task=std::move(queue_.front());queue_.pop_front();}
      bool failed=false;try{task();}catch(...){failed=true;}
      // Release captured native owners while this job still owns its physical
      // reservation; the UI completion may already have run concurrently.
      task={};{std::lock_guard lock(mutex_);if(failed)++failures_;--physical_;}
    }
  }
  const size_t capacity_,worker_limit_;mutable std::mutex mutex_;std::condition_variable available_;
  std::deque<std::function<void()>> queue_;std::vector<std::thread> threads_;
  size_t physical_=0,failures_=0,start_failures_=0;bool closing_=false;
};
} // namespace suitecut
