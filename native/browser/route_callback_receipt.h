// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <vector>
namespace suitecut {
// An immutable observer handle to one actual native-issued callback. Only its
// issuing lease book can mutate the receipt. Logical observer cancellation has
// no access to the physical callback entry or its completion transition.
class NativeRouteCallbackReceipt {
 public:
  bool settled()const{return settled_;}
  bool unavailable()const{return unavailable_;}
 private:
  friend class NativeRouteCallbackLeases;
  bool settled_=false,unavailable_=false;
};
using NativeRouteCallbackSnapshot=std::vector<std::shared_ptr<const NativeRouteCallbackReceipt>>;
}
