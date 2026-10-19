// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <limits>
#include <optional>

namespace suitecut {
// One actual native UI actor orders policy admission. Target installation,
// Inspector event sequence and asynchronous file completion are independent.
// Only this actor can mint a nonzero token; SDK values never enter this type.
class NativeRouteRegistrationOrder {
 public:
  NativeRouteRegistrationOrder()=default;
  static std::optional<NativeRouteRegistrationOrder> Reserve(){
    const auto next=Successor(serial_);if(!next)return std::nullopt;
    serial_=*next;return NativeRouteRegistrationOrder(*next);
  }
  static std::optional<uint64_t> Successor(uint64_t value){
    if(value==std::numeric_limits<uint64_t>::max())return std::nullopt;
    return value+1;
  }
  explicit operator bool()const{return value_!=0;}
  bool operator==(const NativeRouteRegistrationOrder&)const=default;
  bool operator<(const NativeRouteRegistrationOrder&other)const{return value_<other.value_;}
 private:
  explicit NativeRouteRegistrationOrder(uint64_t value):value_(value){}
  inline static uint64_t serial_=0;
  uint64_t value_=0;
};
}
