// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <limits>
// Internal UTF8/WTF8 bookkeeping only: count genuine ECMAScript UTF16 units.
// Surrogate triples are permitted; overlong/invalid scalar byte sequences are not.
inline size_t SuiteCutBindingNameUnits(const std::string& value){
  size_t count=0;
  for(size_t i=0;i<value.size();){
    const auto c=static_cast<unsigned char>(value[i++]);unsigned scalar=c;int rest=0;
    if(c>=0xc2&&c<=0xdf){scalar=c&31;rest=1;}
    else if(c>=0xe0&&c<=0xef){scalar=c&15;rest=2;}
    else if(c>=0xf0&&c<=0xf4){scalar=c&7;rest=3;}
    else if(c>=0x80)return std::numeric_limits<size_t>::max();
    if(i+rest>value.size())return std::numeric_limits<size_t>::max();
    for(int j=0;j<rest;++j){const auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80)return std::numeric_limits<size_t>::max();scalar=(scalar<<6)|(next&63);}
    if((rest==1&&scalar<128)||(rest==2&&scalar<2048)||(rest==3&&(scalar<65536||scalar>0x10ffff)))return std::numeric_limits<size_t>::max();
    count+=scalar>65535?2:1;
  }
  return count;
}
inline std::string SuiteCutBindingNameDiagnostic(const std::string& value){
  constexpr char hex[]="0123456789abcdef";std::string out;
  for(size_t i=0;i<value.size();){const auto c=static_cast<unsigned char>(value[i]);
    if(i+2<value.size()&&c==0xed&&(static_cast<unsigned char>(value[i+1])&0xe0)==0xa0){const unsigned unit=((c&15)<<12)|((static_cast<unsigned char>(value[i+1])&63)<<6)|(static_cast<unsigned char>(value[i+2])&63);out+="\\u";for(int shift=12;shift>=0;shift-=4)out+=hex[(unit>>shift)&15];i+=3;}
    else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];++i;}
    else out+=value[i++];
  }return out;
}
