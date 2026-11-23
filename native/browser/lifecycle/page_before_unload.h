// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace suitecut {
// One reversible close attempt per actual page. Only genuine CEF DoClose
// approval transfers its original policy to irreversible page cleanup.
class PageBeforeUnload {
 public:
  struct Approval { std::string reason; double deadline=0; };
  uint64_t Begin(const std::string&reason,double deadline,double now){
    if(!std::isfinite(now)||!std::isfinite(deadline)||deadline<0||(deadline&&now>=deadline))return 0;
    if(pending_)return pending_;
    if(sequence_==std::numeric_limits<uint64_t>::max())return 0;
    reason_=reason;deadline_=deadline;pending_=++sequence_;return pending_;
  }
  uint64_t Pending()const noexcept{return pending_;}
  void Cancel(uint64_t token){if(token&&token==pending_)Force();}
  void Force(){pending_=0;reason_.clear();deadline_=0;}
  std::optional<Approval> Approve(){
    if(!pending_)return std::nullopt;
    Approval result{std::move(reason_),deadline_};pending_=0;deadline_=0;return result;
  }
 private:
  uint64_t sequence_=0,pending_=0;std::string reason_;double deadline_=0;
};
}
