// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "har_replay.h"
#include "http_multipart.h"
using Replay=suitecut::NativeHARReplay;using Dict=Replay::Dict;
CefRefPtr<CefListValue> Headers(const std::vector<std::pair<std::string,std::string>>&input){auto result=CefListValue::Create();size_t i=0;for(const auto&[name,value]:input){auto pair=CefDictionaryValue::Create();pair->SetString("name",name);pair->SetString("value",value);result->SetDictionary(i++,pair);}return result;}
Dict Entry(const std::string&post,const std::string&body,const std::string&variant,int status=200){auto entry=CefDictionaryValue::Create();auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/actual");request->SetString("method","POST");request->SetList("headers",Headers({{"x-variant",variant}}));auto data=CefDictionaryValue::Create();data->SetString("text",post);data->SetString("_encoding","base64");request->SetDictionary("postData",data);entry->SetDictionary("request",request);auto response=CefDictionaryValue::Create();response->SetInt("status",status);response->SetString("statusText","actual status");response->SetList("headers",Headers({{"Content-Length","999"},{"Content-Encoding","gzip"},{"Set-Cookie","one=1"},{"Set-Cookie","two=2"}}));auto content=CefDictionaryValue::Create();content->SetString("text",body);content->SetString("encoding","base64");response->SetDictionary("content",content);entry->SetDictionary("response",response);return entry;}
Dict Archive(CefRefPtr<CefListValue>entries){auto archive=CefDictionaryValue::Create();auto log=CefDictionaryValue::Create();log->SetList("entries",entries);archive->SetDictionary("log",log);return archive;}
void Multipart(){
 const std::string left="recordedBoundary0123456789",right="actualBoundary9876543210";std::vector<unsigned char>input;for(unsigned i=0;i<256;++i)input.push_back(static_cast<unsigned char>(i));
 const std::vector<suitecut::NativeHTTPMultipartPart>parts={{"same","","",0,2,false},{"same","","",2,2,false},{"file","actual.bin","application/octet-stream",4,input.size()-4,true}};
 const auto encode=[&](const std::string&boundary,const auto&fields){std::vector<unsigned char>body;std::string error;assert(suitecut::EncodeNativeHTTPMultipart(input,fields,boundary,2097152,body,error));return CefBase64Encode(body.data(),body.size()).ToString();};
 const auto recorded=encode(left,parts),actual=encode(right,parts);assert(recorded!=actual);auto entry=Entry(recorded,"T0s=","multipart");entry->GetDictionary("request")->SetList("headers",Headers({{"Content-Type","multipart/form-data; boundary=\""+left+"\""}}));auto entries=CefListValue::Create();entries->SetDictionary(0,entry);Replay replay;std::string error;const auto archive=Archive(entries);entries=archive->GetDictionary("log")->GetList("entries");entry=entries->GetDictionary(0);assert(entry->IsValid()&&entries->IsValid()&&replay.Load(archive,error));
 auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/actual");request->SetString("method","POST");request->SetString("postDataBase64",actual);auto headers=CefDictionaryValue::Create();headers->SetString("content-type","multipart/form-data; boundary="+right);request->SetDictionary("headers",headers);headers=request->GetDictionary("headers");assert(headers->IsValid());const auto original=request->Copy(false),original_entry=entry->Copy(false);const auto match=replay.Lookup(request);assert(match&&match->body==std::vector<uint8_t>({'O','K'}));assert(request->IsEqual(original)&&entry->IsEqual(original_entry));
 // Duplicate or absent outer type cannot authorize boundary equivalence.
 headers->SetString("Content-Type","multipart/form-data; boundary="+right);assert(!replay.Lookup(request));headers->Remove("Content-Type");headers->Remove("content-type");assert(!replay.Lookup(request));headers->SetString("content-type","multipart/form-data; boundary="+right+";boundary="+right);assert(!replay.Lookup(request));headers->SetString("content-type","multipart/form-data; boundary="+right);
 auto changed=parts;changed[0].name="different";request->SetString("postDataBase64",encode(right,changed));assert(!replay.Lookup(request));request->SetString("postDataBase64",actual);input[100]^=1;request->SetString("postDataBase64",encode(right,parts));assert(!replay.Lookup(request));request->SetString("postDataBase64",actual);
 entry->GetDictionary("request")->SetList("headers",Headers({{"content-type","multipart/form-data;boundary="+left},{"Content-Type","multipart/form-data;boundary="+left}}));assert(replay.Load(archive,error)&&!replay.Lookup(request));
}
void HeaderScoringAndIndex(){
 auto first=Entry("","Zmlyc3Q=","unused");auto second=Entry("","c2Vjb25k","unused");
 first->GetDictionary("request")->SetList("headers",Headers({{"X-DUP","a"},{"x-dup","b"},{"X-OTHER","one"}}));
 second->GetDictionary("request")->SetList("headers",Headers({{"x-dup","a"},{"X-OTHER","one"}}));
 auto unrelated=Entry("","b3RoZXI=","unused");unrelated->GetDictionary("request")->SetString("url","https://native.test/unrelated");
 auto different_method=Entry("","bWV0aG9k","unused");different_method->GetDictionary("request")->SetString("method","GET");
 auto entries=CefListValue::Create();entries->SetDictionary(0,unrelated);entries->SetDictionary(1,first);entries->SetDictionary(2,different_method);entries->SetDictionary(3,second);
 Replay replay;std::string error;assert(replay.Load(Archive(entries),error));
 auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/actual");request->SetString("method","POST");
 auto headers=CefDictionaryValue::Create();headers->SetString("X-Dup","a");headers->SetString("x-DUP","b");headers->SetString("x-other","one");request->SetDictionary("headers",headers);
 const auto body=[&](const Replay&source){auto match=source.Lookup(request);assert(match);return std::string(match->body.begin(),match->body.end());};
 // Differently cased duplicate incoming names retain every value; each
 // recorded pair scores once. Equal scores keep original archive order.
 assert(body(replay)=="first");headers=request->GetDictionary("headers");headers->Remove("x-DUP");assert(body(replay)=="first");
 headers->SetString("x-other","different");headers->SetString("X-Dup","different");assert(body(replay)=="first");
 // The index owns numeric positions, so normal copy/move/clear lifetimes
 // cannot leave references into the original archive's strings or storage.
 Replay copy=replay;replay.Clear();assert(!replay.Lookup(request)&&body(copy)=="first");Replay moved=std::move(copy);assert(body(moved)=="first");
 request->SetString("method","GET");assert(body(moved)=="method");request->SetString("method","POST");request->SetString("url","https://native.test/unrelated");assert(body(moved)=="other");
 request->SetString("url","https://native.test/missing");assert(!moved.Lookup(request));
 auto bad=CefListValue::Create();bad->SetDictionary(0,Entry("","invalid base64","unused"));assert(!moved.Load(Archive(bad),error));request->SetString("url","https://native.test/actual");assert(body(moved)=="first");
 auto empty=CefListValue::Create();assert(moved.Load(Archive(empty),error)&&!moved.Lookup(request));
}
int main(){assert(cef_api_hash(CEF_API_VERSION,0));Replay replay;std::string error;std::vector<uint8_t>bytes;for(int i=0;i<256;++i)bytes.push_back(uint8_t(i));auto exact=CefBase64Encode(bytes.data(),bytes.size()).ToString();auto entries=CefListValue::Create();entries->SetDictionary(0,Entry(exact,exact,"first"));entries->SetDictionary(1,Entry(exact,"YWN0dWFsIGJvZHk=","second"));assert(replay.Load(Archive(entries),error)&&replay.size()==2&&replay.bytes()>512);
 auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/actual");request->SetString("method","POST");auto actual=CefListValue::Create();auto part=CefDictionaryValue::Create();part->SetString("bytes",exact);actual->SetDictionary(0,part);request->SetList("postDataEntries",actual->Copy());auto headers=CefDictionaryValue::Create();headers->SetString("X-VARIANT","second");request->SetDictionary("headers",headers);auto matched=replay.Lookup(request);assert(matched&&std::string(matched->body.begin(),matched->body.end())=="actual body");assert(matched->headers.size()==3&&matched->headers[0].first=="Set-Cookie"&&matched->headers[1].second=="two=2"&&matched->headers.back().second=="11");auto unmatched=CefDictionaryValue::Create();unmatched->SetString("X-VARIANT","no actual matching header");request->SetDictionary("headers",unmatched);auto fallback=replay.Lookup(request);assert(fallback&&fallback->body==bytes);
 request->SetString("method","GET");assert(!replay.Lookup(request));request->SetString("method","POST");request->SetString("url","https://native.test/different");assert(!replay.Lookup(request));request->SetString("url","https://native.test/actual");request->Remove("postDataEntries");request->SetBool("hasPostData",true);assert(!replay.Lookup(request));
 // Invalid replacement archive never partially replaces an admitted archive.
 auto bad_entries=CefListValue::Create();bad_entries->SetDictionary(0,Entry(exact,"invalid base64","first"));assert(!replay.Load(Archive(bad_entries),error)&&replay.size()==2);auto absent=Entry(exact,exact,"first");absent->GetDictionary("response")->GetDictionary("content")->Remove("text");auto unavailable=CefListValue::Create();unavailable->SetDictionary(0,absent);assert(replay.Load(Archive(unavailable),error));request->Remove("hasPostData");request->SetList("postDataEntries",actual->Copy());assert(!replay.Lookup(request));
 // A real redirect or HEAD response legitimately has no captured body bytes.
 auto redirect=Entry(exact,exact,"first",302);redirect->GetDictionary("response")->GetDictionary("content")->Remove("text");redirect->GetDictionary("response")->SetList("headers",Headers({{"Location","/actual-next"}}));auto redirect_entries=CefListValue::Create();redirect_entries->SetDictionary(0,redirect);assert(replay.Load(Archive(redirect_entries),error));auto moved=replay.Lookup(request);assert(moved&&moved->status==302&&moved->body.empty()&&moved->headers.front().second=="/actual-next");replay.Clear();assert(replay.size()==0&&replay.bytes()==0);
 // HEAD/304 carry representation metadata, not the length of their empty body.
 for(const bool head:{true,false}){
  auto representation=Entry("","","actual",head?200:304);auto req=representation->GetDictionary("request");req->SetString("method",head?"HEAD":"GET");req->Remove("postData");auto res=representation->GetDictionary("response");res->GetDictionary("content")->Remove("text");res->SetList("headers",Headers({{"Content-Length","256"},{"Content-Encoding","gzip"}}));
  auto list=CefListValue::Create();list->SetDictionary(0,representation);assert(replay.Load(Archive(list),error));auto incoming=CefDictionaryValue::Create();incoming->SetString("url","https://native.test/actual");incoming->SetString("method",head?"HEAD":"GET");incoming->SetDictionary("headers",CefDictionaryValue::Create());auto value=replay.Lookup(incoming);assert(value&&value->body.empty()&&value->headers.size()==2&&value->headers[0].second=="256"&&value->headers[1].second=="gzip");
 }
 HeaderScoringAndIndex();
 Multipart();
}
