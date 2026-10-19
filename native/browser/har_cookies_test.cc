// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "har_capture.h"
using Capture=suitecut::NativeHARCapture;
using Dict=Capture::Dict;
Dict ParseCookies(const char*text){return CefParseJSON(text,JSON_PARSER_RFC)->GetDictionary();}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 Capture::BodyReply body;
 Capture capture([](int,Dict){},[](int,const std::string&,const Capture::Bytes&){},[]{return 1.;},[](const std::string&){assert(false);});
 assert(capture.Start(1));assert(capture.Attach(1,"actual-cookie-owner",[&](const std::string&,const std::string&,double,Capture::BodyReply reply){body=std::move(reply);}));
 auto event=ParseCookies(R"({"event":"request","request":{"id":"cookie-logical","requestId":"cookie-native","session":"actual-session","method":"GET","url":"https://native.test/","headers":{"Cookie":"one=\"value1\"; two=val=ue2; three=val,ue3"}}})");
 assert(capture.Observe(1,"actual-cookie-owner",event,1700000000000.));
 auto response=ParseCookies(R"({"status":200,"headers":{"Set-Cookie":["one=value1; HttpOnly","two=\"value2\"","three=val, ue3; pAtH=/; dOmAiN=example.test; sAmEsItE=lAx","four=v; secure; httponly; SameSite=None","five=v; SameSite=STRICT","six=v; SameSite=unsupported; Expires=invalid","seven=v; secure=false; HttpOnly=0","eight=v; SameSite=Lax; SameSite=unsupported","nine=v; SameSite=unsupported; SameSite=nOnE"]},"requestId":"cookie-native","session":"actual-session"})");
 event->SetDictionary("response",response->Copy(false));event->SetString("event","requestfinished");assert(capture.Observe(1,"actual-cookie-owner",event,1700000000000.));assert(body);body(true,{},"");
 auto entry=capture.Export(1)->GetDictionary("log")->GetList("entries")->GetDictionary(0)->Copy(false);
 auto request=entry->GetDictionary("request")->GetList("cookies");assert(request->GetSize()==3);assert(request->GetDictionary(0)->GetString("value")=="\"value1\"");assert(request->GetDictionary(1)->GetString("value")=="val=ue2");assert(request->GetDictionary(2)->GetString("value")=="val,ue3");
 auto cookies=entry->GetDictionary("response")->GetList("cookies");assert(cookies->GetSize()==9);
 auto one=cookies->GetDictionary(0);assert(one->GetSize()==3&&one->GetBool("httpOnly")&&!one->HasKey("secure"));
 auto two=cookies->GetDictionary(1);assert(two->GetSize()==2&&two->GetString("value")=="\"value2\"");
 auto three=cookies->GetDictionary(2);assert(three->GetSize()==5&&three->GetString("value")=="val, ue3");assert(three->GetString("path")=="/"&&three->GetString("domain")=="example.test"&&three->GetString("sameSite")=="Lax");
 auto four=cookies->GetDictionary(3);assert(four->GetSize()==5&&four->GetBool("secure")&&four->GetBool("httpOnly")&&four->GetString("sameSite")=="None");
 assert(cookies->GetDictionary(4)->GetString("sameSite")=="Strict");assert(cookies->GetDictionary(5)->GetSize()==2);
 auto seven=cookies->GetDictionary(6);assert(seven->GetBool("secure")&&seven->GetBool("httpOnly"));
 assert(!cookies->GetDictionary(7)->HasKey("sameSite"));assert(cookies->GetDictionary(8)->GetString("sameSite")=="None");
 // Exported values own their original header graph after actual source edits.
 response->GetDictionary("headers")->SetString("Set-Cookie","replacement=not-original");assert(two->GetString("value")=="\"value2\"");assert(cookies->GetSize()==9);
 capture.Stop(1);capture.Detach("actual-cookie-owner");assert(capture.retained_bytes()==0&&capture.physical()==0);
}
