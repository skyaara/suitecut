// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "http_storage_visitor.h"

int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Visitor=suitecut::NativeHTTPStorageVisitor;
  auto budget=std::make_shared<std::atomic<size_t>>(0);auto control=std::make_shared<Visitor::Control>();
  CefCookie cookie;CefString(&cookie.name)="session";CefString(&cookie.value)="actual";CefString(&cookie.domain)=".native.test";CefString(&cookie.path)="/";cookie.secure=true;cookie.httponly=true;cookie.same_site=CEF_COOKIE_SAME_SITE_STRICT_MODE;
  bool finished=false,remove=false;
  {
    CefRefPtr<Visitor> visitor=new Visitor(control,budget,[](std::function<void()> callback){callback();},[&](Visitor::Dict state,const std::string& error){
      assert(error.empty() && !budget->load());const auto cookies=state->GetList("cookies");assert(cookies->GetSize()==1 && !state->GetList("origins")->GetSize());
      auto actual=cookies->GetDictionary(0);assert(actual->GetString("name")=="session" && actual->GetString("value")=="actual" && actual->GetString("domain")==".native.test");assert(actual->GetDouble("expires")==-1 && actual->GetBool("httpOnly") && actual->GetBool("secure") && actual->GetString("sameSite")=="Strict");finished=true;
    });assert(visitor->Visit(cookie,0,1,remove));assert(budget->load());
  }
  assert(finished);
  std::vector<std::function<void()>> pending;
  {
    auto cancellation=std::make_shared<Visitor::Control>();CefRefPtr<Visitor> visitor=new Visitor(cancellation,budget,[&](std::function<void()> callback){pending.push_back(std::move(callback));},[&](Visitor::Dict,const std::string& error){assert(!error.empty());});
    assert(visitor->Visit(cookie,0,1,remove));cancellation->canceled.store(true);assert(!visitor->Visit(cookie,0,1,remove));
  }
  assert(budget->load() && pending.size()==1);pending.front()();assert(!budget->load());pending.clear();
  {
    budget->store(Visitor::kBudget);CefRefPtr<Visitor> visitor=new Visitor(control,budget,[](std::function<void()> callback){callback();},[](Visitor::Dict,const std::string& error){assert(error.find("budget")!=std::string::npos);});assert(!visitor->Visit(cookie,0,1,remove));
  }
  assert(budget->load()==Visitor::kBudget);budget->store(0);
}
