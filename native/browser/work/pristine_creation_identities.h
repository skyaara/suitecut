// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <utility>
namespace suitecut {
// Records actual Runtime default-context identities, independently of logical
// contextsCleared. A Runtime re-enable snapshot is not a fresh author realm.
class PristineCreationIdentities {
 public:
  enum class Observation { Fresh, Known, Invalid };
  Observation Observe(uint64_t session_epoch, const std::string& identity) {
    if (!session_epoch || identity.empty()) return Observation::Invalid;
    const auto key = std::make_pair(session_epoch, identity);
    if (known_.contains(key)) return Observation::Known;
    known_.insert(key);
    return Observation::Fresh;
  }
  // Only actual context destruction/session death can forget a known realm.
  void Destroyed(uint64_t epoch, const std::string& identity) { known_.erase({epoch, identity}); }
  void SessionClosed(uint64_t epoch) {
    for (auto it = known_.begin(); it != known_.end();) {
      if (it->first == epoch)
        it = known_.erase(it);
      else
        ++it;
    }
  }
  void Closed() { known_.clear(); }
  size_t Count() const { return known_.size(); }

 private:
  std::set<std::pair<uint64_t, std::string>> known_;
};
}  // namespace suitecut
