// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
struct Command{std::string session,method;Dict params;Controls::Result receipt;};
static Dict Request(const std::string&id,const std::string&method,bool preflight=false,const std::string&parent={}){
  auto event=CefDictionaryValue::Create();event->SetString("requestId",id);event->SetString("type","Fetch");event->SetString("frameId","real-frame");
  auto request=CefDictionaryValue::Create();request->SetString("url","https://actual.test/cors");request->SetString("method",method);auto headers=CefDictionaryValue::Create();headers->SetString("Origin","https://actual-parent.test");headers->SetString("Access-Control-Request-Headers","x-genuine");request->SetDictionary("headers",headers);event->SetDictionary("request",request);
  auto initiator=CefDictionaryValue::Create();initiator->SetString("type",preflight?"preflight":"script");if(preflight)initiator->SetString("requestId",parent);event->SetDictionary("initiator",initiator);return event;
}
static Dict Pause(const std::string&fetch,const std::string&network,const std::string&method){auto event=Request(fetch,method);event->SetString("networkId",network);event->SetString("resourceType","XHR");return event;}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Command>sent;std::vector<Dict>events;std::vector<std::string>failures;double now=0;
 Controls controls([&](const std::string&session,const std::string&method,Dict params,Controls::Result done){if(method=="Fetch.enable"||method=="Fetch.disable"||method=="Network.setCacheDisabled"){done(true,"");return;}sent.push_back({session,method,params,std::move(done)});},[&](Dict event){events.push_back(event);},[&](const std::string&error){failures.push_back(error);},[]{},[&]{return now;});
 auto register_route=[&](const std::string&id,int times){auto config=CefDictionaryValue::Create();config->SetString("action","registerRoute");config->SetString("registrationId",id);config->SetString("url","**/cors");config->SetInt("times",times);bool complete=false;controls.Command(1,config,[&](bool ok,const std::string&,const std::string&){assert(ok);complete=true;});assert(complete);};
 register_route("actual-registration",1);
 controls.Event("Network.requestWillBeSent",Request("actual-parent","DELETE"),"");
 const auto events_before=events.size();
 controls.Event("Fetch.requestPaused",Pause("actual-preflight-pause","actual-preflight","OPTIONS"),"");assert(sent.empty()&&events.size()==events_before);
 controls.Event("Network.requestWillBeSent",Request("actual-preflight","OPTIONS",true,"actual-parent"),"");
 assert(sent.size()==1&&sent.back().method=="Fetch.fulfillRequest"&&sent.back().params->GetString("requestId")=="actual-preflight-pause");
 assert(events.size()==events_before); // genuine automatic preflight never invokes a fabricated SDK Route
 auto state=CefDictionaryValue::Create();controls.Diagnostics(state);assert(state->GetInt("networkClassifyingPauses")==1&&state->GetInt("networkRoutes")==1); // times not consumed
 sent.back().receipt(true,"");sent.back().receipt={};controls.Diagnostics(state);assert(state->GetInt("networkClassifyingPauses")==0);
 controls.Event("Fetch.requestPaused",Pause("actual-parent-pause","actual-parent","DELETE"),"");assert(events.back()->GetString("event")=="route");
 const auto actual=events.back()->GetDictionary("request");assert(actual->GetString("method")=="DELETE"&&actual->GetString("requestId")=="actual-parent");controls.Diagnostics(state);assert(state->GetInt("networkRoutes")==0&&state->GetInt("networkPaused")==1);
 // Authored OPTIONS has Chromium's script initiator and remains observable/routable.
 register_route("author-registration",0);controls.Event("Fetch.requestPaused",Pause("author-pause","author-options","OPTIONS"),"");const auto before_author=events.size();
 controls.Event("Network.requestWillBeSent",Request("author-options","OPTIONS"),"");assert(events.size()==before_author+2&&events.back()->GetString("event")=="route"&&events.back()->GetDictionary("request")->GetString("method")=="OPTIONS");
 // Removing the captured parent policy while classification waits does not
 // authorize a response, even if a later registration matches the same URL.
 controls.Event("Network.requestWillBeSent",Request("unadmitted-parent-native","DELETE"),"");
 controls.Event("Fetch.requestPaused",Pause("old-policy-pause","old-policy-preflight","OPTIONS"),"");
 auto remove=CefDictionaryValue::Create();remove->SetString("action","removeRoute");remove->SetString("registrationId","author-registration");bool removed=false;controls.Command(2,remove,[&](bool ok,const std::string&,const std::string&){assert(ok);removed=true;});assert(removed);
 register_route("new-registration",0);const auto count=sent.size();controls.Event("Network.requestWillBeSent",Request("old-policy-preflight","OPTIONS",true,"unadmitted-parent-native"),"");assert(sent.size()==count+1&&sent.back().method=="Fetch.continueRequest");
 // Exact initiator parent identity is mandatory: URL/header equality alone
 // cannot authorize the hidden CORS transport response.
 controls.Event("Fetch.requestPaused",Pause("unrelated-pause","unrelated-preflight","OPTIONS"),"");const auto count2=sent.size();controls.Event("Network.requestWillBeSent",Request("unrelated-preflight","OPTIONS",true,"missing-parent-id"),"");assert(sent.size()==count2);


 // A real preflight's exact parent metadata can arrive on its observed agent
 // later. Do not release wire or manufacture a Route before that identity.
 const auto delayed_count=sent.size();controls.Event("Fetch.requestPaused",Pause("early-auto-pause","early-auto","OPTIONS"),"");
 controls.Event("Network.requestWillBeSent",Request("early-auto","OPTIONS",true,"delayed-parent-native"),"");assert(sent.size()==delayed_count);
 controls.Event("Network.requestWillBeSent",Request("delayed-parent-native","DELETE"),"");assert(sent.size()==delayed_count+1&&sent.back().method=="Fetch.fulfillRequest");sent.back().receipt(true,"");sent.back().receipt={};
 // The real parent may consume a times:1 handler before automatic preflight
 // arrives. Its exact native admission remains authoritative after the rule
 // disappears, including after the real user continue ACK.
 auto clear_rules=CefDictionaryValue::Create();clear_rules->SetString("action","removeRoute");controls.Command(9,clear_rules,[](bool ok,const std::string&,const std::string&){assert(ok);});
 register_route("parent-first",1);controls.Event("Network.requestWillBeSent",Request("parent-first-native","DELETE"),"");controls.Event("Fetch.requestPaused",Pause("parent-first-pause","parent-first-native","DELETE"),"");
 assert(events.back()->GetString("event")=="route");controls.Diagnostics(state);assert(state->GetInt("networkRoutes")==0);
 const auto first_parent_route=events.back()->GetDictionary("route")->GetString("id");const auto count3=sent.size();controls.Event("Fetch.requestPaused",Pause("parent-first-auto-pause","parent-first-auto","OPTIONS"),"");assert(sent.size()==count3);
 controls.Event("Network.requestWillBeSent",Request("parent-first-auto","OPTIONS",true,"parent-first-native"),"");assert(sent.size()==count3+1&&sent.back().method=="Fetch.fulfillRequest");sent.back().receipt(true,"");sent.back().receipt={};
 auto continue_first=CefDictionaryValue::Create();continue_first->SetString("action","continue");continue_first->SetString("routeId",first_parent_route);controls.Command(10,continue_first,[](bool ok,const std::string&,const std::string&){assert(ok);});sent.back().receipt(true,"");sent.back().receipt={};
 const auto count4=sent.size();controls.Event("Fetch.requestPaused",Pause("continued-parent-auto-pause","continued-parent-auto","OPTIONS"),"");controls.Event("Network.requestWillBeSent",Request("continued-parent-auto","OPTIONS",true,"parent-first-native"),"");assert(sent.size()==count4+1&&sent.back().method=="Fetch.fulfillRequest");sent.back().receipt(true,"");sent.back().receipt={};
 auto terminal=CefDictionaryValue::Create();terminal->SetString("requestId","parent-first-native");controls.Event("Network.loadingFinished",terminal,"");
 const auto count5=sent.size();controls.Event("Fetch.requestPaused",Pause("terminal-parent-auto-pause","terminal-parent-auto","OPTIONS"),"");controls.Event("Network.requestWillBeSent",Request("terminal-parent-auto","OPTIONS",true,"parent-first-native"),"");assert(sent.size()==count5+1&&sent.back().method=="Fetch.continueRequest");
 for(auto&command:sent)if(command.receipt){command.receipt(true,"");command.receipt={};}
 assert(failures.empty());controls.Clear();assert(suitecut::NativeNetworkPreflights::global_pending()==0);
}
