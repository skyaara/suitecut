// SPDX-License-Identifier: MIT
#pragma once
#include "json_text.h"
#include <charconv>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace suitecut::runner::merge_detail {
// Scalars retain their original spelling, including isolated escaped UTF16.
// Decoding is only used for field names, identifiers, and filesystem paths.
struct JSON {
  enum Kind { Object, Array, String, Number, Boolean, Null } kind=Null;
  std::string_view raw;
  std::map<std::u16string,JSON> fields;
  std::vector<JSON> items;
};
[[noreturn]] inline void Invalid(std::string_view reason){throw std::invalid_argument("Native report merge: "+std::string(reason));}
inline std::u16string Units(std::string_view raw){
  std::u16string out;
  for(size_t at=1;at+1<raw.size();++at){
    uint32_t code=static_cast<unsigned char>(raw[at]);
    if(code=='\\'){
      const char escape=raw[++at];
      if(escape=='u'){code=0;for(unsigned j=0;j<4;++j)code=(code<<4)|static_cast<unsigned>(HexDigit(raw[++at]));}
      else if(escape=='b')code=8;else if(escape=='f')code=12;else if(escape=='n')code=10;else if(escape=='r')code=13;else if(escape=='t')code=9;else code=static_cast<unsigned char>(escape);
    }else if(code>=128){
      unsigned count=0;if((code&0xe0)==0xc0){count=1;code&=31;}else if((code&0xf0)==0xe0){count=2;code&=15;}else{count=3;code&=7;}
      for(unsigned j=0;j<count;++j)code=(code<<6)|(static_cast<unsigned char>(raw[++at])&63);
    }
    if(code>65535){code-=65536;out+=static_cast<char16_t>(0xd800+(code>>10));out+=static_cast<char16_t>(0xdc00+(code&1023));}else out+=static_cast<char16_t>(code);
  }
  return out;
}
inline std::string Text(const JSON& value){
  if(value.kind!=JSON::String)Invalid("expected string");
  std::string out;const auto units=Units(value.raw);
  for(size_t i=0;i<units.size();++i){uint32_t code=units[i];
    if(code>=0xd800&&code<=0xdbff){if(i+1==units.size()||units[i+1]<0xdc00||units[i+1]>0xdfff)Invalid("non-scalar identity or filesystem string");code=65536+((code-0xd800)<<10)+(units[++i]-0xdc00);}
    else if(code>=0xdc00&&code<=0xdfff)Invalid("non-scalar identity or filesystem string");
    if(code<128)out+=static_cast<char>(code);
    else if(code<2048){out+=static_cast<char>(0xc0|(code>>6));out+=static_cast<char>(0x80|(code&63));}
    else if(code<65536){out+=static_cast<char>(0xe0|(code>>12));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
    else{out+=static_cast<char>(0xf0|(code>>18));out+=static_cast<char>(0x80|((code>>12)&63));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
  }return out;
}
class JSONParser {
 public:
  explicit JSONParser(std::string_view text):text_(text){}
  JSON Parse(){auto value=Value(0);Space();if(at_!=text_.size())Invalid("trailing JSON bytes");return value;}
 private:
  std::string_view text_;size_t at_=0,nodes_=0;
  void Space(){while(at_<text_.size()&&(text_[at_]==' '||text_[at_]=='\t'||text_[at_]=='\r'||text_[at_]=='\n'))++at_;}
  bool Take(char value){if(at_<text_.size()&&text_[at_]==value){++at_;return true;}return false;}
  void Need(char value){if(!Take(value))Invalid("invalid JSON punctuation");}
  std::string_view String(){const size_t begin=at_;Need('"');bool escaped=false;
    while(at_<text_.size()){const char value=text_[at_++];if(value=='"'&&!escaped){const auto raw=text_.substr(begin,at_-begin);if(!JsonString(raw,128*1024*1024))Invalid("invalid JSON string/UTF8");return raw;}if(value=='\\'&&!escaped)escaped=true;else escaped=false;}
    Invalid("unterminated JSON string");
  }
  void Literal(std::string_view value){if(text_.substr(at_,value.size())!=value)Invalid("invalid JSON literal");at_+=value.size();}
  void Digits(){const auto begin=at_;while(at_<text_.size()&&text_[at_]>='0'&&text_[at_]<='9')++at_;if(begin==at_)Invalid("invalid JSON number");}
  JSON Value(unsigned depth){
    Space();if(depth>64||++nodes_>500000||at_==text_.size())Invalid("JSON depth/node capacity or truncated value");
    const size_t begin=at_;JSON value;
    if(Take('{')){value.kind=JSON::Object;Space();if(!Take('}'))for(;;){Space();const auto key=Units(String());Space();Need(':');auto child=Value(depth+1);if(!value.fields.emplace(key,std::move(child)).second)Invalid("duplicate JSON object key");Space();if(Take('}'))break;Need(',');}}
    else if(Take('[')){value.kind=JSON::Array;Space();if(!Take(']'))for(;;){value.items.push_back(Value(depth+1));Space();if(Take(']'))break;Need(',');}}
    else if(text_[at_]=='"'){value.kind=JSON::String;String();}
    else if(text_[at_]=='t'||text_[at_]=='f'){value.kind=JSON::Boolean;Literal(text_[at_]=='t'?"true":"false");}
    else if(text_[at_]=='n'){value.kind=JSON::Null;Literal("null");}
    else{value.kind=JSON::Number;Take('-');if(!Take('0'))Digits();if(Take('.'))Digits();if(Take('e')||Take('E')){if(!Take('+'))Take('-');Digits();}}
    value.raw=text_.substr(begin,at_-begin);return value;
  }
};
inline const JSON* Find(const JSON& object,std::string_view key){
  if(object.kind!=JSON::Object)Invalid("expected object");
  const auto found=object.fields.find(std::u16string(key.begin(),key.end()));return found==object.fields.end()?nullptr:&found->second;
}
inline const JSON& Get(const JSON& object,std::string_view key){const auto* value=Find(object,key);if(!value)Invalid("missing field "+std::string(key));return *value;}
inline const std::vector<JSON>& Array(const JSON& value){if(value.kind!=JSON::Array)Invalid("expected array");return value.items;}
inline const std::vector<JSON>& Rows(const JSON& value,std::string_view key){return Array(Get(value,key));}
inline const std::vector<JSON>& OptionalRows(const JSON& value,std::string_view key){static const std::vector<JSON> empty;const auto* found=Find(value,key);return found?Array(*found):empty;}
inline uint64_t Integer(const JSON& value,bool string=false){
  if(value.kind!=(string?JSON::String:JSON::Number))Invalid("expected unsigned integer");
  const std::string decoded=string?Text(value):std::string(value.raw);const std::string_view text=decoded;
  if(text.empty()||(text.size()>1&&text.front()=='0'))Invalid("noncanonical unsigned integer");
  uint64_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
  if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())Invalid("invalid/out-of-range unsigned integer");
  return result;
}
inline uint64_t ID(const JSON& object,std::string_view name,bool zero=false){const auto value=Integer(Get(object,name),true);if(!zero&&!value)Invalid("zero identity");return value;}
inline uint64_t UInt(const JSON& object,std::string_view name){return Integer(Get(object,name));}
inline double Number(const JSON& value){
  if(value.kind!=JSON::Number)Invalid("expected finite number");
  double result=0;const auto parsed=std::from_chars(value.raw.data(),value.raw.data()+value.raw.size(),result);
  if(parsed.ec!=std::errc{}||parsed.ptr!=value.raw.data()+value.raw.size()||!std::isfinite(result)||result<0)Invalid("invalid finite nonnegative number");
  return result;
}
inline bool Bool(const JSON& value){if(value.kind!=JSON::Boolean)Invalid("expected boolean");return value.raw=="true";}
inline std::string CheckedString(const JSON& object,std::string_view name,size_t units){const auto& value=Get(object,name);if(value.kind!=JSON::String||!JsonString(value.raw,units))Invalid("invalid bounded string "+std::string(name));return std::string(value.raw);}
} // namespace suitecut::runner::merge_detail
