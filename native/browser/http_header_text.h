// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace suitecut {
// Public raw HTTP header strings preserve every received byte as Latin-1.
inline std::string HTTPHeaderText(const std::string& bytes){
  std::string utf8;utf8.reserve(bytes.size());for(const unsigned char byte:bytes){if(byte<128)utf8+=static_cast<char>(byte);else{utf8+=static_cast<char>(0xc0|(byte>>6));utf8+=static_cast<char>(0x80|(byte&0x3f));}}return utf8;
}
inline bool HTTPValidUTF8(std::string_view bytes){
  for(size_t cursor=0;cursor<bytes.size();){
    const auto first=static_cast<unsigned char>(bytes[cursor++]);if(first<128)continue;
    unsigned scalar=0,minimum=0;size_t count=0;
    if(first>=0xc2&&first<=0xdf){scalar=first&31;minimum=0x80;count=1;}
    else if(first>=0xe0&&first<=0xef){scalar=first&15;minimum=0x800;count=2;}
    else if(first>=0xf0&&first<=0xf4){scalar=first&7;minimum=0x10000;count=3;}else return false;
    if(count>bytes.size()-cursor)return false;
    while(count--){const auto next=static_cast<unsigned char>(bytes[cursor++]);if((next&0xc0)!=0x80)return false;scalar=(scalar<<6)|(next&63);}
    if(scalar<minimum||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff))return false;
  }return true;
}
// An outgoing public header is a byte field. Map Latin-1 code units to their
// actual wire octets and reject controls or scalars outside that byte range.
// This is deliberately distinct from UTF-8 URL and body encoding.
inline bool HTTPEncodeHeaderValue(std::u16string_view text,std::string&bytes){
  bytes.clear();if(text.size()>65536)return false;bytes.reserve(text.size());
  for(const char16_t unit:text){if((unit<32&&unit!=9)||unit==127||unit>255){bytes.clear();return false;}bytes+=static_cast<char>(unit);}
  return true;
}
// Only the protocol-defined Location URL gets IRI decoding. Other authored
// header values retain their byte projection. Invalid UTF-8 remains lossless
// Latin-1 and never acquires replacement characters.
inline std::string HTTPRedirectLocation(const std::string& bytes){return HTTPValidUTF8(bytes)?bytes:HTTPHeaderText(bytes);}
} // namespace suitecut
