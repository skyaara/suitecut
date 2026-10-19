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
struct Call{std::string method;Dict params;Controls::Result receipt;};
static Dict Event(const std::string&id,const std::string&method,bool automatic=false){
 auto event=CefDictionaryValue::Create();event->SetString("requestId",id);event->SetString("type","Fetch");event->SetString("frameId","actual-frame");auto request=CefDictionaryValue::Create();request->SetString("url","https://actual.test/cors");request->SetString("method",method);auto headers=CefDictionaryValue::Create();headers->SetString("Origin","https://parent.test");headers->SetString("Access-Control-Request-Headers","x-actual");request->SetDictionary("headers",headers);event->SetDictionary("request",request);auto initiator=CefDictionaryValue::Create();initiator->SetString("type",automatic?"preflight":"script");if(automatic)initiator->SetString("requestId","exact-parent");event->SetDictionary("initiator",initiator);return event;
}
static Dict Pause(const std::string&id,const std::string&native,const std::string&method){auto event=Event(id,method);event->SetString("networkId",native);event->SetString("resourceType","Fetch");return event;}
static void Verify(bool parent_first,bool continue_before_preflight){
 std::vector<Call>calls;std::vector<std::string>configurations;std::vector<Dict>routes;std::vector<std::string>failures;
 Controls controls([&](const std::string&,const std::string&method,Dict params,Controls::Result done){if(method=="Fetch.enable"||method=="Fetch.disable"||method=="Network.setCacheDisabled"){configurations.push_back(method);done(true,"");}else calls.push_back({method,params,std::move(done)});},[&](Dict event){if(event->GetString("event")=="route")routes.push_back(event->Copy(false));},[&](const std::string&error){failures.push_back(error);},[]{},[]{return 1;});
 auto registration=CefDictionaryValue::Create();registration->SetString("action","registerRoute");registration->SetString("registrationId","exact-times-one");registration->SetString("url","**/cors");registration->SetInt("times",1);controls.Command(1,registration,[](bool ok,const std::string&,const std::string&){assert(ok);});
 controls.Event("Network.requestWillBeSent",Event("exact-parent","DELETE"),"");
 const auto parent_pause=[&]{controls.Event("Fetch.requestPaused",Pause("actual-parent-pause","exact-parent","DELETE"),"");assert(routes.size()==1&&routes.back()->GetDictionary("request")->GetString("method")=="DELETE");auto state=CefDictionaryValue::Create();controls.Diagnostics(state);assert(state->GetInt("networkRoutes")==0);};
 bool continued=false;const auto continue_parent=[&]{auto config=CefDictionaryValue::Create();config->SetString("action","continue");config->SetString("routeId",routes.back()->GetDictionary("route")->GetString("id"));controls.Command(2,config,[&](bool ok,const std::string&,const std::string&){assert(ok);continued=true;});assert(calls.back().method=="Fetch.continueRequest");auto receipt=std::move(calls.back().receipt);receipt(true,"");assert(continued&&configurations.back()=="Fetch.enable");};
 if(parent_first){parent_pause();if(continue_before_preflight)continue_parent();}
 const auto before=calls.size();controls.Event("Fetch.requestPaused",Pause("actual-auto-pause","exact-automatic","OPTIONS"),"");assert(calls.size()==before);
 controls.Event("Network.requestWillBeSent",Event("exact-automatic","OPTIONS",true),"");assert(calls.size()==before+1&&calls.back().method=="Fetch.fulfillRequest");assert(routes.size()==size_t(parent_first?1:0));auto auto_receipt=std::move(calls.back().receipt);auto_receipt(true,"");auto_receipt={};
 if(!parent_first){parent_pause();}
 if(!continued){continue_parent();}
 assert(routes.size()==1&&failures.empty());
 auto terminal=CefDictionaryValue::Create();terminal->SetString("requestId","exact-parent");controls.Event("Network.loadingFinished",terminal,"");assert(configurations.back()=="Fetch.disable");
 // A terminal native parent cannot extend consumed policy to later traffic.
 const auto after=calls.size();controls.Event("Fetch.requestPaused",Pause("later-auto-pause","later-automatic","OPTIONS"),"");assert(calls.size()==after+1&&calls.back().method=="Fetch.continueRequest");auto final_receipt=std::move(calls.back().receipt);final_receipt(true,"");final_receipt={};controls.Clear();assert(suitecut::NativeNetworkPreflights::global_pending()==0&&failures.empty());
}
int main(){assert(cef_api_hash(CEF_API_VERSION,0));Verify(false,false);Verify(true,false);Verify(true,true);}
