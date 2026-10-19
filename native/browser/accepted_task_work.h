// SPDX-License-Identifier: MIT
#pragma once
#include "accepted_work_origin.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
namespace suitecut {
// SuiteCut's actual C++ posted task objects only. This does not count renderer
// timers or opaque CEF internal scheduling, and is not an Engine idle predicate.
class AcceptedTaskWork {
 public:
  static constexpr size_t kPending=8192;
  enum class Kind {Work,BarrierCoordinator,WorkerMonitor,PassiveNetworkMonitor};
  struct Ticket {uint64_t instance=0,nonce=0;Kind kind=Kind::Work;bool operator==(const Ticket&)const=default;};
  struct Snapshot {size_t queued,executing,returned_held;uint64_t admitted,returned,destroyed,never_executed;bool poisoned;};
  AcceptedTaskWork():instance_(Instance()){if(!instance_)poisoned_=true;}
  std::optional<Ticket> Constructed(Kind kind,NativeWorkOrigin origin=CurrentNativeWorkOrigin())noexcept{
    std::lock_guard lock(mutex_);if(next_==UINT64_MAX){poisoned_=true;return std::nullopt;}
    for(auto& row:rows_)if(!row.active){row={{instance_,++next_,kind},true,false,false,origin};return row.ticket;}
    poisoned_=true;return std::nullopt;
  }
  bool Entered(Ticket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);if(!row||row->entered){poisoned_=true;return false;}row->entered=true;return true;
  }
  bool Returned(Ticket ticket,bool unwound)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);if(!row||!row->entered||row->returned){poisoned_=true;return false;}
    row->returned=true;++returned_;if(unwound)poisoned_=true;return true;
  }
  // Actual C++ task/captured-state destruction, not logical cancellation. The
  // receipt member MUST precede the callback field, so it is destroyed last.
  bool Destroyed(Ticket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);if(!row)return false;
    if(row->entered&&!row->returned)poisoned_=true;
    if(!row->entered)++never_executed_;
    row->active=false;++destroyed_;return true;
  }
  bool ReadyAtBarrier(std::optional<Ticket> actual_current,uint64_t actual_worker_owner=0,bool viewer=false)const noexcept{
    std::lock_guard lock(mutex_);if(poisoned_||!actual_current||actual_current->kind!=Kind::BarrierCoordinator)return false;
    const Row* current=nullptr;
    for(const auto& row:rows_)if(row.active){
      if(row.ticket==*actual_current){current=&row;continue;}
      // Only viewer handover may coexist with a bounded passive header-cache
      // timer. Executing callbacks and all ordinary Work retain their fences.
      if(viewer&&row.ticket.kind==Kind::PassiveNetworkMonitor&&(!row.entered||row.returned))continue;
      // A genuine worker-startup monitor keeps its physical task/Source lease.
      // A test-created watchdog retains Command origin and cannot use this
      // exception, even after its logical command completed or it would no-op.
      if(actual_worker_owner&&row.ticket.kind==Kind::WorkerMonitor&&
         row.origin.kind==NativeWorkOrigin::Kind::ResidentWorker&&row.origin.source_lease==actual_worker_owner&&
         (!row.entered||row.returned))continue;
      return false;
    }
    return current&&current->entered&&!current->returned;
  }
  // Actual queued/executing/returned-held task rows retain this scope until
  // real captured-state destruction. Unrelated tasks have no scoped discharge.
  bool LocatorHandlerReady(uint64_t invocation)const noexcept{
    std::lock_guard lock(mutex_);if(!invocation||poisoned_)return false;
    for(const auto& row:rows_)if(row.active&&row.origin.locator_handler_invocation==invocation)return false;
    return true;
  }
  // Trusted Engine use only, after actual callback-operation authentication.
  // A reader task exists before that authentication and initially has no tag.
  bool BindLocatorHandlerInvocation(Ticket actual_current,uint64_t invocation)noexcept;
  std::optional<NativeWorkOrigin> Origin(Ticket ticket)const noexcept{
    std::lock_guard lock(mutex_);for(const auto& row:rows_)if(row.active&&row.ticket==ticket)return row.origin;return std::nullopt;
  }
  Snapshot Inspect()const noexcept{
    std::lock_guard lock(mutex_);Snapshot out{0,0,0,next_,returned_,destroyed_,never_executed_,poisoned_};
    for(const auto& row:rows_)if(row.active){if(!row.entered)++out.queued;else if(!row.returned)++out.executing;else ++out.returned_held;}
    return out;
  }
  void Unsupported()noexcept{std::lock_guard lock(mutex_);poisoned_=true;}
 private:
  static uint64_t Instance()noexcept{
    static std::atomic<uint64_t> sequence{0};auto value=sequence.load(std::memory_order_relaxed);
    while(value!=UINT64_MAX){if(sequence.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value+1;}
    return 0;
  }
  struct Row {Ticket ticket;bool active=false,entered=false,returned=false;NativeWorkOrigin origin{};};
  Row* Find(Ticket ticket)noexcept{for(auto& row:rows_)if(row.active&&row.ticket==ticket)return &row;return nullptr;}
  mutable std::mutex mutex_;const uint64_t instance_;std::array<Row,kPending>rows_{};uint64_t next_=0,returned_=0,destroyed_=0,never_executed_=0;bool poisoned_=false;
};
inline AcceptedTaskWork& NativeProcessTaskWork(){static AcceptedTaskWork work;return work;}
inline bool NativeLocatorHandlerTasksReady(uint64_t invocation)noexcept{
  return invocation&&NativeProcessTaskWork().LocatorHandlerReady(invocation);
}
struct NativeCurrentTask {AcceptedTaskWork* work=nullptr;std::optional<AcceptedTaskWork::Ticket> ticket;};
inline NativeCurrentTask& CurrentNativeTask()noexcept{static thread_local NativeCurrentTask current;return current;}
inline bool AcceptedTaskWork::BindLocatorHandlerInvocation(Ticket actual_current,uint64_t invocation)noexcept{
  std::lock_guard lock(mutex_);const auto current=CurrentNativeTask();auto* row=Find(actual_current);
  if(poisoned_||!invocation||current.work!=this||!current.ticket||*current.ticket!=actual_current||!row||!row->entered||row->returned||
     (row->origin.locator_handler_invocation&&row->origin.locator_handler_invocation!=invocation)){poisoned_=true;return false;}
  row->origin.locator_handler_invocation=invocation;return true;
}
inline bool BindCurrentNativeLocatorHandlerTask(uint64_t invocation)noexcept{
  if(!invocation)return false;
  const auto current=CurrentNativeTask();if(!current.work||!current.ticket)return false;
  auto& process=NativeProcessTaskWork();
  return current.work==&process&&process.BindLocatorHandlerInvocation(*current.ticket,invocation);
}
// Place this field BEFORE std::function/captured-state fields. Real CEF retains
// its task object past Execute; those captures stay charged until destruction.
class AcceptedTaskReceipt {
 public:
  explicit AcceptedTaskReceipt(AcceptedTaskWork::Kind kind=AcceptedTaskWork::Kind::Work,AcceptedTaskWork& work=NativeProcessTaskWork(),NativeWorkOrigin origin=CurrentNativeWorkOrigin())noexcept:work_(work),ticket_(work.Constructed(kind,origin)){}
  AcceptedTaskReceipt(const AcceptedTaskReceipt&)=delete;AcceptedTaskReceipt& operator=(const AcceptedTaskReceipt&)=delete;
  ~AcceptedTaskReceipt(){if(ticket_&&!work_.Destroyed(*ticket_))work_.Unsupported();}
  AcceptedTaskWork& Work()const noexcept{return work_;}
  std::optional<AcceptedTaskWork::Ticket> Ticket()const noexcept{return ticket_;}
  NativeWorkOrigin Origin()const noexcept{return ticket_?work_.Origin(*ticket_).value_or(NativeWorkOrigin{}):NativeWorkOrigin{};}
 private:
  AcceptedTaskWork& work_;std::optional<AcceptedTaskWork::Ticket> ticket_;
};
class AcceptedTaskFrame {
 public:
  explicit AcceptedTaskFrame(AcceptedTaskReceipt& receipt)noexcept:origin_(receipt.Origin()),receipt_(receipt),previous_(CurrentNativeTask()),unwinding_(std::uncaught_exceptions()){
    if(receipt_.Ticket()&&!receipt_.Work().Entered(*receipt_.Ticket()))receipt_.Work().Unsupported();
    CurrentNativeTask()={&receipt_.Work(),receipt_.Ticket()};
  }
  AcceptedTaskFrame(const AcceptedTaskFrame&)=delete;AcceptedTaskFrame& operator=(const AcceptedTaskFrame&)=delete;
  ~AcceptedTaskFrame(){
    if(receipt_.Ticket()&&!receipt_.Work().Returned(*receipt_.Ticket(),std::uncaught_exceptions()>unwinding_))receipt_.Work().Unsupported();
    CurrentNativeTask()=previous_;
  }
 private:
  NativeWorkOriginFrame origin_;
  AcceptedTaskReceipt& receipt_;NativeCurrentTask previous_;int unwinding_;
};
inline bool NativeTaskBarrierReady(uint64_t actual_worker_owner=0,bool viewer=false)noexcept{
  const auto current=CurrentNativeTask();return current.work==&NativeProcessTaskWork()&&NativeProcessTaskWork().ReadyAtBarrier(current.ticket,actual_worker_owner,viewer);
}
} // namespace suitecut
