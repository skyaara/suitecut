// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "context_network_controls.h"

int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::NativeContextNetworkControls;
  std::vector<Controls::Dict> events;Controls controls([&](Controls::Dict record){events.push_back(record);});
  auto event=CefDictionaryValue::Create();event->SetString("event","request");auto request=CefDictionaryValue::Create();request->SetString("id","actual-native-identity");event->SetDictionary("request",request);
  bool first=false,other=false;assert(controls.Wait(1,1,"request",0,30,[&](bool ok,Controls::Dict record,const std::string&){first=ok;assert(record->GetDictionary("network")->GetDictionary("request")->GetString("id")=="actual-native-identity");}));
  assert(controls.Wait(2,2,"request",0,30,[&](bool ok,Controls::Dict,const std::string&){other=ok;}));assert(controls.Observe(1,2,event));assert(first && !other && controls.waiters()==1);
  bool replay=false;assert(controls.Wait(3,1,"request",0,30,[&](bool ok,Controls::Dict,const std::string&){replay=ok;}));assert(replay);
  bool timeout=false;assert(controls.Wait(4,1,"response",1,10,[&](bool ok,Controls::Dict,const std::string& error){timeout=!ok && error.find("deadline")!=std::string::npos;}));controls.Tick(10);assert(timeout && controls.waiters()==1);
  bool closed=false;assert(controls.Wait(5,1,"response",1,0,[&](bool ok,Controls::Dict,const std::string& error){closed=!ok && error.find("closed")!=std::string::npos;}));controls.Closed(1);assert(closed && controls.records()==0);
  assert(!controls.Wait(6,1,"page",1,30,[](bool,Controls::Dict,const std::string&){}));
  assert(!controls.Wait(6,1,"request",2,30,[](bool,Controls::Dict,const std::string&){}));
  controls.Cancel(2);assert(controls.waiters()==0);
  for(int index=0;index<600;++index){assert(controls.Observe(2,3,event));}
  assert(controls.records()==512 && controls.bytes()<=8388608);
  bool lost=false;assert(controls.Wait(7,2,"request",0,30,[&](bool ok,Controls::Dict,const std::string& error){lost=!ok && error.find("journal capacity")!=std::string::npos;}));assert(lost);
  // Native completion removes ownership before a synchronous close callback.
  assert(controls.Wait(8,2,"response",static_cast<double>(controls.sequence()),30,[&](bool ok,Controls::Dict,const std::string&){assert(ok);controls.Clear();}));event->SetString("event","response");assert(controls.Observe(2,3,event));assert(controls.waiters()==0 && controls.records()==0);
  auto worker=CefDictionaryValue::Create();worker->SetString("id","actual-worker-target");worker->SetString("url","https://native.test/sw.js");worker->SetString("type","service_worker");
  const auto cursor=controls.sequence();bool worker_seen=false;
  assert(controls.Wait(9,9,"response",double(cursor),100,[&](bool ok,Controls::Dict value,const std::string&){worker_seen=ok;assert(!value->HasKey("pageId")&&value->GetDictionary("worker")->GetString("id")=="actual-worker-target");}));
  assert(controls.ObserveWorker(9,worker,event));assert(worker_seen&&controls.waiters()==0);
  controls.Closed(9);assert(controls.records()==0);

}
