// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace suitecut {
// One exact native request/channel. No callbacks or retained payloads. The
// adapter must establish native observation coverage before Begin and pin the
// actual owner/session/document; mirrored sessions must never enter this sum.
class NativeHARResponseBytes {
 public:
  static constexpr uint64_t kBytes=16777216;
  static constexpr size_t kChunks=4096;
  void Begin(bool covered){if(attempted_){invalid_=true;return;}attempted_=true;begun_=covered;invalid_=!covered;}
  void Invalidate(){invalid_=true;}
  void CoverageLost(){if(!terminal_)invalid_=true;}
  void Response(bool uncached){if(!begun_||response_||terminal_||!uncached)invalid_=true;response_=true;}
  void Data(double decoded,double encoded){
    if(!begun_||invalid_)return;
    if(!response_||terminal_||chunks_>=kChunks||!Count(decoded)||!Count(encoded)){invalid_=true;return;}
    const auto plain=static_cast<uint64_t>(decoded),wire=static_cast<uint64_t>(encoded);
    if(plain>kBytes-decoded_||wire>kBytes-encoded_){invalid_=true;return;}
    ++chunks_;decoded_+=plain;encoded_+=wire;
  }
  void Terminal(bool success){if(!begun_||!response_||terminal_||!success)invalid_=true;terminal_=true;}
  std::optional<uint64_t> Encoded(size_t actual_body_bytes)const{
    if(!begun_||invalid_||!response_||!terminal_||!chunks_||!encoded_||decoded_!=actual_body_bytes)return std::nullopt;
    return encoded_;
  }
 private:
  static bool Count(double value){return std::isfinite(value)&&value>=0&&value<=kBytes&&std::floor(value)==value;}
  bool attempted_=false,begun_=false,invalid_=true,response_=false,terminal_=false;
  size_t chunks_=0;uint64_t decoded_=0,encoded_=0;
};
}
