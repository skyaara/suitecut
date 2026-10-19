// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace suitecut {
// Logical observers of a particular manager-owned physical Client lease. A
// canceled observer never removes or releases that Client's CEF ownership.
class NativeContextWorkerCloseWaits {
 public:
  using Reply = std::function<void(bool, uint64_t, const std::string&)>;
  static constexpr size_t kWaits = 64;
  bool Admit(int owner, int context, uint64_t token, double deadline,
             double now, Reply reply) {
    if (owner < 1 || context < 1 || !token || !std::isfinite(deadline) ||
        !std::isfinite(now) || deadline < 0 ||
        (deadline != 0 && deadline <= now) || !reply ||
        waits_.contains(owner) || waits_.size() >= kWaits) return false;
    waits_.emplace(owner, Wait{context, token, deadline, std::move(reply)});
    return true;
  }
  void Cancel(int owner) { waits_.erase(owner); }
  void Closed(int context, uint64_t token, uint64_t cursor, double now,
              bool cursor_accepted) {
    std::vector<std::pair<Reply, bool>> replies;
    for (auto it = waits_.begin(); it != waits_.end();) {
      if (it->second.context != context || it->second.token != token) {
        ++it;
        continue;
      }
      replies.emplace_back(std::move(it->second.reply),
                           std::isfinite(now) && (it->second.deadline == 0 || now < it->second.deadline));
      it = waits_.erase(it);
    }
    // Remove every selected wait before invoking anything reentrant. No member
    // access follows callbacks, which may destroy this observer collection.
    for (auto& [reply, current] : replies)
      reply(current && cursor_accepted, cursor,
            !current ? "Native worker owner-close wait timed out"
                     : !cursor_accepted ? "Native worker final physical cursor rejected" : "");
  }
  void Clear() {
    std::vector<Reply> replies;
    for (auto& [owner, wait] : waits_) replies.push_back(std::move(wait.reply));
    waits_.clear();
    for (auto& reply : replies)
      reply(false, 0, "Native worker owner-close observer disposed");
  }
  size_t size() const { return waits_.size(); }
 private:
  struct Wait { int context; uint64_t token; double deadline; Reply reply; };
  std::map<int, Wait> waits_;
};
}  // namespace suitecut
