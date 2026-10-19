// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <utility>
namespace suitecut {
// UI-thread native origin accounting. An exhausted scope cannot admit even an
// existing pair again: its caller must keep navigation canceled through close.
template<class Value>class NativePermissionScopes {
 public:
  using Scope=std::pair<std::string,std::string>;
  explicit NativePermissionScopes(size_t capacity=4096):capacity_(capacity){}
  bool Observe(const std::string&origin,const std::string&top){
    if(failed_)return false;
    const Scope self{origin,origin},embedded{origin,top.empty()?origin:top};
    size_t extra=entries_.contains(self)?0:1;if(embedded!=self&&!entries_.contains(embedded))++extra;
    if(extra>capacity_-entries_.size()){failed_=true;return false;}
    entries_.try_emplace(self,Value{});entries_.try_emplace(embedded,Value{});return true;
  }
  auto& entries(){return entries_;}
  const auto& entries()const{return entries_;}
  bool failed()const{return failed_;}
 private:
  size_t capacity_;bool failed_=false;std::map<Scope,Value> entries_;
};
}
