// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_timeout_clocks.h"
#include <memory>

namespace suitecut::runner {
struct FixtureAttemptClockOwner {
  uint64_t scope = 0, generation = 0, worker = 0;
  int64_t pid = 0;
  bool operator==(const FixtureAttemptClockOwner&) const = default;
};
// Associated attempt/all-hook clocks only. Caller is the actual native phase
// controller; this object admits no author callback and retires no physical job.
class FixtureAttemptClocks {
 public:
  struct Observation { double charged, wall; bool closed, excluded; };
  FixtureAttemptClocks(FixtureAttemptClockOwner owner, uint64_t timeout, double now, double charged = 0,
                        std::optional<double> prior_wall = std::nullopt)
      : owner_(owner), started_(now-prior_wall.value_or(charged)), main_(std::make_shared<FixtureChargedClock>(timeout, now, charged)) {
    if (!owner.scope || !owner.generation || !owner.worker || owner.pid <= 0 || charged > now ||
        (prior_wall && (!std::isfinite(*prior_wall) || *prior_wall < charged || *prior_wall > now)))
      throw std::invalid_argument("Native fixture attempt clock owner rejected");
  }
  const FixtureAttemptClockOwner& Owner() const { return owner_; }
  std::shared_ptr<FixtureChargedClock> Active(const FixtureAttemptClockOwner& owner) const {
    if (owner != owner_ || closed_) return {};
    return cleanup_ ? cleanup_ : main_;
  }
  bool BeginCleanup(const FixtureAttemptClockOwner& owner, double now) {
    if (owner != owner_ || closed_ || cleanup_ || !main_->CanObserve(now)) return false;
    // Reserve before stopping main work; allocation failure leaves its original
    // clock and every fixture owner intact for actual emergency retirement.
    auto next = std::make_shared<FixtureChargedClock>(main_->Timeout(), now);
    if (!main_->Freeze(now)) return false;
    if(!main_->CarryExclusion(next,now))throw std::logic_error("Native fixture cleanup exclusion ownership rejected");
    cleanup_ = std::move(next);
    return true;
  }
  bool Amend(const FixtureAttemptClockOwner& owner, uint64_t timeout, double now) {
    const auto current = Active(owner);
    return current && current->Amend(timeout, now);
  }
  bool Finish(const FixtureAttemptClockOwner& owner, double now) {
    const auto current = Active(owner);
    if (!current || !current->Freeze(now)) return false;
    closed_ = true;
    return true;
  }
  Observation Observe(const FixtureAttemptClockOwner& owner, double now) const {
    if (owner != owner_) throw std::invalid_argument("Native fixture attempt observation owner rejected");
    const auto charged = main_->Charged(now) + (cleanup_ ? cleanup_->Charged(now) : 0);
    return {charged, now - started_, closed_, main_->Excluded() || (cleanup_ && cleanup_->Excluded())};
  }
  bool RetirementReady(const FixtureAttemptClockOwner& owner) const {
    return owner == owner_ && closed_ && !main_->Excluded() && (!cleanup_ || !cleanup_->Excluded());
  }
 private:
  FixtureAttemptClockOwner owner_;
  double started_;
  std::shared_ptr<FixtureChargedClock> main_, cleanup_;
  bool closed_ = false;
};
} // namespace suitecut::runner
