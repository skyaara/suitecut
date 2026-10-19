// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <atomic>
#include <optional>
#include <cmath>
#include <memory>
#include "network_post_data.h"

namespace suitecut {
// Only native callers supply these opaque context references; this is never
// an SDK/context-ID lookup fallback. A closing registry supplies a null ref.
template<class Context>bool NativeRouteFetchContextMatches(const Context& expected,const Context& current){
  return expected.id>0&&expected.cef&&current.id==expected.id&&current.cef==expected.cef;
}
inline std::optional<double> NativeRouteFetchDeadline(CefRefPtr<CefDictionaryValue> options,double now,double original,double paused,std::string& error){
  if(!options||!std::isfinite(now)||!std::isfinite(original)||!std::isfinite(paused)||original<=now||paused<=now){error="Native route fetch original operation deadline expired";return std::nullopt;}
  if(options->HasKey("timeoutMs")&&(options->GetType("timeoutMs")!=VTYPE_INT||options->GetInt("timeoutMs")<0)){error="Invalid native route fetch timeout";return std::nullopt;}
  const int timeout=options->HasKey("timeoutMs")?options->GetInt("timeoutMs"):30000;
  double deadline=std::min(original,paused);if(timeout>0)deadline=std::min(deadline,now+double(timeout));
  return deadline;
}
// These inputs are native snapshots of one actual paused request. The SDK
// supplies overrides, never the request identity, method or inherited body.
inline CefRefPtr<CefDictionaryValue> NativeRouteFetchRequest(
    CefRefPtr<CefDictionaryValue> request,CefRefPtr<CefDictionaryValue> options,
    double now,double original,double paused,std::string& error) {
  auto fail=[&](const char* message)->CefRefPtr<CefDictionaryValue>{error=message;return nullptr;};
  if(!request||!NativeRouteFetchDeadline(options,now,original,paused,error))return nullptr;
  for(const char* field:{"maxRedirects","maxRetries","timeoutMs"})if(options->HasKey(field)&&options->GetType(field)!=VTYPE_INT)return fail("Invalid native route fetch limits");
  const int redirects=options->HasKey("maxRedirects")?options->GetInt("maxRedirects"):20;
  const int retries=options->HasKey("maxRetries")?options->GetInt("maxRetries"):0;
  const int timeout=options->HasKey("timeoutMs")?options->GetInt("timeoutMs"):30000;
  if(redirects<0||redirects>100||retries<0||retries>100||timeout<0)return fail("Invalid native route fetch limits");
  for(const char* field:{"url","method","postData"})if(options->HasKey(field)&&options->GetType(field)!=VTYPE_STRING)return fail("Invalid native route fetch override");
  if(options->HasKey("headers")&&options->GetType("headers")!=VTYPE_LIST)return fail("Invalid native route fetch headers");
  auto result=CefDictionaryValue::Create();result->SetString("action","fetch");
  result->SetString("url",options->HasKey("url")?options->GetString("url"):request->GetString("url"));
  result->SetString("method",options->HasKey("method")?options->GetString("method"):request->GetString("method"));
  result->SetInt("apiContextId",options->GetInt("apiContextId"));result->SetInt("maxRedirects",redirects);result->SetInt("maxRetries",retries);result->SetInt("timeoutMs",timeout);
  if(options->HasKey("headers"))result->SetList("headers",options->GetList("headers")->Copy());
  else if(auto inherited=request->GetDictionary("headers")){
    auto headers=CefListValue::Create();CefDictionaryValue::KeyList keys;inherited->GetKeys(keys);
    for(const auto& key:keys){if(inherited->GetType(key)!=VTYPE_STRING)return fail("Native route request headers are unavailable");auto pair=CefDictionaryValue::Create();pair->SetString("name",key);pair->SetString("value",inherited->GetString(key));headers->SetDictionary(headers->GetSize(),pair);}result->SetList("headers",headers);
  }
  if(options->HasKey("postData")){
    auto exact=CefDictionaryValue::Create();exact->SetString("postDataBase64",options->GetString("postData"));
    if(CaptureNativePostData(exact).kind==NativePostData::Kind::Unavailable)return fail("Invalid or oversized native route fetch postData");
    result->SetString("body",options->GetString("postData"));
  }
  else {
    const auto body=CaptureNativePostData(request);
    if(body.kind==NativePostData::Kind::Unavailable)return fail("Native route request body is unavailable; provide postData explicitly");
    if(body.kind==NativePostData::Kind::Text){const auto text=request->GetString("postData").ToString();result->SetString("body",CefBase64Encode(text.data(),text.size()));}
    else if(body.kind==NativePostData::Kind::Binary)result->SetString("body",CefBase64Encode(body.bytes.data(),body.bytes.size()));
  }
  return result;
}
// Shared with the real HTTP state and outstanding native cookie callbacks.
// Cancellation closes logical delivery; only actual callback destruction drops
// this physical ownership, including after the NetworkControls owner is gone.
struct NativeRouteFetchLease {
  explicit NativeRouteFetchLease(std::shared_ptr<std::atomic_size_t> count):count_(std::move(count)){++*count_;}
  ~NativeRouteFetchLease(){--*count_;}
  NativeRouteFetchLease(const NativeRouteFetchLease&)=delete;
  std::shared_ptr<std::atomic_size_t> count_;
};
} // namespace suitecut
