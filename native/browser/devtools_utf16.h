// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "include/cef_parser.h"
#include "storage_utf16.h"
namespace suitecut {
// Private Inspector transport representation. Only native protocol fields whose
// genuine type is string may consume this tag. Never reinterpret author objects.
inline constexpr char kDevToolsUTF16Tag[]="__suitecutNativeUTF16Scalar";
inline int DevToolsHex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
inline bool DevToolsStringUnits(std::string_view token,std::u16string& units){
  units.clear();if(token.size()<2||token.front()!='"'||token.back()!='"')return false;
  for(size_t i=1;i+1<token.size();){
    auto c=static_cast<unsigned char>(token[i++]);
    if(c=='\\'){
      if(i>=token.size()-1)return false;
      const char e=token[i++];
      if(e=='u'){if(i+4>token.size()-1)return false;unsigned value=0;for(int j=0;j<4;++j){const int d=DevToolsHex(token[i++]);if(d<0)return false;value=value*16+d;}units+=static_cast<char16_t>(value);}
      else if(e=='"'||e=='\\'||e=='/')units+=e;
      else if(e=='b')units+='\b';else if(e=='f')units+='\f';else if(e=='n')units+='\n';else if(e=='r')units+='\r';else if(e=='t')units+='\t';else return false;
      continue;
    }
    if(c<0x20||c=='"')return false;
    if(c<0x80){units+=c;continue;}
    unsigned value=0;size_t extra=0;unsigned minimum=0;
    if(c>=0xc2&&c<=0xdf){value=c&31;extra=1;minimum=0x80;}
    else if(c>=0xe0&&c<=0xef){value=c&15;extra=2;minimum=0x800;}
    else if(c>=0xf0&&c<=0xf4){value=c&7;extra=3;minimum=0x10000;}else return false;
    if(i+extra>token.size()-1)return false;
    for(size_t n=0;n<extra;++n){const auto next=static_cast<unsigned char>(token[i++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
    if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    if(value<0x10000)units+=static_cast<char16_t>(value);else{value-=0x10000;units+=static_cast<char16_t>(0xd800+(value>>10));units+=static_cast<char16_t>(0xdc00+(value&1023));}
  }return true;
}
inline bool DevToolsHasIsolated(const std::u16string& units){for(size_t i=0;i<units.size();++i){const auto c=units[i];if(c>=0xd800&&c<=0xdbff){if(i+1<units.size()&&units[i+1]>=0xdc00&&units[i+1]<=0xdfff){++i;continue;}return true;}if(c>=0xdc00&&c<=0xdfff)return true;}return false;}
inline std::string DevToolsUnitsHex(const std::u16string& units){constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(units.size()*4);for(const auto c:units)for(int shift=12;shift>=0;shift-=4)out+=hex[(c>>shift)&15];return out;}
inline bool DevToolsReadTag(CefRefPtr<CefValue> value,std::u16string& units){
  auto dict=value&&value->GetType()==VTYPE_DICTIONARY?value->GetDictionary():nullptr;
  if(!dict||dict->GetSize()!=1||dict->GetType(kDevToolsUTF16Tag)!=VTYPE_STRING)return false;
  const auto hex=dict->GetString(kDevToolsUTF16Tag).ToString();if(hex.empty()||hex.size()%4||hex.size()>4*1048576)return false;
  units.clear();units.reserve(hex.size()/4);for(size_t i=0;i<hex.size();i+=4){unsigned c=0;for(size_t j=0;j<4;++j){const int digit=DevToolsHex(hex[i+j]);if(digit<0)return false;c=c*16+digit;}units+=static_cast<char16_t>(c);}return DevToolsHasIsolated(units);
}
// Returns only separately validated, lossless CEF-safe JSON. Isolated object
// keys are explicitly unsupported here; Inspector protocol field keys are fixed.
// No replacement scalar or opaque caption token is emitted.
inline bool DevToolsLosslessProjection(const std::string& original,std::string& output,std::string& error){
  if(original.size()>16*1048576){error="Native lossless Inspector response byte limit exceeded";return false;}
  const auto validation=StorageJSONValidationProjection(original);if(!CefParseJSON(validation,JSON_PARSER_RFC)){error="Invalid native Inspector JSON";return false;}
  output.clear();output.reserve(original.size());size_t tagged_units=0,strings=0;
  for(size_t pos=0;pos<original.size();){
    if(original[pos]!='"'){output+=original[pos++];continue;}
    const size_t start=pos++;bool escaped=false;
    while(pos<original.size()){const char c=original[pos++];if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}if(c=='"')break;}
    // Borrow only while original is unchanged in this synchronous call.
    const auto token=std::string_view(original).substr(start,pos-start);std::u16string units;
    if(++strings>100000||!DevToolsStringUnits(token,units)){error="Native lossless Inspector string limit or encoding error";return false;}
    if(!DevToolsHasIsolated(units)){output+=token;continue;}
    size_t next=pos;while(next<original.size()&&(original[next]==' '||original[next]=='\n'||original[next]=='\r'||original[next]=='\t'))++next;
    if(next<original.size()&&original[next]==':'){error="Native lossless Inspector object key is unsupported";return false;}
    if(units.size()>1048576||units.size()>2*1048576-tagged_units){error="Native lossless Inspector scalar unit limit exceeded";return false;}tagged_units+=units.size();
    output+="{\"";output+=kDevToolsUTF16Tag;output+="\":\"";output+=DevToolsUnitsHex(units);output+="\"}";
    if(output.size()>24*1048576){error="Native lossless Inspector projection byte limit exceeded";return false;}
  }
  return true;
}
// WTF-8 is an internal std::string representation only. No CefSetString may
// receive it: setters below retain isolated units as the private typed sidecar.
inline std::string DevToolsUnitsWTF8(const std::u16string& units){
  std::string out;out.reserve(units.size()*3);
  for(size_t i=0;i<units.size();++i){unsigned c=units[i];if(c>=0xd800&&c<=0xdbff&&i+1<units.size()&&units[i+1]>=0xdc00&&units[i+1]<=0xdfff){c=0x10000+((c-0xd800)<<10)+(units[++i]-0xdc00);}
    if(c<0x80)out+=static_cast<char>(c);else if(c<0x800){out+=static_cast<char>(0xc0|(c>>6));out+=static_cast<char>(0x80|(c&63));}else if(c<0x10000){out+=static_cast<char>(0xe0|(c>>12));out+=static_cast<char>(0x80|((c>>6)&63));out+=static_cast<char>(0x80|(c&63));}else{out+=static_cast<char>(0xf0|(c>>18));out+=static_cast<char>(0x80|((c>>12)&63));out+=static_cast<char>(0x80|((c>>6)&63));out+=static_cast<char>(0x80|(c&63));}}
  return out;
}
inline std::string DevToolsString(CefRefPtr<CefValue> value){if(value&&value->GetType()==VTYPE_STRING)return value->GetString().ToString();std::u16string units;return DevToolsReadTag(value,units)?DevToolsUnitsWTF8(units):std::string();}
inline void DevToolsSetString(CefRefPtr<CefDictionaryValue> dict,const std::string& key,const std::string& text){
  // Decode internal WTF-8 using a JSON token with escaped surrogate triples.
  std::string token="\"";constexpr char hex[]="0123456789abcdef";
  for(size_t i=0;i<text.size();){const auto c=static_cast<unsigned char>(text[i]);
    if(i+2<text.size()&&c==0xed&&(static_cast<unsigned char>(text[i+1])&0xe0)==0xa0&&(static_cast<unsigned char>(text[i+2])&0xc0)==0x80){const unsigned u=((c&15)<<12)|((static_cast<unsigned char>(text[i+1])&63)<<6)|(static_cast<unsigned char>(text[i+2])&63);token+="\\u";for(int shift=12;shift>=0;shift-=4)token+=hex[(u>>shift)&15];i+=3;}
    else{if(c=='"'||c=='\\'){token+='\\';token+=text[i];}else if(c<32){token+="\\u00";token+=hex[c>>4];token+=hex[c&15];}else token+=text[i];++i;}}
  token+='"';std::u16string units;
  const bool decoded=DevToolsStringUnits(token,units);
  if(decoded&&DevToolsHasIsolated(units)){auto value=CefDictionaryValue::Create();value->SetString(kDevToolsUTF16Tag,DevToolsUnitsHex(units));dict->SetDictionary(key,value);}
  else dict->SetString(key,decoded?DevToolsUnitsWTF8(units):text);
}
inline std::string DevToolsQuoteUnits(const std::u16string& units){constexpr char hex[]="0123456789abcdef";std::string out="\"";for(const auto c:units){out+="\\u";for(int shift=12;shift>=0;shift-=4)out+=hex[(c>>shift)&15];}return out+'"';}
// Keep a complete internal UTF8/WTF8 prefix, including at truncated boundaries.
inline std::string DevToolsTruncateWTF8(const std::string& input,size_t limit){
  size_t end=0;
  while(end<input.size()&&end<limit){const auto c=static_cast<unsigned char>(input[end]);const size_t width=c<0x80?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;if(!width||width>input.size()-end||width>limit-end)break;bool valid=true;for(size_t i=1;i<width;++i)if((static_cast<unsigned char>(input[end+i])&0xc0)!=0x80)valid=false;if(!valid)break;end+=width;}
  return input.substr(0,end);
}
// Only call for native-produced typed metadata, never arbitrary author objects.
inline bool DevToolsLosslessJSON(CefRefPtr<CefValue> value,std::string& out,size_t limit=65536,size_t depth=0){
  if(!value||depth>64||out.size()>limit)return false;
  std::u16string units;
  if(DevToolsReadTag(value,units))out+=DevToolsQuoteUnits(units);
  else if(value->GetType()==VTYPE_DICTIONARY){auto dict=value->GetDictionary();CefDictionaryValue::KeyList keys;dict->GetKeys(keys);out+='{';bool first=true;for(const auto& key:keys){if(!first)out+=',';first=false;auto name=CefValue::Create();name->SetString(key);out+=CefWriteJSON(name,JSON_WRITER_DEFAULT).ToString();out+=':';if(!DevToolsLosslessJSON(dict->GetValue(key),out,limit,depth+1))return false;}out+='}';}
  else if(value->GetType()==VTYPE_LIST){auto list=value->GetList();out+='[';for(size_t i=0;i<list->GetSize();++i){if(i)out+=',';if(!DevToolsLosslessJSON(list->GetValue(i),out,limit,depth+1))return false;}out+=']';}
  else out+=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();
  return out.size()<=limit;
}
}
