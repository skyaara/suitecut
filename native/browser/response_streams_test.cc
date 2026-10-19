// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "response_streams.h"
using Streams=suitecut::NativeResponseStreams;using Dict=Streams::Dict;
Dict Chunk(const std::string&base64){auto value=CefDictionaryValue::Create();value->SetString("data",base64);auto bytes=CefBase64Decode(base64);value->SetInt("dataLength",bytes?static_cast<int>(bytes->GetSize()):0);return value;}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));double now=1;std::vector<Streams::Result>acks;Streams stream([&](const auto&,const auto&,double,Streams::Result done){acks.push_back(std::move(done));},[&]{return now;},[]{});
 assert(!stream.Start("","doc","Document",100));assert(acks.empty());
 assert(stream.Start("child","image","Image",100));assert(stream.physical()==1);
 // Genuine data events can be delivered before the enable reply; the actual
 // buffered prefix must still precede those already-observed streamed spans.
 stream.Data("child","image",Chunk("/w=="));stream.Finished("child","image",true);assert(!stream.Body("child","image"));
 acks[0](true,R"({"bufferedData":"AA=="})");assert(stream.physical()==0&&stream.Body("child","image")==Streams::Bytes({0,255}));
 assert(stream.Move("child","parent","image"));assert(!stream.Body("child","image"));assert(stream.Body("parent","image")==Streams::Bytes({0,255}));
 const auto recovered=stream.Resolve("parent","image",true,R"({"body":"","base64Encoded":true})");assert(recovered.first&&recovered.second.find("AP8=")!=std::string::npos);
 const auto primary=stream.Resolve("parent","image",true,R"({"body":"AQ==","base64Encoded":true})");assert(primary.second==R"({"body":"AQ==","base64Encoded":true})");
 assert(stream.Resolve("parent","image",false,"Actual Chromium body unavailable").first);
 const auto malformed=stream.Resolve("parent","image",true,"{}");assert(malformed.second=="{}");
 stream.Discard("parent","image");assert(stream.bytes()==0&&stream.entries()==0);
 // Expiry and logical ID replacement never release an actual pending ACK.
 assert(stream.Start("old","reuse","Image",10));stream.Tick(10);assert(stream.entries()==0&&stream.physical()==1);
 assert(stream.Start("old","reuse","Image",100));acks[1](true,R"({"bufferedData":"/w=="})");assert(stream.physical()==1&&!stream.Body("old","reuse"));
 acks[2](true,R"({"bufferedData":"AA=="})");stream.Finished("old","reuse",true);assert(stream.Body("old","reuse")==Streams::Bytes({0}));stream.Discard("old","reuse");
 // A migrated cache keeps its in-flight command's original physical session.
 assert(stream.Start("original","moving","Image",100));assert(stream.Move("original","destination","moving"));stream.SessionClosed("destination");assert(stream.physical()==1);stream.SessionClosed("original");assert(stream.physical()==0);acks[3](true,R"({"bufferedData":"AA=="})");assert(stream.entries()==0);
 assert(stream.Start("","bad","Image",100));acks[4](true,R"({"bufferedData":"not-base64"})");assert(stream.entries()==0&&stream.bytes()==0);
 // Reaching the exact resource bound is valid; another genuine byte drops
 // only optional byte retention, while its physical receipt remains owned.
 assert(stream.Start("","bounded","Image",100));std::vector<uint8_t>block(1048576,255);const auto encoded=CefBase64Encode(block.data(),block.size()).ToString();stream.Data("","bounded",Chunk(encoded));stream.Data("","bounded",Chunk(encoded));assert(stream.bytes()==2097152);stream.Data("","bounded",Chunk("AA=="));assert(stream.entries()==0&&stream.bytes()==0&&stream.physical()==1);acks[5](true,R"({"bufferedData":""})");assert(stream.physical()==0);
 assert(stream.Start("","missing-span","Image",100));auto absent=CefDictionaryValue::Create();absent->SetInt("dataLength",5);stream.Data("","missing-span",absent);assert(stream.entries()==0&&stream.physical()==1);acks[6](true,R"({"bufferedData":""})");assert(stream.physical()==0);
 // Redirects sharing an already-streaming physical ID are deliberately
 // excluded: a second buffered prefix could overlap genuine streamed spans.
 auto initial=CefDictionaryValue::Create();initial->SetString("requestId","redirect");initial->SetString("type","Image");stream.Event("Network.requestWillBeSent",initial,"");const auto before_redirect=acks.size();auto redirected=initial->Copy(false);redirected->SetDictionary("redirectResponse",CefDictionaryValue::Create());stream.Event("Network.requestWillBeSent",redirected,"");assert(acks.size()==before_redirect&&stream.entries()==0&&stream.physical()==1);acks.back()(true,R"({"bufferedData":"AA=="})");assert(stream.physical()==0);
 // Full retained-byte and physical quotas are real independent ledgers.
 std::vector<Streams::Result>budget_acks;Streams budget([&](const auto&,const auto&,double,Streams::Result done){budget_acks.push_back(std::move(done));},[&]{return now;},[]{});
 std::vector<uint8_t>whole(2097152,1);const auto whole64=CefBase64Encode(whole.data(),whole.size()).ToString();
 for(int i=0;i<8;++i){assert(budget.Start("","cache"+std::to_string(i),"Image",100));budget_acks.back()(true,"{\"bufferedData\":\""+whole64+"\"}");budget.Finished("","cache"+std::to_string(i),true);}
 assert(budget.bytes()==16*1048576&&budget.entries()==8&&budget.physical()==0);
 assert(budget.Start("","overflow","Image",100));budget_acks.back()(true,R"({"bufferedData":"AA=="})");assert(budget.bytes()==16*1048576&&budget.entries()==8&&budget.physical()==0);budget.Close();
 std::vector<Streams::Result>physical_acks;Streams held([&](const auto&,const auto&,double,Streams::Result done){physical_acks.push_back(std::move(done));},[&]{return now;},[]{});
 for(int i=0;i<64;++i){assert(held.Start("","held"+std::to_string(i),"Image",100));}
 assert(!held.Start("","over-physical","Image",100)&&held.physical()==64);
 held.Tick(100);assert(held.entries()==0&&held.physical()==64);for(auto&ack:physical_acks)ack(true,R"({"bufferedData":""})");assert(held.physical()==0);held.Close();
 // A synchronous transport callback may destroy the owning native collector.
 std::unique_ptr<Streams>owner;owner=std::make_unique<Streams>([&](const auto&,const auto&,double,Streams::Result done){owner.reset();done(true,R"({"bufferedData":""})");},[&]{return now;},[]{});owner->Start("","reentrant","Image",100);
 stream.Close();assert(stream.bytes()==0&&stream.physical()==0);
}
