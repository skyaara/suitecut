// SPDX-License-Identifier: MIT
// Included by Source and its controlled native publication host. A retry that
// crosses the original deadline may publish only an already admitted mismatch.
bool CompleteExpiredExpectation(int id, uint64_t generation) {
  if (!operations_.IsCurrent(id, generation) ||
      !operations_.IsExpired(id, generation, Now())) return false;
  const auto found = expect_results_.find(id);
  if (found == expect_results_.end()) return false;
  // Complete retires the cache entry; retain the exact observed bytes first.
  const auto snapshot = found->second;
  Complete(id, true, "", snapshot);
  return true;
}
