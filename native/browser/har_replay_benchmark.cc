// SPDX-License-Identifier: MIT
// Standalone paired microbenchmark. Compile with the ordinary CEF test linker
// flags and -DSUITECUT_HAR_REPLAY_BASELINE='"/path/to/pristine/har_replay.h"'.
// The immutable baseline is compiled into this same process, using identical
// CEF dictionaries, compiler options, warmups and matching assertions.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "har_replay.h"
#ifdef SUITECUT_HAR_REPLAY_BASELINE
#define NativeHARReplay NativeHARReplayBaseline
#include SUITECUT_HAR_REPLAY_BASELINE
#undef NativeHARReplay
#else
#error "Provide a pristine SUITECUT_HAR_REPLAY_BASELINE header"
#endif
namespace {
using Dict=CefRefPtr<CefDictionaryValue>;
using Replay=suitecut::NativeHARReplay;
using Baseline=suitecut::NativeHARReplayBaseline;
using Clock=std::chrono::steady_clock;
struct Fixture {Dict archive,request;std::string expected;};
Fixture Make(size_t count,size_t header_count,bool same_url){
  auto entries=CefListValue::Create();
  for(size_t i=0;i<count;++i){
    auto request=CefDictionaryValue::Create();
    request->SetString("url",same_url?"https://native.test/same":"https://native.test/item/"+std::to_string(i));
    request->SetString("method","GET");
    auto headers=CefListValue::Create();
    for(size_t h=0;h<header_count;++h){auto pair=CefDictionaryValue::Create();pair->SetString("name","X-Header-"+std::to_string(h));pair->SetString("value",h+1==header_count?std::to_string(i):"actual-value");headers->SetDictionary(h,pair);}
    request->SetList("headers",headers);
    auto response=CefDictionaryValue::Create();response->SetInt("status",200);response->SetList("headers",CefListValue::Create());
    auto content=CefDictionaryValue::Create();content->SetString("text",std::to_string(i));response->SetDictionary("content",content);
    auto entry=CefDictionaryValue::Create();entry->SetDictionary("request",request);entry->SetDictionary("response",response);entries->SetDictionary(i,entry);
  }
  auto log=CefDictionaryValue::Create();log->SetList("entries",entries);auto archive=CefDictionaryValue::Create();archive->SetDictionary("log",log);
  auto request=CefDictionaryValue::Create();request->SetString("url",same_url?"https://native.test/same":"https://native.test/item/"+std::to_string(count-1));request->SetString("method","GET");
  auto headers=CefDictionaryValue::Create();for(size_t h=0;h<header_count;++h)headers->SetString("x-header-"+std::to_string(h),h+1==header_count?std::to_string(count-1):"actual-value");request->SetDictionary("headers",headers);
  return {archive,request,std::to_string(count-1)};
}
template<class T>double Measure(const T&replay,const Fixture&fixture){
  const auto started=Clock::now();const auto matched=replay.Lookup(fixture.request);const auto finished=Clock::now();
  assert(matched&&std::string(matched->body.begin(),matched->body.end())==fixture.expected);
  return std::chrono::duration<double,std::micro>(finished-started).count();
}
void Scenario(const char*name,size_t count,size_t headers,bool same_url){
  auto fixture=Make(count,headers,same_url);Replay candidate;Baseline baseline;std::string error;
  assert(baseline.Load(fixture.archive,error)&&candidate.Load(fixture.archive,error));
  Measure(baseline,fixture);Measure(candidate,fixture);
  std::vector<double> baseline_samples,candidate_samples;
  for(int repeat=0;repeat<7;++repeat){
    if(repeat%2){candidate_samples.push_back(Measure(candidate,fixture));baseline_samples.push_back(Measure(baseline,fixture));}
    else{baseline_samples.push_back(Measure(baseline,fixture));candidate_samples.push_back(Measure(candidate,fixture));}
  }
  const auto median=[](auto samples){std::sort(samples.begin(),samples.end());return samples[samples.size()/2];};
  const auto before=median(baseline_samples),after=median(candidate_samples);
  std::cout<<"{\"scenario\":\""<<name<<"\",\"entries\":"<<count<<",\"headers_per_entry\":"<<headers<<",\"baseline_median_us\":"<<before<<",\"candidate_median_us\":"<<after<<",\"speedup\":"<<before/after<<",\"index_bytes\":"<<count*sizeof(size_t);
  const auto emit=[](const char*name,const auto&samples){std::cout<<",\""<<name<<"\":[";for(size_t i=0;i<samples.size();++i){if(i)std::cout<<',';std::cout<<samples[i];}std::cout<<']';};
  emit("baseline_samples_us",baseline_samples);emit("candidate_samples_us",candidate_samples);std::cout<<"}\n";
  // A miss must agree and must not require decoding request body bytes.
  fixture.request->SetString("url","https://native.test/absent");assert(!candidate.Lookup(fixture.request)&&!baseline.Lookup(fixture.request));
}
}
int main(){assert(cef_api_hash(CEF_API_VERSION,0));Scenario("duplicate_url_headers",1024,128,true);Scenario("distinct_urls",4096,16,false);}
