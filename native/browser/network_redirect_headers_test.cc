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
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>events;
 Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict event){events.push_back(event->Copy(false));},[](const std::string&){assert(false);},[]{},[]{return 1;});
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"actual-same-id","type":"Document","request":{"url":"http://native.test/one","method":"GET","headers":{}}})"),"");
 // The genuine true redirectHasExtraInfo marker describes both raw streams.
 controls.Event("Network.requestWillBeSentExtraInfo",Parse(R"({"requestId":"actual-same-id","headers":{"Actual-Wire":"one"}})"),"");
 controls.Event("Network.responseReceivedExtraInfo",Parse(R"({"requestId":"actual-same-id","headers":{"sec-test-header":"one","Set-Cookie":"one=1\ntwo=2"}})"),"");
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"actual-same-id","type":"Document","redirectHasExtraInfo":true,"redirectResponse":{"url":"http://native.test/one","status":302,"headers":{"sec-test-header":"one"}},"request":{"url":"http://native.test/two","method":"GET","headers":{}}})"),"");
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"actual-same-id","type":"Document","redirectHasExtraInfo":false,"redirectResponse":{"url":"http://native.test/two","status":302,"headers":{"sec-test-header":"two"}},"request":{"url":"http://native.test/final","method":"GET","headers":{}}})"),"");
 controls.Event("Network.responseReceived",Parse(R"({"requestId":"actual-same-id","type":"Document","hasExtraInfo":false,"response":{"url":"http://native.test/final","status":200,"headers":{"sec-test-header":"final"}}})"),"");
 controls.Event("Network.loadingFinished",Parse(R"({"requestId":"actual-same-id"})"),"");
 std::vector<std::string>actual;for(const auto&event:events)if(event->GetString("event")=="response"){const auto response=event->GetDictionary("response");actual.push_back(response->GetDictionary("headers")->GetString("sec-test-header").ToString());if(actual.size()==1)assert(response->GetDictionary("headers")->GetString("Set-Cookie")=="one=1\ntwo=2");}
 assert(actual==std::vector<std::string>({"one","two","final"}));auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkPendingHeaderBytes")==0);controls.Clear();
}
