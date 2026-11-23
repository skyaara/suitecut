// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <climits>
#include <cmath>
#include <optional>
#include <stdexcept>
namespace suitecut {
struct NativeContextVideoSize{int width=0,height=0;bool operator==(const NativeContextVideoSize&)const=default;};
inline NativeContextVideoSize ContextVideoCanvas(int actual_css_width,int actual_css_height,std::optional<NativeContextVideoSize>requested){
  // No suitecut size cap: the encoder enforces its own. A BGRA row must still fit the int stride.
  const auto valid=[](NativeContextVideoSize value){return value.width>=1&&value.height>=1&&value.width<=INT_MAX/4&&value.height<=INT_MAX/4;};
  if(requested){if(!valid(*requested))throw std::runtime_error("Native recordVideo.size requires positive integer width and height");return *requested;}
  if(actual_css_width<1||actual_css_height<1)throw std::runtime_error("Native video default size lacks actual CSS viewport dimensions");
  const double scale=std::min({1.,800./actual_css_width,800./actual_css_height});
  return {std::max(1,static_cast<int>(std::floor(actual_css_width*scale))),std::max(1,static_cast<int>(std::floor(actual_css_height*scale)))};
}
}
