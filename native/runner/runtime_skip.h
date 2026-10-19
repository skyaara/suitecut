// SPDX-License-Identifier: MIT
#pragma once
#include "json_text.h"
namespace suitecut::runner {
// Exact UTF16LE code-unit transport keeps the maximum admitted reason within
// the existing64KiB control packet budget, including isolated units and NUL.
inline std::string RuntimeSkipReason(std::string_view hex) {
  if(hex=="-")return "\"\"";
  if(hex.empty()||hex.size()%4||hex.size()>8192*4)
    throw std::invalid_argument("Invalid native runtime skip reason size");
  std::string json="\"";json.reserve(hex.size()/4*6+2);
  for(size_t at=0;at<hex.size();at+=4){
    for(size_t i=0;i<4;++i)if(HexDigit(hex[at+i])<0)
      throw std::invalid_argument("Invalid native runtime skip reason digit");
    json+="\\u";json+=hex.substr(at+2,2);json+=hex.substr(at,2);
  }
  json+='"';return json;
}
}
