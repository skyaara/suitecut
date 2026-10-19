// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "har_capture.h"
using Capture=suitecut::NativeHARCapture;using Dict=Capture::Dict;
Dict Parse(const char*text){return CefParseJSON(text,JSON_PARSER_RFC)->GetDictionary();}
void PostMetadata(const char*type,const char*text,const char*coding,bool body_ok=true,bool observed=true,bool ready=true,bool oversized=false){
 double now=10;Capture::BodyReply body;
 Capture capture([](int,Dict){},[](int,const std::string&,const Capture::Bytes&){},[&]{return now;},[](const std::string&){assert(false);});
 assert(capture.Start(1));assert(capture.Attach(1,"actual-owner",[&](const std::string&,const std::string&,double,Capture::BodyReply reply){body=std::move(reply);}));
 auto request=Parse(R"({"method":"POST","url":"https://native.test/post","headers":{}})");request->GetDictionary("headers")->SetString("Content-Type",type);request->SetString("postData",text);
 auto raw=Parse(R"({"requestId":"actual-post","wallTime":1700000000})");raw->SetDictionary("request",request->Copy(false));assert(capture.OriginalRequest(1,"actual-owner","actual-session",raw));
 request->SetString("id","logical-post");request->SetString("requestId","actual-post");request->SetString("session","actual-session");auto event=Parse(R"({"event":"request"})");event->SetDictionary("request",request->Copy(false));assert(capture.Observe(1,"actual-owner",event,1700000000000));
 auto response=Parse(R"({"status":200,"headers":{"Content-Type":"text/plain","Content-Length":"2"},"requestId":"actual-post","session":"actual-session"})");response->SetBool("_nativeHeadersObserved",observed);response->SetBool("headersReady",ready);if(coding){if(std::string(coding)=="mixed-list"){auto values=CefListValue::Create();values->SetString(0,"identity");values->SetString(1,"gzip");response->GetDictionary("headers")->SetList("Content-Encoding",values);}else response->GetDictionary("headers")->SetString("Content-Encoding",coding);}event->SetDictionary("response",response);event->SetString("event","requestfinished");assert(capture.Observe(1,"actual-owner",event,1700000000000));assert(body);body(body_ok,body_ok?Capture::Bytes{'O','K'}:Capture::Bytes{},body_ok?"":"Actual body read failure");
 auto entry=capture.Export(1)->GetDictionary("log")->GetList("entries")->GetDictionary(0)->Copy(false);auto post=entry->GetDictionary("request")->GetDictionary("postData");assert(post&&post->GetString("text")==text);auto params=post->GetList("params");if(oversized){assert(!params&&post->GetString("_paramsError")=="Native HAR form parameter capacity exceeded");capture.Stop(1);capture.Detach("actual-owner");assert(capture.retained_bytes()==0&&capture.physical()==0);return;}assert(params);
 if(std::string(type).starts_with("Application/X-WWW-Form-Urlencoded")){
  assert(params->GetSize()==6);const char*names[]={"a","a","empty","token","flag","literal"};const char*values[]={"one two","✓","","a=b","","#hash"};for(size_t i=0;i<6;++i){assert(params->GetDictionary(i)->GetString("name")==names[i]);assert(params->GetDictionary(i)->GetString("value")==values[i]);}
 }else assert(params->GetSize()==0);
 auto content=entry->GetDictionary("response")->GetDictionary("content");const bool identity=!coding||std::string(coding)==" Identity ";assert(content->HasKey("compression")==bool(body_ok&&identity&&observed&&ready));if(body_ok&&identity&&observed&&ready)assert(content->GetInt("compression")==0);assert(entry->GetDictionary("response")->GetInt("bodySize")==-1);capture.Stop(1);capture.Detach("actual-owner");assert(capture.retained_bytes()==0&&capture.physical()==0);
}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));double clock=1;Capture::BodyReply body;
 Capture capture([](int,Dict){},[](int,const std::string&,const Capture::Bytes&){},[&]{return clock;},[](const std::string&){assert(false);});
 assert(capture.Start(1));assert(capture.Attach(1,"page:81",[&](const std::string&,const std::string&,double,Capture::BodyReply reply){body=std::move(reply);}));assert(capture.PageCreated(1,"page:81","page@81",1700000000000));
 assert(capture.PageLifecycle(1,"page:81","request",100,1700000000100,"actual-loader"));assert(capture.PageTitle(1,"page:81","Actual native title"));assert(capture.PageLifecycle(1,"page:81","DOMContentLoaded",130,1700000000130,"actual-loader"));assert(capture.PageLifecycle(1,"page:81","load",140,1700000000140,"actual-loader"));
 auto raw=Parse(R"({"requestId":"actual-request","wallTime":1700000000.1,"request":{"method":"GET","url":"https://native.test/folder/source","headers":{}}})");assert(capture.OriginalRequest(1,"page:81","actual-session",raw));
 auto event=Parse(R"({"event":"request","request":{"id":"1","requestId":"actual-request","session":"actual-session","frameId":"actual-frame","method":"GET","url":"https://native.test/folder/source","headers":{}}})");assert(capture.Observe(1,"page:81",event,1700000000100));event->SetString("event","requestfinished");event->SetDictionary("response",Parse(R"({"status":302,"statusText":"Found","protocol":"http/1.1","requestId":"actual-request","session":"actual-session","serverAddr":{"ipAddress":"127.0.0.1","port":8443},"securityDetails":{"issuer":"actual issuer","protocol":"TLS 1.3","subjectName":"native.test","validFrom":1700000000,"validTo":2200000000},"headers":{"Location":"../final"}})"));assert(capture.Observe(1,"page:81",event,1700000000100));body(true,{},"");
 auto archive=capture.Export(1);auto log=archive->GetDictionary("log");assert(log->GetList("pages")->GetSize()==1);auto page=log->GetList("pages")->GetDictionary(0);auto entry=log->GetList("entries")->GetDictionary(0);assert(page->GetString("id")=="page@81"&&entry->GetString("pageref")==page->GetString("id"));assert(page->GetString("title")=="Actual native title"&&page->GetDictionary("pageTimings")->GetDouble("onContentLoad")==30);assert(entry->GetDictionary("request")->GetInt("bodySize")==0);assert(entry->GetDictionary("response")->GetString("redirectURL")=="https://native.test/final");assert(!entry->GetDictionary("response")->GetDictionary("content")->HasKey("compression"));assert(entry->GetString("serverIPAddress")=="127.0.0.1"&&entry->GetInt("_serverPort")==8443);assert(entry->GetDictionary("_securityDetails")->GetString("issuer")=="actual issuer");assert(entry->GetDictionary("_securityDetails")->GetDouble("validTo")==2200000000.);
 assert(capture.Attach(1,"broker:98",[](const std::string&,const std::string&,double,Capture::BodyReply){assert(false);},Capture::OwnerKind::WorkerBroker));assert(!capture.PageCreated(1,"broker:98","invented-page",1700000000100));event->GetDictionary("request")->SetString("id","2");event->GetDictionary("request")->SetString("requestId","actual-worker-request");event->SetString("event","request");capture.Observe(1,"broker:98",event,1700000000100);event->SetString("event","requestfailed");event->SetString("errorText","net::ERR_FAILED");capture.Observe(1,"broker:98",event,1700000000100);auto worker=capture.Export(1)->GetDictionary("log")->GetList("entries")->GetDictionary(1)->Copy(false);assert(!worker->HasKey("pageref")&&!worker->HasKey("_frameref"));
 capture.Stop(1);capture.Detach("page:81");capture.Detach("broker:98");assert(capture.retained_bytes()==0);capture.Clear();
 PostMetadata("text/plain;charset=UTF-8","Hello",nullptr);
 PostMetadata("Application/X-WWW-Form-Urlencoded; charset=UTF-8","&&a=one+two&&a=%E2%9C%93&empty=&token=a%3Db&flag&literal=%23hash&",nullptr);
 PostMetadata("application/octet-stream","Actual bytes", "gzip");
 PostMetadata("text/plain","Hello", " Identity ");
 PostMetadata("text/plain","Hello", "gzip, br");
 PostMetadata("text/plain","Hello", "mixed-list");
 PostMetadata("text/plain","Hello",nullptr,false);
 PostMetadata("text/plain","Hello",nullptr,true,false);
 PostMetadata("text/plain","Hello",nullptr,true,true,false);
 std::string excessive;for(int i=0;i<257;++i)excessive+="a=b&";PostMetadata("application/x-www-form-urlencoded",excessive.c_str(),nullptr,true,true,true,true);
}
