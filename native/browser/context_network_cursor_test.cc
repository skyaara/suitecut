// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "dictionary_number.h"
#include "context_network_controls.h"
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::NativeContextNetworkControls;
  auto parsed=CefParseJSON("{\"afterSequence\":1}",JSON_PARSER_RFC)->GetDictionary();assert(parsed->GetType("afterSequence")==VTYPE_INT);assert(parsed->GetDouble("afterSequence")==0);assert(suitecut::DictionaryNumber(parsed,"afterSequence")==1);
  Controls controls([](Controls::Dict){});auto event=CefDictionaryValue::Create();event->SetString("event","request");assert(controls.Observe(1,1,event));bool done=false;
  assert(controls.Wait(1,1,"request",suitecut::DictionaryNumber(parsed,"afterSequence"),100,[&](bool ok,Controls::Dict result,const std::string&){assert(ok && result->GetDouble("sequence")==2);done=true;}));assert(!done);assert(controls.Observe(1,1,event));assert(done);
  parsed=CefParseJSON("{\"afterSequence\":2.5}",JSON_PARSER_RFC)->GetDictionary();assert(suitecut::DictionaryNumber(parsed,"afterSequence")==2.5);
  parsed=CefParseJSON("{\"afterSequence\":\"2\"}",JSON_PARSER_RFC)->GetDictionary();assert(!std::isfinite(suitecut::DictionaryNumber(parsed,"afterSequence")));
}
