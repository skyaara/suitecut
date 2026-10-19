// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include "include/cef_api_hash.h"
#include "har_capture.h"
using Expiry=suitecut::NativeHARCookieExpiry;
using Capture=suitecut::NativeHARCapture;
using Dict=Capture::Dict;
Dict ExpiryObject(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
void PureExpiry(){
 assert(Expiry::ParseUTC("Thu, 01 Jan 1970 00:00:00 GMT")==0.);
 assert(Expiry::ParseUTC("Mon, 01 Jan 1601 00:00:00 GMT")==-11644473600000.);
 assert(Expiry::Format(-1.)=="1969-12-31T23:59:59.999Z");
 assert(Expiry::Format(-11644473599877.)=="1601-01-01T00:00:00.123Z");
 assert(Expiry::Format(253402300799999.)=="9999-12-31T23:59:59.999Z");
 assert(!Expiry::Format(253402300800000.)&&!Expiry::Format(std::numeric_limits<double>::quiet_NaN()));
 for(const char*value:{"Fri, 29 Feb 2024 01:02:03 GMT","Thu, 30 Feb 2024 01:02:03 GMT","Thu, 29 Feb 2024 24:02:03 GMT","Thu, 29 Feb 2024 01:60:03 GMT","Thu, 29 Feb 2024 01:02:60 GMT","Sat Sep 14 01:02:27 CET 2024","Thu, 29 Feb 2024 01:02:03 UTC"})assert(!Expiry::ParseUTC(value));
 assert(Expiry::ParseUTC("Sunday, 06-Nov-94 08:49:37 GMT")==784111777000.);
 assert(Expiry::ParseUTC("Sun Nov  6 08:49:37 1994")==784111777000.);
 assert(Expiry::ParseUTC("Thursday, 29-Feb-24 01:02:03 GMT")==1709168523000.);
 assert(Expiry::ParseUTC("Thu Feb 29 01:02:03 2024")==1709168523000.);
 assert(Expiry::ParseUTC("thursday, 01-Jan-70 00:00:00 gmt")==0.);
 assert(Expiry::Format(*Expiry::ParseUTC("Tuesday, 01-Jan-69 00:00:00 GMT"))=="2069-01-01T00:00:00.000Z");
 for(const char* value:{"Monday, 06-Nov-94 08:49:37 GMT","Sunday, 30-Feb-94 08:49:37 GMT","Sunday, 06-Nov-94 24:49:37 GMT","Sunday, 06-Nov-94 08:60:37 GMT","Sunday, 06-Nov-94 08:49:60 GMT","Sunday, 06-Nov-94 08:49:37 UTC","Sun Nov  6 08:49:37 1594","Sun Nov  6 08:49:37 1994 GMT","Sun Nov  6 08:49:37","Sun Nov  x 08:49:37 1994","Sun Foo  6 08:49:37 1994","Sunday, 06-Nov-9x 08:49:37 GMT"})assert(!Expiry::ParseUTC(value));
 Expiry legacy;legacy.Attribute("expires","Sunday, 06-Nov-94 08:49:37 GMT");assert(legacy.Resolve({})=="1994-11-06T08:49:37.000Z");
 legacy.Attribute("max-age","60");assert(!legacy.Resolve({}));assert(legacy.Resolve(1700000000123.)=="2023-11-14T22:14:20.123Z");
 legacy.Attribute("max-age","999999999999999999999999");assert(!legacy.Resolve(1700000000123.));
 Expiry absolute;absolute.Attribute("expires","Thu, 29 Feb 2024 01:02:03 GMT");assert(absolute.Resolve({})=="2024-02-29T01:02:03.000Z");
 absolute.Attribute("expires","invalid");assert(absolute.Resolve({})=="2024-02-29T01:02:03.000Z");
 absolute.Attribute("max-age","+60");assert(absolute.Resolve({})=="2024-02-29T01:02:03.000Z");
 absolute.Attribute("max-age","60");assert(!absolute.Resolve({}));assert(!absolute.Resolve(std::numeric_limits<double>::infinity()));assert(absolute.Resolve(1700000000123.)=="2023-11-14T22:14:20.123Z");
 absolute.Attribute("max-age","invalid");assert(absolute.Resolve(1700000000123.)=="2023-11-14T22:14:20.123Z");
 absolute.Attribute("max-age","0");assert(absolute.Resolve(1700000000123.)=="2023-11-14T22:13:20.123Z");
 absolute.Attribute("max-age","-60");assert(absolute.Resolve(1700000000123.)=="2023-11-14T22:12:20.123Z");
 absolute.Attribute("max-age","999999999999999999999999");assert(!absolute.Resolve(1700000000123.));
 absolute.Attribute("max-age","9223372036854775807");assert(!absolute.Resolve(1700000000123.));
 absolute.Attribute("max-age","-9223372036854775808");assert(!absolute.Resolve(1700000000123.));
 absolute.Attribute("max-age","30");assert(absolute.Resolve(1700000000123.)=="2023-11-14T22:13:50.123Z");
}
void LegacyHeaderProjection(){
 int reads=0;Capture::BodyReply body;
 Capture capture([](int,Dict){},[](int,const std::string&,const Capture::Bytes&){},[]{return 1.;},[](const std::string&){assert(false);},[&]{++reads;return std::numeric_limits<double>::quiet_NaN();});
 assert(capture.Start(1));assert(capture.Attach(1,"legacy-native-owner",[&](const std::string&,const std::string&,double,Capture::BodyReply reply){body=std::move(reply);}));
 auto event=ExpiryObject(R"({"event":"request","request":{"id":"legacy","requestId":"actual-legacy-native","session":"actual-session","method":"GET","url":"https://native.test/legacy","headers":{}}})");assert(capture.Observe(1,"legacy-native-owner",event,1700000000000.));
 constexpr const char* raw[]={"rfc=v; Expires=Sunday, 06-Nov-94 08:49:37 GMT","ascii=v; Expires=Sun Nov  6 08:49:37 1994","fallback=v; Max-Age=invalid; Expires=Thursday, 29-Feb-24 01:02:03 GMT","unknown=v; Max-Age=60; Expires=Sunday, 06-Nov-94 08:49:37 GMT","unsupported=v; Expires=Sunday, 06-Nov-94 08:49:37 UTC"};
 auto headers=CefDictionaryValue::Create();auto values=CefListValue::Create();for(size_t i=0;i<5;++i)values->SetString(i,raw[i]);headers->SetList("Set-Cookie",values);
 auto response=ExpiryObject(R"({"status":200,"_nativeHeadersObserved":true,"headersReady":true,"headers":{},"requestId":"actual-legacy-native","session":"actual-session"})");response->SetDictionary("headers",headers);
 event->SetDictionary("response",response->Copy(false));event->SetString("event","requestfinished");assert(capture.Observe(1,"legacy-native-owner",event,1700000000000.));assert(body);body(true,{},"");assert(reads==1);
 auto exported=capture.Export(1);auto saved=exported->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("response")->Copy(false);
 const auto cookies=saved->GetList("cookies");assert(cookies->GetSize()==5);
 assert(cookies->GetDictionary(0)->GetString("expires")=="1994-11-06T08:49:37.000Z");assert(cookies->GetDictionary(1)->GetString("expires")=="1994-11-06T08:49:37.000Z");assert(cookies->GetDictionary(2)->GetString("expires")=="2024-02-29T01:02:03.000Z");assert(!cookies->GetDictionary(3)->HasKey("expires")&&!cookies->GetDictionary(4)->HasKey("expires"));
 const auto retained=saved->GetList("headers");assert(retained->GetSize()==5);for(size_t i=0;i<5;++i)assert(retained->GetDictionary(i)->GetString("value")==raw[i]);
 response->GetDictionary("headers")->SetString("Set-Cookie","replacement=not-original");(void)capture.Export(1);assert(reads==1);assert(retained->GetDictionary(0)->GetString("value")==raw[0]);
 capture.Stop(1);capture.Detach("legacy-native-owner");assert(capture.retained_bytes()==0&&capture.physical()==0);
}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));PureExpiry();LegacyHeaderProjection();
 int reads=0;double wall=1700000000123.;bool fail_clock=false;Capture::BodyReply body;
 Capture capture([](int,Dict){},[](int,const std::string&,const Capture::Bytes&){},[]{return 1.;},[](const std::string&){assert(false);},[&]{++reads;if(fail_clock)return std::numeric_limits<double>::quiet_NaN();return wall;});
 assert(capture.Start(1));assert(capture.Attach(1,"native-owner",[&](const std::string&,const std::string&,double,Capture::BodyReply reply){body=std::move(reply);}));
 auto event=ExpiryObject(R"({"event":"request","request":{"id":"1","requestId":"same-redirect-native","session":"actual-session","method":"GET","url":"https://native.test/one","headers":{}}})");assert(capture.Observe(1,"native-owner",event,1700000000000.));
 auto response=ExpiryObject(R"({"status":200,"_nativeHeadersObserved":true,"headersReady":false,"headers":{"Set-Cookie":"one=v; Max-Age=60; Expires=Thu, 29 Feb 2024 01:02:03 GMT"},"requestId":"same-redirect-native","session":"actual-session"})");
 event->SetDictionary("response",response->Copy(false));event->SetString("event","response");assert(capture.Observe(1,"native-owner",event,1700000000000.));assert(reads==0);
 response->SetBool("headersReady",true);event->SetDictionary("response",response->Copy(false));event->SetString("event","requestupdated");assert(capture.Observe(1,"native-owner",event,1700000000000.));assert(reads==1);
 wall+=1000000.;event->SetString("event","requestfinished");assert(capture.Observe(1,"native-owner",event,1700000000000.));body(true,{},"");
 auto exported=[&]{return capture.Export(1)->GetDictionary("log")->GetList("entries")->Copy();};
 auto first=exported()->GetDictionary(0)->Copy(false);auto cookies=first->GetDictionary("response")->GetList("cookies");assert(cookies->GetDictionary(0)->GetString("expires")=="2023-11-14T22:14:20.123Z");
 assert(capture.Observe(1,"native-owner",event,1700000000000.));assert(reads==1);assert(exported()->GetDictionary(0)->GetDictionary("response")->GetList("cookies")->GetDictionary(0)->GetString("expires")=="2023-11-14T22:14:20.123Z");
 // Redirect hops share a native request ID, but keep distinct logical records
 // and header receipts. No old record's wall timestamp authorizes a new hop.
 event->GetDictionary("request")->SetString("id","2");event->GetDictionary("request")->SetString("url","https://native.test/two");event->Remove("response");event->SetString("event","request");assert(capture.Observe(1,"native-owner",event,1700000000000.));
 response->SetBool("_nativeHeadersObserved",false);event->SetDictionary("response",response->Copy(false));event->SetString("event","response");assert(capture.Observe(1,"native-owner",event,1700000000000.));assert(reads==1);
 response->SetBool("_nativeHeadersObserved",true);event->SetDictionary("response",response->Copy(false));fail_clock=true;assert(capture.Observe(1,"native-owner",event,1700000000000.));assert(reads==2);
 fail_clock=false;wall+=1000000.;event->SetString("event","requestfinished");assert(capture.Observe(1,"native-owner",event,1700000000000.));body(true,{},"");assert(reads==2);
 auto unknown=exported()->GetDictionary(1)->GetDictionary("response")->GetList("cookies")->GetDictionary(0)->Copy(false);assert(!unknown->HasKey("expires"));
 assert(exported()->GetSize()==2);assert(reads==2);
 event->Remove("response");event->GetDictionary("request")->SetString("id","3");event->GetDictionary("request")->SetString("url","https://native.test/three");event->SetString("event","request");assert(capture.Observe(1,"native-owner",event,1700000000000.));
 event->SetDictionary("response",response->Copy(false));event->SetString("event","requestfinished");assert(capture.Observe(1,"native-owner",event,1700000000000.));body(true,{},"");assert(reads==3);
 assert(exported()->GetDictionary(2)->GetDictionary("response")->GetList("cookies")->GetDictionary(0)->GetString("expires")=="2023-11-14T22:47:40.123Z");
 // Absolute supported dates need no invented wall time. Valid Max-Age in hop2
 // remained unknown above; an invalid directive permits a real absolute date.
 event->Remove("response");event->GetDictionary("request")->SetString("id","4");event->GetDictionary("request")->SetString("url","https://native.test/four");event->SetString("event","request");assert(capture.Observe(1,"native-owner",event,1700000000000.));
 response->GetDictionary("headers")->SetString("Set-Cookie","absolute=v; Max-Age=invalid; Expires=Thu, 29 Feb 2024 01:02:03 GMT");fail_clock=true;event->SetDictionary("response",response->Copy(false));event->SetString("event","requestfinished");assert(capture.Observe(1,"native-owner",event,1700000000000.));body(true,{},"");assert(reads==4);
 assert(exported()->GetDictionary(3)->GetDictionary("response")->GetList("cookies")->GetDictionary(0)->GetString("expires")=="2024-02-29T01:02:03.000Z");
 capture.Stop(1);capture.Detach("native-owner");assert(capture.retained_bytes()==0&&capture.physical()==0);
}
