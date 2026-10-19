// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
struct NativePostData {
  enum class Kind {Absent,Text,Binary,Unavailable};
  Kind kind=Kind::Absent;
  std::vector<uint8_t> bytes;
};
// CDP provides UTF8 text alongside exact base64 postDataEntries. Invalid UTF8
// binary uploads must use the genuine bytes, never reconstructed replacement text.
inline NativePostData CaptureNativePostData(CefRefPtr<CefDictionaryValue> request) {
  NativePostData result;
  if(request->GetType("postDataBase64")==VTYPE_STRING){
    const auto encoded=request->GetString("postDataBase64").ToString();result.kind=NativePostData::Kind::Binary;
    auto decoded=encoded.empty()?nullptr:CefBase64Decode(encoded);
    if(encoded.size()>2796204||(!encoded.empty()&&!decoded)||(decoded&&decoded->GetSize()>2097152)){result.kind=NativePostData::Kind::Unavailable;return result;}
    if(decoded){result.bytes.resize(decoded->GetSize());decoded->GetData(result.bytes.data(),result.bytes.size(),0);}return result;
  }
  if(request->GetBool("postDataUnavailable")||request->GetType("postDataBodyId")==VTYPE_STRING){result.kind=NativePostData::Kind::Unavailable;return result;}
  const bool text=request->GetType("postData")==VTYPE_STRING;
  const auto rendered=text?request->GetString("postData").ToString():std::string{};
  auto entries=request->GetList("postDataEntries");
  if(!entries || !entries->GetSize()) {
    if(text){result.kind=rendered.size()>2097152?NativePostData::Kind::Unavailable:NativePostData::Kind::Text;}
    else if(request->GetBool("hasPostData"))result.kind=NativePostData::Kind::Unavailable;
    return result;
  }
  for(size_t i=0;i<entries->GetSize();++i) {
    auto entry=entries->GetDictionary(i);
    if(!entry || entry->GetType("bytes")!=VTYPE_STRING){result.kind=NativePostData::Kind::Unavailable;std::vector<uint8_t>().swap(result.bytes);return result;}
    const auto encoded=entry->GetString("bytes").ToString();
    if(encoded.size()>2796204){result.kind=NativePostData::Kind::Unavailable;std::vector<uint8_t>().swap(result.bytes);return result;}
    if(encoded.empty())continue;
    auto decoded=CefBase64Decode(encoded);
    if(!decoded || decoded->GetSize()>2097152-result.bytes.size()){result.kind=NativePostData::Kind::Unavailable;std::vector<uint8_t>().swap(result.bytes);return result;}
    const auto offset=result.bytes.size();result.bytes.resize(offset+decoded->GetSize());decoded->GetData(result.bytes.data()+offset,decoded->GetSize(),0);
  }
  const bool exact_text=text && rendered.size()==result.bytes.size() && std::equal(result.bytes.begin(),result.bytes.end(),rendered.begin(),[](uint8_t actual,char value){return actual==static_cast<uint8_t>(value);});
  result.kind=exact_text?NativePostData::Kind::Text:NativePostData::Kind::Binary;
  if(exact_text)std::vector<uint8_t>().swap(result.bytes);
  return result;
}
}  // namespace suitecut
