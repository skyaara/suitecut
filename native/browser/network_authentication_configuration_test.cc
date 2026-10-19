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
int main(){assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;struct Call{std::string session,method;Dict params;Controls::Result done;};std::vector<Call>calls;Controls controls([&](const std::string&session,const std::string&method,Dict params,Controls::Result done){calls.push_back({session,method,params?params->Copy(false):nullptr,std::move(done)});},[](Dict){},[](const std::string&){assert(false);},[]{},[]{return 0;});controls.SessionAttached("");bool installed=false;controls.SetAuthentication([](Dict){auto response=CefDictionaryValue::Create();response->SetString("response","CancelAuth");return response;},[&](bool ok,const std::string&){installed=ok;});assert(!installed&&calls.size()==2);assert(calls[1].method=="Fetch.enable"&&calls[1].params->GetBool("handleAuthRequests")&&calls[1].params->GetList("patterns")->GetSize()==1);calls[0].done(true,"");calls[1].done(true,"");assert(installed);
 const auto begin=calls.size();bool child=false;controls.SessionAttached("actual-oopif",[&](bool ok,const std::string&){child=ok;});assert(calls.size()==begin+2&&!child);calls[begin].done(true,"");calls[begin+1].done(true,"");assert(child);
 const auto worker=calls.size();controls.SessionAttached("actual-worker",[](bool ok,const std::string&){assert(ok);},false);assert(calls.size()==worker+1&&calls.back().method=="Network.setCacheDisabled");calls.back().done(true,"");
 auto route=CefDictionaryValue::Create();route->SetString("action","registerRoute");route->SetString("url","**/actual");route->SetString("registrationId","actual-handler");const auto route_begin=calls.size();controls.Command(1,route,[](bool ok,const std::string&,const std::string&){assert(ok);});bool route_enabled=false;for(size_t i=route_begin;i<calls.size();++i){if(calls[i].method=="Fetch.enable"){assert(calls[i].params->GetBool("handleAuthRequests"));assert(calls[i].params->GetList("patterns")->GetSize()==1);route_enabled=true;}calls[i].done(true,"");}assert(route_enabled);
 // The actual broker service-worker session keeps auth-enabled Fetch setup,
 // while its existing native worker ancestry still excludes offline commands.
 auto offline=CefDictionaryValue::Create();offline->SetString("action","setOffline");offline->SetBool("offline",true);const auto offline_begin=calls.size();controls.Command(2,offline,[](bool ok,const auto&,const auto&){assert(ok);});for(size_t i=offline_begin;i<calls.size();++i)calls[i].done(true,"");
 assert(!controls.WorkerScriptAttached("actual-service-target","","actual-service-session","https://native.test/service.js"));
 const auto service_begin=calls.size();bool service_ready=false;controls.SessionAttached("actual-service-session",[&](bool ok,const auto&){assert(ok);service_ready=true;},true);
 assert(!service_ready&&calls.size()==service_begin+2&&calls[service_begin].method=="Network.setCacheDisabled"&&calls[service_begin+1].method=="Fetch.enable");
 assert(calls[service_begin+1].params->GetBool("handleAuthRequests"));
 calls[service_begin].done(true,"");assert(!service_ready);calls[service_begin+1].done(true,"");assert(service_ready);
 controls.Clear();
}
