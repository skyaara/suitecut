// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include "include/cef_api_hash.h"
#ifdef __APPLE__
#include "include/wrapper/cef_library_loader.h"
#endif
#include "context_worker_network.h"
#include "context_network_controls.h"
using Controls=suitecut::NativeContextWorkerNetwork;using Dict=Controls::Dict;
Dict Event(const char*kind,const char*local,const char*session,const char*previous=""){
 auto event=CefDictionaryValue::Create();event->SetString("event",kind);auto request=CefDictionaryValue::Create();request->SetString("id",local);request->SetString("session",session);request->SetString("requestId","844458.6");request->SetString("url","http://native.test/actual-worker-request");request->SetString("method","GET");request->SetString("resourceType","Fetch");request->SetString("frameId","");request->SetBool("isNavigationRequest",false);request->SetDictionary("headers",CefDictionaryValue::Create());if(*previous)request->SetString("redirectedFrom",previous);event->SetDictionary("request",request);return event;
}
int main(){
#ifdef __APPLE__
 if (!cef_load_library(SUITECUT_TEST_CEF_LIBRARY)) { std::fprintf(stderr,"Could not load the native CEF test library\n"); return 1; }
#endif
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>published;std::vector<std::function<void(bool,Controls::Bytes,const std::string&)>>physical;std::string selected;int callbacks=0,cancels=0;
 Controls controls([&](int context,Dict worker,Dict event){assert(context==1&&worker->GetString("id")=="CCFADD71C3AF8461C357B1DDCD3B1D38");published.push_back(event->Copy(false));});
 auto worker=CefDictionaryValue::Create();worker->SetString("id","CCFADD71C3AF8461C357B1DDCD3B1D38");worker->SetString("url","http://native.test/service.js");worker->SetString("type","service_worker");
 auto attach=[&](const char*owner,bool broker){assert(controls.Attach(1,owner,broker,[&](int,Dict command,double deadline,Controls::Reply reply){assert(deadline==99);selected=command->GetString("requestId");reply(true,command->Copy(false),"");},[&,broker](const std::string&session,const std::string&native,double deadline,std::function<void(bool,Controls::Bytes,const std::string&)>reply){assert(broker&&session=="FC65536B5D431DC9650DFA16088B72AE"&&native=="844458.6"&&deadline==99);physical.push_back(std::move(reply));},[&](int){++cancels;}));};
 attach("page:1",false);attach("broker:2",true);
 assert(controls.Observe(1,"page:1",worker,Event("request","4","A98E5D3361C44B2D605644D26469D214")));
 assert(controls.Observe(1,"broker:2",worker,Event("request","1","FC65536B5D431DC9650DFA16088B72AE")));assert(published.size()==1&&controls.records()==1&&controls.aliases()==2);
 const auto id=published[0]->GetDictionary("request")->GetString("id").ToString();assert(published[0]->GetDictionary("request")->GetBool("serviceWorker"));
 assert(controls.Observe(1,"page:1",worker,Event("response","4","A98E5D3361C44B2D605644D26469D214"))&&controls.Observe(1,"broker:2",worker,Event("response","1","FC65536B5D431DC9650DFA16088B72AE")));assert(published.size()==2);
 assert(controls.Observe(1,"page:1",worker,Event("requestfinished","4","A98E5D3361C44B2D605644D26469D214"))&&controls.Observe(1,"broker:2",worker,Event("requestfinished","1","FC65536B5D431DC9650DFA16088B72AE")));assert(published.size()==3&&published.back()->GetDictionary("request")->GetString("id")==id);
 auto command=CefDictionaryValue::Create();command->SetString("action","requestHeaders");assert(controls.Handle(1,1,id,command,99,[&](bool ok,Dict result,const std::string&){assert(ok&&result->GetString("requestId")=="1");++callbacks;}));assert(selected=="1"&&callbacks==1);
 // A real page close removes only that physical alias, preserving actual broker body ownership.
 controls.Detach("page:1");assert(controls.aliases()==1);command->SetString("action","body");assert(controls.Handle(2,1,id,command,99,[&](bool ok,Dict result,const std::string&){assert(ok&&result->GetString("body")=="AAH/");++callbacks;}));assert(controls.physical()==1);physical[0](true,{0,1,255},"");assert(controls.physical()==0&&callbacks==2);
 // Duplicate observers of a genuine redirect retain one canonical predecessor.
 assert(controls.Observe(1,"broker:2",worker,Event("request","2","FC65536B5D431DC9650DFA16088B72AE","1")));assert(published.size()==4&&published.back()->GetDictionary("request")->GetString("redirectedFrom")==id);
 const auto next=published.back()->GetDictionary("request")->GetString("id").ToString();assert(next!=id);
 assert(controls.Handle(3,1,next,command,99,[&](bool,Dict,const std::string&){assert(false);}));controls.CancelOwner(3);assert(controls.physical()==1);controls.Clear();assert(controls.physical()==1);physical[1](false,{},"Actual native owner closed");assert(controls.physical()==0&&cancels==0&&callbacks==2);
 assert(!controls.Handle(4,1,next,command,99,[](bool,Dict,const std::string&){}));
 // Deduplication must not orphan the second observer's real body reservation.
 std::vector<Dict>body_events;suitecut::NativeContextNetworkControls journal([&](Dict record){body_events.push_back(record->GetDictionary("network")->Copy(false));});
 Controls bodies([&](int context,Dict actual_worker,Dict event){assert(journal.ObserveWorker(context,actual_worker,event));});
 for(const auto&owner:{"page:1","broker:2"})assert(bodies.Attach(1,owner,std::string(owner)=="broker:2",[](int,Dict,double,Controls::Reply){},[](const std::string&,const std::string&,double,auto){},[](int){}));
 auto page_body=Event("request","4","page-session");page_body->GetDictionary("request")->SetString("postDataBodyId","17");
 auto broker_body=Event("request","1","broker-session");broker_body->GetDictionary("request")->SetString("postDataBodyId","23");
 assert(bodies.Observe(1,"page:1",worker,page_body)&&bodies.Observe(1,"broker:2",worker,broker_body));
 assert(journal.records()==2&&!journal.Wait(99,1,"requestupdated",0,99,[](bool,Dict,const std::string&){}));
 assert(body_events.size()==2&&body_events[0]->GetString("event")=="request"&&body_events[1]->GetString("event")=="requestupdated");
 assert(body_events[0]->GetDictionary("request")->GetString("id")==body_events[1]->GetDictionary("request")->GetString("id"));
 assert(body_events[1]->GetDictionary("request")->GetString("postDataOwner")=="broker:2"&&body_events[1]->GetDictionary("request")->GetString("postDataBodyId")=="23");

}
