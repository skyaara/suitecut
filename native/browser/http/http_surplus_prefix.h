// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
namespace suitecut {
// Only the actual HTTP/1 raw-body observer may enable this parser, after a
// verified Content-Length and absence of transfer encoding. It validates the
// protocol token solely in surplus bytes genuinely received beyond that body;
// it neither guesses unseen bytes nor inspects a content-encoding label.
class NativeHTTP1SurplusPrefix {
 public:
  void Reset(){enabled_=false;failed_=false;body_=0;length_=0;prefix_=0;}
  void Configure(size_t length,bool enabled){Reset();length_=length;enabled_=enabled;}
  bool Observe(std::span<const unsigned char> bytes){
    if(!enabled_||failed_)return !failed_;
    const auto body=std::min(length_-body_,bytes.size());body_+=body;bytes=bytes.subspan(body);
    constexpr std::string_view token="HTTP/";
    for(const auto byte:bytes){
      if(prefix_==token.size())break;
      if(byte!=static_cast<unsigned char>(token[prefix_])){failed_=true;return false;}
      ++prefix_;
    }
    return true;
  }
  bool Failed()const{return failed_;}
  size_t BodyObserved()const{return body_;}
  size_t PrefixObserved()const{return prefix_;}
 private:
  bool enabled_=false,failed_=false;
  size_t body_=0,length_=0,prefix_=0;
};
} // namespace suitecut
