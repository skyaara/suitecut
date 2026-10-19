// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

// Private command lines include their newline in the 64 KiB bound. A terminal
// error cannot be reset by later bytes. Delivery may reject a line under load.
class SuiteCutCommandFramer {
 public:
  static constexpr std::size_t kMaxLineBytes = 65536;
  template <typename Deliver>
  bool Push(std::string_view bytes, Deliver deliver) {
    if (failed_) return false;
    while (!bytes.empty()) {
      // Append a whole transport span at once. The two searches use bounded
      // byte scans; no per-byte string growth or locale/UTF-8 interpretation.
      const auto newline = bytes.find('\n');
      const auto nul = bytes.substr(0, newline).find('\0');
      const auto end = std::min(newline, nul);
      const auto length = end == std::string_view::npos ? bytes.size() : end;
      const auto available = kMaxLineBytes - 1 - pending_.size();
      pending_.append(bytes.data(), std::min(length, available));
      if (length > available || (end != std::string_view::npos && nul == end)) {
        failed_ = true;
        return false;
      }
      if (end == std::string_view::npos) break;
      if (!deliver(std::move(pending_))) { failed_ = true; return false; }
      pending_.clear();
      bytes.remove_prefix(end + 1);
    }
    return true;
  }
  bool End() const { return !failed_ && pending_.empty(); }
  std::size_t BufferedBytes() const { return pending_.size(); }
 private:
  std::string pending_;
  bool failed_ = false;
};
