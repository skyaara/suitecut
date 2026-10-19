// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using C=suitecut::NetworkControls;using Dict=C::Dict;
int main(){assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>public_events,private_events;C c([](const std::string&,const std::string&,Dict,C::Result done){done(true,"");},[&](Dict v){public_events.push_back(v->Copy(false));},[](const std::string&){assert(false);},[]{},[]{return 0;});c.SetObserver([&](Dict v){private_events.push_back(v->Copy(false));});
  auto event=CefDictionaryValue::Create();event->SetString("requestId","real-preflight");event->SetString("type","Other");auto initiator=CefDictionaryValue::Create();initiator->SetString("type","preflight");initiator->SetString("requestId","actual-author-post");event->SetDictionary("initiator",initiator);event->SetString("frameId","");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/same");request->SetString("method","OPTIONS");auto headers=CefDictionaryValue::Create();headers->SetString("Access-Control-Request-Method","POST");request->SetDictionary("headers",headers);event->SetDictionary("request",request);c.Event("Network.requestWillBeSent",event,"");auto response=CefDictionaryValue::Create();response->SetString("url","https://native.test/same");response->SetInt("status",204);response->SetDictionary("headers",CefDictionaryValue::Create());event->SetDictionary("response",response);c.Event("Network.responseReceived",event,"");c.Event("Network.loadingFinished",event,"");assert(public_events.empty()&&private_events.size()==3);
  // Exact native provenance, not URL, method, frame guessing or headers.
  event->SetString("requestId","actual-author-options");event->SetString("type","Fetch");event->GetDictionary("initiator")->SetString("type","script");event->SetString("frameId","actual-frame");c.Event("Network.requestWillBeSent",event,"");c.Event("Network.responseReceived",event,"");c.Event("Network.loadingFinished",event,"");assert(public_events.size()==3&&private_events.size()==6);for(const auto&v:public_events){assert(v->GetDictionary("request")->GetString("method")=="OPTIONS");assert(v->GetDictionary("request")->GetString("resourceType")=="Fetch");}auto diagnostics=CefDictionaryValue::Create();c.Diagnostics(diagnostics);assert(diagnostics->GetInt("networkRequests")==2&&diagnostics->GetInt("networkJournal")==3);
}
