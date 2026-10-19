// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

// UI-thread-owned operation identities. A generation prevents delayed callbacks
// from completing a new operation after its client ID has been reused.
class SuiteCutOperationRegistry {
 public:
  static constexpr std::size_t kCapacity = 64;
  static constexpr std::size_t kControlCapacity = 64, kTotalCapacity = kCapacity + kControlCapacity;
  struct Entry { std::uint64_t generation; double deadline_ms; bool control = false; };
  std::optional<Entry> Begin(int id, double now_ms, double timeout_ms) {
    return BeginOwned(id,now_ms,timeout_ms,false);
  }
  std::optional<Entry> BeginControl(int id, double now_ms, double timeout_ms) {
    return BeginOwned(id,now_ms,timeout_ms,true);
  }
  bool Contains(int id) const { return operations_.contains(id); }
  std::optional<Entry> Get(int id) const {
    const auto found = operations_.find(id);
    return found == operations_.end() ? std::nullopt : std::optional<Entry>(found->second);
  }
  bool IsCurrent(int id, std::uint64_t generation) const {
    const auto found = operations_.find(id);
    return found != operations_.end() && found->second.generation == generation;
  }
  bool IsExpired(int id, std::uint64_t generation, double now_ms) const {
    const auto found = operations_.find(id);
    return found != operations_.end() && found->second.generation == generation &&
           found->second.deadline_ms != 0 && now_ms >= found->second.deadline_ms;
  }
  // A native prepared lease transfers its absolute clock to a newly admitted
  // commit operation. An existing finite clock may only become stricter.
  bool BindOriginalDeadline(int id,std::uint64_t generation,double deadline_ms) {
    const auto found=operations_.find(id);
    if(found==operations_.end()||found->second.generation!=generation||!std::isfinite(deadline_ms)||deadline_ms<0||
       (found->second.deadline_ms!=0&&(deadline_ms==0||deadline_ms>found->second.deadline_ms)))return false;
    found->second.deadline_ms=deadline_ms;return true;
  }
  bool Finish(int id) {
    const auto found = operations_.find(id); if(found == operations_.end()) return false;
    if(found->second.control) --controls_; else --ordinary_;
    operations_.erase(found); return true;
  }
  std::vector<int> Expired(double now_ms) const {
    std::vector<int> ids;
    for (const auto& [id, entry] : operations_)
      if (entry.deadline_ms != 0 && now_ms >= entry.deadline_ms) ids.push_back(id);
    return ids;
  }
  std::vector<int> Ids() const {
    std::vector<int> ids; ids.reserve(operations_.size());
    for (const auto& [id, entry] : operations_) ids.push_back(id);
    return ids;
  }
  void Clear() { operations_.clear(); ordinary_ = controls_ = 0; }
  std::size_t Controls() const { return controls_; }
  std::size_t Size() const { return operations_.size(); }
 private:
  std::optional<Entry> BeginOwned(int id, double now_ms, double timeout_ms, bool control) {
    if(!std::isfinite(now_ms) || now_ms < 0 || !std::isfinite(timeout_ms) || timeout_ms < 0 ||
       (timeout_ms != 0 && !std::isfinite(now_ms + timeout_ms)) ||
       id < 1 || operations_.contains(id) || operations_.size() >= kTotalCapacity ||
       (control ? controls_ >= kControlCapacity : ordinary_ >= kCapacity)) return std::nullopt;
    const Entry entry{++generation_,timeout_ms == 0 ? 0 : now_ms + timeout_ms,control};
    operations_.emplace(id,entry); if(control) ++controls_; else ++ordinary_; return entry;
  }
  std::size_t ordinary_ = 0, controls_ = 0;
  std::uint64_t generation_ = 0;
  std::map<int, Entry> operations_;
};
