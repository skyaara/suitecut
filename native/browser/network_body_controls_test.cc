// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_body_controls.h"

int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  using Controls=suitecut::NativeNetworkBodyControls;using Prepared=Controls::Prepared;
  std::vector<uint8_t> wire;std::vector<Controls::Dict> events;
  Controls controls([&](uint64_t,size_t offset,size_t total,std::span<const uint8_t> bytes,bool canceled){assert(!canceled);if(!offset)wire.clear();wire.insert(wire.end(),bytes.begin(),bytes.end());assert(wire.size()<=total);return true;},[&](Controls::Dict event){events.push_back(event);},[](const std::string&){assert(false);});
  auto request=CefDictionaryValue::Create();request->SetString("postData","\xef\xbf\xbd");request->SetBool("hasPostData",true);
  assert(controls.Prepare(request,100,true)==Prepared::NeedsFetch);
  auto entries=CefListValue::Create();auto entry=CefDictionaryValue::Create();entry->SetString("bytes","AP8NCg==");entries->SetDictionary(0,entry);request->SetList("postDataEntries",entries);
  assert(controls.Prepare(request,100,true)==Prepared::Transferred);assert(!request->HasKey("postData") && !request->HasKey("postDataEntries"));
  const auto id=std::stoull(request->GetString("postDataBodyId").ToString());auto event=CefDictionaryValue::Create();event->SetDictionary("request",request);assert(controls.publication().Queue(event));
  controls.publication().Tick(1);assert(wire==std::vector<uint8_t>({0,255,13,10}) && events.empty());assert(controls.publication().Received(id));assert(events.size()==1);assert(controls.publication().Taken(id));
  const std::vector<uint8_t> large(2097152,255);auto override=CefDictionaryValue::Create();override->SetString("postDataBase64",CefBase64Encode(large.data(),large.size()));
  assert(controls.PrepareOverride(override,100)==Prepared::Transferred);const auto large_id=std::stoull(override->GetString("postDataBodyId").ToString());
  for(size_t i=0;i<8;++i){controls.publication().Tick(1);}
  assert(wire==large);assert(controls.publication().Received(large_id));assert(controls.publication().Taken(large_id));
  auto unavailable=CefDictionaryValue::Create();unavailable->SetString("postDataBase64",std::string(2796205,'A'));assert(controls.PrepareOverride(unavailable,100)==Prepared::Unavailable);assert(unavailable->GetBool("postDataUnavailable"));
  auto file=CefDictionaryValue::Create();entries=CefListValue::Create();entries->SetDictionary(0,CefDictionaryValue::Create());file->SetList("postDataEntries",entries);assert(controls.Prepare(file,100,true)==Prepared::Unavailable);
}
