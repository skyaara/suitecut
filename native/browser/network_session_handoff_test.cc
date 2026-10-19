// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>events;std::vector<std::string>moves;
  Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict e){events.push_back(e);},[](const std::string&){assert(false);},[]{},[]{return 0;});
  controls.SetSessionHandoff([&](const std::string&id,const std::string&old,const std::string&now){moves.push_back(id+"|"+old+"|"+now);});
  auto request=[](const char*id,const char*frame){auto d=Dict(CefDictionaryValue::Create());d->SetString("requestId",id);d->SetString("frameId",frame);d->SetString("type","Document");auto r=CefDictionaryValue::Create();r->SetString("url","https://same-url.test/");r->SetString("method","GET");r->SetDictionary("headers",CefDictionaryValue::Create());d->SetDictionary("request",r);return d;};
  auto child=request("first-native-id","real-child-frame");controls.Event("Network.requestWillBeSent",child,"");const auto first=events.back()->GetDictionary("request")->GetString("id");
  auto r=CefDictionaryValue::Create();r->SetString("url","https://same-url.test/");r->SetInt("status",200);r->SetDictionary("headers",CefDictionaryValue::Create());child->SetDictionary("response",r->Copy(false));child->SetBool("hasExtraInfo",false);controls.Event("Network.responseReceived",child,"");
  assert(controls.FrameSessionAttached("actual-child","","real-child-frame"));controls.Event("Network.dataReceived",child,"actual-child");assert(moves.size()==1&&moves.back()=="first-native-id||actual-child");
  controls.Event("Network.loadingFinished",child,"actual-child");assert(events.back()->GetString("event")=="requestfinished");assert(events.back()->GetDictionary("request")->GetString("id")==first);assert(events.back()->GetDictionary("response")->GetString("session")=="actual-child");
  auto merge=request("merge-native-id","real-child-frame");controls.Event("Network.requestWillBeSent",merge,"actual-child");const auto second=events.back()->GetDictionary("request")->GetString("id");merge->SetDictionary("response",r->Copy(false));merge->SetBool("hasExtraInfo",true);controls.Event("Network.responseReceived",merge,"actual-child");
  const auto before=events.size();controls.SessionDetached("actual-child");assert(events.size()==before); // Detach is no fabricated loadingFailed.
  // ExtraInfo identifies a request but cannot choose the response body channel.
  auto extra=CefDictionaryValue::Create();extra->SetString("requestId","merge-native-id");auto headers=CefDictionaryValue::Create();headers->SetString("Actual-Wire","yes");extra->SetDictionary("headers",headers);controls.Event("Network.responseReceivedExtraInfo",extra,"");assert(moves.size()==1);assert(events.back()->GetDictionary("response")->GetString("session")=="actual-child");
  controls.Event("Network.loadingFinished",merge,"");assert(moves.size()==2&&moves.back()=="merge-native-id|actual-child|");assert(events.back()->GetString("event")=="requestfinished");assert(events.back()->GetDictionary("request")->GetString("id")==second);assert(events.back()->GetDictionary("response")->GetString("session")=="");assert(events.back()->GetDictionary("response")->GetDictionary("headers")->GetString("Actual-Wire")=="yes");
  // A matching URL/ID in an unrelated actual frame cannot steal a request.
  assert(controls.FrameSessionAttached("unrelated","","different-frame"));auto scoped=request("scoped-id","real-child-frame");controls.Event("Network.requestWillBeSent",scoped,"");const auto scoped_count=events.size();controls.Event("Network.loadingFinished",scoped,"unrelated");assert(events.size()==scoped_count);controls.Event("Network.loadingFinished",scoped,"");assert(events.back()->GetString("event")=="requestfinished");
  // An ordinary worker/channel detach retains existing failure behavior.
  auto worker=request("worker-id","worker-frame");controls.Event("Network.requestWillBeSent",worker,"worker-session");controls.SessionDetached("worker-session");assert(events.back()->GetString("event")=="requestfailed");
}
