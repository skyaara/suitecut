// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>events;std::vector<std::string>methods;
 Controls controls([&](const std::string&,const std::string&method,Dict,Controls::Result done){methods.push_back(method);done(true,"");},[&](Dict event){events.push_back(event->Copy(false));},[](const std::string&){assert(false);},[]{},[]{return 10;});
 controls.Command(1,Parse(R"({"action":"registerRoute","registrationId":"actual-page-handler","url":"**","times":0})"),[](bool ok,const std::string&,const std::string&){assert(ok);});
 auto replay=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(replay->Load(Parse(R"({"log":{"entries":[]}})"),error));assert(controls.BindReplay("actual-context-har","**",replay,false,[](bool ok,const std::string&){assert(ok);},0,true));
 controls.Event("Fetch.requestPaused",Parse(R"({"requestId":"actual-fetch","networkId":"actual-request","frameId":"actual-frame","resourceType":"Fetch","request":{"url":"https://native.test/actual","method":"GET","headers":{}}})"),"");assert(events.size()==1&&events[0]->GetString("event")=="route"&&events[0]->GetDictionary("route")->GetString("registrationId")=="actual-page-handler");
 auto fallback=CefDictionaryValue::Create();fallback->SetString("action","fallback");fallback->SetString("routeId",events[0]->GetDictionary("route")->GetString("id"));bool complete=false;controls.Command(2,fallback,[&](bool ok,const std::string&,const std::string&){assert(ok);complete=true;});assert(complete&&methods.back()=="Fetch.failRequest");
 controls.Command(3,Parse(R"({"action":"removeRoute","registrationId":""})"),[](bool ok,const std::string&,const std::string&){assert(ok);});assert(methods.back()=="Fetch.enable"); // Context HAR still owns Fetch.
 controls.Event("Fetch.requestPaused",Parse(R"({"requestId":"actual-fetch-after-page-unroute","networkId":"actual-request2","frameId":"actual-frame","resourceType":"Fetch","request":{"url":"https://native.test/actual","method":"GET","headers":{}}})"),"");assert(events.size()==1&&methods.back()=="Fetch.failRequest");controls.Clear();
}
