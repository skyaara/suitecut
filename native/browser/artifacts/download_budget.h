// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>

namespace suitecut {
// Byte accounting for diagnostics only: suitecut sets no per-file or total
// download limit. Chromium and the file system enforce their own.
class DownloadQuota {
 public:
  bool Reserve(std::uint64_t bytes) {
    auto used = used_.load();
    do {
      if (used > std::numeric_limits<std::uint64_t>::max() - bytes) return false;
    } while (!used_.compare_exchange_weak(used, used + bytes));
    return true;
  }
  void Release(std::uint64_t bytes) { used_.fetch_sub(bytes); }
  std::uint64_t Used() const { return used_.load(); }

 private:
  std::atomic<std::uint64_t> used_{0};
};
class DownloadQuotaLease {
 public:
  explicit DownloadQuotaLease(std::shared_ptr<DownloadQuota> quota) : quota_(std::move(quota)) {}
  ~DownloadQuotaLease() { quota_->Release(bytes_.load()); }
  bool Add(std::uint64_t bytes) {
    auto current = bytes_.load();
    do {
      if (current > std::numeric_limits<std::uint64_t>::max() - bytes) return false;
    } while (!bytes_.compare_exchange_weak(current, current + bytes));
    if (!quota_->Reserve(bytes)) {
      bytes_.fetch_sub(bytes);
      return false;
    }
    return true;
  }
  bool ReduceTo(std::uint64_t bytes) {
    const auto before = bytes_.load();
    if (bytes > before) return false;
    bytes_ = bytes;
    if (before > bytes) quota_->Release(before - bytes);
    return true;
  }
  std::uint64_t Bytes() const { return bytes_.load(); }

 private:
  std::shared_ptr<DownloadQuota> quota_;
  std::atomic<std::uint64_t> bytes_{0};
};
}  // namespace suitecut
