// SPDX-License-Identifier: MIT
#include "context_worker_network.h"
#include "include/cef_api_hash.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

int main(){
  using C=suitecut::NativeContextWorkerNetwork;using Dict=C::Dict;
  assert(cef_api_hash(CEF_API_VERSION,0));
  std::vector<C::Reply>receipts;std::vector<uint64_t>generations;int canceled=0,old_replies=0,new_replies=0;
  C controls([](int,Dict,Dict){});
  assert(controls.Attach(7,"actual-owned-channel",true,[&](int operation,Dict command,double deadline,C::Reply reply){
    assert(operation==13&&deadline==99);const auto generation=C::RouteOperationGeneration(command);assert(generation&&generation!=999);generations.push_back(generation);receipts.push_back(std::move(reply));
  },[](const std::string&,const std::string&,double,auto){assert(false);},[&](int operation){assert(operation==13);++canceled;}));
  auto config=CefDictionaryValue::Create();config->SetString("action","fallback");config->SetString("callbackScope","1");config->SetString("routeId","actual-pause");config->SetString("suitecutRouteOperation","999");
  assert(controls.HandleRoute(13,7,"actual-owned-channel",config,99,[&](bool,Dict,const auto&){++old_replies;}));
  assert(receipts.size()==1);controls.CancelOwner(13);assert(canceled==1&&receipts.size()==1); // sender still owns the original callback
  assert(controls.HandleRoute(13,7,"actual-owned-channel",config,99,[&](bool ok,Dict,const auto&){assert(ok);++new_replies;}));
  assert(generations.size()==2&&generations[0]!=generations[1]);
  auto first=std::move(receipts[0]);first(true,CefDictionaryValue::Create(),"");assert(!old_replies&&!new_replies);
  auto second=std::move(receipts[1]);second(true,CefDictionaryValue::Create(),"");assert(!old_replies&&new_replies==1);
  // A physical close invalidates the owner and never reassigns its late ACK.
  assert(controls.HandleRoute(13,7,"actual-owned-channel",config,99,[&](bool ok,Dict,const auto&){assert(!ok);++new_replies;}));
  controls.Detach("actual-owned-channel");assert(new_replies==2);
  auto closed=std::move(receipts[2]);closed(true,CefDictionaryValue::Create(),"");assert(new_replies==2);
  assert(!controls.HandleRoute(13,7,"actual-owned-channel",config,99,[](bool,Dict,const auto&){assert(false);}));
  controls.Clear();std::cout<<"Context worker route bridge: cancel/reuse/late ACK and real owner-close groups passed\n";
}
