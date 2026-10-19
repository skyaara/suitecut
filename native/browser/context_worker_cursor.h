// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>

namespace suitecut {
// Native manager-owned sequence reservation. A receipt from a physically closed
// Client is the only path which makes the same context available for replacement.
class NativeContextWorkerCursors {
 public:
  static constexpr uint64_t kMaxSequence = 9007199254740991ULL;
  static constexpr size_t kTargets = 1024;
  // Each of 1024 admitted active targets can have a channel-close receipt and
  // then an author-close receipt; 1024 already-detached targets can close once.
  // Ordinary event exhaustion rejects new attachments before their admission,
  // so these actual retirement receipts have a finite reserved upper bound.
  static constexpr uint64_t kRetirementSequences = 3 * kTargets;
  static constexpr uint64_t kAdmissionSequence = kMaxSequence - kRetirementSequences;
  static constexpr size_t kContexts = 32;
  struct Lease {
    int context = 0;
    uint64_t token = 0;
    uint64_t initial = 0;
  };
  std::optional<Lease> Acquire(int context) {
    if (context < 1 || next_token_ == std::numeric_limits<uint64_t>::max()) return {};
    const auto found = entries_.find(context);
    if (found == entries_.end()) {
      if (entries_.size() >= kContexts) return {};
    } else if (found->second.active || found->second.invalid || found->second.sequence >= kAdmissionSequence) return {};
    const uint64_t initial = found == entries_.end() ? 0 : found->second.sequence;
    const uint64_t token = ++next_token_;
    entries_[context] = Entry{initial, token, true, false};
    return Lease{context, token, initial};
  }
  // Use only after all OnBeforeClose worker/console retirement and retained
  // callback processing has finished. Invalid cursors quarantine this context.
  bool Closed(Lease lease, uint64_t final_sequence) {
    auto* entry = Current(lease);
    if (!entry) return false;
    entry->active = false;
    if (final_sequence < lease.initial || final_sequence > kMaxSequence) {
      entry->invalid = true;
      return false;
    }
    entry->sequence = final_sequence;
    return true;
  }
  // CEF rejected CreateBrowser before accepting any browser ownership. This is
  // not an Inspector close receipt and cannot advance or reset the cursor.
  bool Uncreated(Lease lease) {
    auto* entry = Current(lease);
    if (!entry) return false;
    entry->active = false;
    return true;
  }
  // Called only from the actual context-gone callback, never logical cancellation.
  bool ContextGone(int context) {
    const auto found = entries_.find(context);
    if (found == entries_.end()) return true;
    if (found->second.active) return false;
    entries_.erase(found);
    return true;
  }
  static bool CanNext(uint64_t sequence, bool actual_retirement = false) {
    return sequence < (actual_retirement ? kMaxSequence : kAdmissionSequence);
  }
  static bool Next(uint64_t& sequence, bool actual_retirement = false) {
    if (!CanNext(sequence, actual_retirement)) return false;
    ++sequence;
    return true;
  }
  size_t size() const { return entries_.size(); }
 private:
  struct Entry {
    uint64_t sequence;
    uint64_t token;
    bool active;
    bool invalid;
  };
  Entry* Current(Lease lease) {
    auto found = entries_.find(lease.context);
    if (found == entries_.end() || !found->second.active || found->second.token != lease.token || found->second.sequence != lease.initial) return nullptr;
    return &found->second;
  }
  std::map<int, Entry> entries_;
  uint64_t next_token_ = 0;
};
}  // namespace suitecut
