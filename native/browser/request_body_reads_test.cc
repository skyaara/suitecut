// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "request_body_reads.h"
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));using Reads=suitecut::NativeRequestBodyReads;using Bytes=Reads::Bytes;
 double now=1;std::vector<Reads::Result>acks;int errors=0;std::vector<Bytes>received;
 Reads reads([&](const std::string&session,const std::string&request,double deadline,Reads::Result ack){assert(session=="actual-session"&&request=="actual-post"&&deadline==31);acks.push_back(std::move(ack));},[&]{return now;},[]{});
 const auto reply=[&](bool ok,Bytes bytes,bool text,const std::string&error){if(ok){assert(error.empty()&&!text);received.push_back(std::move(bytes));}else{assert(!error.empty());++errors;}};
 Bytes exact;for(int i=0;i<256;++i)exact.push_back(uint8_t(i));auto value=CefDictionaryValue::Create();value->SetString("postData",CefBase64Encode(exact.data(),exact.size()));value->SetBool("base64Encoded",true);auto wrapper=CefValue::Create();wrapper->SetDictionary(value);const auto json=CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString();
 reads.Read("actual-session","actual-post",31,reply);assert(reads.physical()==1);acks.back()(true,json);acks.back()(true,json);assert(reads.physical()==0&&received.size()==1&&received[0]==exact);
 // A canceled consumer never receives data or refunds its outstanding receipt.
 reads.Read("actual-session","actual-post",31,reply);reads.CancelAll();assert(reads.physical()==1);acks.back()(true,json);assert(reads.physical()==0&&received.size()==1&&errors==0);
 for(int i=0;i<64;++i){reads.Read("actual-session","actual-post",31,reply);}
 assert(reads.physical()==64);now=31;reads.Tick(now);assert(errors==64&&reads.physical()==64&&!reads.NeedsTick());
 now=2;reads.Read("actual-session","actual-post",31,reply);assert(errors==65&&reads.physical()==64);for(size_t i=2;i<acks.size();++i)acks[i](true,json);assert(reads.physical()==0&&received.size()==1);
 // The original deadline is checked on actual ACK before a periodic Tick.
 now=1;reads.Read("actual-session","actual-post",31,reply);now=31;acks.back()(true,json);assert(errors==66&&reads.physical()==0);
 int receipts=0;now=1;reads.Read("actual-session","actual-post",31,reply,[&]{++receipts;});now=31;reads.Tick(now);assert(receipts==0&&reads.physical()==1);acks.back()(true,json);assert(receipts==1&&reads.physical()==0);
 now=1;reads.Read("actual-session","actual-post",31,reply,[&]{++receipts;});reads.CancelAll();assert(receipts==1);reads.Close();assert(receipts==2&&reads.physical()==0);acks.back()(true,json);assert(receipts==2);
 Reads synchronous([&](const std::string&,const std::string&,double,Reads::Result ack){ack(true,"{\"postData\":\"actual UTF8 café\",\"base64Encoded\":false}");},[&]{return now;},[]{});
 synchronous.Read("","actual-post",31,[&](bool ok,Bytes bytes,bool text,const std::string&){assert(ok&&text&&std::string(bytes.begin(),bytes.end())=="actual UTF8 café"&&synchronous.physical()==0);synchronous.Close();});
 // A synchronous genuine receipt may dispose its owning native controller.
 std::unique_ptr<Reads> disposed;
 disposed=std::make_unique<Reads>([](const std::string&,const std::string&,double,Reads::Result ack){ack(true,"{\"postData\":\"actual\",\"base64Encoded\":false}");},[&]{return now;},[]{assert(false);});
 disposed->Read("","actual-post",31,[&](bool ok,Bytes,bool,const std::string&){assert(ok);disposed.reset();});assert(!disposed);
}
