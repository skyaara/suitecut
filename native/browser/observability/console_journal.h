// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class SuiteCutConsoleJournal {
 public:
  bool Push(std::uint64_t sequence, std::string json) {
    if (!sequence || json.empty()) return false;
    bytes_ += json.size(); entries_.push_back({sequence, std::move(json)}); return true;
  }
  void Navigation(std::uint64_t sequence) { navigation_ = sequence; }
  std::vector<std::string> Read(bool all) const {
    std::vector<std::string> result;
    for (const auto& entry : entries_) if (all || entry.sequence > navigation_) result.push_back(entry.json);
    return result;
  }
  void Clear() { entries_.clear(); bytes_ = 0; }
  std::size_t Size() const { return entries_.size(); }
  std::size_t Bytes() const { return bytes_; }
  std::uint64_t Oldest() const { return entries_.empty() ? 0 : entries_.front().sequence; }
 private:
  struct Entry { std::uint64_t sequence; std::string json; };
  std::deque<Entry> entries_;
  std::uint64_t navigation_ = 0;
  std::size_t bytes_ = 0;
};
