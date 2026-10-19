// SPDX-License-Identifier: MIT
#include "network_controls.h"
#include "include/cef_api_hash.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
Dict Parse(const std::string&text){auto value=CefParseJSON(text,JSON_PARSER_RFC);assert(value);return value->GetDictionary()->Copy(false);}
struct Call{std::string method;Dict params;Controls::Result receipt;double deadline=0;};
struct Fixture{
  double now=0;int owner=1;std::vector<Call>calls;std::vector<Dict>events;std::vector<std::string>failures;Controls controls;
  Fixture():controls([this](const std::string&,const std::string&method,Dict params,Controls::Result receipt){
    if(method=="Fetch.enable"||method=="Fetch.disable"||method=="Network.setCacheDisabled")receipt(true,"");else calls.push_back({method,params->Copy(false),std::move(receipt)});
  },[this](Dict event){events.push_back(event->Copy(false));},[this](const auto&error){failures.push_back(error);},[]{},[this]{return now;}){controls.SessionAttached("");controls.SetBoundedSender([this](const std::string&session,const std::string&method,Dict params,Controls::Result receipt,double deadline){if(method=="Fetch.enable"||method=="Fetch.disable"||method=="Network.setCacheDisabled"){receipt(true,"");return;}assert(deadline==0||deadline>now);(void)session;calls.push_back({method,params->Copy(false),std::move(receipt),deadline});});}
  ~Fixture(){controls.Clear();}
  void Register(const char*id,bool host,int times=0){auto config=Parse(R"({"action":"registerRoute","url":"**/*"})");config->SetString("registrationId",id);config->SetInt("times",times);if(host)config->SetBool("hostMatcher",true);bool done=false;controls.Command(owner++,config,[&](bool ok,const auto&,const auto&){assert(ok);done=true;});assert(done);}
  Dict Pause(const char*fetch,const char*native="native-parent",const char*method="GET"){auto value=Parse(R"({"resourceType":"XHR","frameId":"actual-frame","request":{"url":"https://native.test/original","method":"GET","headers":{}}})");value->SetString("requestId",fetch);value->SetString("networkId",native);value->GetDictionary("request")->SetString("method",method);if(std::string(method)=="OPTIONS")value->GetDictionary("request")->GetDictionary("headers")->SetString("Origin","https://author.test");return value;}
  Dict Last(const char*kind){for(auto it=events.rbegin();it!=events.rend();++it)if((*it)->GetString("event")==kind)return (*it)->Copy(false);assert(false);return nullptr;}
  static uint64_t Counter(Dict dict,const char*name){const auto text=dict->GetString(name).ToString();uint64_t value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);assert(parsed.ec==std::errc{});return value;}
  void Start(Dict event,const char*field){auto lease=event->GetDictionary(field);assert(suitecut::GlobalRouteCallbackLeases().Start(Counter(lease,"callbackToken"),Counter(lease,"callbackScope"),lease->GetString("id")));}
  void Finish(Dict event,const char*field){auto lease=event->GetDictionary(field);assert(suitecut::GlobalRouteCallbackLeases().Finish(Counter(lease,"callbackToken"),Counter(lease,"callbackScope"),lease->GetString("id")));}
  bool Decide(Dict event,std::optional<bool>matched){auto info=event->GetDictionary("matcher");auto config=info->Copy(false);config->SetString("action","matchRoute");config->SetString("routeId",info->GetString("id"));if(matched)config->SetBool("matched",*matched);else config->SetBool("invalid",true);bool called=false,success=false;controls.Command(owner++,config,[&](bool ok,const auto&,const auto&){called=true;success=ok;});assert(called);return success;}
  void Ack(){while(!calls.empty()){auto call=std::move(calls.front());calls.erase(calls.begin());call.receipt(true,"");}}
  Dict Will(const char*native,const char*method,bool preflight=false){auto value=Parse(R"({"frameId":"actual-frame","loaderId":"actual-loader","type":"Fetch","request":{"url":"https://native.test/original","method":"GET","headers":{}}})");value->SetString("requestId",native);value->GetDictionary("request")->SetString("method",method);if(preflight)value->SetDictionary("initiator",Parse(R"({"type":"preflight","requestId":"native-parent"})"));return value;}
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  {
    Fixture f;f.Register("glob",false);f.Register("host",true,1);f.controls.Event("Fetch.requestPaused",f.Pause("actual-fetch"),"");auto matcher=f.Last("routematch");assert(!f.Decide(matcher,true));f.Start(matcher,"matcher");assert(!f.Decide(matcher,true));f.Finish(matcher,"matcher");assert(f.Decide(matcher,false));assert(f.Last("route")->GetDictionary("route")->GetString("registrationId")=="glob");
    f.controls.Event("Fetch.requestPaused",f.Pause("second-fetch","second-network"),"");matcher=f.Last("routematch");f.Start(matcher,"matcher");f.Finish(matcher,"matcher");assert(f.Decide(matcher,true));assert(f.Last("route")->GetDictionary("route")->GetString("registrationId")=="host");f.Ack();
  }
  {
    Fixture f;f.Register("host",true);f.Register("top",false);f.controls.Event("Fetch.requestPaused",f.Pause("fallback-fetch"),"");auto route=f.Last("route");f.Start(route,"route");auto config=Parse(R"({"action":"fallback","url":"https://changed.test/new"})");config->SetString("routeId",route->GetDictionary("route")->GetString("id"));int replies=0;
    f.controls.Command(51,config,[&](bool,const auto&,const auto&){++replies;},100,7,1);auto old=f.Last("routematch");assert(old->GetDictionary("matcher")->GetString("url")=="https://native.test/original");f.Start(old,"matcher");assert(!replies);f.controls.Cancel(51);assert(f.calls.size()==1&&f.calls[0].method=="Fetch.failRequest"&&f.calls[0].params->GetString("errorReason")=="Aborted");
    // Original callbacks remain real running receipts after logical cancellation.
    f.Finish(route,"route");assert(suitecut::GlobalRouteCallbackLeases().running()>=1);
    f.controls.Event("Fetch.requestPaused",f.Pause("replacement-fetch","replacement-network"),"");auto replacement=f.Last("route");config->SetString("routeId",replacement->GetDictionary("route")->GetString("id"));f.controls.Command(51,config,[&](bool ok,const auto&,const auto&){assert(ok);++replies;},100,8,1);auto current=f.Last("routematch");
    f.Finish(old,"matcher");assert(!f.Decide(old,true));assert(!replies);f.Start(current,"matcher");f.Finish(current,"matcher");assert(f.Decide(current,false));assert(f.calls.back().method=="Fetch.continueRequest"&&f.calls.back().params->GetString("url")=="https://changed.test/new");f.Ack();assert(replies==1);
  }
  {
    Fixture f;f.Register("host",true);f.Register("top",false);f.controls.Event("Fetch.requestPaused",f.Pause("deadline-fetch"),"");auto route=f.Last("route");auto config=Parse(R"({"action":"fallback"})");config->SetString("routeId",route->GetDictionary("route")->GetString("id"));int replies=0;f.controls.Command(11,config,[&](bool ok,const auto&,const auto&){assert(!ok);++replies;},5,1,1);auto matcher=f.Last("routematch");f.Start(matcher,"matcher");f.now=5;f.controls.Tick(5);assert(f.calls.size()==1&&f.calls[0].method=="Fetch.failRequest"&&f.calls[0].params->GetString("errorReason")=="TimedOut");assert(!replies);f.Ack();assert(replies==1);f.Finish(matcher,"matcher");assert(!f.Decide(matcher,true));
  }
  {
    Fixture f;f.Register("host",true);f.Register("top",false);f.controls.Event("Fetch.requestPaused",f.Pause("late-wire-ack"),"");auto route=f.Last("route");
    auto config=Parse(R"({"action":"fallback"})");config->SetString("routeId",route->GetDictionary("route")->GetString("id"));int replies=0;
    f.controls.Command(11,config,[&](bool ok,const auto&error,const auto&){assert(!ok&&error.find("original operation deadline")!=std::string::npos);++replies;},5,1,1);
    auto matcher=f.Last("routematch");f.Start(matcher,"matcher");f.Finish(matcher,"matcher");f.now=4;assert(f.Decide(matcher,false));
    assert(f.calls.size()==1&&f.calls[0].method=="Fetch.continueRequest"&&f.calls[0].deadline==5&&!replies);
    f.now=6;f.Ack();assert(replies==1&&f.calls.empty()); // real ACK arrives before any expiry Tick
  }
  {
    Fixture f;f.Register("host",true,1);f.controls.Event("Network.requestWillBeSent",f.Will("native-parent","DELETE"),"");f.controls.Event("Network.requestWillBeSent",f.Will("native-preflight","OPTIONS",true),"");f.controls.Event("Fetch.requestPaused",f.Pause("child-pause","native-preflight","OPTIONS"),"");assert(f.calls.empty());
    f.controls.Event("Fetch.requestPaused",f.Pause("parent-pause","native-parent","DELETE"),"");auto matcher=f.Last("routematch");f.Start(matcher,"matcher");assert(f.calls.empty());f.Finish(matcher,"matcher");assert(f.Decide(matcher,true));assert(f.calls.size()==1&&f.calls[0].method=="Fetch.fulfillRequest"&&f.calls[0].params->GetInt("responseCode")==204&&f.calls[0].params->GetString("requestId")=="child-pause");f.Ack();
  }
  {
    Fixture f;f.Register("host",true);f.controls.Event("Network.requestWillBeSent",f.Will("native-parent","DELETE"),"");f.controls.Event("Network.requestWillBeSent",f.Will("native-preflight","OPTIONS",true),"");f.controls.Event("Fetch.requestPaused",f.Pause("child-pause","native-preflight","OPTIONS"),"");assert(f.calls.empty());f.controls.Event("Fetch.requestPaused",f.Pause("parent-pause","native-parent","DELETE"),"");auto matcher=f.Last("routematch");f.Start(matcher,"matcher");assert(f.Decide(matcher,std::nullopt));assert(f.calls.size()==2);for(const auto&call:f.calls)assert(call.method=="Fetch.failRequest");f.Finish(matcher,"matcher");f.Ack();
  }
  assert(suitecut::GlobalRouteCallbackLeases().size()==0);std::cout<<"Native matcher adapter: admission, original URL, cancel/reuse, original deadline and exact preflight parent groups passed\n";
}
