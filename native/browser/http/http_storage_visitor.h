// SPDX-License-Identifier: MIT
#pragma once
#include "locators/locator_handler_async_work.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include "include/cef_cookie.h"
#include "include/cef_parser.h"

namespace suitecut {
// A real CEF cookie traversal. Cancellation releases the reply owner immediately;
// this visitor stays physically accounted until CEF releases its callback.
class NativeHTTPStorageVisitor : public CefCookieVisitor {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Done=std::function<void(Dict,const std::string&)>;
  using Post=std::function<void(std::function<void()>)>;
  struct Control {std::atomic<bool> canceled{false};};
  NativeHTTPStorageVisitor(std::shared_ptr<Control> control,std::shared_ptr<std::atomic<size_t>> budget,Post post,Done done)
      :control_(std::move(control)),budget_(std::move(budget)),post_(std::move(post)),done_(BindNativeLocatorHandlerAsync(std::move(done))),cookies_(CefListValue::Create()){}
  ~NativeHTTPStorageVisitor() override {
    auto result=CefDictionaryValue::Create();result->SetList("cookies",cookies_);result->SetList("origins",CefListValue::Create());
    const auto budget=budget_;const auto bytes=bytes_;auto done=std::move(done_);auto error=std::move(error_);
    if(control_->canceled.load())error="Native API storage query was canceled";
    post_([budget,bytes,done=std::move(done),result,error=std::move(error)]{budget->fetch_sub(bytes);done(result,error);});
  }
  bool Visit(const CefCookie& cookie,int,int,bool&) override {
    if(control_->canceled.load())return false;
    auto item=Cookie(cookie);auto wrapper=CefValue::Create();wrapper->SetDictionary(item);
    const size_t bytes=CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString().size();
    // Byte accounting for diagnostics only; no cookie count or byte limit.
    budget_->fetch_add(bytes);
    bytes_+=bytes;cookies_->SetDictionary(cookies_->GetSize(),item);return true;
  }
  void Reject(){error_="CEF cookie storage is unavailable";}
  static Dict Cookie(const CefCookie& cookie) {
    auto item=CefDictionaryValue::Create();item->SetString("name",CefString(&cookie.name));item->SetString("value",CefString(&cookie.value));
    item->SetString("domain",CefString(&cookie.domain));item->SetString("path",CefString(&cookie.path));
    double expiration=-1;
    if(cookie.has_expires){cef_time_t time{};cef_time_from_basetime(cookie.expires,&time);cef_time_to_doublet(&time,&expiration);}
    item->SetDouble("expires",expiration);item->SetBool("httpOnly",cookie.httponly);item->SetBool("secure",cookie.secure);
    item->SetString("sameSite",cookie.same_site==CEF_COOKIE_SAME_SITE_STRICT_MODE?"Strict":cookie.same_site==CEF_COOKIE_SAME_SITE_NO_RESTRICTION?"None":"Lax");return item;
  }
 private:
  std::shared_ptr<Control> control_;std::shared_ptr<std::atomic<size_t>> budget_;Post post_;Done done_;CefRefPtr<CefListValue> cookies_;size_t bytes_=0;std::string error_;
  IMPLEMENT_REFCOUNTING(NativeHTTPStorageVisitor);
};
}  // namespace suitecut
