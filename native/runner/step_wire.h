// SPDX-License-Identifier: MIT
#pragma once
#include "runtime_skip.h"
namespace suitecut::runner {
// Compact UTF16 scalar wire, bounded to four ASCII bytes per original unit.
// A checked native JSON scalar is decoded without normalizing isolated units.
inline std::string StepWire(const std::string& json){
  if(!JsonString(json,8192))throw std::invalid_argument("Invalid native step wire scalar");
  static constexpr char hex[]="0123456789abcdef";std::string out;
  const auto unit=[&](uint32_t value){out+=hex[(value>>4)&15];out+=hex[value&15];out+=hex[(value>>12)&15];out+=hex[(value>>8)&15];};
  for(size_t i=1;i+1<json.size();++i){uint32_t code=static_cast<unsigned char>(json[i]);
    if(code=='\\'){const char escaped=json[++i];if(escaped=='u'){code=0;for(unsigned j=0;j<4;++j)code=(code<<4)|static_cast<unsigned>(HexDigit(json[++i]));}else if(escaped=='b')code=8;else if(escaped=='f')code=12;else if(escaped=='n')code=10;else if(escaped=='r')code=13;else if(escaped=='t')code=9;else code=static_cast<unsigned char>(escaped);}
    else if(code>=128){unsigned count;if((code&0xe0)==0xc0){count=1;code&=31;}else if((code&0xf0)==0xe0){count=2;code&=15;}else{count=3;code&=7;}for(unsigned j=0;j<count;++j)code=(code<<6)|(static_cast<unsigned char>(json[++i])&63);}
    if(code>65535){code-=65536;unit(0xd800+(code>>10));unit(0xdc00+(code&1023));}else unit(code);
  }
  return out.empty()?"-":out;
}
} // namespace suitecut::runner
