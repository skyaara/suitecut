// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace suitecut {

// CEF's strict JSON parser rejects lone UTF-16 surrogates, although JavaScript
// and CDP can serialize them. Mask them only in the metadata copy used for
// routing. Forward original argument/result bytes to preserve their value.
inline std::string CDPMetadataJSON(std::string_view input) {
  std::string result(input);
  auto unit=[&](size_t offset)->int {
    if(offset+6>input.size() || input[offset]!='\\' || input[offset+1]!='u')return -1;
    int value=0;
    for(size_t i=offset+2;i<offset+6;++i) {
      const char c=input[i];const int digit=c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : -1;
      if(digit<0)return -1;
      value=value*16+digit;
    }
    return value;
  };
  bool string=false;
  for(size_t i=0;i<input.size();++i) {
    if(input[i]=='"') {string=!string;continue;}
    if(!string || input[i]!='\\')continue;
    const int value=unit(i);
    if(value>=0xd800 && value<=0xdbff) {
      const int next=unit(i+6);
      if(next>=0xdc00 && next<=0xdfff) {i+=11;continue;}
      result.replace(i,6,"\\ufffd");i+=5;
    } else if(value>=0xdc00 && value<=0xdfff) {result.replace(i,6,"\\ufffd");i+=5;}
    else if(i+1<input.size()) ++i;
  }
  return result;
}

// Extract a top-level value span from JSON already validated by CEF through
// CDPMetadataJSON. This is a lexical scanner, not a replacement JSON parser.
// No argument/result object needs decoding or re-encoding across the shim.
inline std::optional<std::pair<size_t,size_t>> CDPFieldSpan(std::string_view input,std::string_view key) {
  auto whitespace=[&](size_t& i) {while(i<input.size() && (input[i]==' ' || input[i]=='\n' || input[i]=='\r' || input[i]=='\t'))++i;};
  auto string_end=[&](size_t start)->size_t {
    if(start>=input.size() || input[start]!='"')return std::string_view::npos;
    for(size_t i=start+1;i<input.size();++i) {
      if(input[i]=='\\') {if(i+1==input.size())return std::string_view::npos;++i;}
      else if(input[i]=='"')return i+1;
    }
    return std::string_view::npos;
  };
  size_t i=0;whitespace(i);if(i==input.size() || input[i++]!='{')return std::nullopt;
  std::optional<std::pair<size_t,size_t>> found;
  while(i<input.size()) {
    whitespace(i);if(i<input.size() && input[i]=='}')return found;
    const auto name=i;const auto end=string_end(i);if(end==std::string_view::npos)return std::nullopt;
    const bool match=input.substr(name+1,end-name-2)==key;i=end;whitespace(i);
    if(i==input.size() || input[i++]!=':')return std::nullopt;
    whitespace(i);
    const auto begin=i;int depth=0;
    for(;i<input.size();++i) {
      const auto c=input[i];
      if(c=='"') {const auto end=string_end(i);if(end==std::string_view::npos)return std::nullopt;i=end-1;}
      else if(c=='{' || c=='[') {if(++depth>256)return std::nullopt;}
      else if(c=='}' || c==']') {if(depth==0)break;--depth;}
      else if(c==',' && depth==0)break;
    }
    if(i==input.size() || i==begin)return std::nullopt;
    if(match)found=std::pair{begin,i};
    if(input[i]=='}')return found;
    if(input[i++]!=',')return std::nullopt;
  }
  return std::nullopt;
}

inline std::string CDPRawField(std::string_view input,std::string_view key) {
  const auto span=CDPFieldSpan(input,key);
  return span ? std::string(input.substr(span->first,span->second-span->first)) : std::string();
}

inline std::string CDPReplaceField(std::string input,std::string_view key,std::string_view encoded) {
  if(const auto span=CDPFieldSpan(input,key))input.replace(span->first,span->second-span->first,encoded);
  else if(const auto end=input.find_last_of('}');end!=std::string::npos) {
    const auto begin=input.find_first_of('{');
    bool empty=true;
    for(size_t i=begin+1;i<end;++i)if(input[i]!=' ' && input[i]!='\n' && input[i]!='\r' && input[i]!='\t') {empty=false;break;}
    input.insert(end,std::string(empty ? "" : ",")+'"'+std::string(key)+"\":"+std::string(encoded));
  }
  return input;
}
}
