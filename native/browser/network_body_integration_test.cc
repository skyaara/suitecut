// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#ifndef SUITECUT_NETWORK_CONTROL_HEADER
#define SUITECUT_NETWORK_CONTROL_HEADER "network_controls.h"
#endif
#include SUITECUT_NETWORK_CONTROL_HEADER
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
  std::vector<Dict> events;std::vector<uint8_t> delivered;std::string token;double now=0;
  Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict info){events.push_back(info);},[](const std::string&){assert(false);},[]{},[&]{return now;},[&](uint64_t id,size_t offset,size_t total,std::span<const uint8_t> bytes,bool canceled){assert(!canceled);assert(offset==delivered.size());assert(total==256);token=std::to_string(id);delivered.insert(delivered.end(),bytes.begin(),bytes.end());return true;});
  std::vector<uint8_t> expected(256);for(size_t i=0;i<expected.size();++i)expected[i]=static_cast<uint8_t>(i);
  auto data=CefDictionaryValue::Create();data->SetString("requestId","actual-binary");data->SetString("type","Fetch");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/binary");request->SetString("method","POST");request->SetBool("hasPostData",true);request->SetDictionary("headers",CefDictionaryValue::Create());auto entries=CefListValue::Create();auto entry=CefDictionaryValue::Create();entry->SetString("bytes",CefBase64Encode(expected.data(),expected.size()));entries->SetDictionary(0,entry);request->SetList("postDataEntries",entries);data->SetDictionary("request",request);
  controls.Event("Network.requestWillBeSent",data,"");assert(events.empty());assert(controls.NeedsTick());controls.Tick(now);assert(delivered==expected);assert(events.empty());
  assert(controls.BodyAcknowledged(token,"received"));assert(events.size()==1);assert(events.back()->GetDictionary("request")->GetString("postDataBodyId")==token);assert(!events.back()->GetDictionary("request")->HasKey("postData"));assert(controls.BodyAcknowledged(token,"taken"));assert(!controls.BodyAcknowledged(token,"received"));assert(controls.BodyAcknowledged(token,"taken"));
  auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(diagnostics->GetInt("networkBodyOwners")==0);assert(diagnostics->GetInt("networkBodyBytes")==0);
  controls.Clear();
}
