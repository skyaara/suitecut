// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

// UI-thread-owned pauses. Taking a continuation removes its state before user
// code resumes, so completion and retries can safely reenter the registry.
class SuiteCutActionPauseRegistry {
 public:
  static constexpr std::size_t kCapacity = 64;
  static constexpr std::uint64_t kMaxWireInteger = 9007199254740991ULL;
  using Continuation = std::function<void()>;
  std::optional<std::uint64_t> Pause(int owner, std::uint64_t generation,
      std::string client_token, Continuation continuation) {
    if (owner < 1 || generation == 0 || generation > kMaxWireInteger ||
        client_token.empty() || client_token.size() > 128 || !continuation ||
        entries_.contains(owner) || entries_.size() >= kCapacity ||
        next_token_ >= kMaxWireInteger) return std::nullopt;
    const auto token = ++next_token_;
    entries_.emplace(owner, Entry{generation, token, std::move(client_token), std::move(continuation)});
    return token;
  }
  std::optional<Continuation> Take(int owner, std::uint64_t generation,
      std::uint64_t token, const std::string& client_token) {
    auto found = entries_.find(owner);
    if (found == entries_.end() || found->second.generation != generation ||
        found->second.token != token || found->second.client_token != client_token)
      return std::nullopt;
    auto continuation = std::move(found->second.continuation);
    entries_.erase(found);
    return continuation;
  }
  bool Contains(int owner) const { return entries_.contains(owner); }
  void Cancel(int owner) { entries_.erase(owner); }
  void Clear() { entries_.clear(); }
  std::size_t Size() const { return entries_.size(); }
 private:
  struct Entry {
    std::uint64_t generation, token;
    std::string client_token;
    Continuation continuation;
  };
  std::uint64_t next_token_ = 0;
  std::map<int, Entry> entries_;
};
