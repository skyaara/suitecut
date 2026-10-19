// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "context_worker_network.h"
using Controls=suitecut::NativeContextWorkerNetwork;using Dict=Controls::Dict;
Dict Event(const char*kind,const char*local,const char*session,const char*previous=""){
 auto event=CefDictionaryValue::Create();event->SetString("event",kind);auto request=CefDictionaryValue::Create();request->SetString("id",local);request->SetString("session",session);request->SetString("requestId","844458.6");request->SetString("url","http://native.test/actual-worker-request");request->SetString("method","GET");request->SetString("resourceType","Fetch");request->SetString("frameId","");request->SetBool("isNavigationRequest",false);request->SetDictionary("headers",CefDictionaryValue::Create());if(*previous)request->SetString("redirectedFrom",previous);event->SetDictionary("request",request);return event;
}
int main(){
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
}
