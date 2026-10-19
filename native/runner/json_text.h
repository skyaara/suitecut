// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
namespace suitecut::runner {
inline int HexDigit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return-1;}
inline std::string DecodeHex(std::string_view source,size_t limit){
 if(source=="-")return{};
 if(source.size()%2||source.size()/2>limit)throw std::invalid_argument("Invalid native host hex size");
 std::string decoded;decoded.reserve(source.size()/2);
 for(size_t i=0;i<source.size();i+=2){const int a=HexDigit(source[i]),b=HexDigit(source[i+1]);if(a<0||b<0)throw std::invalid_argument("Invalid native host hex digit");decoded.push_back(static_cast<char>((a<<4)|b));}return decoded;
}
// A checked JSON string is retained as an actual serialized scalar. It can be
// emitted in the native report without losing escaped isolated UTF16 units.
inline bool JsonString(std::string_view text,size_t unit_limit){
 if(text.size()<2||text.front()!='"'||text.back()!='"')return false;
 size_t units=0;
 for(size_t i=1;i+1<text.size();++i){const auto c=static_cast<unsigned char>(text[i]);
  if(c<32||c=='"')return false;
  if(c=='\\'){
   if(++i+1>=text.size())return false;
   const char escape=text[i];
   if(escape=='u'){if(i+4>=text.size()-1)return false;for(size_t j=1;j<=4;++j)if(HexDigit(text[i+j])<0)return false;i+=4;}
   else if(escape!='"'&&escape!='\\'&&escape!='/'&&escape!='b'&&escape!='f'&&escape!='n'&&escape!='r'&&escape!='t')return false;
   ++units;
  }else if(c<128)++units;
  else{
   unsigned continuation=0;uint32_t code=0,minimum=0;
   if((c&0xe0)==0xc0){continuation=1;code=c&31;minimum=128;}
   else if((c&0xf0)==0xe0){continuation=2;code=c&15;minimum=2048;}
   else if((c&0xf8)==0xf0){continuation=3;code=c&7;minimum=65536;}
   else return false;
   if(i+continuation>=text.size()-1)return false;
   for(unsigned j=0;j<continuation;++j){const auto tail=static_cast<unsigned char>(text[++i]);if((tail&0xc0)!=0x80)return false;code=(code<<6)|(tail&63);}
   if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
   units+=code>65535?2:1;
  }
  if(units>unit_limit)return false;
 }
 return true;
}
}
