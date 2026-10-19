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
  std::vector<Dict> events;std::vector<std::pair<std::string,std::string>> calls;
  Controls controls([&](const std::string& session,const std::string& method,Dict,Controls::Result done){calls.emplace_back(session,method);done(true,"");},[&](Dict value){events.push_back(value);},[](const std::string&){assert(false);},[]{},[]{return 0;});
  controls.SessionAttached("");auto route=CefDictionaryValue::Create();route->SetString("action","registerRoute");route->SetString("registrationId","actual-rule");route->SetString("url","**/data");controls.Command(1,route,[](bool ok,const std::string&,const std::string&){assert(ok);});
  bool ready=false;controls.SessionAttached("worker-session",[&](bool ok,const std::string&){ready=ok;},false);assert(ready);
  assert(std::find(calls.begin(),calls.end(),std::pair<std::string,std::string>{"worker-session","Fetch.enable"})==calls.end());assert(std::find(calls.begin(),calls.end(),std::pair<std::string,std::string>{"","Fetch.enable"})!=calls.end());
  auto data=CefDictionaryValue::Create();data->SetString("requestId","actual-target-id");data->SetString("type","Script");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/worker.js");request->SetString("method","GET");request->SetDictionary("headers",CefDictionaryValue::Create());data->SetDictionary("request",request);controls.Event("Network.requestWillBeSent",data,"");const auto logical=events.back()->GetDictionary("request")->GetString("id");
  assert(!controls.WorkerScriptAttached("different-target","","worker-session","https://native.test/worker.js"));assert(!controls.WorkerScriptAttached("actual-target-id","","worker-session","https://different.test/worker.js"));assert(controls.WorkerScriptAttached("actual-target-id","","worker-session","https://native.test/worker.js"));assert(events.back()->GetString("event")=="requestupdated");assert(events.back()->GetDictionary("request")->GetString("id")==logical);
  auto response=CefDictionaryValue::Create();response->SetString("url","https://native.test/worker.js");response->SetInt("status",200);response->SetString("protocol","http/1.1");data->SetDictionary("response",response);controls.Event("Network.responseReceived",data,"worker-session");assert(events.back()->GetString("event")=="response");assert(events.back()->GetDictionary("request")->GetString("id")==logical);assert(events.back()->GetDictionary("response")->GetString("session")=="worker-session");assert(events.back()->GetDictionary("response")->GetString("protocol")=="http/1.1");controls.Event("Network.loadingFinished",data,"worker-session");assert(events.back()->GetString("event")=="requestfinished");assert(events.back()->GetDictionary("request")->GetString("id")==logical);
  controls.Clear();
}
