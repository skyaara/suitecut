// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include "include/cef_values.h"
namespace suitecut {
inline bool ElementBoundsReadQuad(CefRefPtr<CefListValue> value,std::array<double,8>& quad){
  if(!value||value->GetSize()!=quad.size())return false;
  std::array<double,8> parsed{};
  for(std::size_t i=0;i<parsed.size();++i){
    if(value->GetType(i)!=VTYPE_INT&&value->GetType(i)!=VTYPE_DOUBLE)return false;
    parsed[i]=value->GetType(i)==VTYPE_INT?value->GetInt(i):value->GetDouble(i);
    if(!std::isfinite(parsed[i]))return false;
  }
  quad=parsed;return true;
}
}
