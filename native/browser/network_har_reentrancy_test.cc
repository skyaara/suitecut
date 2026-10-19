// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Controls=suitecut::NetworkControls;
using Dict=Controls::Dict;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 for(const int boundary:{0,1,2}){
  double now=1;int failed=0,rejected=0;bool boundary_observed=false;
  struct Read{suitecut::NativeNetworkPostPreparation::Data data;suitecut::NativeNetworkPostPreparation::Receipt receipt;};std::vector<Read>reads;
  std::unique_ptr<Controls>owner;
  owner=std::make_unique<Controls>([&](const auto&,const std::string&method,Dict,auto done){if(method=="Fetch.failRequest")++rejected;done(true,"");},[](Dict){},[&](const std::string&error){assert(error.find("original")!=std::string::npos);++failed;if(boundary==0)owner->Clear();else if(boundary==1)owner->SessionDetached("");else {owner->HARWorkChannelClosed();owner.reset();}boundary_observed=true;},[]{},[&]{return now;},[](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;});
  owner->SetRequestBodyReader([&](const auto&,const auto&,double,auto data,auto receipt){reads.push_back({std::move(data),std::move(receipt)});});
  auto archive=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(archive->Load(Parse(R"({"log":{"entries":[]}})"),error));
  auto policy=suitecut::NativeHARRouteRegistration::Create(archive,false);std::weak_ptr<const suitecut::NativeHARRouteRegistration>weak=policy;
  assert(owner->BindReplayPolicy("actual-har","**",policy,[](bool ok,const auto&){assert(ok);}));policy.reset();
  for(const char*id:{"actual-one","actual-two"}){
   auto pause=Parse(R"({"resourceType":"Fetch","request":{"url":"https://native.test/actual","method":"POST","headers":{},"hasPostData":true,"postDataEntries":[{}]}})");pause->SetString("requestId",id);pause->SetString("networkId",id);owner->Event("Fetch.requestPaused",pause,"");
  }
  assert(reads.size()==2&&failed==0&&rejected==0);now=30002;
  std::cout<<"Actual timeout boundary "<<boundary<<": two admitted HAR POST preparations, synchronous rejection receipt"<<std::endl;
  owner->Tick(now);
  assert(boundary_observed&&failed==1&&rejected==1&&!weak.expired());
  // The controlled host now supplies its real channel-close boundary before
  // fixture teardown. Logical Clear/destruction alone is not that receipt.
  if(owner){owner->HARWorkChannelClosed();owner->Clear();}
  reads[0].receipt();assert(!weak.expired());reads[1].receipt();assert(weak.expired());
 }
 assert(suitecut::NativeHARRouteRegistration::retained_policies()==0);
 std::cout<<"Native HAR timeout reentrancy: Clear/session-detach/destruction and physical receipt groups passed\n";
}
