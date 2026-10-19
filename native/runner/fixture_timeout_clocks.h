// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace suitecut::runner {
// A native charged clock never changes its wall origin. Independent fixture
// work suspends charging, not the suite clock or any physical-retirement fence.
class FixtureChargedClock {
 public:
  static constexpr uint64_t kMaxTimeout = 120000;
  FixtureChargedClock(uint64_t timeout, double now, double charged = 0)
      : timeout_(timeout), origin_(now), last_(now), charged_(charged) {
    if (!Time(now) || timeout > kMaxTimeout || !Time(charged) || charged < 0)
      throw std::invalid_argument("Native charged clock admission rejected");
    expired_ = timeout && charged >= static_cast<double>(timeout);
  }
  bool CanObserve(double now) const { return Time(now) && now >= last_; }
  bool Observe(double now) {
    if (!CanObserve(now)) return false;
    if (!excluded_ && !frozen_) {
      const auto next = charged_ + (now - last_);
      if (!std::isfinite(next)) return false;
      charged_ = next;
    }
    last_ = now;
    expired_ |= timeout_ && charged_ >= static_cast<double>(timeout_);
    return true;
  }
  bool Exclude(uint64_t token, double now, bool native_cleanup = false) {
    if (!token || excluded_ || (!native_cleanup && frozen_) || !Observe(now) || (expired_ && !native_cleanup)) return false;
    excluded_ = token;
    return true;
  }
  bool Resume(uint64_t token, double now) {
    if (!excluded_ || *excluded_ != token || !Observe(now)) return false;
    if (forwarded_ && !forwarded_->Resume(token,now)) return false;
    excluded_.reset();
    forwarded_.reset();
    return true;
  }
  bool CarryExclusion(const std::shared_ptr<FixtureChargedClock>& cleanup,double now){
    if(!frozen_||!cleanup||cleanup.get()==this||forwarded_||!CanObserve(now)||
        !cleanup->CanObserve(now)||cleanup->Excluded()||cleanup->Frozen())return false;
    if(!excluded_)return true;
    if(!cleanup->Exclude(*excluded_,now,true))return false;
    forwarded_=cleanup;return true;
  }
  bool Amend(uint64_t timeout, double now) {
    if (timeout > kMaxTimeout || frozen_ || !Observe(now) || expired_) return false;
    timeout_ = timeout;
    expired_ |= timeout && charged_ >= static_cast<double>(timeout);
    return true;
  }
  std::optional<double> Deadline(double now) {
    if (!Observe(now)) throw std::invalid_argument("Native charged clock time rejected");
    if (!timeout_ || excluded_ || frozen_) return std::nullopt;
    return now + static_cast<double>(timeout_) - charged_;
  }
  bool Expired(double now) { return Observe(now) && expired_; }
  double Charged(double now) {
    if (!Observe(now)) throw std::invalid_argument("Native charged clock time rejected");
    return charged_;
  }
  double Wall(double now) const {
    if (!Time(now) || now < last_) throw std::invalid_argument("Native charged wall time rejected");
    return now - origin_;
  }
  bool Freeze(double now) {
    if (frozen_ || !Observe(now)) return false;
    frozen_ = true;
    return true; // Logical clock stop does not settle an excluded fixture lease.
  }
  bool Frozen() const { return frozen_; }
  uint64_t Timeout() const { return timeout_; }
  bool Excluded() const { return excluded_.has_value(); }
 private:
  static bool Time(double now) { return std::isfinite(now) && now >= 0; }
  uint64_t timeout_;
  double origin_, last_, charged_;
  std::optional<uint64_t> excluded_;
  std::shared_ptr<FixtureChargedClock> forwarded_;
  bool expired_ = false, frozen_ = false;
};

struct FixtureClockOwner {
  uint64_t token = 0, scope = 0, generation = 0, declaration = 0, worker = 0;
  int64_t pid = 0;
  bool worker_fixture = false;
  bool operator==(const FixtureClockOwner&) const = default;
};
enum class FixtureClockPhase { Setup, Ready, Teardown, Settled, WorkerExited };
struct FixtureClockRecord {
  FixtureClockOwner owner;
  FixtureClockPhase phase;
  uint64_t timeout;
  double started, elapsed = 0;
  bool independent, expired = false, canceled = false, raw_settled = false;
  FixtureClockPhase slot = FixtureClockPhase::Setup;
};
// Timing sidecar only: the actual FixtureOwners/ScopedFixtures manager retains
// callback tokens and owns host admission. READY is not physical settlement.
// A supplied charged clock must outlive the corresponding active slot.
class FixtureTimeoutClocks {
 public:
  static constexpr size_t kPhysical = 64, kHistory = 8192;
  bool Begin(const FixtureClockOwner& owner, std::optional<uint64_t> custom,
             uint64_t inherited, double now, FixtureChargedClock* charged) {
    if (!Owner(owner) || !Time(now) || inherited > FixtureChargedClock::kMaxTimeout ||
        (custom && *custom > FixtureChargedClock::kMaxTimeout) ||
        active_.size() >= kPhysical || records_.size() >= kHistory || active_.contains(owner.token)) return false;
    const bool independent = custom.has_value() || owner.worker_fixture;
    if (!independent && !charged) return false;
    if (charged && !charged->CanObserve(now)) return false;
    if (charged && (charged->Excluded() || charged->Expired(now))) return false;
    const size_t index = records_.size();
    records_.push_back({owner, FixtureClockPhase::Setup, custom.value_or(inherited), now, 0, independent});
    try { active_.emplace(owner.token, Slot{owner, index, custom, inherited, charged, false}); }
    catch (...) { records_.pop_back(); throw; }
    if (independent && charged && !charged->Exclude(owner.token, now)) {
      active_.erase(owner.token); records_.pop_back(); return false;
    }
    active_.at(owner.token).excluded = independent && charged;
    return true;
  }
  bool Ready(const FixtureClockOwner& owner, double now) {
    auto* slot = Find(owner);
    if (!slot || records_[slot->record].phase != FixtureClockPhase::Setup || !Observe(*slot, now)) return false;
    auto& record = records_[slot->record];
    if (record.expired || record.canceled) return false;
    Resume(*slot, now); record.phase = FixtureClockPhase::Ready;
    slot->charged = nullptr; // READY worker leases may outlive this test clock.
    return true;
  }
  bool Teardown(const FixtureClockOwner& owner, uint64_t inherited, double now,
                FixtureChargedClock* cleanup) {
    auto* slot = Find(owner);
    if (!slot || inherited > FixtureChargedClock::kMaxTimeout || !Time(now) ||
        records_[slot->record].phase != FixtureClockPhase::Ready ||
        now < records_[slot->record].started + records_[slot->record].elapsed || records_.size() >= kHistory) return false;
    const bool independent = records_[slot->record].independent;
    if (cleanup && !cleanup->CanObserve(now)) return false;
    if (cleanup && cleanup->Excluded()) return false;
    const size_t next = records_.size();
    // Reserve the history record before changing either clock or lease state.
    const auto next_inherited = owner.worker_fixture ? slot->inherited : inherited;
    records_.push_back({owner, FixtureClockPhase::Teardown, slot->custom.value_or(next_inherited), now, 0, independent});
    records_.back().slot = FixtureClockPhase::Teardown;
    if (independent && cleanup && !cleanup->Exclude(owner.token, now, true)) { records_.pop_back(); return false; }
    slot->record = next; slot->charged = cleanup; slot->inherited = next_inherited; slot->excluded = independent && cleanup;
    return true;
  }
  bool Amend(const FixtureClockOwner& owner, uint64_t timeout, double now) {
    auto* slot = Find(owner);
    if (!slot || timeout > FixtureChargedClock::kMaxTimeout || !Observe(*slot, now)) return false;
    auto& record = records_[slot->record];
    if (!Active(record) || record.expired || record.canceled) return false;
    if (!record.independent && (!slot->charged || !slot->charged->Amend(timeout, now))) return false;
    record.timeout = timeout;
    if (record.independent) slot->custom = timeout; // Independent amendment persists into the fresh teardown slot.
    record.expired = record.independent ? timeout && record.elapsed >= static_cast<double>(timeout) : slot->charged->Expired(now);
    return true;
  }
  std::optional<double> Deadline(const FixtureClockOwner& owner, double now) {
    auto* slot = Find(owner);
    if (!slot || !Observe(*slot, now)) throw std::invalid_argument("Native fixture clock owner/time rejected");
    const auto& record = records_[slot->record];
    if (!Active(record)) return std::nullopt;
    if (!record.independent) return slot->charged ? slot->charged->Deadline(now) : std::nullopt;
    if (!record.timeout) return std::nullopt;
    return record.started + static_cast<double>(record.timeout);
  }
  // The caller supplies its genuinely established suite/stop cleanup cap. This
  // never intersects an independent fixture slot with the charged test budget.
  std::optional<double> NextDeadline(const FixtureClockOwner& owner, double now,
                                     std::optional<double> original_cap) {
    if (original_cap && !Time(*original_cap))
      throw std::invalid_argument("Native fixture original cap rejected");
    const auto local = Deadline(owner, now);
    if (!local) return original_cap;
    return original_cap ? std::optional<double>(std::min(*local, *original_cap)) : local;
  }
  bool Cancel(const FixtureClockOwner& owner, double now) {
    auto* slot = Find(owner);
    if (!slot || !Observe(*slot, now) || !Active(records_[slot->record])) return false;
    records_[slot->record].canceled = true;
    return true; // Logical cancel neither resumes charging nor retires a callback.
  }
  bool Settled(const FixtureClockOwner& owner, double now) {
    auto* slot = Find(owner);
    if (!slot || !Observe(*slot, now)) return false;
    Resume(*slot, now);
    auto& record = records_[slot->record];
    record.phase = FixtureClockPhase::Settled; record.raw_settled = true;
    active_.erase(owner.token);
    return true;
  }
  // Call only after this exact actual worker's waitpid receipt; not on EOF,
  // timeout, cancellation, READY, or host logical callback completion.
  bool ActualWorkerExited(uint64_t worker, int64_t pid, double now) {
    if (!worker || pid <= 0 || !Time(now)) return false;
    bool matched = false;
    for (const auto& [token, slot] : active_) {
      (void)token;
      if (slot.owner.worker == worker && slot.owner.pid == pid) matched = true;
      if (slot.owner.worker == worker && slot.owner.pid == pid &&
          (now < records_[slot.record].started + records_[slot.record].elapsed ||
           (slot.charged && !slot.charged->CanObserve(now)))) return false;
    }
    if (!matched) return false;
    for (auto it = active_.begin(); it != active_.end();) {
      auto& slot = it->second;
      if (slot.owner.worker != worker || slot.owner.pid != pid) { ++it; continue; }
      (void)Observe(slot, now); Resume(slot, now);
      records_[slot.record].phase = FixtureClockPhase::WorkerExited;
      it = active_.erase(it);
    }
    return true;
  }
  const FixtureClockRecord* Current(const FixtureClockOwner& owner) const {
    const auto found = active_.find(owner.token);
    return found != active_.end() && found->second.owner == owner ? &records_[found->second.record] : nullptr;
  }
  size_t Physical() const { return active_.size(); }
  bool IdleReady() const {
    for (const auto& [token, slot] : active_) {
      (void)token;
      if (!slot.owner.worker_fixture || records_[slot.record].phase != FixtureClockPhase::Ready) return false;
    }
    return true;
  }
  const std::vector<FixtureClockRecord>& Records() const { return records_; }
 private:
  struct Slot { FixtureClockOwner owner; size_t record; std::optional<uint64_t> custom; uint64_t inherited; FixtureChargedClock* charged; bool excluded; };
  static bool Time(double now) { return std::isfinite(now) && now >= 0; }
  static bool Owner(const FixtureClockOwner& owner) {
    return owner.token && owner.scope && owner.generation && owner.declaration && owner.worker && owner.pid > 0;
  }
  static bool Active(const FixtureClockRecord& record) {
    return record.phase == FixtureClockPhase::Setup || record.phase == FixtureClockPhase::Teardown;
  }
  Slot* Find(const FixtureClockOwner& owner) {
    const auto found = active_.find(owner.token);
    return found != active_.end() && found->second.owner == owner ? &found->second : nullptr;
  }
  bool Observe(Slot& slot, double now) {
    auto& record = records_[slot.record];
    if (!Time(now) || now < record.started + record.elapsed ||
        (slot.charged && !slot.charged->CanObserve(now))) return false;
    if (Active(record)) {
      record.elapsed = now - record.started;
      record.expired |= record.independent && record.timeout && record.elapsed >= static_cast<double>(record.timeout);
      if (!record.independent && slot.charged) record.expired |= slot.charged->Expired(now);
    }
    return true;
  }
  static void Resume(Slot& slot, double now) {
    if (slot.excluded) {
      if (!slot.charged->Resume(slot.owner.token, now)) throw std::logic_error("Native fixture charged interval ownership rejected");
      slot.excluded = false;
    }
  }
  std::map<uint64_t, Slot> active_;
  std::vector<FixtureClockRecord> records_;
};
} // namespace suitecut::runner
