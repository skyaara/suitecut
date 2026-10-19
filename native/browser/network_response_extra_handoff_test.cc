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
 assert(controls.FrameSessionAttached("child","","actual-frame"));
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"exact-native-id","frameId":"actual-frame","type":"Document","request":{"url":"https://native.test/","method":"GET","headers":{}}})"),"");
 const auto logical=events.back()->GetDictionary("request")->GetString("id");
 // The actual response is the first metadata to choose its renderer channel.
 // Its expected ExtraInfo belongs to the same physical request in the parent.
 controls.Event("Network.responseReceived",Parse(R"({"requestId":"exact-native-id","frameId":"actual-frame","type":"Document","hasExtraInfo":true,"response":{"url":"https://native.test/","status":200,"headers":{}}})"),"child");
 controls.Event("Network.loadingFinished",Parse(R"({"requestId":"exact-native-id"})"),"child");
 auto query=Parse(R"({"action":"responseHeaders"})");query->SetString("requestId",logical);bool answered=false;
 controls.Command(1,query,[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual-Wire")=="yes");answered=true;});assert(!answered);
 controls.Event("Network.responseReceivedExtraInfo",Parse(R"({"requestId":"exact-native-id","headers":{"Actual-Wire":"yes"}})"),"");
 assert(answered);assert(events.back()->GetDictionary("request")->GetString("id")==logical);assert(events.back()->GetDictionary("response")->GetString("session")=="child");
 auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkResponseHeaderBytes")==0);controls.Clear();
}
