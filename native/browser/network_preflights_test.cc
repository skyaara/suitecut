// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "network_preflights.h"
using Controls=suitecut::NativeNetworkPreflights;using Dict=Controls::Dict;
static Dict Pause(const char*fetch="actual-fetch",const char*network="actual-network",const char*method="OPTIONS"){
  auto data=CefDictionaryValue::Create();data->SetString("requestId",fetch);data->SetString("networkId",network);
  auto request=CefDictionaryValue::Create();request->SetString("url","https://actual.test/cors");request->SetString("method",method);
  auto headers=CefDictionaryValue::Create();headers->SetString("Origin","https://actual-parent.test");headers->SetString("Access-Control-Request-Headers","x-genuine-header");request->SetDictionary("headers",headers);data->SetDictionary("request",request);return data;
}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  double now=0;std::vector<std::string>sent,failures,resumed;std::vector<Dict>params;std::vector<Controls::Result>receipts;
  auto send=[&](const std::string&session,const std::string&method,Dict data,Controls::Result done,double deadline){assert(session=="real-session");assert(deadline==100);sent.push_back(method);params.push_back(data);receipts.push_back(std::move(done));};
  auto resume=[&](Dict data,const std::string&session,suitecut::NativeRouteRegistry::Plan plan,double deadline){assert(session=="real-session"&&deadline==100&&plan.candidates==std::vector<std::string>{"actual-registration"});resumed.push_back(data->GetString("networkId"));};
  auto failure=[&](const std::string&error){failures.push_back(error);};
  suitecut::NativeRouteRegistry::Plan plan{{"actual-registration"},0};
  {
    Controls controls(send,resume,failure,[]{},[&]{return now;});auto pause=Pause();
    assert(controls.Hold(pause,"real-session",plan,100));pause->Clear(); // genuine detached ownership
    controls.Resolve([](const auto&,const auto&,const auto&){return Controls::Classification{};});assert(sent.empty()&&resumed.empty()&&controls.pending()==1);
    controls.Resolve([](const auto&network,const auto&session,const auto&candidate){assert(network=="actual-network"&&session=="real-session"&&candidate.candidates.size()==1);return Controls::Classification{true,true,true,"DELETE"};});
    assert(sent==std::vector<std::string>{"Fetch.fulfillRequest"}&&resumed.empty()&&controls.pending()==1);
    assert(params.back()->GetInt("responseCode")==204&&params.back()->GetString("requestId")=="actual-fetch");
    auto headers=params.back()->GetList("responseHeaders");assert(headers->GetDictionary(0)->GetString("value")=="https://actual-parent.test");assert(headers->GetDictionary(1)->GetString("value")=="DELETE");
    controls.Tick(100);assert(failures.size()==1&&controls.pending()==1&&Controls::global_pending()==1); // original logical expiry never fabricates a receipt
    receipts.back()(true,"");receipts.clear();assert(controls.pending()==0&&Controls::global_pending()==0);
  }
  sent.clear();params.clear();failures.clear();
  {
    Controls controls(send,resume,failure,[]{},[&]{return now;});assert(controls.Hold(Pause(),"real-session",plan,100));
    controls.Resolve([](const auto&,const auto&,const auto&){return Controls::Classification{true,false,false,{}};});
    assert(resumed==std::vector<std::string>{"actual-network"}&&sent.empty()&&controls.pending()==0&&Controls::global_pending()==0);
    assert(controls.Hold(Pause("real-second","actual-author","OPTIONS"),"real-session",plan,100));
    controls.Resolve([](const auto&,const auto&,const auto&){return Controls::Classification{true,false,false,{}};});assert(resumed.back()=="actual-author");
    assert(controls.Hold(Pause(),"real-session",plan,100));
    controls.Resolve([](const auto&,const auto&,const auto&){return Controls::Classification{true,true,false,{}};});
    assert(sent==std::vector<std::string>{"Fetch.continueRequest"}&&controls.pending()==1);receipts.back()(true,"");receipts.clear();
  }
  sent.clear();params.clear();resumed.clear();
  {
    auto controls=std::make_unique<Controls>(send,resume,failure,[]{},[&]{return now;});assert(controls->Hold(Pause(),"real-session",plan,100));
    controls->Resolve([&](const auto&,const auto&,const auto&){controls.reset();return Controls::Classification{true,false,false,{}};});assert(!controls&&resumed.empty()&&Controls::global_pending()==0);
  }
  {
    Controls controls(send,resume,failure,[]{},[&]{return now;});auto huge=Pause();huge->GetDictionary("request")->SetString("postData",std::string(524288,'x'));
    assert(!controls.Hold(huge,"real-session",plan,100)&&Controls::global_pending()==0);
    for(size_t i=0;i<64;++i){const auto id=std::to_string(i);assert(controls.Hold(Pause(id.c_str(),id.c_str()),"real-session",plan,100));}
    assert(!controls.Hold(Pause("over","over"),"real-session",plan,100)&&controls.pending()==64);controls.SessionClosed("unrelated");assert(controls.pending()==64);controls.SessionClosed("real-session");assert(controls.pending()==0&&Controls::global_pending()==0&&Controls::global_bytes()==0);
  }
  {
    Controls controls(send,resume,failure,[]{},[&]{return now;});assert(controls.Hold(Pause(),"real-session",plan,100));
    controls.Resolve([](const auto&,const auto&,const auto&){return Controls::Classification{true,true,true,"DELETE"};});
    controls.Clear();assert(controls.pending()==0&&Controls::global_pending()==1); // actual sender still retains the native completion
    receipts.back()(true,"");receipts.clear();assert(Controls::global_pending()==0&&Controls::global_bytes()==0);
  }
  {
    Controls controls(send,resume,failure,[]{},[&]{return now;});assert(controls.Hold(Pause(),"real-session",plan,100));
    controls.Resolve([](const auto&,const auto&,const auto&){return Controls::Classification{true,true,true,"DELETE"};});
    const auto before=failures.size();controls.SessionClosed("real-session");assert(controls.pending()==0&&Controls::global_pending()==1);
    receipts.back()(false,"Target closed");receipts.clear();assert(failures.size()==before&&Controls::global_pending()==0);
  }

}
