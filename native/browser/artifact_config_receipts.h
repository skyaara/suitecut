// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
namespace suitecut {
// The real Source DevTools record retains this callback through logical
// cancellation. Its actual ACK/close retires the configuration lease. A final
// physical capture callback still runs after cancellation solely to release
// the existing controller latch; ordinary stale continuations never run.
inline std::function<void(bool,const std::string&)> RetainArtifactConfiguration(
 std::shared_ptr<void> lease,std::function<bool()> current,
 std::function<bool()> expired,std::function<void()> deadline,
 std::function<void(bool,const std::string&)> original,bool physical){
 return [lease=std::move(lease),current=std::move(current),expired=std::move(expired),deadline=std::move(deadline),original=std::move(original),physical](bool ok,const std::string&json){
  (void)lease;
  if(current()){
   if(!expired()){original(ok,json);return;}
   deadline();
  }
  if(physical)original(ok,json);
 };
}
} // namespace suitecut
