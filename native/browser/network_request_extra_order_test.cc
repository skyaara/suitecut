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
using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
Dict Query(const std::string&id){auto value=Parse(R"({"action":"requestHeaders"})");value->SetString("requestId",id);return value;}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>events;double now=1;
 auto send=[](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");};
 auto emit=[&](Dict event){events.push_back(event->Copy(false));};
 auto failure=[](const std::string&){assert(false);};
 Controls controls(send,emit,failure,[]{},[&]{return now;});
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"same-id","type":"Document","request":{"url":"http://native.test/one","method":"GET","headers":{"Actual":"renderer-one"}}})"),"");
 const auto first=events.back()->GetDictionary("request")->GetString("id").ToString();
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"same-id","type":"Document","redirectHasExtraInfo":true,"redirectResponse":{"status":302,"headers":{}},"request":{"url":"http://native.test/two","method":"GET","headers":{"Actual":"renderer-two"}}})"),"");
 const auto second=events.back()->GetDictionary("request")->GetString("id").ToString();
 controls.Event("Network.responseReceived",Parse(R"({"requestId":"same-id","type":"Document","hasExtraInfo":true,"response":{"status":200,"headers":{}}})"),"");
 controls.Event("Network.loadingFinished",Parse(R"({"requestId":"same-id"})"),"");
 bool one=false,two=false;
 controls.Command(1,Query(first),[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")=="wire-one");one=true;});
 controls.Command(2,Query(second),[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")=="wire-two");two=true;});
 assert(!one&&!two); // Finished is not proof of its marked raw-header receipt.
 controls.Event("Network.requestWillBeSentExtraInfo",Parse(R"({"requestId":"same-id","headers":{"Actual":"wire-one"}})"),"");
 assert(one&&!two);
 controls.Event("Network.requestWillBeSentExtraInfo",Parse(R"({"requestId":"same-id","headers":{"Actual":"wire-two"}})"),"");
 assert(one&&two);auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkPendingHeaderBytes")==0);controls.Clear();
 // An early receipt may precede the marker that skips a cached/non-extra hop.
 events.clear();Controls early(send,emit,failure,[]{},[&]{return now;});
 early.Event("Network.requestWillBeSent",Parse(R"({"requestId":"early","request":{"url":"http://native.test/a","method":"GET","headers":{"Actual":"rendered-a"}}})"),"");
 const auto skipped=events.back()->GetDictionary("request")->GetString("id").ToString();
 early.Event("Network.requestWillBeSentExtraInfo",Parse(R"({"requestId":"early","headers":{"Actual":"wire-b"}})"),"");
 early.Event("Network.requestWillBeSent",Parse(R"({"requestId":"early","redirectHasExtraInfo":false,"redirectResponse":{"status":302,"headers":{}},"request":{"url":"http://native.test/b","method":"GET","headers":{}}})"),"");
 const auto final=events.back()->GetDictionary("request")->GetString("id").ToString();
 early.Command(3,Query(skipped),[](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")=="rendered-a");});
 early.Event("Network.responseReceived",Parse(R"({"requestId":"early","hasExtraInfo":true,"response":{"status":200,"headers":{}}})"),"");
 bool canceled=false;early.Command(4,Query(final),[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")=="wire-b");canceled=true;});assert(canceled);early.Clear();
 // The exact native handoff keeps old-observer receipts bound to the same hop.
 events.clear();Controls moved(send,emit,failure,[]{},[&]{return now;});
 moved.Event("Network.requestWillBeSent",Parse(R"({"requestId":"worker-target","request":{"url":"http://native.test/worker.js","method":"GET","headers":{}}})"),"");
 const auto moved_id=events.back()->GetDictionary("request")->GetString("id").ToString();
 assert(moved.WorkerScriptAttached("worker-target","","actual-child","http://native.test/worker.js"));
 moved.Event("Network.responseReceived",Parse(R"({"requestId":"worker-target","hasExtraInfo":true,"response":{"status":200,"headers":{}}})"),"actual-child");
 moved.Event("Network.loadingFinished",Parse(R"({"requestId":"worker-target"})"),"actual-child");
 bool canceled_called=false,observed=false;moved.Command(5,Query(moved_id),[&](bool,const std::string&,const std::string&){canceled_called=true;});moved.Cancel(5);
 moved.Command(6,Query(moved_id),[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")=="original-observer");observed=true;});
 moved.Event("Network.requestWillBeSentExtraInfo",Parse(R"({"requestId":"worker-target","headers":{"Actual":"original-observer"}})"),"");assert(observed&&!canceled_called);moved.Clear();
 // An accepted Fetch pause's actual headers can be read before wire continuation.
 events.clear();std::vector<std::string>sent;Controls paused([&](const std::string&,const std::string&method,Dict,Controls::Result done){sent.push_back(method);done(true,"");},emit,failure,[]{},[&]{return now;});
 paused.SessionAttached("");paused.Command(7,Parse(R"({"action":"registerRoute","registrationId":"actual-route","url":"**"})"),[](bool ok,const std::string&,const std::string&){assert(ok);});
 paused.Event("Fetch.requestPaused",Parse(R"({"requestId":"actual-fetch","networkId":"actual-network","resourceType":"Fetch","request":{"url":"http://native.test/held","method":"GET","headers":{"Cookie":"actual=owned"}}})"),"");
 const auto intercepted=events.back()->GetDictionary("request")->GetString("id").ToString();bool read=false;
 paused.Command(8,Query(intercepted),[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Cookie")=="actual=owned");read=true;});assert(read&&std::find(sent.begin(),sent.end(),"Fetch.continueRequest")==sent.end());paused.Clear();
 // A still missing exact receipt fails on real owner close or original retention deadline.
 for(bool close:{false,true}){events.clear();now=1;Controls missing(send,emit,failure,[]{},[&]{return now;});
  missing.Event("Network.requestWillBeSent",Parse(R"({"requestId":"missing","request":{"url":"http://native.test/missing","method":"GET","headers":{}}})"),"child");
  const auto id=events.back()->GetDictionary("request")->GetString("id").ToString();missing.Event("Network.responseReceived",Parse(R"({"requestId":"missing","hasExtraInfo":true,"response":{"status":200,"headers":{}}})"),"child");bool rejected=false;
  missing.Command(9,Query(id),[&](bool ok,const std::string&error,const std::string&){assert(!ok&&error.find(close?"closed":"deadline")!=std::string::npos);rejected=true;});assert(!rejected);
  if(close)missing.SessionDetached("child");else{now=30001;missing.Tick(now);}assert(rejected);missing.Clear();
 }
 // A response-less actual failure has no hasExtraInfo marker. A remaining
 // receipt can only belong to this terminal hop after all marked redirects;
 // absent receipts remain optional, so no header fields or errors are invented.
 for(bool receipt_before:{false,true}){events.clear();now=1;Controls failed(send,emit,failure,[]{},[&]{return now;});
  failed.Event("Network.requestWillBeSent",Parse(R"({"requestId":"failed-final","request":{"url":"http://native.test/failed","method":"GET","headers":{"Actual":"rendered-partial"}}})"),"");
  const auto id=events.back()->GetDictionary("request")->GetString("id").ToString();
  auto actual=Parse(R"({"requestId":"failed-final","headers":{"Actual":"actual-failed-wire"}})");
  if(receipt_before)failed.Event("Network.requestWillBeSentExtraInfo",actual,"");
  failed.Event("Network.loadingFailed",Parse(R"({"requestId":"failed-final","errorText":"net::ERR_CONNECTION_RESET"})"),"");
  failed.Command(10,Query(id),[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")== (receipt_before?"actual-failed-wire":"rendered-partial"));});
  if(!receipt_before){failed.Event("Network.requestWillBeSentExtraInfo",actual,"");failed.Command(11,Query(id),[](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")=="actual-failed-wire");});}
  failed.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkPendingHeaderBytes")==0);failed.Clear();
 }

 events.clear();now=1;Controls absent(send,emit,failure,[]{},[&]{return now;});
 absent.Event("Network.requestWillBeSent",Parse(R"({"requestId":"no-optional-receipt","request":{"url":"http://native.test/no-optional","method":"GET","headers":{"Actual":"partial-only"}}})"),"");
 const auto absent_id=events.back()->GetDictionary("request")->GetString("id").ToString();
 absent.Event("Network.loadingFailed",Parse(R"({"requestId":"no-optional-receipt","errorText":"net::ERR_BLOCKED_BY_CLIENT"})"),"");
 now=30001;absent.Tick(now);bool partial=false;absent.Command(12,Query(absent_id),[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("Actual")=="partial-only");partial=true;});assert(partial);absent.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkPendingHeaderBytes")==0);absent.Clear();

}
