// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "include/cef_api_hash.h"
#include "context_worker_network.h"
using Controls=suitecut::NativeContextWorkerNetwork;using Dict=Controls::Dict;
Dict Event(const char*kind,const char*local){
  auto event=CefDictionaryValue::Create();event->SetString("event",kind);event->SetInt("sequence",1);
  auto request=CefDictionaryValue::Create();request->SetString("id",local);request->SetString("requestId","actual-network-id");request->SetString("session","actual-child");request->SetString("url","https://native.test/route");request->SetString("method","GET");request->SetString("resourceType","Fetch");request->SetString("frameId","");request->SetBool("isNavigationRequest",false);request->SetDictionary("headers",CefDictionaryValue::Create());event->SetDictionary("request",request);
  if(std::string(kind)=="route"){auto route=CefDictionaryValue::Create();route->SetString("id","actual-fetch");route->SetString("registrationId","actual-registration");route->SetString("callbackScope","91");route->SetString("callbackToken","44");event->SetDictionary("route",route);}
  return event;
}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>requests,routes;std::vector<Controls::Reply>physical;std::string selected;int callbacks=0,cancels=0;
  Controls controls([&](int context,Dict,Dict event){assert(context==1);requests.push_back(event->Copy(false));});
  controls.SetRouteEmitter([&](int context,const std::string&owner,Dict worker,Dict event){assert(context==1&&owner=="actual-source"&&worker->GetString("id")=="actual-worker");routes.push_back(event->Copy(false));});
  auto attach=[&](const std::string&owner,bool broker){assert(controls.Attach(1,owner,broker,[&,owner](int operation,Dict params,double deadline,Controls::Reply reply){assert(operation>0&&deadline==99);selected=owner;if(params->GetString("action")=="requestHeaders")assert(params->GetString("requestId")=="source-local");else assert(params->GetString("callbackScope")=="91");physical.push_back(std::move(reply));},[](const auto&,const auto&,double,auto){assert(false);},[&](int){++cancels;}));};
  attach("actual-source",false);attach("actual-broker",true);
  auto worker=CefDictionaryValue::Create();worker->SetString("id","actual-worker");worker->SetString("type","service_worker");worker->SetString("url","https://native.test/service.js");
  assert(controls.Observe(1,"actual-source",worker,Event("request","source-local")));assert(controls.Observe(1,"actual-broker",worker,Event("request","broker-local")));assert(requests.size()==1);
  assert(controls.Observe(1,"actual-source",worker,Event("route","source-local")));assert(routes.size()==1);
  assert(routes[0]->GetDictionary("request")->GetString("id")==requests[0]->GetDictionary("request")->GetString("id"));assert(routes[0]->GetDictionary("request")->GetString("frameId").empty());assert(routes[0]->GetDictionary("route")->GetString("callbackToken")=="44");
  auto headers=CefDictionaryValue::Create();headers->SetString("action","requestHeaders");
  assert(controls.Handle(10,1,requests[0]->GetDictionary("request")->GetString("id"),headers,99,[&](bool ok,Dict,const auto&){assert(ok);}));assert(selected=="actual-source");physical.back()(true,nullptr,"");physical.clear();
  auto params=CefDictionaryValue::Create();params->SetString("action","fulfill");params->SetString("routeId","actual-fetch");params->SetString("callbackScope","91");
  assert(!controls.HandleRoute(1,2,"actual-source",params,99,[](bool,Dict,const auto&){assert(false);}));assert(!controls.HandleRoute(1,1,"unknown-owner",params,99,[](bool,Dict,const auto&){assert(false);}));
  assert(controls.HandleRoute(1,1,"actual-source",params,99,[&](bool ok,Dict,const auto&){assert(ok);++callbacks;}));assert(selected=="actual-source"&&physical.size()==1);physical[0](true,nullptr,"");assert(callbacks==1);
  params->SetString("action","registerRoute");assert(!controls.HandleRoute(2,1,"actual-source",params,99,[](bool,Dict,const auto&){assert(false);}));params->SetString("action","continue");
  assert(controls.HandleRoute(2,1,"actual-source",params,99,[](bool,Dict,const auto&){assert(false);}));controls.CancelOwner(2);assert(cancels==1);physical[1](true,nullptr,"");assert(callbacks==1);
  controls.Detach("actual-source");assert(!controls.HandleRoute(3,1,"actual-source",params,99,[](bool,Dict,const auto&){assert(false);}));
  // A real Fetch observation can reserve identity before Network creation;
  // only the later actual request packet publishes the request event.
  Controls early([&](int,Dict,Dict event){requests.push_back(event->Copy(false));});int earlyRoutes=0;std::string canonical;
  assert(early.Attach(1,"early",false,[](int,Dict,double,Controls::Reply){},[](const auto&,const auto&,double,auto){},[](int){}));
  early.SetRouteEmitter([&](int,const auto&,Dict,Dict event){++earlyRoutes;canonical=event->GetDictionary("request")->GetString("id");});
  assert(early.Observe(1,"early",worker,Event("route","early-local")));assert(earlyRoutes==1&&requests.size()==1);
  assert(early.Observe(1,"early",worker,Event("request","early-local")));assert(requests.size()==2&&requests.back()->GetDictionary("request")->GetString("id")==canonical);
  std::cout<<"Native context route owners: 4 exact-owner/canonical-identity groups passed\n";
}
