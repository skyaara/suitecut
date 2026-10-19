// SPDX-License-Identifier: MIT
#pragma once
#include "locator_handler_async_work.h"
#include <algorithm>
#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include "include/cef_cookie.h"
#include "include/cef_values.h"
namespace suitecut {
struct HTTPCookieResultReservation {
  std::shared_ptr<std::atomic_size_t> count;size_t bytes=0;
  explicit HTTPCookieResultReservation(std::shared_ptr<std::atomic_size_t> counter):count(std::move(counter)){}
  bool Reserve(size_t size){if(bytes||size>33554432)return false;auto current=count->load(std::memory_order_acquire);do{if(current>33554432||size>33554432-current)return false;}while(!count->compare_exchange_weak(current,current+size,std::memory_order_acq_rel));bytes=size;return true;}
  ~HTTPCookieResultReservation(){count->fetch_sub(bytes,std::memory_order_acq_rel);}
};
inline std::string HTTPActualCookieIdentity(const std::string&name,const std::string&value,const std::string&domain,const std::string&path){return std::to_string(name.size())+":"+name+std::to_string(value.size())+":"+value+std::to_string(domain.size())+":"+domain+path;}
inline std::string HTTPActualCookieIdentity(CefRefPtr<CefDictionaryValue>cookie){return HTTPActualCookieIdentity(cookie->GetString("name"),cookie->GetString("value"),cookie->GetString("domain"),cookie->GetString("path"));}
// A read-only supplemental observation. Values and partition ownership always
// come from the typed browser cookie channel; only uniquely correlated real
// CEF creation timestamps may order an independent API jar. CEF omits CHIPS
// keys, so an ambiguous partition collision never supplies a guessed timestamp.
class HTTPCookieCreationVisitor:public CefCookieVisitor {
 public:
  using Times=std::map<std::string,std::optional<int64_t>>;
  using Done=std::function<void(Times,const std::string&)>;
  using Post=std::function<void(std::function<void()>)>;
  HTTPCookieCreationVisitor(Post post,Done done):post_(std::move(post)),done_(BindNativeLocatorHandlerAsync(std::move(done))){}
  ~HTTPCookieCreationVisitor()override{post_([done=std::move(done_),times=std::move(times_),error=std::move(error_)]()mutable{done(std::move(times),error);});}
  bool Visit(const CefCookie&cookie,int,int,bool&)override{
    auto key=HTTPActualCookieIdentity(CefString(&cookie.name),CefString(&cookie.value),CefString(&cookie.domain),CefString(&cookie.path));
    if(times_.size()>=4096||key.size()>1048576-bytes_){error_="Native HTTP cookie creation observation capacity exceeded";return false;}
    bytes_+=key.size();const auto found=times_.find(key);if(found==times_.end())times_.emplace(std::move(key),cookie.creation.val);else found->second.reset();return true;
  }
  void Reject(){error_="Native HTTP cookie creation observation is unavailable";}
 private:
  Post post_;Done done_;Times times_;size_t bytes_=0;std::string error_;IMPLEMENT_REFCOUNTING(HTTPCookieCreationVisitor);
};
inline CefRefPtr<CefListValue> HTTPOrderCreationCookies(CefRefPtr<CefListValue>cookies,const HTTPCookieCreationVisitor::Times&times){
  std::vector<std::pair<size_t,std::optional<int64_t>>>order;order.reserve(cookies->GetSize());
  for(size_t i=0;i<cookies->GetSize();++i){auto cookie=cookies->GetDictionary(i);const auto time=cookie&&!cookie->HasKey("partitionKey")?times.find(HTTPActualCookieIdentity(cookie)):times.end();order.emplace_back(i,time==times.end()?std::nullopt:time->second);}
  // A partition-ambiguous or unobserved timestamp cannot provide an ordering.
  if(std::all_of(order.begin(),order.end(),[](const auto&entry){return entry.second.has_value();}))std::stable_sort(order.begin(),order.end(),[](const auto&a,const auto&b){return *a.second<*b.second;});
  auto ordered=CefListValue::Create();for(const auto&[index,time]:order)ordered->SetDictionary(ordered->GetSize(),cookies->GetDictionary(index)->Copy(false));return ordered;
}

// CEF deadlocks when a list owned by a dictionary is reinserted into that
// same dictionary. Detach owned lists before replacing their parent field.
inline CefRefPtr<CefListValue> HTTPPublicationCookies(CefRefPtr<CefListValue>cookies){return cookies&&cookies->IsOwned()?cookies->Copy():cookies;}

}
