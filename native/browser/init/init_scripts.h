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
  struct Entry {
    int id;
    std::string source;
  };
  // Playwright's addInitScript has no size or count limit, and neither does this
  // list: Chromium enforces its own IPC message limits.
  bool Add(std::string source, std::string& error) {
    if (source.empty() || source.find('\0') != std::string::npos) {
      error = source.empty() ? "addInitScript: the script is empty"
                             : "addInitScript: the script contains a NUL character";
      return false;
    }
    bytes_ += source.size();
    scripts_.push_back({static_cast<int>(scripts_.size() + 1), std::move(source)});
    return true;
  }
  const std::vector<Entry>& Entries() const { return scripts_; }
  size_t Bytes() const { return bytes_; }
  int Version() const { return static_cast<int>(scripts_.size()); }

 private:
  std::vector<Entry> scripts_;
  size_t bytes_ = 0;
};
