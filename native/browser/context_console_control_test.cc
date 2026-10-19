// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include "include/cef_api_hash.h"
#include "context_console_controls.h"
int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  using Controls=suitecut::NativeContextConsoleControls;
  size_t emitted=0,replies=0;Controls controls([&](Controls::Dict record){++emitted;assert(record->GetInt("pageId")>0);});
  auto console=CefDictionaryValue::Create();console->SetDouble("sequence",9);console->SetString("text","actual native event metadata");
  assert(!controls.Observe(0,1,console));assert(controls.sequence()==0);
  auto reply=[&](bool ok,Controls::Dict record,const std::string& error){assert(ok&&record->GetInt("contextId")==2&&record->GetInt("pageId")==7&&record->GetDictionary("console")->GetDouble("sequence")==9&&error.empty());++replies;};
  assert(controls.Wait(1,2,0,135,reply));assert(controls.Wait(2,3,0,135,[&](bool,Controls::Dict,const std::string&){assert(false);}));
  assert(controls.Observe(2,7,console));assert(replies==1&&emitted==1&&controls.waiters()==1);
  console->SetString("text","caller mutated after native journal copy");
  assert(controls.Wait(3,2,0,135,[&](bool ok,Controls::Dict record,const std::string&){assert(ok&&record->GetDictionary("console")->GetString("text")=="actual native event metadata");++replies;}));
  controls.Cancel(2);assert(controls.waiters()==0);
  assert(controls.Wait(4,2,1,135,[&](bool ok,Controls::Dict record,const std::string& error){assert(!ok&&!record&&error.find("deadline")!=std::string::npos);++replies;}));
  controls.Tick(134);assert(controls.waiters()==1);controls.Tick(135);assert(controls.waiters()==0&&replies==3);
  assert(controls.Wait(5,3,1,0,[&](bool ok,Controls::Dict record,const std::string& error){assert(!ok&&!record&&error.find("closed")!=std::string::npos);++replies;}));
  controls.Closed(2);assert(controls.records()==0&&controls.waiters()==1);controls.Closed(3);assert(controls.waiters()==0&&replies==4);
  for(int i=0;i<600;++i){assert(controls.Observe(4,8,console));}
  assert(controls.records()==512&&controls.bytes()<=8388608);
  assert(controls.Wait(6,4,0,0,[&](bool ok,Controls::Dict record,const std::string& error){assert(!ok&&!record&&error.find("capacity")!=std::string::npos);++replies;}));
  assert(!controls.Wait(6,4,controls.sequence()+1,0,reply));
  controls.Clear();assert(controls.waiters()==0&&controls.records()==0&&controls.bytes()==0);
  Controls worker_controls([](Controls::Dict record){assert(record->GetType("pageId")==VTYPE_NULL&&record->GetInt("contextId")==11);});
  auto worker_console=CefDictionaryValue::Create();worker_console->SetDouble("sequence",1);worker_console->SetString("worker","actual-worker-id");
  assert(!worker_controls.ObserveWorker(11,worker_console));
  auto worker=CefDictionaryValue::Create();worker->SetString("id","actual-worker-id");worker->SetString("type","service_worker");worker_console->SetDictionary("workerInfo",worker);
  bool owned_reply=false;assert(worker_controls.Wait(91,11,0,135,[&](bool ok,Controls::Dict record,const std::string& error){assert(ok&&error.empty()&&record->GetType("pageId")==VTYPE_NULL&&record->GetDictionary("console")->GetString("worker")=="actual-worker-id");owned_reply=true;}));
  assert(worker_controls.Wait(92,12,0,135,[](bool,Controls::Dict,const std::string&){assert(false);}));
  assert(worker_controls.ObserveWorker(11,worker_console));assert(owned_reply&&worker_controls.waiters()==1);
  worker_controls.Cancel(92);assert(worker_controls.waiters()==0);
  worker=worker_console->GetDictionary("workerInfo");worker->SetString("type","iframe");assert(!worker_controls.ObserveWorker(11,worker_console));
  worker->SetString("type","shared_worker");worker->SetString("id","foreign-worker-id");assert(!worker_controls.ObserveWorker(11,worker_console));
  assert(worker_controls.sequence()==1);worker_controls.Closed(11);assert(worker_controls.records()==0&&worker_controls.bytes()==0);

}
