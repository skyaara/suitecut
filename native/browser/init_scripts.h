// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Own browser-context source text independently of renderer/document lifetimes.
class SuiteCutInitScripts {
 public:
  struct Entry { int id; std::string source; };
  static constexpr size_t kMaxScripts = 128, kMaxBytes = 1048576, kMaxScriptBytes = 65536;
  bool Add(std::string source, std::string& error) {
    if (source.empty() || source.size() > kMaxScriptBytes || source.find('\0') != std::string::npos) {
      error = "Native init script must contain 1 through 65536 bytes without nulls"; return false;
    }
    if (scripts_.size() >= kMaxScripts || source.size() > kMaxBytes - bytes_) {
      error = "Native context init script capacity exceeded"; return false;
    }
    bytes_ += source.size(); scripts_.push_back({static_cast<int>(scripts_.size() + 1), std::move(source)}); return true;
  }
  const std::vector<Entry>& Entries() const { return scripts_; }
  size_t Bytes() const { return bytes_; }
  int Version() const { return static_cast<int>(scripts_.size()); }
 private:
  std::vector<Entry> scripts_;
  size_t bytes_ = 0;
};
