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
  // Playwright's addInitScript has no size or count limit. Every frame gets the
  // whole list in one Chromium IPC message, and Chromium refuses a message over
  // 128 MiB, so the list stays at half that to leave room for JSON escaping.
  static constexpr size_t kMaxScripts = 1u << 20, kMaxBytes = 64u << 20, kMaxScriptBytes = kMaxBytes;
  bool Add(std::string source, std::string& error) {
    if (source.empty() || source.find('\0') != std::string::npos) {
      error = source.empty() ? "addInitScript: the script is empty" : "addInitScript: the script contains a NUL character"; return false;
    }
    if (scripts_.size() >= kMaxScripts || source.size() > kMaxBytes - bytes_) {
      error = scripts_.size() >= kMaxScripts ? "addInitScript: this context already has 1048576 init scripts"
            : "addInitScript: this context's init scripts would total " + std::to_string((bytes_ + source.size()) >> 20) +
              " MiB; they must stay under 64 MiB so their JSON fits Chromium's 128 MiB IPC message";
      return false;
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
