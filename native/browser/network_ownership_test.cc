// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "network_controls.h"

namespace {
using Controller=suitecut::NetworkControls;
using Dict=Controller::Dict;
struct Call {std::string session,method;Dict params;Controller::Result callback;};
struct Fixture {
  double now=0;
  std::deque<Call> calls;
  std::vector<Dict> events;
  std::vector<std::string> errors;
  Controller controller{
    [this](const std::string& session,const std::string& method,Dict params,Controller::Result callback){calls.push_back({session,method,params,std::move(callback)});},
    [this](Dict event){events.push_back(event);},
    [this](const std::string& error){errors.push_back(error);},[] {},[this] {return now;}};
  Dict Oversized(const std::string& id) {
    auto data=CefDictionaryValue::Create();data->SetString("requestId",id);
    auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/large");request->SetString("method","POST");request->SetString("postData",std::string(1048577,'x'));
    data->SetDictionary("request",request);return data;
  }
  int Pending() {auto result=CefDictionaryValue::Create();controller.Diagnostics(result);return result->GetInt("networkPaused");}
  void Ack(bool ok=true) {assert(!calls.empty());auto call=std::move(calls.front());calls.pop_front();call.callback(ok,ok?"":"actual dispatch failure");}
};
}

int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  {
    Fixture f;f.controller.RejectOversizedPause(f.Oversized("physical1"),"child");
    assert(f.Pending()==1);assert(f.controller.NeedsTick());assert(f.errors.empty());
    assert(f.calls.size()==1);assert(f.calls.front().method=="Fetch.failRequest");
    assert(f.calls.front().session=="child");assert(f.calls.front().params->GetString("requestId")=="physical1");
    assert(f.calls.front().params->GetString("errorReason")=="BlockedByClient");
    assert(f.events.size()==1 && f.events.front()->GetString("event")=="routeerror");
    assert(Controller::Json(f.events.front()).size()<4096);
    f.controller.RejectOversizedPause(f.Oversized("physical1"),"child");assert(f.calls.size()==1);
    f.Ack();assert(f.Pending()==0 && !f.controller.NeedsTick());assert(f.errors.empty());
  }
  {
    Fixture f;f.controller.RejectOversizedPause(f.Oversized("physical2"),"");f.Ack(false);
    assert(f.Pending()==1);assert(f.errors.size()==1 && f.errors.front().find("actual dispatch failure")!=std::string::npos);
    f.controller.SessionDetached("");assert(f.Pending()==0);
  }
  {
    Fixture f;f.controller.RejectOversizedPause(f.Oversized("physical3"),"child");
    f.now=29999;f.controller.Tick(f.now);assert(f.errors.empty() && f.Pending()==1);
    f.now=30000;f.controller.Tick(f.now);assert(f.errors.size()==1 && f.Pending()==1);
    f.controller.Tick(f.now+1);assert(f.errors.size()==1 && f.Pending()==1);
    f.controller.Clear();f.Ack();assert(f.Pending()==0 && !f.controller.NeedsTick());
  }
  {
    Fixture f;f.controller.RejectOversizedPause(f.Oversized("physical4"),"child");
    f.controller.SessionDetached("child");assert(f.Pending()==0);f.Ack(false);assert(f.errors.empty());
  }
  {
    Fixture f;bool registered=false;
    auto route=CefDictionaryValue::Create();route->SetString("action","registerRoute");route->SetString("registrationId","owner");route->SetString("url","**/*");
    f.controller.Command(1,route,[&](bool ok,const std::string&,const std::string&){registered=ok;});
    assert(f.calls.size()==2);f.Ack();assert(!registered);f.Ack();assert(registered);
    auto headers=CefDictionaryValue::Create();headers->SetString("action","setExtraHTTPHeaders");headers->SetDictionary("headers",CefDictionaryValue::Create());
    f.controller.Command(2,headers,[](bool ok,const std::string&,const std::string&){assert(ok);});f.Ack();
    auto offline=CefDictionaryValue::Create();offline->SetString("action","setOffline");offline->SetBool("offline",true);
    f.controller.Command(3,offline,[](bool ok,const std::string&,const std::string&){assert(ok);});f.Ack();
    bool initialized=false;
    f.controller.SessionAttached("child",[&](bool ok,const std::string&){assert(ok);initialized=true;});
    assert(f.calls.size()==4);f.Ack();assert(!initialized);f.Ack();assert(!initialized);f.Ack();assert(!initialized);f.Ack();assert(initialized);
  }
}
