// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_timeout_clocks.h"
#include <array>
#include <functional>
#include <memory>

namespace suitecut::runner {
// Native fixture timing ingress. The caller supplies an already-admitted real
// fixture owner; this controller neither creates author callbacks nor waits for
// processes. In particular a timeout is not a raw settlement receipt.
class FixtureTimeoutRuntime {
 public:
  struct Callbacks {
    std::function<void(const FixtureClockRecord&, double)> expired;
    std::function<void(const FixtureClockOwner&)> physical_timeout;
  };
  struct Amendment { uint64_t timeout; bool independent, expired; };
  explicit FixtureTimeoutRuntime(Callbacks callbacks)
      : callbacks_(std::move(callbacks)) {
    if (!callbacks_.expired || !callbacks_.physical_timeout)
      throw std::invalid_argument("Native fixture timeout callbacks missing");
  }
  bool Begin(const FixtureClockOwner& owner, std::optional<uint64_t> configured,
             uint64_t project_timeout, std::shared_ptr<FixtureChargedClock> charged,
             double now) {
    if (contexts_.contains(owner.token) || contexts_.size() >= FixtureTimeoutClocks::kPhysical)
      return false;
    // Reserve the owning shared pointer before the timing sidecar can borrow it.
    // Pre-spawn/allocation failures leave the caller's genuine lease intact.
    const auto inherited = owner.worker_fixture ? project_timeout :
        charged ? charged->Timeout() : project_timeout;
    auto [context, inserted] = contexts_.emplace(owner.token, Context{owner, std::move(charged)});
    if (!inserted) return false;
    try {
      if (clocks_.Begin(owner, configured, inherited, now, context->second.charged.get())) return true;
    } catch (...) { contexts_.erase(context); throw; }
    contexts_.erase(context);
    return false;
  }
  bool Ready(const FixtureClockOwner& owner, double now) {
    auto* context = Find(owner);
    if (!context || !Current(owner) || context->drain_deadline) return false;
    Expire(*context, now);
    context = Find(owner);
    if (!context || context->drain_deadline || !clocks_.Ready(owner, now)) return false;
    context->charged.reset(); // Same live worker lease can outlast its old test.
    return true;
  }
  bool Teardown(const FixtureClockOwner& owner, uint64_t inherited,
                std::shared_ptr<FixtureChargedClock> cleanup, double now) {
    auto* context = Find(owner);
    if (!context || context->drain_deadline || !clocks_.Teardown(owner, inherited, now, cleanup.get()))
      return false;
    context->charged = std::move(cleanup);
    return true;
  }
  std::optional<Amendment> Amend(const FixtureClockOwner& owner, uint64_t request,
                                  uint64_t timeout, double now) {
    auto* context = Find(owner);
    if (!context || !request || request <= context->last_request || context->drain_deadline)
      return std::nullopt;
    // A rejected genuine request is consumed too. Neither replay nor a later
    // request can revive an expired slot or move its original clock origin.
    context->last_request = request;
    if (!clocks_.Amend(owner, timeout, now)) return std::nullopt;
    const auto record = *clocks_.Current(owner);
    Expire(*context, now);
    return Amendment{record.timeout, record.independent, record.expired};
  }
  bool Cancel(const FixtureClockOwner& owner, double now, double actual_drain_deadline) {
    auto* context = Find(owner);
    if (!context || !std::isfinite(actual_drain_deadline) || actual_drain_deadline <= now ||
        !clocks_.Cancel(owner, now)) return false;
    // Repeated logical stop cannot extend an already-established physical cap.
    context->drain_deadline = context->drain_deadline ?
        std::min(*context->drain_deadline, actual_drain_deadline) : actual_drain_deadline;
    return true;
  }
  bool Settled(const FixtureClockOwner& owner, double now) {
    if (!Find(owner) || !clocks_.Settled(owner, now)) return false;
    contexts_.erase(owner.token);
    return true; // Timing sidecar only. Real callback/file owners retire elsewhere.
  }
  void Tick(double now) {
    if (!std::isfinite(now) || now < 0) return;
    // Never hold a map iterator across callbacks; authentic expiration may
    // synchronously cancel a scope or observe actual worker exit.
    std::array<FixtureClockOwner, FixtureTimeoutClocks::kPhysical> owners{};
    size_t count = 0;
    for (const auto& [token, context] : contexts_) { (void)token; owners[count++] = context.owner; }
    for (size_t i = 0; i < count; ++i) {
      auto* context = Find(owners[i]);
      if (!context) continue;
      Expire(*context, now);
      context = Find(owners[i]);
      if (context && context->drain_deadline && now >= *context->drain_deadline &&
          !context->physical_reported) {
        context->physical_reported = true;
        callbacks_.physical_timeout(owners[i]);
      }
    }
  }
  std::optional<double> SlotDeadline(const FixtureClockOwner& owner, double now,
                                     std::optional<double> original_cap) {
    const auto* context = Find(owner);
    const auto* record = clocks_.Current(owner);
    if (!context || !record || context->drain_deadline ||
        (record->phase != FixtureClockPhase::Setup && record->phase != FixtureClockPhase::Teardown))
      return std::nullopt;
    const auto local = clocks_.NextDeadline(owner, now, original_cap);
    return local;
  }
  std::optional<double> DrainDeadline(const FixtureClockOwner& owner) const {
    const auto found = contexts_.find(owner.token);
    return found != contexts_.end() && found->second.owner == owner ?
        found->second.drain_deadline : std::nullopt;
  }
  bool ActualWorkerExited(uint64_t worker, int64_t pid, double now) {
    if (!clocks_.ActualWorkerExited(worker, pid, now)) return false;
    for (auto it = contexts_.begin(); it != contexts_.end();) {
      if (it->second.owner.worker == worker && it->second.owner.pid == pid) it = contexts_.erase(it);
      else ++it;
    }
    return true; // Caller must already hold this exact OS waitpid receipt.
  }
  const FixtureClockRecord* Current(const FixtureClockOwner& owner) const { return clocks_.Current(owner); }
  const std::vector<FixtureClockRecord>& Records() const { return clocks_.Records(); }
  size_t Physical() const { return contexts_.size(); }
  bool IdleReady() const { return clocks_.IdleReady(); }
 private:
  struct Context {
    FixtureClockOwner owner;
    std::shared_ptr<FixtureChargedClock> charged;
    uint64_t last_request = 0;
    std::optional<double> drain_deadline = std::nullopt;
    bool physical_reported = false;
  };
  Context* Find(const FixtureClockOwner& owner) {
    const auto found = contexts_.find(owner.token);
    return found != contexts_.end() && found->second.owner == owner ? &found->second : nullptr;
  }
  void Expire(Context& context, double now) {
    const auto* current = clocks_.Current(context.owner);
    if (!current || context.drain_deadline ||
        (current->phase != FixtureClockPhase::Setup && current->phase != FixtureClockPhase::Teardown)) return;
    const auto deadline = clocks_.Deadline(context.owner, now);
    if (!deadline || now < *deadline) return;
    const auto owner = context.owner;
    if (!clocks_.Cancel(owner, now)) return;
    // Physical grace starts at the original expiration, never the polling time.
    context.drain_deadline = *deadline + 1000;
    const auto record = *clocks_.Current(owner);
    callbacks_.expired(record, now);
  }
  Callbacks callbacks_;
  FixtureTimeoutClocks clocks_;
  std::map<uint64_t, Context> contexts_;
};
} // namespace suitecut::runner
