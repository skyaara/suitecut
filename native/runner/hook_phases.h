// SPDX-License-Identifier: MIT
#pragma once
#include "json_text.h"
#include "fixture_attempt_clocks.h"
#include <algorithm>
#include <cmath>
#include <charconv>
#include <limits>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace suitecut::runner {
enum class PhaseStatus { Passed, Failed, TimedOut, Interrupted, Skipped };
struct PhaseObservation { PhaseStatus status = PhaseStatus::Passed; double duration = 0; };
inline const char* PhaseStatusName(PhaseStatus status) {
  switch (status) {
    case PhaseStatus::Passed: return "passed";
    case PhaseStatus::Skipped: return "skipped";
    case PhaseStatus::Failed: return "failed";
    case PhaseStatus::TimedOut: return "timedOut";
    case PhaseStatus::Interrupted: return "interrupted";
  }
  throw std::invalid_argument("Invalid native phase status");
}
inline std::string PhaseMilliseconds(double value) {
  if (!std::isfinite(value) || value < 0) throw std::invalid_argument("Invalid native elapsed duration");
  char bytes[64];
  const auto result = std::to_chars(bytes, bytes + sizeof(bytes), value, std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (result.ec != std::errc{}) throw std::runtime_error("Native duration serialization failed");
  return std::string(bytes, result.ptr);
}
enum class HookKind { BeforeAll, BeforeEach, Body, AfterEach, AfterAll };
struct HookCall {
  HookKind kind;
  uint64_t callback;
  std::vector<std::string> fixtures;
};

// One group is either one beforeAll/afterAll hook (its own fixture scope), or
// beforeEach + body + afterEach (one shared fixture scope). Native C++ owns the
// ordering. A timeout cannot turn a still-running host callback into settlement.
class HookPhases {
 public:
  struct Acquire {
    uint64_t request, scope, generation;
    std::vector<std::string> fixtures;
    double deadline;
    bool cleanup;
  };
  struct Invoke {
    uint64_t token, scope, generation, callback;
    HookKind kind;
    double deadline;
    PhaseObservation observation{};
    std::string cancel_reason;
  };
  struct Release { uint64_t request, scope, generation; double deadline; };
  struct Result { uint64_t scope, generation; bool ok; std::vector<std::string> errors; bool timed_out = false; PhaseObservation observation{}; bool skipped = false; std::string skip_reason_json = "\"\""; bool infrastructure_failure = false; uint64_t skip_token = 0, skip_callback = 0; HookKind skip_kind = HookKind::Body; std::vector<uint64_t> error_sources{}; };
  struct Callbacks {
    std::function<void(const Acquire&)> acquire;
    std::function<void(const Invoke&)> invoke;
    std::function<void(const Invoke&)> abort;
    std::function<void(const Release&)> release;
    std::function<void(const Result&)> completed;
    std::function<void(const std::string&)> terminate;
    std::function<bool(uint64_t, uint64_t)> cleanup_ready = {};
  };
  struct State { size_t physical; bool active, terminating, retiring; };
  explicit HookPhases(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

  bool Begin(uint64_t scope, uint64_t generation, std::vector<HookCall> calls,
             double timeout, double now, PhaseObservation initial = {}, double budget_elapsed = 0,
             std::shared_ptr<FixtureAttemptClocks> timing = {}) {
    if (!std::isfinite(now) || !std::isfinite(timeout) || timeout < 0 ||
        timeout > 3153600000000.0 || !scope || !generation || active_ || terminating_ || retiring_)
      return false;
    if (!std::isfinite(initial.duration) || initial.duration < 0 || !std::isfinite(budget_elapsed) ||
        budget_elapsed < 0 || budget_elapsed > initial.duration || budget_elapsed >= (timeout == 0 ? 3153600000000.0 : timeout)) return false;
    if(timing&&(timing->Owner().scope!=scope||timing->Owner().generation!=generation||
        !timing->Active(timing->Owner())||timing->Active(timing->Owner())->Expired(now)))return false;
    (void)PhaseStatusName(initial.status);
    Validate(calls);
    const double budget = timeout == 0 ? 3153600000000.0 : timeout;
    if (!std::isfinite(now + budget)) return false;
    timeout_at_.reset();
    // Native preparation is part of the original test budget. Its duration was
    // already included in the associated attempt observation; count it once.
    initial.duration -= budget_elapsed;
    active_ = Group{scope, generation, std::move(calls), budget, now + budget - budget_elapsed, now - budget_elapsed, initial, std::move(timing)};
    Advance(now);
    return true;
  }

  void Acquired(uint64_t request, uint64_t scope, uint64_t generation,
                bool ok, const std::string& error, double now) {
    if (!std::isfinite(now)) return;
    ObserveDeadline(now);
    if (!Matches(scope, generation) || active_->phase != Phase::Acquire ||
        active_->request != request || terminating_) return;
    if (!ok) { active_->infrastructure_failure = true; Fail(error, now); return; }
    if (leases_.size() >= 64) { Terminate("Physical hook callback capacity64 exceeded"); return; }
    const auto token = NextToken();
    if (!token) return;
    const auto& call = active_->calls[active_->index];
    Invoke command{token, scope, generation, call.callback, call.kind, active_->deadline, Observe(now), {}};
    leases_.emplace(token, command);
    active_->phase = Phase::Invoke;
    active_->token = token;
    callbacks_.invoke(command);
  }

  void Settled(uint64_t token, uint64_t scope, uint64_t generation,
               bool ok, const std::string& error, double now, bool skipped_settlement = false, uint64_t originating_step = 0) {
    if(originating_step&&(ok||skipped_settlement))throw std::invalid_argument("Successful hook receipt cannot claim an originating failure step");
    if (!std::isfinite(now)) return;
    ObserveDeadline(now);
    auto found = leases_.find(token);
    if (found == leases_.end() || found->second.scope != scope ||
        found->second.generation != generation) return;
    leases_.erase(found); // Only actual raw callback settlement releases this lease.
    if (!Matches(scope, generation) || terminating_) return;
    const bool skip_owner = active_->skip_requested && active_->skip_token == token;
    if (skipped_settlement && !skip_owner) {
      if (active_->phase == Phase::Invoke && active_->token == token)
        Fail("Runtime skip settlement has no accepted native request", now);
      else Record("Runtime skip settlement has no accepted native request");
      return;
    }
    if (skip_owner) {
      if (!ok && !skipped_settlement) Record(error,originating_step);
      if (active_->phase == Phase::SkipDrain) StartCleanup(now);
      else if (active_->phase == Phase::Drain && !HasPhysical(scope, generation)) Complete(now);
      return;
    }
    if (active_->phase == Phase::CleanupWait) { Advance(now); return; }
    if (active_->phase != Phase::Invoke || active_->token != token) return;
    active_->token = 0;
    if (!ok) { Fail(error, now,originating_step); return; }
    ++active_->index;
    Advance(now);
  }

  bool NoteSoftFailure(uint64_t token,uint64_t scope,uint64_t generation,double now){
    if(!std::isfinite(now))return false;
    ObserveDeadline(now);if(!Matches(scope,generation)||terminating_||active_->phase!=Phase::Invoke||active_->token!=token)return false;
    const auto lease=leases_.find(token);if(lease==leases_.end()||lease->second.scope!=scope||lease->second.generation!=generation||
      (lease->second.kind!=HookKind::BeforeEach&&lease->second.kind!=HookKind::Body&&lease->second.kind!=HookKind::AfterEach))return false;
    const bool cleanup=active_->cleanup&&lease->second.kind==HookKind::AfterEach;
    if((active_->timed_out||active_->interrupted||active_->skip_requested)&&!cleanup)return false;
    active_->soft_failed=true;active_->soft_cleanup|=cleanup;return true;
  }

  bool CanExpectFailure(uint64_t token,uint64_t scope,uint64_t generation,double now) {
    if(!std::isfinite(now))return false;
    ObserveDeadline(now);
    if(!Matches(scope,generation)||terminating_||active_->phase!=Phase::Invoke||
        active_->token!=token||!token||active_->cleanup||active_->timed_out||
        active_->interrupted||active_->skip_requested||!active_->errors.empty())return false;
    const auto lease=leases_.find(token);
    return lease!=leases_.end()&&lease->second.scope==scope&&lease->second.generation==generation&&
        (lease->second.kind==HookKind::Body||lease->second.kind==HookKind::BeforeEach);
  }

  bool RequestSkip(uint64_t token, uint64_t scope, uint64_t generation,
                   const std::string& reason_json, double now, std::function<void()> accepted = {}, bool defer_cleanup = false) {
    if (!std::isfinite(now) || !JsonString(reason_json, 8192)) return false;
    ObserveDeadline(now);
    if (!Matches(scope, generation) || terminating_ || active_->phase != Phase::Invoke ||
        active_->token != token || !token || active_->cleanup || active_->timed_out ||
        active_->interrupted || active_->skip_requested || active_->soft_failed || !active_->errors.empty()) return false;
    const auto lease = leases_.find(token);
    if (lease == leases_.end() || lease->second.scope != scope ||
        lease->second.generation != generation ||
        (lease->second.kind != HookKind::Body && lease->second.kind != HookKind::BeforeEach && lease->second.kind != HookKind::BeforeAll) ||
        (defer_cleanup && lease->second.kind == HookKind::BeforeAll)) return false;
    active_->skip_requested = true;
    active_->skip_token = token;
    active_->skip_callback = lease->second.callback;
    active_->skip_kind = lease->second.kind;
    active_->skip_reason_json = reason_json;
    if(accepted)accepted();
    AbortActive("Native runtime skip requested");
    if(defer_cleanup){active_->phase=Phase::SkipDrain;active_->drain_deadline=std::min(active_->deadline,now+1000);}
    else if(active_->skip_kind==HookKind::BeforeAll){active_->index=active_->calls.size();Advance(now);}
    else StartCleanup(now);
    return true;
  }

  void Released(uint64_t request, uint64_t scope, uint64_t generation,
                bool ok, const std::string& error, double now) {
    if (!std::isfinite(now)) return;
    ObserveDeadline(now);
    if (!Matches(scope, generation) || active_->phase != Phase::Release ||
        active_->request != request || terminating_) return;
    if (!ok) { active_->infrastructure_failure = true; Record(error); }
    if (active_->skip_requested && HasPhysical(scope, generation)) {
      active_->phase = Phase::Drain;
      active_->drain_deadline = std::min(active_->deadline, now + 1000);
      return;
    }
    Complete(now);
  }

  void Cancel(const std::string& error, double now, bool interrupted = true) {
    if (!active_ || !std::isfinite(now) || terminating_) return;
    ObserveDeadline(now);
    if (!active_ || terminating_) return;
    active_->interrupted |= interrupted;
    if (active_->phase == Phase::Release || active_->phase == Phase::Drain) return;
    if (active_->phase == Phase::SkipDrain) { Record(error); return; }
    // A caller cancellation is applied once to main work. Cleanup callbacks run
    // under their own original cleanup deadline, not repeatedly canceled.
    if (active_->cleanup) return;
    Record(error);
    AbortActive();
    StartCleanup(now);
  }

  std::optional<double> AmendTimeout(uint64_t token, uint64_t scope, uint64_t generation,
                                     uint64_t timeout, double now) {
    if (!std::isfinite(now) || timeout > 120000) return std::nullopt;
    ObserveDeadline(now); // An expired original budget cannot be revived by a late request.
    if (!Matches(scope, generation) || terminating_ || active_->phase != Phase::Invoke ||
        active_->token != token || !token || (active_->timed_out && !active_->cleanup) || active_->interrupted)
      return std::nullopt;
    const auto lease = leases_.find(token);
    if (lease == leases_.end() || lease->second.scope != scope || lease->second.generation != generation)
      return std::nullopt;
    const auto budget = timeout ? static_cast<double>(timeout) : 3153600000000.0;
    if(active_->timing&&!active_->timing->Amend(active_->timing->Owner(),timeout,now))return std::nullopt;
    const auto deadline = active_->timing?active_->timing->Active(active_->timing->Owner())->Deadline(now).value_or(now+3153600000000.0):
        (active_->cleanup ? active_->cleanup_started : active_->started) + budget;
    if (!std::isfinite(deadline)) return std::nullopt;
    active_->timeout = budget;
    active_->deadline = deadline;
    lease->second.deadline = deadline;
    return deadline; // Caller synchronizes the fixture owner before Tick handles an elapsed new budget.
  }
  bool HasErrors(uint64_t scope,uint64_t generation)const{return Matches(scope,generation)&&!active_->errors.empty();}
  std::optional<double> TimeoutAt() const { return timeout_at_; }
  std::shared_ptr<FixtureChargedClock> ActiveChargedClock(uint64_t scope,uint64_t generation)const{
    return Matches(scope,generation)&&active_->timing?active_->timing->Active(active_->timing->Owner()):nullptr;
  }
  void RefreshFixtureBudget(double now){RefreshChargedDeadline(now);}
  std::optional<double> ActiveDeadline() const { return active_ ? std::optional<double>(active_->deadline) : std::nullopt; }

  void ResourceFailed(uint64_t scope, uint64_t generation, const std::string& error, double now) {
    if (!Matches(scope, generation) || !std::isfinite(now) || terminating_) return;
    ObserveDeadline(now);
    if (!Matches(scope, generation) || terminating_) return;
    active_->infrastructure_failure = true;
    if (active_->phase != Phase::Release) Fail(error, now);
  }

  void Tick(double now) { if (std::isfinite(now)) ObserveDeadline(now); }
  bool Retire(double deadline, double now) {
    if (!std::isfinite(now) || !std::isfinite(deadline) || deadline <= now ||
        active_ || retiring_ || terminating_) return false;
    retiring_ = true;
    physical_deadline_ = deadline;
    return true;
  }
  // A native transport exception is infrastructure failure, never an authored
  // settlement. Freeze phase admission and retain every physical lease.
  void InfrastructureFailure(const std::string& error) { Terminate(error); }
  void WorkerClosed(double now = 0) {
    // Caller must first confirm actual worker process exit with waitpid.
    leases_.clear();
    terminating_ = true;
    if (active_) {
      active_->infrastructure_failure = true;
      Record("Host worker physically exited");
      const Result result{active_->scope, active_->generation, false, active_->errors, active_->timed_out, Observe(now), active_->skip_requested, active_->skip_reason_json, active_->infrastructure_failure, active_->skip_token, active_->skip_callback, active_->skip_kind};
      active_.reset();
      callbacks_.completed(result);
    }
  }
  State Inspect() const { return {leases_.size(), active_.has_value(), terminating_, retiring_}; }

 private:
  enum class Phase { Acquire, Invoke, Release, SkipDrain, Drain, CleanupWait };
  struct Group {
    uint64_t scope, generation;
    std::vector<HookCall> calls;
    double timeout, deadline, started, last_now;
    double cleanup_started = 0;
    PhaseObservation initial;
    std::shared_ptr<FixtureAttemptClocks> timing;
    size_t index = 0;
    uint64_t request = 0, token = 0;
    bool cleanup = false, timed_out = false, interrupted = false, skip_requested = false, infrastructure_failure = false, soft_failed = false, soft_cleanup = false;
    uint64_t skip_token = 0, skip_callback = 0;
    HookKind skip_kind = HookKind::Body;
    double drain_deadline = 0;
    std::optional<double> release_drain_charge;
    std::string skip_reason_json = "\"\"";
    Phase phase = Phase::Acquire;
    std::vector<std::string> errors;
    std::vector<uint64_t> error_sources;
    Group(uint64_t s, uint64_t g, std::vector<HookCall> c, double t, double d, double n, PhaseObservation o, std::shared_ptr<FixtureAttemptClocks> clock)
        : scope(s), generation(g), calls(std::move(c)), timeout(t), deadline(d), started(n), last_now(n), initial(o), timing(std::move(clock)) {}
  };
  PhaseObservation Observe(double now) const {
    const auto& group = *active_;
    const auto status = group.interrupted || group.initial.status == PhaseStatus::Interrupted ? PhaseStatus::Interrupted :
        group.timed_out || group.initial.status == PhaseStatus::TimedOut ? PhaseStatus::TimedOut :
        !group.errors.empty() || group.soft_failed || group.initial.status == PhaseStatus::Failed ? PhaseStatus::Failed :
        group.skip_requested || group.initial.status == PhaseStatus::Skipped ? PhaseStatus::Skipped : PhaseStatus::Passed;
    const auto duration=group.timing?group.timing->Observe(group.timing->Owner(),std::max(now,group.last_now)).charged:
        std::max(0.0,std::max(now,group.last_now)-group.started);
    return {status, group.initial.duration + duration};
  }
  static bool Cleanup(HookKind kind) { return kind == HookKind::AfterEach; }
  static void Validate(const std::vector<HookCall>& calls) {
    if (calls.empty() || calls.size() > 256) throw std::invalid_argument("Hook group capacity must be 1..256");
    if (calls[0].kind == HookKind::BeforeAll || calls[0].kind == HookKind::AfterAll) {
      if (calls.size() != 1) throw std::invalid_argument("Each all-hook requires a fresh fixture scope");
    } else {
      int bodies = 0; bool after = false;
      for (const auto& call : calls) {
        if (call.kind == HookKind::BeforeEach && (bodies || after)) throw std::invalid_argument("beforeEach order invalid");
        if (call.kind == HookKind::Body && (after || ++bodies > 1)) throw std::invalid_argument("Test body order invalid");
        if (call.kind == HookKind::AfterEach) { after = true; if (!bodies) throw std::invalid_argument("afterEach requires a declared body"); }
        if (call.kind == HookKind::BeforeAll || call.kind == HookKind::AfterAll) throw std::invalid_argument("All-hook mixed into test fixture scope");
      }
      if (bodies != 1) throw std::invalid_argument("Test group requires one body");
    }
    size_t names = 0;
    for (const auto& call : calls) {
      if (call.kind != HookKind::BeforeAll && call.kind != HookKind::BeforeEach &&
          call.kind != HookKind::Body && call.kind != HookKind::AfterEach &&
          call.kind != HookKind::AfterAll) throw std::invalid_argument("Unknown native hook kind");
      if (!call.callback || call.fixtures.size() > 256) throw std::invalid_argument("Invalid native hook callback declaration");
      for (const auto& fixture : call.fixtures) {
        if (fixture.empty() || fixture.size() > 256) throw std::invalid_argument("Invalid native hook fixture name");
        names += fixture.size();
      }
    }
    if (names > 1048576) throw std::invalid_argument("Hook fixture metadata budget1MiB exceeded");
  }
  bool Matches(uint64_t scope, uint64_t generation) const {
    return active_ && active_->scope == scope && active_->generation == generation;
  }
  uint64_t NextToken() {
    if (nonce_ == UINT64_MAX) { Terminate("Native hook token space exhausted"); return 0; }
    return ++nonce_;
  }
  void Record(const std::string& error,uint64_t originating_step=0) {
    if (active_ && active_->errors.size() < 256){
      active_->errors.push_back(JsonString(error, 8192) ? error : error.substr(0, 8192));
      active_->error_sources.push_back(originating_step);
    }
  }
  bool HasPhysical(uint64_t scope, uint64_t generation) const {
    return std::any_of(leases_.begin(), leases_.end(), [=](const auto& entry) {
      return entry.second.scope == scope && entry.second.generation == generation;
    });
  }
  bool HasMainPhysical(uint64_t scope, uint64_t generation) const {
    return std::any_of(leases_.begin(), leases_.end(), [=](const auto& entry) {
      const auto& lease = entry.second;
      return lease.scope == scope && lease.generation == generation &&
          (lease.kind == HookKind::Body || lease.kind == HookKind::BeforeEach);
    });
  }
  void Complete(double now) {
    const auto& group = *active_;
    const Result result{group.scope, group.generation, group.errors.empty()&&!group.soft_failed, group.errors,
        group.timed_out, Observe(now), group.skip_requested, group.skip_reason_json, group.infrastructure_failure||group.soft_cleanup, group.skip_token, group.skip_callback, group.skip_kind,group.error_sources};
    if(group.timing&&!group.timing->Finish(group.timing->Owner(),now))throw std::runtime_error("Native charged phase clock completion rejected");
    active_.reset();
    callbacks_.completed(result);
  }
  void Terminate(const std::string& error) {
    if (terminating_) return;
    terminating_ = true;
    if(active_)active_->infrastructure_failure = true;
    Record(error);
    callbacks_.terminate(error);
  }
  void AbortActive(const std::string& reason = {}) {
    if (!active_ || active_->phase != Phase::Invoke || !active_->token) return;
    const auto found = leases_.find(active_->token);
    if (found != leases_.end()) { auto actual = found->second; actual.cancel_reason = reason; callbacks_.abort(actual); }
    active_->token = 0; // Logical detach only; leases_ retains the actual callback.
  }
  void EnterCleanup(double now){
    auto& group=*active_;
    if(group.cleanup)return;
    if(group.timing&&!group.timing->BeginCleanup(group.timing->Owner(),now))throw std::runtime_error("Native charged phase cleanup transition rejected");
    group.cleanup=true;group.cleanup_started=now;group.deadline=now+group.timeout;
  }
  void RefreshChargedDeadline(double now){
    if(active_&&active_->timing){const auto clock=active_->timing->Active(active_->timing->Owner());
      if(!clock)throw std::runtime_error("Native charged phase has no active clock");
      active_->deadline=clock->Deadline(now).value_or(now+3153600000000.0);
      if(active_->phase==Phase::Release&&active_->release_drain_charge)
        active_->deadline=now+1000-(clock->Charged(now)-*active_->release_drain_charge);
    }
  }
  void StartCleanup(double now) {
    auto& group = *active_;
    group.index = static_cast<size_t>(std::find_if(group.calls.begin(), group.calls.end(),
        [](const auto& call) { return Cleanup(call.kind); }) - group.calls.begin());
    EnterCleanup(now); // One original teardown budget for all afterEach hooks.
    Advance(now);
  }
  void Fail(const std::string& error, double now,uint64_t originating_step=0) {
    if (!active_ || terminating_) return;
    if(active_->cleanup)active_->infrastructure_failure = true;
    Record(error,originating_step);
    if (active_->phase == Phase::SkipDrain || active_->phase == Phase::CleanupWait) return;
    AbortActive();
    if (!active_->cleanup && active_->calls[0].kind != HookKind::BeforeAll &&
        active_->calls[0].kind != HookKind::AfterAll) StartCleanup(now);
    else { ++active_->index; Advance(now); }
  }
  void ObserveDeadline(double now) {
    if (active_) active_->last_now = std::max(active_->last_now, now);
    RefreshChargedDeadline(now);
    if (retiring_ && !terminating_ && !leases_.empty() && now >= physical_deadline_) {
      Terminate("Hook raw callback physical cleanup deadline expired"); return;
    }
    if (!active_ || terminating_) return;
    if (active_->phase == Phase::CleanupWait) {
      if (now >= active_->deadline) {
        if (!timeout_at_) timeout_at_ = active_->deadline;
        active_->timed_out = true;
        Terminate("Native managed callback cleanup admission deadline expired");
      } else Advance(now);
      return;
    }
    if (active_->phase == Phase::Drain || active_->phase == Phase::SkipDrain) {
      if (now < active_->drain_deadline) return;
      if (now >= active_->deadline) {
        if (!timeout_at_) timeout_at_ = active_->deadline;
        active_->timed_out = true;
      }
      Terminate("Runtime skip raw callback physical cleanup deadline expired");
      return;
    }
    if (now < active_->deadline) return;
    if (!timeout_at_) timeout_at_ = active_->deadline;
    if (active_->phase == Phase::Release) {
      active_->timed_out = true;
      Terminate("Hook fixture release physical deadline expired"); return;
    }
    Record("Native hook group original deadline expired");
    active_->timed_out = true;
    AbortActive();
    if (active_->cleanup) {
      // Expired cleanup slots never extend the original teardown budget. The
      // suite controller may report later callbacks as unrun; not as passed.
      ++active_->index;
      Advance(now);
    } else if (active_->calls[0].kind == HookKind::BeforeAll || active_->calls[0].kind == HookKind::AfterAll) {
      active_->index = active_->calls.size(); Advance(now);
    } else StartCleanup(now);
  }
  void Advance(double now) {
    if (!active_ || terminating_) return;
    auto& group = *active_;
    if (group.index < group.calls.size() && Cleanup(group.calls[group.index].kind) && !group.cleanup) {
      EnterCleanup(now);
    }
    RefreshChargedDeadline(now);
    // Only actual managed callback ownership opts this group into waiting.
    // Once blocked, retain its parent raw lease through all joint fences too.
    if (group.cleanup && ((group.phase == Phase::CleanupWait &&
        HasMainPhysical(group.scope, group.generation)) ||
        (callbacks_.cleanup_ready && !callbacks_.cleanup_ready(group.scope, group.generation)))) {
      group.phase = Phase::CleanupWait;
      return;
    }
    const auto request = NextToken();
    if (!request) return;
    group.request = request;
    if (group.index >= group.calls.size()) {
      if (!group.cleanup && group.calls[0].kind != HookKind::BeforeAll &&
          group.calls[0].kind != HookKind::AfterAll) {
        EnterCleanup(now);
      }
      // Normal fixture teardown uses the original hook/cleanup budget. Only an
      // already-failed deadline gets a separate bounded physical drain window.
      if (now >= group.deadline) {
        if(group.timing)group.release_drain_charge=group.timing->Active(group.timing->Owner())->Charged(now);
        group.deadline = now + 1000;
      }
      group.phase = Phase::Release;
      callbacks_.release({request, group.scope, group.generation, group.deadline});
    } else {
      group.phase = Phase::Acquire;
      callbacks_.acquire({request, group.scope, group.generation,
          group.calls[group.index].fixtures, group.deadline, group.cleanup});
    }
  }
  std::optional<double> timeout_at_;
  Callbacks callbacks_;
  std::optional<Group> active_;
  std::map<uint64_t, Invoke> leases_;
  uint64_t nonce_ = 0;
  bool terminating_ = false, retiring_ = false;
  double physical_deadline_ = 0;
};
} // namespace suitecut::runner
