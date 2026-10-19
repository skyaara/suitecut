// SPDX-License-Identifier: MIT
#pragma once
#include "test_steps.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace suitecut::runner {
// Scheduling only. The adapter must supply a genuinely admitted native step
// for each invocation and its joint raw/descendant/Source physical receipt.
// Logical rejection never substitutes for that receipt or actual host waitpid.
class CallbackRetrySchedule {
 public:
  static constexpr size_t kIntervals=64;
  static constexpr uint64_t kSafeDuration=9007199254740991ULL;
  enum class Verdict {Pending,Passed,TimedOut,Canceled,InfrastructureFailure};
  enum class Phase {Ready,Running,Draining,Backoff,Terminal};
  enum class Raw {Returned,Threw,InfrastructureFailure};
  enum class Fence {Clean,AuthoredFailure,InfrastructureFailure};
  enum class Receipt {Pending,Settled,WorkerExited};
  struct Identity {
    uint64_t id=0,parent_step=0;
    StepOwner owner;
    bool operator==(const Identity&)const=default;
  };
  struct Invocation {
    uint64_t assertion=0,nonce=0,step=0;
    bool operator==(const Invocation&)const=default;
  };
  struct Options {
    uint64_t timeout=0;
    std::optional<std::span<const uint64_t>> intervals;
  };
  struct Current {
    Invocation invocation;
    double started=0;
    std::optional<double> raw_received,physical_received;
    std::optional<Raw> raw;
    std::optional<Fence> fence;
    Receipt receipt=Receipt::Pending;
  };

  CallbackRetrySchedule(Identity identity,Options options,double owner_deadline,double now)
      :identity_(std::move(identity)),started_(now),owner_deadline_(owner_deadline),next_due_(now){
    if(!Valid(identity_)||!std::isfinite(now)||now<0||!std::isfinite(owner_deadline)||owner_deadline<=now||options.timeout>kSafeDuration)
      throw std::invalid_argument("Native callback retry admission identity/deadline rejected");
    if(options.intervals){
      if(options.intervals->empty()||options.intervals->size()>kIntervals)
        throw std::invalid_argument("Native callback retry interval count rejected");
      interval_count_=options.intervals->size();
      for(size_t i=0;i<interval_count_;++i){
        if((*options.intervals)[i]>kSafeDuration)
          throw std::invalid_argument("Native callback retry interval duration rejected");
        intervals_[i]=(*options.intervals)[i];
      }
    }else{
      interval_count_=4;intervals_[0]=100;intervals_[1]=250;intervals_[2]=500;intervals_[3]=1000;
    }
    if(options.timeout){
      const auto duration=static_cast<double>(options.timeout);
      if(now>std::numeric_limits<double>::max()-duration)
        throw std::invalid_argument("Native callback retry absolute deadline overflow");
      own_deadline_=now+duration;
      if(!std::isfinite(*own_deadline_)||*own_deadline_<=now)
        throw std::invalid_argument("Native callback retry absolute deadline rejected");
    }
    last_now_=now;
  }

  // Called once per actual native dispatch. It does not allocate a step, run
  // author code, or loop over overdue retries; the coordinator does that work.
  Invocation Start(uint64_t native_step,double now){
    Tick(now);
    if(verdict_!=Verdict::Pending||(phase_!=Phase::Ready&&phase_!=Phase::Backoff)||now<next_due_||
       !native_step||(attempts_?native_step<=current_.invocation.step:native_step!=identity_.id)||attempts_==UINT64_MAX)
      throw std::invalid_argument("Native callback retry dispatch is stale, early or exhausted");
    current_=Current{{identity_.id,attempts_+1,native_step},now,std::nullopt,std::nullopt,std::nullopt,std::nullopt,Receipt::Pending};
    ++attempts_;phase_=Phase::Running;physical_=true;return current_.invocation;
  }

  void RawSettled(const Invocation& invocation,Raw raw,double now){
    Tick(now);Exact(invocation);
    if(phase_!=Phase::Running||current_.raw_received||
       (raw!=Raw::Returned&&raw!=Raw::Threw&&raw!=Raw::InfrastructureFailure))
      throw std::invalid_argument("Native callback retry raw receipt is duplicate or invalid");
    current_.raw=raw;current_.raw_received=now;phase_=Phase::Draining;
    if(raw==Raw::InfrastructureFailure)FailInfrastructure(now);
  }

  // The caller has already joined actual raw invocation, descendant steps,
  // soft/file/publication and authenticated Source cut/resume receipts.
  void PhysicallySettled(const Invocation& invocation,double now,Fence fence=Fence::Clean){
    Tick(now);Exact(invocation);
    if(phase_!=Phase::Draining||!current_.raw_received||current_.physical_received||
       (fence!=Fence::Clean&&fence!=Fence::AuthoredFailure&&fence!=Fence::InfrastructureFailure))
      throw std::invalid_argument("Native callback retry physical receipt lacks actual raw settlement");
    if(fence==Fence::InfrastructureFailure)FailInfrastructure(now);
    current_.physical_received=now;current_.fence=fence;current_.receipt=Receipt::Settled;physical_=false;
    if(verdict_!=Verdict::Pending){phase_=Phase::Terminal;return;}
    if(*current_.raw==Raw::Returned&&fence==Fence::Clean){verdict_=Verdict::Passed;phase_=Phase::Terminal;return;}
    // Start backoff at the physical fence, not the earlier Promise rejection.
    // Compare against the remaining cap before adding a possibly huge safe
    // integer duration, preserving the exact original deadline on saturation.
    next_due_=BackoffDue();
    phase_=Phase::Backoff;
  }

  // Existing native author/step authority may amend its cap. Test the OLD cap
  // first so no late amendment revives an expired assertion or owner.
  bool AmendOwner(double deadline,double now){
    Tick(now);
    if(verdict_!=Verdict::Pending||!std::isfinite(deadline)||deadline<0)return false;
    owner_deadline_=deadline;
    if(phase_==Phase::Backoff)next_due_=BackoffDue();
    Tick(now);return true;
  }
  void Tick(double now){
    Time(now);
    if(verdict_==Verdict::Pending&&now>=Deadline())FinishLogical(Verdict::TimedOut);
  }
  bool Cancel(double now){
    Tick(now);if(verdict_!=Verdict::Pending)return false;
    FinishLogical(Verdict::Canceled);return true;
  }
  bool FailInfrastructure(double now){
    Tick(now);
    if(!physical_&&phase_==Phase::Terminal)return false;
    FinishLogical(Verdict::InfrastructureFailure);return true;
  }
  // This API accepts only the exact host identity retained by the operation.
  // Its caller must already own the real OS waitpid receipt; pidfd readiness,
  // shutdown ACK, cancellation and an absent SDK map are not such receipts.
  bool ConfirmedWorkerExit(unsigned worker,int64_t pid,double now){
    Time(now);
    if(worker!=identity_.owner.worker_index||pid!=identity_.owner.pid)return false;
    if(phase_==Phase::Terminal&&!physical_)return false;
    verdict_=Verdict::InfrastructureFailure;phase_=Phase::Terminal;
    if(physical_){current_.physical_received=now;current_.receipt=Receipt::WorkerExited;physical_=false;}
    return true;
  }

  bool Ready(double now){Tick(now);return verdict_==Verdict::Pending&&!physical_&&(phase_==Phase::Ready||phase_==Phase::Backoff)&&now>=next_due_;}
  std::optional<double> NextWake()const noexcept{
    if(verdict_!=Verdict::Pending)return std::nullopt;
    return phase_==Phase::Ready||phase_==Phase::Backoff?std::min(next_due_,Deadline()):Deadline();
  }
  double Deadline()const noexcept{return own_deadline_?std::min(*own_deadline_,owner_deadline_):owner_deadline_;}
  std::optional<double> OwnDeadline()const noexcept{return own_deadline_;}
  const Identity& Owner()const noexcept{return identity_;}
  const Current& Actual()const noexcept{return current_;}
  uint64_t Attempts()const noexcept{return attempts_;}
  double Started()const noexcept{return started_;}
  Verdict Result()const noexcept{return verdict_;}
  Phase State()const noexcept{return phase_;}
  bool Physical()const noexcept{return physical_;}

 private:
  static bool Valid(const Identity& identity){
    const auto& o=identity.owner;
    return identity.id&&o.test_id&&o.attempt_generation&&o.scope&&o.scope_generation&&o.worker_index&&o.pid>0&&o.pid<=INT32_MAX&&
      (o.callback_kind=="body"||((o.callback_kind=="beforeEach"||o.callback_kind=="afterEach")&&o.callback_token));
  }
  void Time(double now){if(!std::isfinite(now)||now<last_now_)throw std::invalid_argument("Native callback retry observation time rejected");last_now_=now;}
  void Exact(const Invocation& invocation)const{if(!physical_||current_.invocation!=invocation)throw std::invalid_argument("Native callback retry invocation identity is stale or foreign");}
  double BackoffDue()const noexcept{
    const auto interval=intervals_[static_cast<size_t>(std::min<uint64_t>(attempts_-1,interval_count_-1))];
    const auto origin=*current_.physical_received,deadline=Deadline();
    return static_cast<double>(interval)>=deadline-origin?deadline:std::min(deadline,origin+static_cast<double>(interval));
  }
  void FinishLogical(Verdict verdict){verdict_=verdict;if(!physical_)phase_=Phase::Terminal;}
  Identity identity_;std::array<uint64_t,kIntervals> intervals_{};size_t interval_count_=0;
  double started_=0,owner_deadline_=0,next_due_=0,last_now_=0;
  std::optional<double> own_deadline_;
  Current current_{};uint64_t attempts_=0;Verdict verdict_=Verdict::Pending;Phase phase_=Phase::Ready;bool physical_=false;
};
} // namespace suitecut::runner
