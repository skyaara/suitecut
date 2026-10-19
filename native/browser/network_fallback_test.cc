// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#ifndef SUITECUT_NETWORK_CONTROL_HEADER
#define SUITECUT_NETWORK_CONTROL_HEADER "network_controls.h"
#endif
#include SUITECUT_NETWORK_CONTROL_HEADER

namespace {
using Controller=suitecut::NetworkControls;
using Dict=Controller::Dict;
struct Call {std::string session,method;Dict params;Controller::Result callback;};
struct Fixture {
  double now=0;std::deque<Call> calls;std::vector<Dict> events;std::vector<std::string> errors;
  Controller controller{
    [this](const std::string& session,const std::string& method,Dict params,Controller::Result callback){calls.push_back({session,method,params,std::move(callback)});},
    [this](Dict event){events.push_back(event);},[this](const std::string& error){errors.push_back(error);},[] {},[this]{return now;}};
  void Ack() {assert(!calls.empty());auto call=std::move(calls.front());calls.pop_front();call.callback(true,"");}
  void Drain() {for(size_t limit=0;!calls.empty() && limit<1000;++limit)Ack();assert(calls.empty());}
  void Register(const std::string& id,const std::string& pattern,int times=0) {
    auto config=CefDictionaryValue::Create();config->SetString("action","registerRoute");config->SetString("registrationId",id);config->SetString("url",pattern);config->SetInt("times",times);
    bool registered=false;controller.Command(1,config,[&](bool ok,const std::string&,const std::string&){assert(ok);registered=true;});Drain();assert(registered);
  }
  void Pause() {
    auto data=CefDictionaryValue::Create();data->SetString("requestId","physical-fetch");data->SetString("networkId","physical-network");data->SetString("resourceType","Fetch");
    auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/original");request->SetString("method","GET");request->SetDictionary("headers",CefDictionaryValue::Create());data->SetDictionary("request",request);
    controller.Event("Fetch.requestPaused",data,"child");
  }
  Dict LastRoute() {for(auto it=events.rbegin();it!=events.rend();++it)if((*it)->GetString("event")=="route")return *it;assert(false);return nullptr;}
  Dict Resolve(const std::string& action) {auto config=CefDictionaryValue::Create();config->SetString("action",action);config->SetString("routeId",LastRoute()->GetDictionary("route")->GetString("id"));return config;}
  void Command(Dict config) {bool complete=false;controller.Command(2,config,[&](bool ok,const std::string&,const std::string&){assert(ok);complete=true;});Drain();assert(complete);}
};
}
int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  {
    Fixture f;f.Register("first","**/original",1);f.Register("changed-url","**/changed");f.Register("last","**/original",1);f.Pause();
    auto original=f.LastRoute();assert(original->GetDictionary("route")->GetString("registrationId")=="last");assert(original->GetDictionary("route")->GetBool("exhausted"));
    auto fallback=f.Resolve("fallback");fallback->SetString("url","https://native.test/changed");fallback->SetString("method","POST");fallback->SetString("postData","AP8NCg==");
    const auto old_id=fallback->GetString("routeId").ToString();f.Command(fallback);
    auto next=f.LastRoute();assert(next->GetDictionary("route")->GetString("registrationId")=="first");assert(next->GetDictionary("route")->GetBool("exhausted"));
    assert(next->GetDictionary("route")->GetString("id")!=old_id);assert(next->GetDictionary("request")->GetString("url")=="https://native.test/changed");assert(next->GetDictionary("request")->GetString("method")=="POST");assert(next->GetDictionary("request")->GetString("postDataBase64")=="AP8NCg==");
    bool rejected=false;f.controller.Command(3,fallback,[&](bool ok,const std::string&,const std::string&){rejected=!ok;});assert(rejected);
    auto terminal=f.Resolve("fallback");bool complete=false;f.controller.Command(4,terminal,[&](bool ok,const std::string&,const std::string&){assert(ok);complete=true;});
    assert(f.calls.front().method=="Fetch.continueRequest");assert(f.calls.front().session=="child");assert(f.calls.front().params->GetString("requestId")=="physical-fetch");
    assert(f.calls.front().params->GetString("url")=="https://native.test/changed");assert(f.calls.front().params->GetString("method")=="POST");assert(f.calls.front().params->GetString("postData")=="AP8NCg==");f.Drain();assert(complete && f.errors.empty());
  }
  {
    Fixture f;f.Register("removed","**/*");f.Register("first","**/*");f.Pause();
    auto remove=CefDictionaryValue::Create();remove->SetString("action","removeRoute");remove->SetString("registrationId","removed");f.Command(remove);
    f.Register("late","**/*");const auto before=f.events.size();f.Command(f.Resolve("fallback"));
    assert(f.events.size()==before);assert(f.errors.empty());
  }
  {
    Fixture f;f.Register("first","**/*");f.Register("last","**/*");f.Pause();f.now=29999;f.Command(f.Resolve("fallback"));
    f.now=30000;f.controller.Tick(f.now);assert(f.calls.front().method=="Fetch.failRequest");assert(f.events.back()->GetString("event")=="routeexpired");
    auto diagnostics=CefDictionaryValue::Create();f.controller.Diagnostics(diagnostics);assert(diagnostics->GetInt("networkPaused")==0);assert(diagnostics->GetDouble("networkPausedMetadataBytes")==0);
  }
  {
    Fixture f;f.Register("only","**/*");f.Pause();auto terminal=f.Resolve("fallback");
    const std::string body(900000,'x');terminal->SetString("postData",CefBase64Encode(body.data(),body.size()));
    const auto events=f.events.size();f.Command(terminal);assert(f.events.size()==events && f.errors.empty());
  }
  {
    Fixture f;f.Register("first","**/*",1);f.Register("last","**/*",1);f.Pause();auto large=f.Resolve("fallback");
    const std::string body(900000,'x');large->SetString("postData",CefBase64Encode(body.data(),body.size()));
    bool rejected=false;f.controller.Command(2,large,[&](bool ok,const std::string& error,const std::string&){rejected=!ok && error.find("remains owned")!=std::string::npos;});assert(rejected);
    auto diagnostics=CefDictionaryValue::Create();f.controller.Diagnostics(diagnostics);assert(diagnostics->GetInt("networkRoutes")==1 && diagnostics->GetInt("networkPaused")==1);
    f.Command(f.Resolve("fallback"));assert(f.LastRoute()->GetDictionary("route")->GetString("registrationId")=="first");assert(f.errors.empty());
  }
  {
    Fixture f;
    for(int index=0;index<40;++index) {
      auto data=CefDictionaryValue::Create();data->SetString("requestId",std::to_string(index));data->SetString("type","Fetch");
      auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/body");request->SetString("method","POST");request->SetString("postData",std::string(1000000,'x'));request->SetDictionary("headers",CefDictionaryValue::Create());data->SetDictionary("request",request);f.controller.Event("Network.requestWillBeSent",data,"");
      auto finished=CefDictionaryValue::Create();finished->SetString("requestId",std::to_string(index));f.controller.Event("Network.loadingFinished",finished,"");
    }
    auto diagnostics=CefDictionaryValue::Create();f.controller.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkRequestMetadataBytes")<=33554432 && diagnostics->GetInt("networkRequests")<40 && f.errors.empty());
    f.controller.Clear();f.controller.Diagnostics(diagnostics);assert(!diagnostics->GetDouble("networkRequestMetadataBytes"));
  }

}
