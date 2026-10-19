// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "har_capture.h"
#include "har_replay.h"
using Capture=suitecut::NativeHARCapture;using Dict=Capture::Dict;
Dict MimeObject(const char*text){return CefParseJSON(text,JSON_PARSER_RFC)->GetDictionary();}
void PostMIME(const char*type,bool binary,bool direct_only=false){
 Capture::BodyReply body;Capture::Bytes actual={0,1,2,3,255};const std::string text="Actual body text";Capture::Bytes expected=binary?actual:Capture::Bytes(text.begin(),text.end());std::vector<Capture::Bytes>resources;
 Capture capture([](int,Dict){},[&](int,const std::string&,const Capture::Bytes&bytes){resources.push_back(bytes);},[]{return 1.;},[](const std::string&){assert(false);});assert(capture.Start(1));assert(capture.Attach(1,"page:actual",[&](const std::string&,const std::string&,double,Capture::BodyReply callback){body=std::move(callback);}));
 auto request=MimeObject(R"({"url":"https://native.test/post","method":"POST","headers":{}})");if(type)request->GetDictionary("headers")->SetString("Content-Type",type);if(binary)request->SetString("postDataBase64",CefBase64Encode(actual.data(),actual.size()));else request->SetString("postData",text);
 auto raw=MimeObject(R"({"requestId":"native-post","wallTime":1700000000})");raw->SetDictionary("request",request->Copy(false));if(!direct_only)assert(capture.OriginalRequest(1,"page:actual","",raw));
 request->SetString("id","one");request->SetString("requestId","native-post");request->SetString("session","");auto event=MimeObject(R"({"event":"request"})");event->SetDictionary("request",request->Copy(false));assert(capture.Observe(1,"page:actual",event,1700000000000.));event->SetString("event","requestfinished");event->SetDictionary("response",MimeObject(R"({"status":200,"session":"","requestId":"native-post","headers":{"Content-Type":"text/plain"}})"));assert(capture.Observe(1,"page:actual",event,1700000000000.));body(true,{'O','K'},"");
 auto archive=capture.Export(1);auto entry=archive->GetDictionary("log")->GetList("entries")->GetDictionary(0);auto output=entry->GetDictionary("request");auto post=output->GetDictionary("postData");assert(post);assert(post->GetString("mimeType")==((type&&*type)?type:"application/octet-stream"));assert(post->GetList("params")->GetSize()==0);
 auto headers=output->GetList("headers");assert(headers->GetSize()==(type?1:0));if(type){assert(headers->GetDictionary(0)->GetString("name")=="Content-Type"&&headers->GetDictionary(0)->GetString("value")==type);}
 if(binary){assert(post->GetString("_encoding")=="base64");assert(post->GetString("text")==CefBase64Encode(actual.data(),actual.size()));}else assert(post->GetString("text")==text);
 if(!direct_only){assert(!resources.empty()&&resources.front()==expected);}
 // Public HAR body bytes still match real request bytes for replay. No empty
 // text or invented Content-Type is used to satisfy the stock binary oracle.
 suitecut::NativeHARReplay replay;std::string error;assert(replay.Load(archive,error));auto match=replay.Lookup(request);assert(match);
 capture.Stop(1);capture.Detach("page:actual");assert(capture.retained_bytes()==0&&capture.physical()==0);
}
int main(){assert(cef_api_hash(CEF_API_VERSION,0));PostMIME(nullptr,false);PostMIME("",false);PostMIME(nullptr,true);PostMIME("",true);PostMIME("application/custom",true);PostMIME("text/plain;charset=UTF-8",false);PostMIME(nullptr,false,true);}
