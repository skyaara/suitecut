// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <functional>
#include <memory>
#include "context/context_route_registration.h"
#include "network/route_callback_receipt.h"
#include "network/page_route_callback_selection.h"
#include <optional>
#include <string>

namespace suitecut {
// UI-thread leases include transport-queued invocations and actually started
// host closures. Request timeout does not settle an authored asynchronous call.
class NativeRouteCallbackLeases {
 public:
  NativeRouteCallbackLeases() = default;
  NativeRouteCallbackLeases(const NativeRouteCallbackLeases&) = delete;
  NativeRouteCallbackLeases& operator=(const NativeRouteCallbackLeases&) = delete;
  ~NativeRouteCallbackLeases() { Clear(); }
  enum class Stage { Issued, Running };
  std::optional<uint64_t> Scope() {
    if (scope_serial_ == UINT64_MAX) return std::nullopt;
    return ++scope_serial_;
  }
  std::optional<uint64_t> Issue(uint64_t scope, const std::string& invocation,
                                std::shared_ptr<NativeContextRouteRegistration> policy = {}) {
    if (!scope || scope > scope_serial_ || invocation.empty() || serial_ == UINT64_MAX)
      return std::nullopt;
    const auto token = ++serial_;
    entries_.emplace(token, Entry{scope, invocation, Stage::Issued, false, std::move(policy),
                                  std::make_shared<NativeRouteCallbackReceipt>(), false});
    return token;
  }
  bool Start(uint64_t token, uint64_t scope, const std::string& invocation) {
    const auto found = entries_.find(token);
    if (found == entries_.end() || !Matches(found->second, scope, invocation) ||
        found->second.stage != Stage::Issued || found->second.closed)
      return false;
    found->second.stage = Stage::Running;
    return true;
  }
  // A completed lease answers its host Finish idempotently while its scope is
  // open. Once the scope is closed, a lease's one Finish is its last use, so it
  // is released then (or at Close when that Finish already arrived).
  bool Finish(uint64_t token, uint64_t scope, const std::string& invocation) {
    const auto found = entries_.find(token);
    if (found == entries_.end()) {
      const auto completed = completed_.find(token);
      if (completed == completed_.end() || !Matches(completed->second, scope, invocation))
        return false;
      if (completed->second.closed)
        completed_.erase(completed);
      else
        completed->second.finished = true;
      return true;
    }
    if (!Matches(found->second, scope, invocation)) return false;
    if (found->second.closed)
      found->second.receipt->settled_ = true;
    else
      Complete(token, found->second, true);
    entries_.erase(found);
    return true;
  }
  void Revoke(uint64_t scope, const std::string& invocation) {
    for (auto it = entries_.begin(); it != entries_.end();) {
      if (!Matches(it->second, scope, invocation) || it->second.stage == Stage::Running) {
        ++it;
        continue;
      }
      Complete(it->first, it->second);
      it = entries_.erase(it);
    }
  }
  void Close(uint64_t scope) {
    for (auto it = entries_.begin(); it != entries_.end();) {
      if (it->second.scope != scope) {
        ++it;
        continue;
      }
      if (it->second.stage == Stage::Running) {
        it->second.closed = true;
        ++it;
        continue;
      }
      Complete(it->first, it->second);
      it = entries_.erase(it);
    }
    // Finished leases of a closed scope are unreachable; the rest await their one Finish.
    for (auto it = completed_.begin(); it != completed_.end();) {
      if (it->second.scope != scope) {
        ++it;
        continue;
      }
      if (it->second.finished) {
        it = completed_.erase(it);
        continue;
      }
      it->second.closed = true;
      ++it;
    }
  }
  std::shared_ptr<const NativeRouteCallbackReceipt> Receipt(uint64_t token, uint64_t scope,
                                                            const std::string& invocation) const {
    const auto found = entries_.find(token);
    return found != entries_.end() && Matches(found->second, scope, invocation)
               ? found->second.receipt
               : nullptr;
  }
  // No lease count cap; only an exhausted token serial refuses a new lease.
  bool Available() const { return serial_ != UINT64_MAX; }
  size_t size() const { return entries_.size(); }
  size_t completed() const { return completed_.size(); }
  size_t running() const {
    size_t count = 0;
    for (const auto& [token, entry] : entries_)
      if (entry.stage == Stage::Running) ++count;
    return count;
  }
  size_t SizeForScope(uint64_t scope) const {
    size_t count = 0;
    for (const auto& [token, entry] : entries_)
      if (entry.scope == scope) ++count;
    return count;
  }
  NativeRouteCallbackSnapshot Capture(
      const std::function<bool(const NativeContextRouteRegistration&)>& matches) const {
    NativeRouteCallbackSnapshot result;
    for (const auto& [token, entry] : entries_)
      if (entry.policy && matches(*entry.policy)) result.push_back(entry.receipt);
    return result;
  }
  NativePageRouteCallbackSelection CapturePage(uint64_t scope) const {
    NativePageRouteCallbackSelection result;
    if (!scope || scope > scope_serial_) return result;
    result.scope = scope;
    // A context callback has its actual shared context registration; plain
    // Page handlers have no such policy. HAR replay is handled natively and
    // does not issue a language callback lease.
    for (const auto& [token, entry] : entries_)
      if (entry.scope == scope && !entry.policy) {
        result.tokens.push_back(token);
        result.receipts.push_back(entry.receipt);
      }
    return result;
  }
  void Clear() {
    for (auto& [token, entry] : entries_) entry.receipt->unavailable_ = true;
    entries_.clear();
    completed_.clear();
  }

 private:
  struct Entry {
    uint64_t scope;
    std::string invocation;
    Stage stage;
    bool closed;
    std::shared_ptr<NativeContextRouteRegistration> policy;
    std::shared_ptr<NativeRouteCallbackReceipt> receipt;
    bool finished;
  };
  static bool Matches(const Entry& entry, uint64_t scope, const std::string& invocation) {
    return entry.scope == scope && entry.invocation == invocation;
  }
  void Complete(uint64_t token, const Entry& entry, bool finished = false) {
    entry.receipt->settled_ = true;
    auto completed = entry;
    completed.policy.reset();
    completed.receipt.reset();
    completed.finished = finished;
    completed_.emplace(token, std::move(completed));
  }
  std::map<uint64_t, Entry> entries_, completed_;
  uint64_t serial_ = 0, scope_serial_ = 0;
};
inline NativeRouteCallbackLeases& GlobalRouteCallbackLeases() {
  static NativeRouteCallbackLeases leases;
  return leases;
}
}  // namespace suitecut
