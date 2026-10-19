// SPDX-License-Identifier: MIT
#ifndef SUITECUT_DRAG_COMPLETIONS_H_
#define SUITECUT_DRAG_COMPLETIONS_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace suitecut {

// UI-thread-only. Source supplies actual retained protocol receipts and calls
// ChannelClosed only after the real Inspector channel has closed. This header
// alone does not integrate native pointer dispatch or manufacture an ACK.
class DragCompletions : public std::enable_shared_from_this<DragCompletions> {
 public:
  struct Scope {
    uint64_t channel = 0, incarnation = 0;
    int owner = 0;
    uint64_t generation = 0;
    bool operator==(const Scope&) const = default;
  };
  enum class Kind { Manual, Helper };
  enum class Stage { Drop, MouseRelease, DisableIntercept };
  enum class Outcome { Acknowledged, Error, Refused };
  using Payload = std::shared_ptr<void>;
  using Reply = std::function<void(Outcome, const std::string&)>;
  struct Budget {
    size_t records = 0, bytes = 0;
    size_t max_records = 64, max_bytes = 16 * 1048576;
  };
  struct Hooks {
    std::function<double()> now;
    std::function<void()> wake;
    // false means callback ownership was never accepted and no command was
    // dispatched. A registered-but-refused submission replies Outcome::Refused.
    std::function<bool(const Scope&, Stage, const Payload&, Reply)> send;
    std::function<bool(const Scope&)> operation_current;
    std::function<bool(const Scope&)> incarnation_current;
    std::function<void(const Scope&, const Payload&, bool, const std::string&)> logical;
    std::function<void(const Scope&, const std::string&)> close_channel;
  };

  static std::shared_ptr<DragCompletions> Create(Hooks hooks,
                                                std::shared_ptr<Budget> budget) {
    if (!budget || !hooks.now || !hooks.wake || !hooks.send ||
        !hooks.operation_current || !hooks.incarnation_current ||
        !hooks.logical || !hooks.close_channel) return nullptr;
    return std::shared_ptr<DragCompletions>(
        new DragCompletions(std::move(hooks), std::move(budget)));
  }
  ~DragCompletions() { assert(records_.empty()); }

  bool Begin(Scope scope, Kind kind, size_t bytes, double deadline,
             Payload payload) {
    const double now = hooks_.now();
    if (closed_ || !records_.empty() || !scope.channel || !scope.incarnation ||
        scope.owner < 1 || !scope.generation || !payload || bytes > 1048576 ||
        !std::isfinite(now) || !std::isfinite(deadline) || deadline < 0 ||
        (deadline > 0 && (now >= deadline || !std::isfinite(deadline + kGrace))) ||
        budget_->records >= budget_->max_records ||
        bytes > budget_->max_bytes - std::min(budget_->bytes, budget_->max_bytes))
      return false;
    auto record = std::make_shared<Record>();
    record->scope = scope; record->kind = kind; record->bytes = bytes;
    record->deadline = deadline; record->payload = std::move(payload);
    // Insert before reserving counters: an allocation exception cannot leave a
    // capacity ghost or claim any native command was admitted.
    records_.emplace(scope.incarnation, record);
    ++budget_->records; budget_->bytes += bytes; record->reserved = true;
    Dispatch(record, Stage::Drop);
    return true;
  }
  void Cancel(const Scope& scope) {
    const auto record = Find(scope);
    if (!record || record->closing) return;
    record->logical = false;
    ArmRetirement(record);
  }
  void Tick() {
    std::vector<std::shared_ptr<Record>> snapshot;
    for (const auto& [id, record] : records_) snapshot.push_back(record);
    for (const auto& record : snapshot) {
      if (!Live(record) || record->closing) continue;
      Expire(record);
      if (Live(record) && !record->closing && record->retirement > 0 &&
          hooks_.now() >= record->retirement)
        Close(record, "Native drag exceeded its physical cleanup grace");
    }
  }
  bool NeedsTick() const {
    for (const auto& [id, record] : records_)
      if (!record->closing && (record->deadline > 0 || record->retirement > 0))
        return true;
    return false;
  }
  void ChannelClosed() {
    closed_ = true;
    std::vector<std::shared_ptr<Record>> snapshot;
    for (const auto& [id, record] : records_) snapshot.push_back(record);
    for (const auto& record : snapshot) Retire(record);
  }
  bool Busy() const { return !records_.empty(); }
  size_t Retained() const { return records_.size(); }
  size_t Pending() const {
    size_t count = 0;
    for (const auto& [id, record] : records_) count += record->waiting;
    return count;
  }
  size_t Closing() const {
    size_t count = 0;
    for (const auto& [id, record] : records_) count += record->closing;
    return count;
  }

 private:
  static constexpr double kGrace = 30000;
  struct Record {
    Scope scope;
    Kind kind = Kind::Manual;
    Stage stage = Stage::Drop;
    Payload payload;
    size_t bytes = 0;
    double deadline = 0, retirement = 0;
    uint64_t sequence = 0;
    bool logical = true, waiting = false, closing = false, reserved = false;
  };
  DragCompletions(Hooks hooks, std::shared_ptr<Budget> budget)
      : hooks_(std::move(hooks)), budget_(std::move(budget)) {}
  std::shared_ptr<Record> Find(const Scope& scope) const {
    const auto it = records_.find(scope.incarnation);
    return it != records_.end() && it->second->scope == scope ? it->second : nullptr;
  }
  bool Live(const std::shared_ptr<Record>& record) const {
    return !closed_ && Find(record->scope) == record && record->reserved;
  }
  void ArmRetirement(const std::shared_ptr<Record>& record) {
    const double limit = hooks_.now() + kGrace;
    const double original = record->deadline > 0 ? record->deadline + kGrace : limit;
    const double retirement = std::min(limit, original);
    record->retirement = record->retirement > 0
                             ? std::min(record->retirement, retirement) : retirement;
    hooks_.wake();
  }
  void Fail(const std::shared_ptr<Record>& record, const std::string& error) {
    if (!record->logical) return;
    record->logical = false;
    ArmRetirement(record);
    if (hooks_.operation_current(record->scope))
      hooks_.logical(record->scope, record->payload, false, error.substr(0,4096));
  }
  void Expire(const std::shared_ptr<Record>& record) {
    if (!record->logical) return;
    if (!hooks_.operation_current(record->scope)) {
      record->logical = false; ArmRetirement(record); return;
    }
    if (record->deadline > 0 && hooks_.now() >= record->deadline)
      Fail(record, "Native drag original operation timed out");
  }
  void Close(const std::shared_ptr<Record>& record, const std::string& error) {
    if (!Live(record) || record->closing) return;
    record->closing = true;
    Fail(record, error);
    // This hook must post Source closure; it must not close from an observer.
    if (Live(record)) hooks_.close_channel(record->scope, error);
  }
  void Retire(const std::shared_ptr<Record>& record) {
    if (!record->reserved) return;
    record->reserved = false; record->waiting = false; record->logical = false;
    assert(budget_->records && budget_->bytes >= record->bytes);
    --budget_->records; budget_->bytes -= record->bytes;
    record->payload.reset();
    const auto it = records_.find(record->scope.incarnation);
    if (it != records_.end() && it->second == record) records_.erase(it);
  }
  void Finished(const std::shared_ptr<Record>& record) {
    if (!Live(record)) return;
    Expire(record);
    const bool publish = Live(record) && record->logical &&
                         hooks_.incarnation_current(record->scope) &&
                         hooks_.operation_current(record->scope);
    const auto payload = record->payload;
    Retire(record);
    if (publish) hooks_.logical(record->scope, payload, true, "");
  }
  void Dispatch(const std::shared_ptr<Record>& record, Stage stage) {
    if (!Live(record) || record->closing) return;
    if (!hooks_.incarnation_current(record->scope)) {
      Close(record, "Native drag owner changed before physical cleanup"); return;
    }
    record->stage = stage; record->waiting = true;
    const auto sequence = ++record->sequence;
    auto self = shared_from_this();
    bool owned = false;
    try {
      owned = hooks_.send(record->scope, stage, record->payload,
          [self, record, sequence](Outcome outcome, const std::string& error) {
            self->Acknowledged(record, sequence, outcome, error);
          });
    } catch (...) {
      // An exception is not proof of transport refusal or successful cleanup.
      // Expose failure and request real channel closure, retaining this bounded
      // record and any already accepted callback until actual receipt/death.
      Close(record, "Native drag transport failed while establishing ownership");
      return;
    }
    if (!owned) Acknowledged(record, sequence, Outcome::Refused,
                            "Native drag command was not admitted");
    if (Live(record)) hooks_.wake();
  }
  void Acknowledged(const std::shared_ptr<Record>& record, uint64_t sequence,
                    Outcome outcome, const std::string& error) {
    if (!Live(record) || !record->waiting || record->sequence != sequence) return;
    record->waiting = false;
    Expire(record);
    if (!Live(record)) return;
    if (record->closing) return;
    if (!hooks_.incarnation_current(record->scope)) {
      Close(record, "Native drag owner changed before physical cleanup"); return;
    }
    if (outcome != Outcome::Acknowledged) {
      Fail(record, error.empty() ? "Native drag command failed" : error);
      if (!Live(record)) return;
      // Refusal proves this command did not run; it does not identify the
      // original press widget for a replacement cleanup mouse-up. Keep the
      // ownership reservation until actual owning-channel closure.
      Close(record, "Native drag physical command failed: " + error.substr(0,4096));
      return;
    }
    // A canceled helper cannot send an old final mouse-up through a current
    // widget lookup. Public CEF has no immutable widget delivery lease here;
    // actual owning-channel closure is the conservative settlement boundary.
    if (!record->logical && record->kind == Kind::Helper) {
      Close(record, "Native canceled helper drag requires owning-channel closure"); return;
    }
    switch (record->stage) {
      case Stage::Drop:
        Dispatch(record, record->kind == Kind::Helper
                             ? Stage::MouseRelease : Stage::DisableIntercept); break;
      case Stage::MouseRelease: Dispatch(record, Stage::DisableIntercept); break;
      case Stage::DisableIntercept: Finished(record); break;
    }
  }
  Hooks hooks_;
  std::shared_ptr<Budget> budget_;
  std::map<uint64_t, std::shared_ptr<Record>> records_;
  bool closed_ = false;
};
}  // namespace suitecut
#endif
