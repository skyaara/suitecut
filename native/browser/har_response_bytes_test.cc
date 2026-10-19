// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include "include/cef_api_hash.h"
#include "har_capture.h"
using Wire=suitecut::NativeHARResponseBytes;
using Capture=suitecut::NativeHARCapture;
using Dict=Capture::Dict;
Dict WireObject(const char*text){return CefParseJSON(text,JSON_PARSER_RFC)->GetDictionary();}
void PureWire(){
 auto good=[](){Wire wire;wire.Begin(true);wire.Response(true);return wire;};
 auto a=good();a.Data(100,25);a.Terminal(true);assert(a.Encoded(100)==25);assert(!a.Encoded(99));a.CoverageLost();assert(a.Encoded(100)==25);
 auto b=good();b.Terminal(true);assert(!b.Encoded(0));
 auto c=good();c.Data(100,0);c.Terminal(true);assert(!c.Encoded(100));
 for(double bad:{-1.,0.5,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),double(Wire::kBytes)+1}){auto wire=good();wire.Data(100,bad);wire.Terminal(true);assert(!wire.Encoded(100));}
 auto overflow=good();overflow.Data(Wire::kBytes,1);overflow.Data(1,1);overflow.Terminal(true);assert(!overflow.Encoded(Wire::kBytes));
 auto many=good();for(size_t i=0;i<=Wire::kChunks;++i)many.Data(1,1);many.Terminal(true);assert(!many.Encoded(Wire::kChunks+1));
 auto late=good();late.Data(100,25);late.Terminal(true);late.Data(0,1);assert(!late.Encoded(100));
 auto failed=good();failed.Data(100,25);failed.Terminal(false);assert(!failed.Encoded(100));
 Wire early;early.Begin(true);early.Data(100,25);early.Response(true);early.Terminal(true);assert(!early.Encoded(100));
 auto lost=good();lost.Data(100,25);lost.CoverageLost();lost.Terminal(true);assert(!lost.Encoded(100));
 auto duplicate=good();duplicate.Response(true);duplicate.Data(100,25);duplicate.Terminal(true);assert(!duplicate.Encoded(100));
 Wire uncovered;uncovered.Begin(false);uncovered.Response(true);uncovered.Data(100,25);uncovered.Terminal(true);assert(!uncovered.Encoded(100));
 Wire warmed;warmed.Begin(false);warmed.Begin(true);warmed.Response(true);warmed.Data(100,25);warmed.Terminal(true);assert(!warmed.Encoded(100));
 auto restart=good();restart.Data(100,25);restart.Terminal(true);restart.Begin(true);assert(!restart.Encoded(100));
 auto twice=good();twice.Data(100,25);twice.Terminal(true);twice.Terminal(true);assert(!twice.Encoded(100));
}
struct Fixture {
 Capture::BodyReply body;Dict event,raw,raw_response;int reads=0;
 Capture capture;
 Fixture(bool ready=true):capture([](int,Dict){},[](int,const std::string&,const Capture::Bytes&){},[]{return 1.;},[](const std::string&){assert(false);}){
  assert(capture.Start(1));assert(capture.Attach(1,"page:actual",[this](const std::string&,const std::string&,double,Capture::BodyReply callback){++reads;body=std::move(callback);}));
  capture.PrimaryObservation(1,"page:actual",ready);
  raw=WireObject(R"({"requestId":"native-wire","frameId":"actual-frame","loaderId":"actual-loader","wallTime":1700000000,"request":{"url":"http://native.test/body","method":"GET","headers":{}}})");
  assert(capture.OriginalRequest(1,"page:actual","",raw));
  event=WireObject(R"({"event":"request","request":{"id":"logical-one","requestId":"native-wire","session":"","frameId":"actual-frame","method":"GET","url":"http://native.test/body","headers":{}}})");assert(capture.Observe(1,"page:actual",event,1700000000000.));
  raw_response=WireObject(R"({"requestId":"native-wire","frameId":"actual-frame","loaderId":"actual-loader","response":{"url":"http://native.test/body","fromDiskCache":false,"fromServiceWorker":false,"fromPrefetchCache":false}})");
 }
 void Response(const char*coding="gzip"){
  capture.ResponseBytes(1,"page:actual","Network.responseReceived",raw_response,"");
  auto response=WireObject(R"({"status":200,"headersReady":true,"_nativeHeadersObserved":true,"session":"","requestId":"native-wire","headers":{"Content-Encoding":"gzip"}})");response->GetDictionary("headers")->SetString("Content-Encoding",coding);event->SetDictionary("response",response->Copy(false));event->SetString("event","response");assert(capture.Observe(1,"page:actual",event,1700000000000.));
 }
 void Data(double decoded=100,double encoded=25,const char*session="",const char*owner="page:actual"){
  auto data=WireObject(R"({"requestId":"native-wire"})");data->SetDouble("dataLength",decoded);data->SetDouble("encodedDataLength",encoded);capture.ResponseBytes(1,owner,"Network.dataReceived",data,session);
 }
 void Finish(size_t size=100,bool success=true){
  capture.ResponseBytes(1,"page:actual",success?"Network.loadingFinished":"Network.loadingFailed",raw,"");
  event->SetString("event",success?"requestfinished":"requestfailed");if(!success)event->SetString("errorText","Actual load failed");assert(capture.Observe(1,"page:actual",event,1700000000000.));if(success){assert(body);body(true,Capture::Bytes(size,'x'),"");}
 }
 Dict ResponseEntry(){auto output=capture.Export(1);return output->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("response")->Copy(false);}
 void Known(int encoded,int compression){auto entry=ResponseEntry();assert(entry->GetInt("bodySize")==encoded);assert(entry->GetDictionary("content")->HasKey("compression")&&entry->GetDictionary("content")->GetInt("compression")==compression);}
 void Unknown(){auto entry=ResponseEntry();assert(entry->GetInt("bodySize")==-1&&!entry->GetDictionary("content")->HasKey("compression"));}
 ~Fixture(){capture.Stop(1);capture.Detach("page:actual");assert(capture.retained_bytes()==0&&capture.physical()==0);}
};
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));PureWire();
 {Capture capture([](int,Dict){},[](int,const std::string&,const Capture::Bytes&){},[]{return 1.;},[](const std::string&){assert(false);});assert(capture.Start(1));assert(capture.Attach(1,"page:capacity",[](const std::string&,const std::string&,double,Capture::BodyReply){}));capture.PrimaryObservation(1,"page:capacity",true);auto raw=WireObject(R"({"requestId":"pending-pin","wallTime":1700000000,"request":{"url":"http://native.test/pending","method":"GET","headers":{}}})");raw->SetString("frameId",std::string(256,'f'));raw->SetString("loaderId",std::string(256,'l'));assert(capture.OriginalRequest(1,"page:capacity","",raw));assert(capture.retained_bytes()>=512);capture.Tick(30001);assert(capture.retained_bytes()==0);capture.Stop(1);capture.Detach("page:capacity");}

 {Fixture f;f.Response();f.Data();f.Finish();f.Known(25,75);}
 {Fixture f;f.Response("identity");f.Data(100,100);f.Finish();f.Known(100,0);}
 {Fixture f;f.Response();f.Data(100,125);f.Finish();f.Known(125,-25);}
 {Fixture f;f.Response();f.Data();f.Finish(99);f.Unknown();}
 {Fixture f;f.Response();f.Finish(0);f.Unknown();}
 {Fixture f;f.Response();f.Data(100,0);f.Finish();f.Unknown();}
 {Fixture f(false);f.capture.PrimaryObservation(1,"page:actual",true);f.Response();f.Data();f.Finish();f.Unknown();}
 {Fixture f;f.raw_response->GetDictionary("response")->SetBool("fromDiskCache",true);f.Response();f.Data();f.Finish();f.Unknown();}
 {Fixture f;f.raw_response->GetDictionary("response")->Remove("fromPrefetchCache");f.Response();f.Data();f.Finish();f.Unknown();}
 {Fixture f;f.Response();f.capture.ResponseBytes(1,"page:actual","Network.requestServedFromCache",f.raw,"");f.Data();f.Finish();f.Unknown();}
 {Fixture f;f.Response();f.Data(100,25,"mirrored-private");f.Data(100,25,"","other-page");f.Data();f.Finish();f.Known(25,75);}
 {Fixture f;f.Response();f.Data(100,25,"mirrored-private");f.Finish();f.Unknown();}
 {Fixture f;f.Response();f.Data();f.event->GetDictionary("request")->SetString("session","moved-child");f.Finish();f.Unknown();}
 {Fixture f;f.Response();f.Data();f.event->GetDictionary("response")->GetDictionary("headers")->SetString("Content-Encoding","identity");f.Finish();f.Unknown();}
 {Fixture f;f.Response();f.Data();f.capture.PrimaryObservation(1,"page:actual",false);f.Finish();f.Unknown();}
 {Fixture f;f.Response();f.Data();f.Finish();f.capture.PrimaryObservation(1,"page:actual",false);f.Known(25,75);}
 {Fixture f;f.Response();f.Data();f.Finish(100,false);f.Unknown();}
 {Fixture f;f.Response();f.Data();f.capture.OriginalRequest(1,"page:actual","",f.raw);f.Finish();f.Unknown();}
 {Fixture f;f.raw_response->SetString("loaderId","replacement-loader");f.Response();f.Data();f.Finish();f.Unknown();}
 {Fixture f;f.Response("br");f.Data();f.Finish();f.Unknown();}
}
