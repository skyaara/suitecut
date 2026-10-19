// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "response_body_reads.h"
int main(){assert(cef_api_hash(CEF_API_VERSION,0));using Reads=suitecut::NativeResponseBodyReads;using Bytes=Reads::Bytes;double now=1;std::vector<Reads::Result>callbacks;std::vector<Bytes>received;int errors=0;Reads reads([&](const std::string&session,const std::string&id,double deadline,Reads::Result callback){assert(session=="actual-worker-session"&&id=="actual-request"&&deadline>=now);callbacks.push_back(std::move(callback));},[&]{return now;},[]{});auto reply=[&](bool ok,Bytes bytes,const std::string&error){if(ok){assert(error.empty());received.push_back(std::move(bytes));}else{assert(!error.empty());++errors;}};
 Bytes exact;for(int i=0;i<256;++i)exact.push_back(uint8_t(i));auto object=CefDictionaryValue::Create();object->SetString("body",CefBase64Encode(exact.data(),exact.size()));object->SetBool("base64Encoded",true);auto wrapped=CefValue::Create();wrapped->SetDictionary(object);const auto json=CefWriteJSON(wrapped,JSON_WRITER_DEFAULT).ToString();reads.Read("actual-worker-session","actual-request",31,reply);assert(reads.physical()==1);callbacks.back()(true,json);callbacks.back()(true,json);assert(reads.physical()==0&&received.size()==1&&received[0]==exact);
 // Logical timeouts retain all actual pending callbacks and enforce capacity.
 for(int i=0;i<64;++i){reads.Read("actual-worker-session","actual-request",31,reply);}
 assert(reads.physical()==64);now=31;reads.Tick(now);assert(errors==64&&reads.physical()==64&&!reads.NeedsTick());reads.Read("actual-worker-session","actual-request",61,reply);assert(errors==65&&reads.physical()==64);for(size_t i=1;i<callbacks.size();++i){callbacks[i](true,json);}
 assert(reads.physical()==0&&received.size()==1);
 // A genuine close settles remaining callers before ignoring late CDP replies.
 reads.Read("actual-worker-session","actual-request",61,reply);reads.Close();assert(errors==66&&reads.physical()==0);callbacks.back()(true,json);assert(errors==66&&received.size()==1);
 // Immediate, reentrant CDP replies erase their owner before delivering bytes.
 Reads synchronous([&](const std::string&,const std::string&,double,Reads::Result callback){callback(true,"{\"body\":\"real UTF8 café\",\"base64Encoded\":false}");},[&]{return now;},[]{});synchronous.Read("","actual-request",61,[&](bool ok,Bytes bytes,const std::string&){assert(ok&&std::string(bytes.begin(),bytes.end())=="real UTF8 café"&&synchronous.physical()==0);synchronous.Close();});assert(synchronous.physical()==0);
}
