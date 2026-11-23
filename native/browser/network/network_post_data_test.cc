// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network/network_post_data.h"

namespace {
CefRefPtr<CefDictionaryValue> Request(const std::string& text,const std::vector<std::string>& chunks) {
  auto request=CefDictionaryValue::Create();request->SetString("postData",text);auto entries=CefListValue::Create();
  for(size_t i=0;i<chunks.size();++i){auto entry=CefDictionaryValue::Create();entry->SetString("bytes",chunks[i]);entries->SetDictionary(i,entry);}request->SetList("postDataEntries",entries);return request;
}
}
int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));using Kind=suitecut::NativePostData::Kind;
  auto binary=suitecut::CaptureNativePostData(Request(std::string("\0\xef\xbf\xbd\r\n",6),{"AP8NCg=="}));assert(binary.kind==Kind::Binary && binary.bytes==std::vector<uint8_t>({0,255,13,10}));
  auto chunks=suitecut::CaptureNativePostData(Request("replacement",{"AP8=","DQo="}));assert(chunks.kind==Kind::Binary && chunks.bytes==binary.bytes);
  const std::string large(1020003,'x');auto text=suitecut::CaptureNativePostData(Request(large,{CefBase64Encode(large.data(),large.size()).ToString()}));assert(text.kind==Kind::Text && text.bytes.empty());
  const std::string unicode="\xcf\x80";assert(suitecut::CaptureNativePostData(Request(unicode,{"z4A="})).kind==Kind::Text);
  assert(suitecut::CaptureNativePostData(Request("",{""})).kind==Kind::Text);
  auto missing=Request("partial",{});auto entries=CefListValue::Create();entries->SetDictionary(0,CefDictionaryValue::Create());missing->SetList("postDataEntries",entries);assert(suitecut::CaptureNativePostData(missing).kind==Kind::Unavailable);
  // Only malformed base64 is unavailable; there is no size cap.
  assert(suitecut::CaptureNativePostData(Request("",{"%%%%"})).kind==Kind::Unavailable);
  const std::vector<uint8_t> whole(3*1048576+1,0);auto passed=suitecut::CaptureNativePostData(Request("",{CefBase64Encode(whole.data(),whole.size()).ToString()}));assert(passed.kind==Kind::Binary&&passed.bytes==whole);
}
