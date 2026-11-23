// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <string>
#include <utility>
namespace suitecut {
// UI-thread native origin accounting. Every observed (origin, top) pair is
// retained until its context closes; there is no suitecut count cap.
template<class Value>class NativePermissionScopes {
 public:
  using Scope=std::pair<std::string,std::string>;
  bool Observe(const std::string&origin,const std::string&top){
    const Scope self{origin,origin},embedded{origin,top.empty()?origin:top};
    entries_.try_emplace(self,Value{});entries_.try_emplace(embedded,Value{});return true;
  }
  auto& entries(){return entries_;}
  const auto& entries()const{return entries_;}
 private:
  std::map<Scope,Value> entries_;
};
}
