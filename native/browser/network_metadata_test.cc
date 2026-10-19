// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#ifndef SUITECUT_NETWORK_CONTROL_HEADER
#define SUITECUT_NETWORK_CONTROL_HEADER "network_controls.h"
#endif
#include SUITECUT_NETWORK_CONTROL_HEADER

namespace {
void JournalBounds(size_t payload){
  using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
  std::deque<std::pair<Dict,size_t>> expected;size_t bytes=0;
  Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict value){
    const auto size=Controls::Json(value).size();expected.push_back({value->Copy(false),size});bytes+=size;
    while(expected.size()>256||bytes>4194304){bytes-=expected.front().second;expected.pop_front();}
    // The public consumer must not be able to change a retained journal entry.
    value->SetString("consumerMutation",std::string(16384,'m'));
  },[](const std::string&){assert(false);},[]{},[]{return 0;});
  for(int i=0;i<300;++i){
    auto data=CefDictionaryValue::Create();data->SetString("requestId","journal-"+std::to_string(i));data->SetString("type","Fetch");
    auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/journal");request->SetString("method","GET");auto headers=CefDictionaryValue::Create();headers->SetString("large",std::string(payload,'h'));request->SetDictionary("headers",headers);data->SetDictionary("request",request);
    controls.Event("Network.requestWillBeSent",data,"");controls.Event("Network.loadingFinished",data,"");
  }
  auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(size_t(diagnostics->GetInt("networkJournal"))==expected.size());
  assert(!expected.empty());const auto first=expected.front().first->GetDouble("sequence");assert(first>2);
  auto query=CefDictionaryValue::Create();query->SetString("action","waitEvent");query->SetString("event","request");query->SetDouble("afterSequence",first-2);
  bool rejected=false;controls.Command(900,query,[&](bool ok,const std::string&error,const std::string&){assert(!ok&&error.find("journal capacity")!=std::string::npos);rejected=true;});assert(rejected);
  query->SetDouble("afterSequence",first-1);bool found=false;
  const auto match=std::find_if(expected.begin(),expected.end(),[](const auto&entry){return entry.first->GetString("event")=="request";});assert(match!=expected.end());
  controls.Command(901,query,[&](bool ok,const std::string&,const std::string&json){assert(ok&&json==Controls::Json(match->first));found=true;});assert(found);
  controls.Clear();diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(diagnostics->GetInt("networkJournal")==0);
}
}

int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
  std::vector<Dict> events;
  Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict value){events.push_back(value);},[](const std::string&){assert(false);},[]{},[]{return 0;});
  auto data=CefDictionaryValue::Create();data->SetString("requestId","real-native-request");data->SetString("type","Other");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/sse");request->SetString("method","GET");request->SetDictionary("headers",CefDictionaryValue::Create());data->SetDictionary("request",request);controls.Event("Network.requestWillBeSent",data,"");
  const auto id=events.back()->GetDictionary("request")->GetString("id");
  auto query=CefDictionaryValue::Create();query->SetString("requestId",id);query->SetString("action","requestHeaders");bool request_ready=false;
  controls.Command(1,query,[&](bool ok,const std::string&,const std::string& json){assert(ok);auto parsed=CefParseJSON(json,JSON_PARSER_RFC);assert(parsed->GetDictionary()->GetString("User-Agent")=="actual-wire-agent");request_ready=true;});assert(!request_ready);
  auto extra=CefDictionaryValue::Create();extra->SetString("requestId","real-native-request");auto headers=CefDictionaryValue::Create();headers->SetString("User-Agent","actual-wire-agent");extra->SetDictionary("headers",headers);controls.Event("Network.requestWillBeSentExtraInfo",extra,"");assert(!request_ready);
  auto response=CefDictionaryValue::Create();response->SetString("url","https://native.test/sse");response->SetInt("status",200);response->SetDictionary("headers",CefDictionaryValue::Create());data->SetDictionary("response",response);data->SetString("type","EventSource");data->SetBool("hasExtraInfo",true);controls.Event("Network.responseReceived",data,"");assert(request_ready);assert(events.back()->GetDictionary("request")->GetString("resourceType")=="EventSource");
  query->SetString("action","responseHeaders");bool response_ready=false;controls.Command(2,query,[&](bool ok,const std::string&,const std::string& json){assert(ok);auto parsed=CefParseJSON(json,JSON_PARSER_RFC);assert(parsed->GetDictionary()->GetString("Set-Cookie")=="a=1\nb=2");response_ready=true;});assert(!response_ready);
  headers=CefDictionaryValue::Create();headers->SetString("Set-Cookie","a=1\nb=2");extra->SetDictionary("headers",headers);controls.Event("Network.responseReceivedExtraInfo",extra,"");assert(response_ready);
  data->SetString("blockedReason","mixed-content");data->SetString("errorText","net::ERR_FAILED");controls.Event("Network.loadingFailed",data,"");assert(events.back()->GetString("errorText")=="mixed-content");
  data->SetString("requestId","genuine-inspector-abort");controls.Event("Network.requestWillBeSent",data,"");data->SetString("blockedReason","inspector");data->SetString("errorText","net::ERR_BLOCKED_BY_CLIENT");controls.Event("Network.loadingFailed",data,"");assert(events.back()->GetString("errorText")=="net::ERR_BLOCKED_BY_CLIENT");assert(events.back()->GetDictionary("request")->GetString("blockedReason")=="inspector");
  // A header wait is genuinely cancelable without consuming a later field event.
  data->SetString("requestId","cancel-real-request");data->SetString("type","Fetch");controls.Event("Network.requestWillBeSent",data,"");query->SetString("requestId",events.back()->GetDictionary("request")->GetString("id"));query->SetString("action","requestHeaders");controls.Command(3,query,[](bool,const std::string&,const std::string&){assert(false);});controls.Cancel(3);extra->SetString("requestId","cancel-real-request");controls.Event("Network.requestWillBeSentExtraInfo",extra,"");
  JournalBounds(64);JournalBounds(30720);
}
