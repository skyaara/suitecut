// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Dict>events;double now=1;
 Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict event){events.push_back(event->Copy(false));},[](const std::string&){assert(false);},[]{},[&]{return now;});
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"actual-id","type":"Document","request":{"url":"http://native.test/one","method":"GET","headers":{}}})"),"");
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"actual-id","type":"Document","redirectHasExtraInfo":true,"redirectResponse":{"url":"http://native.test/one","status":302,"headers":{"actual":"rendered-one"}},"request":{"url":"http://native.test/two","method":"GET","headers":{}}})"),"");
 const auto old=events[1]->GetDictionary("request")->GetString("id");assert(!events[1]->GetDictionary("response")->GetBool("headersReady"));
 bool answered=false;auto command=Parse(R"({"action":"responseHeaders"})");command->SetString("requestId",old);controls.Command(1,command,[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("actual")=="raw-one");answered=true;});assert(!answered);
 // Exact20f ordering: first response ExtraInfo is late and shares the same
 // physical ID as the current second hop. It belongs only to logical hop one.
 controls.Event("Network.responseReceivedExtraInfo",Parse(R"({"requestId":"actual-id","headers":{"actual":"raw-one"}})"),"");assert(answered&&events.back()->GetString("event")=="requestupdated"&&events.back()->GetDictionary("request")->GetString("id")==old);
 controls.Event("Network.requestWillBeSent",Parse(R"({"requestId":"actual-id","type":"Document","redirectHasExtraInfo":false,"redirectResponse":{"url":"http://native.test/two","status":302,"headers":{"actual":"rendered-two"}},"request":{"url":"http://native.test/final","method":"GET","headers":{}}})"),"");
 controls.Event("Network.responseReceived",Parse(R"({"requestId":"actual-id","type":"Document","hasExtraInfo":false,"response":{"url":"http://native.test/final","status":200,"headers":{"actual":"rendered-final"}}})"),"");controls.Event("Network.loadingFinished",Parse(R"({"requestId":"actual-id"})"),"");
 std::vector<std::string>headers;for(const auto&event:events)if(event->GetString("event")=="response")headers.push_back(event->GetDictionary("response")->GetDictionary("headers")->GetString("actual").ToString());assert(headers==std::vector<std::string>({"rendered-one","rendered-two","rendered-final"}));
 // Retained logical redirect metadata receives the real late field; final hop
 // and native raw-header waiter are not substituted with another hop.
 auto snapshot=Parse(R"({"action":"response"})");snapshot->SetString("requestId",old);controls.Command(2,snapshot,[](bool ok,const std::string&,const std::string&json){assert(ok);auto data=Parse(json.c_str());assert(data->GetBool("headersReady")&&data->GetDictionary("headers")->GetString("actual")=="raw-one");});
 auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkResponseHeaderBytes")==0&&diagnostics->GetInt("networkResponseHeaderHops")==0);controls.Clear();
 // Several expected redirect hops can all precede their genuine receipt FIFO.
 // Cancellation consumes the waiter, not a later physical metadata receipt.
 events.clear();Controls multi([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict event){events.push_back(event->Copy(false));},[](const std::string&){assert(false);},[]{},[&]{return now;});
 multi.Event("Network.requestWillBeSent",Parse(R"({"requestId":"fifo","request":{"url":"http://native.test/a","method":"GET","headers":{}}})"),"");
 multi.Event("Network.requestWillBeSent",Parse(R"({"requestId":"fifo","redirectHasExtraInfo":true,"redirectResponse":{"url":"http://native.test/a","status":302,"headers":{}},"request":{"url":"http://native.test/b","method":"GET","headers":{}}})"),"");
 const auto first=events[1]->GetDictionary("request")->GetString("id");
 multi.Event("Network.requestWillBeSent",Parse(R"({"requestId":"fifo","redirectHasExtraInfo":true,"redirectResponse":{"url":"http://native.test/b","status":302,"headers":{}},"request":{"url":"http://native.test/c","method":"GET","headers":{}}})"),"");
 const auto second=events[4]->GetDictionary("request")->GetString("id");
 auto canceled=Parse(R"({"action":"responseHeaders"})");canceled->SetString("requestId",first);bool canceled_called=false;multi.Command(3,canceled,[&](bool,const std::string&,const std::string&){canceled_called=true;});multi.Cancel(3);
 auto waiting=Parse(R"({"action":"responseHeaders"})");waiting->SetString("requestId",second);bool second_called=false;multi.Command(4,waiting,[&](bool ok,const std::string&,const std::string&json){assert(ok&&Parse(json.c_str())->GetString("actual")=="raw-b");second_called=true;});
 multi.Event("Network.responseReceived",Parse(R"({"requestId":"fifo","hasExtraInfo":false,"response":{"url":"http://native.test/c","status":200,"headers":{"actual":"rendered-c"}}})"),"");multi.Event("Network.loadingFinished",Parse(R"({"requestId":"fifo"})"),"");
 multi.Event("Network.responseReceivedExtraInfo",Parse(R"({"requestId":"fifo","headers":{"actual":"raw-a"}})"),"");assert(!second_called&&!canceled_called);
 multi.Event("Network.responseReceivedExtraInfo",Parse(R"({"requestId":"fifo","headers":{"actual":"raw-b"}})"),"");assert(second_called&&!canceled_called);
 multi.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkResponseHeaderBytes")==0);multi.Clear();
 // Exact channel destruction fails a still expected header waiter promptly.
 Controls closing([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict event){events.push_back(event->Copy(false));},[](const std::string&){assert(false);},[]{},[&]{return now;});events.clear();
 closing.Event("Network.requestWillBeSent",Parse(R"({"requestId":"closing","request":{"url":"http://native.test/a","method":"GET","headers":{}}})"),"child");
 closing.Event("Network.responseReceived",Parse(R"({"requestId":"closing","hasExtraInfo":true,"response":{"url":"http://native.test/a","status":200,"headers":{}}})"),"child");
 // The native command looks up the genuine generated identity, not the URL.
 auto native=Parse(R"({"action":"responseHeaders"})");native->SetString("requestId",events.front()->GetDictionary("request")->GetString("id"));bool closed_called=false;
 closing.Command(5,native,[&](bool ok,const std::string&error,const std::string&){assert(!ok&&error.find("closed")!=std::string::npos);closed_called=true;});assert(!closed_called);closing.SessionDetached("child");assert(closed_called);closing.Clear();
}
