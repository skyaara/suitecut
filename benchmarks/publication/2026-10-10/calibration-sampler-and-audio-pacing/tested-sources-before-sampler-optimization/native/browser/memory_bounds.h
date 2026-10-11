// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace suitecut {
inline int NextCommandID(int previous) {
  return previous<1 || previous==std::numeric_limits<int>::max() ? 1 : previous+1;
}
// Keep sample offsets representable before any float-to-integer conversion.
inline bool MediaTimestamp(double at) { return std::isfinite(at) && std::abs(at) <= 1e12; }
inline bool CopyAudioChunk(std::span<const int16_t> source, double source_at,
                           std::span<int16_t> destination, std::span<uint8_t> covered,
                           double destination_at) {
  if (!MediaTimestamp(source_at) || !MediaTimestamp(destination_at) || source.size()%2 ||
      destination.size()%2 || covered.size()!=destination.size()/2 || source.size()>960000) return false;
  const int64_t offset = std::llround((source_at-destination_at)*48);
  const int64_t first = std::max<int64_t>(0,offset);
  const int64_t last = std::min<int64_t>(covered.size(),offset+static_cast<int64_t>(source.size()/2));
  if (last>first) {
    std::copy_n(source.data()+(first-offset)*2,(last-first)*2,destination.data()+first*2);
    std::fill(covered.begin()+first,covered.begin()+last,1);
  }
  return true;
}
inline bool CompositeBGRA(std::span<uint8_t> view, int width, int height,
                          std::span<const uint8_t> popup, int popup_width, int popup_height,
                          double x, double y) {
  auto valid=[](int w,int h,size_t bytes) {
    return w>0 && h>0 && w<=3840 && h<=2160 && bytes==static_cast<size_t>(w)*h*4;
  };
  if (!valid(width,height,view.size()) || !valid(popup_width,popup_height,popup.size()) ||
      !std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1e10 || std::abs(y)>1e10) return false;
  const int64_t left=std::llround(x), top=std::llround(y);
  const int64_t start=std::max<int64_t>(0,-left), end=std::min<int64_t>(popup_width,width-left);
  if(end<=start) return true;
  for(int64_t row=0;row<popup_height;++row) {
    if(top+row<0 || top+row>=height) continue;
    std::memcpy(view.data()+((top+row)*width+left+start)*4,
                popup.data()+(row*popup_width+start)*4,static_cast<size_t>(end-start)*4);
  }
  return true;
}
}
