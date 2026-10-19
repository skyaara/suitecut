// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_archives.h"
#include "request_body_reads.h"
using HAR=suitecut::NativeHARCapture;using Dict=HAR::Dict;
Dict Request(const std::string&id){auto request=CefDictionaryValue::Create();request->SetString("id",id);request->SetString("requestId","wire-"+id);request->SetString("session","");request->SetString("url","http://actual-native.test/post");request->SetString("method","POST");request->SetDictionary("headers",CefDictionaryValue::Create());request->SetBool("postDataUnavailable",true);return request;}
Dict Event(const std::string&kind,const std::string&id){auto value=CefDictionaryValue::Create();value->SetString("event",kind);value->SetDictionary("request",Request(id));if(kind!="request"){auto response=CefDictionaryValue::Create();response->SetInt("status",200);response->SetString("requestId","wire-"+id);response->SetString("session","");response->SetDictionary("headers",CefDictionaryValue::Create());value->SetDictionary("response",response);}return value;}
int main(){assert(cef_api_hash(CEF_API_VERSION,0));double now=1;std::vector<HAR::BodyReply>bodies;std::vector<Dict>snapshots;std::vector<suitecut::NativeRequestBodyReads::Result>acks;
 HAR capture([&](int,Dict snapshot){snapshots.push_back(snapshot->Copy(false));},[](int,const std::string&,const HAR::Bytes&){},[&]{return now;},[](const std::string&){assert(false);});
 assert(capture.Attach(1,"page:actual",[&](const std::string&,const std::string&,double,HAR::BodyReply reply){bodies.push_back(std::move(reply));}));assert(capture.Start(1));assert(capture.Observe(1,"page:actual",Event("request","one"),1000));auto post=capture.ReservePost(1,"page:actual","one");assert(post.receipt&&post.data&&capture.pending()==1&&capture.physical()==1);
 suitecut::NativeRequestBodyReads reader([&](const std::string&,const std::string&,double,auto ack){acks.push_back(std::move(ack));},[&]{return now;},[]{});
 reader.Read("","wire-one",31,[&](bool ok,auto bytes,bool text,const std::string&){post.data(ok,bytes,text);},post.receipt);
 capture.Seal(1);assert(capture.Observe(1,"page:actual",Event("requestfinished","one"),1000));assert(bodies.size()==1&&capture.pending()==2);bodies[0](true,{'r'},"");assert(!capture.Ready(1)&&snapshots.empty());
 now=31;reader.Tick(now);assert(reader.physical()==1&&capture.physical()==1&&capture.pending()==1&&!capture.Ready(1));acks[0](true,"{\"postData\":\"actual late bytes\",\"base64Encoded\":false}");assert(reader.physical()==0&&capture.physical()==0&&capture.Ready(1)&&snapshots.size()==1);assert(capture.Export(1)->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("request")->GetBool("_postDataUnavailable"));
 // Stopped capture callbacks hold weak records. Late old bytes cannot enter a
 // replacement capture even with an identical page/logical request identity.
 capture.Stop(1);assert(capture.Start(1));assert(capture.Observe(1,"page:actual",Event("request","reuse"),1000));auto old=capture.ReservePost(1,"page:actual","reuse");assert(old.receipt);old.data(true,std::vector<uint8_t>(2097152,'o'),false);assert(capture.retained_bytes()>=2097152);capture.Stop(1);assert(capture.retained_bytes()<2097152&&capture.pending()==0&&capture.physical()==1);
 assert(capture.Start(1));assert(capture.Observe(1,"page:actual",Event("request","reuse"),1000));auto current=capture.ReservePost(1,"page:actual","reuse");assert(current.receipt);old.data(true,{'o','l','d'},true);old.receipt();assert(capture.pending()==1&&capture.physical()==1);current.data(true,{'n','e','w'},true);assert(capture.Observe(1,"page:actual",Event("requestfinished","reuse"),1000));bodies.back()(true,{'r'},"");current.receipt();assert(capture.Ready(1));assert(capture.Export(1)->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("request")->GetDictionary("postData")->GetString("text")=="new");capture.Stop(1);assert(capture.physical()==0);
 // A genuine original body already captured from raw metadata is authoritative.
 // A later Fetch-only missing-body lookup must not replace or pin it again.
 assert(capture.Start(1));auto raw=CefDictionaryValue::Create();raw->SetString("requestId","wire-known");auto known=Request("known");known->Remove("postDataUnavailable");known->SetString("postData","actual original");raw->SetDictionary("request",known);assert(capture.OriginalRequest(1,"page:actual","",raw));assert(capture.Observe(1,"page:actual",Event("request","known"),1000));assert(!capture.ReservePost(1,"page:actual","known").receipt);capture.Stop(1);
 // Archive sealing freezes only new admission, while a real old terminal event
 // still settles its original trace reservation. Endless unreserved requests
 // never become an implicit body-read wait.
 int publications=0;std::vector<HAR::BodyReply>trace_bodies;suitecut::NativeNetworkArchives archives([&]{return now;},[](const std::string&){assert(false);});archives.SetReservation([&](int){HAR::Publication publication;publication.snapshot=[&](Dict){++publications;};return publication;});assert(archives.Attach(1,"page:trace",[&](const std::string&,const std::string&,double,HAR::BodyReply reply){trace_bodies.push_back(std::move(reply));}));assert(archives.BeginTrace(1,7));assert(archives.Observe(1,"page:trace",Event("request","old"),1000));archives.SealTrace(1,7);assert(archives.Active(1));assert(archives.Observe(1,"page:trace",Event("request","new-unadmitted"),1000));assert(archives.pending(1,7)==0);assert(archives.Observe(1,"page:trace",Event("requestfinished","old"),1000));assert(trace_bodies.size()==1&&archives.pending(1,7)==1);trace_bodies[0](true,{'r'},"");assert(publications==1&&archives.pending(1,7)==0);archives.EndTrace(1,7);archives.Clear();capture.Clear();
}
