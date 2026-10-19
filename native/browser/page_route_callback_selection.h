// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "route_callback_receipt.h"
namespace suitecut {
// The actual native scope/token pair is opaque to the language SDK. It is
// returned only for callbacks already admitted by this exact Page owner.
struct NativePageRouteCallbackSelection {
  uint64_t scope=0;
  std::vector<uint64_t>tokens;
  NativeRouteCallbackSnapshot receipts;
  std::size_t bytes()const{return tokens.capacity()*sizeof(uint64_t)+receipts.capacity()*sizeof(NativeRouteCallbackSnapshot::value_type);}
};
}  // namespace suitecut
