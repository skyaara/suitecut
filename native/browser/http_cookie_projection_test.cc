// SPDX-License-Identifier: MIT
#include "http_cookie_projection.h"
#include "http_cookie_creation.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <iostream>
int main(){cef_api_hash(CEF_API_VERSION,0);const auto cookies=CefParseJSON(R"([{"name":"first","value":"native","domain":"LOCALHOST","path":"/","expires":-1,"secure":true},{"name":"child","value":"yes","domain":".localhost","path":"/input","expires":-1,"secure":false},{"name":"old","value":"no","domain":"localhost","path":"/","expires":1,"secure":false},{"name":"partition","value":"private","domain":"localhost","path":"/","expires":-1,"secure":true,"partitionKey":"https://top.test"}])",JSON_PARSER_RFC)->GetList();
  assert(suitecut::HTTPJarHeader(cookies,"http://localhost/input/button",100)=="first=native; child=yes");
  assert(suitecut::HTTPJarHeader(cookies,"http://localhost/input-other",100)=="first=native");
  assert(suitecut::HTTPJarHeader(cookies,"http://sub.localhost/input/button",100)=="child=yes");
  assert(suitecut::HTTPJarHeader(cookies,"https://evil-localhost/input/button",100).empty());
  assert(suitecut::HTTPJarHeader(cookies,"https://localhost.evil.test/input/button",100).empty());
  for(const auto&host:{"localhost","a.localhost","127.0.0.1","127.1.2.3","::1","[::1]"})assert(suitecut::HTTPLoopback(host));
  for(const auto&host:{"localhost.evil.test","xlocalhost","128.0.0.1","0.0.0.0","::2"})assert(!suitecut::HTTPLoopback(host));
  // Creation metadata must be a genuine observation; path order is never
  // silently promoted to API creation order when CEF omits partition identity.
  auto paths=CefParseJSON(R"([{"name":"later","value":"v2","domain":"localhost","path":"/input"},{"name":"earlier","value":"v1","domain":"localhost","path":"/"}])",JSON_PARSER_RFC)->GetList();
  suitecut::HTTPCookieCreationVisitor::Times times;
  times[suitecut::HTTPActualCookieIdentity(paths->GetDictionary(0))]=200;
  times[suitecut::HTTPActualCookieIdentity(paths->GetDictionary(1))]=100;
  auto ordered=suitecut::HTTPOrderCreationCookies(paths,times);assert(ordered->GetDictionary(0)->GetString("name")=="earlier");
  times.begin()->second.reset();ordered=suitecut::HTTPOrderCreationCookies(paths,times);assert(ordered->GetDictionary(0)->GetString("name")=="later");
  times[suitecut::HTTPActualCookieIdentity(paths->GetDictionary(1))]=100;paths->GetDictionary(0)->SetString("partitionKey","https://top.test");ordered=suitecut::HTTPOrderCreationCookies(paths,times);assert(ordered->GetDictionary(0)->GetString("name")=="later");
  // Budget serialization must not transfer the live asynchronous result to
  // the temporary CefValue's ownership.
  auto output=CefDictionaryValue::Create();output->SetList("cookies",paths->Copy());
  {auto temporary=CefValue::Create();temporary->SetDictionary(output->Copy(false));assert(!CefWriteJSON(temporary,JSON_WRITER_DEFAULT).empty());}
  assert(output->IsValid()&&output->GetList("cookies")->IsValid());
  auto retained=output->GetList("cookies");
  {auto temporary=CefValue::Create();temporary->SetList(retained->Copy());assert(!CefWriteJSON(temporary,JSON_WRITER_DEFAULT).empty());}
  assert(retained->IsValid()&&retained->GetSize()==2);
  CefRefPtr<CefListValue> asynchronousCookies;
  {auto parent=CefDictionaryValue::Create();parent->SetList("cookies",paths->Copy());asynchronousCookies=parent->GetList("cookies")->Copy();}
  assert(asynchronousCookies->IsValid()&&asynchronousCookies->GetSize()==2);
  auto publication=CefDictionaryValue::Create();publication->SetList("cookies",paths->Copy());auto owned=publication->GetList("cookies");assert(owned->IsOwned());publication->SetList("cookies",suitecut::HTTPPublicationCookies(owned));assert(publication->GetList("cookies")->GetSize()==2);
  auto budget=std::make_shared<std::atomic_size_t>(0);
  {suitecut::HTTPCookieResultReservation a(budget),b(budget);assert(a.Reserve(16777216));assert(!a.Reserve(1));assert(b.Reserve(16777216));suitecut::HTTPCookieResultReservation denied(budget);assert(!denied.Reserve(1));assert(budget->load()==33554432);}
  assert(budget->load()==0);
  bool completed=false;suitecut::HTTPCookieCreationVisitor::Times observed;
  {CefRefPtr<suitecut::HTTPCookieCreationVisitor> visitor=new suitecut::HTTPCookieCreationVisitor([](std::function<void()>done){done();},[&](auto entries,const auto&error){assert(error.empty());observed=std::move(entries);completed=true;});CefCookie cookie{};CefString(&cookie.name)="actual";CefString(&cookie.value)="value";CefString(&cookie.domain)="localhost";CefString(&cookie.path)="/";cookie.creation.val=123456;bool remove=false;assert(visitor->Visit(cookie,0,1,remove));assert(!completed);assert(visitor->Visit(cookie,1,2,remove));assert(!remove);cef_string_clear(&cookie.name);cef_string_clear(&cookie.value);cef_string_clear(&cookie.domain);cef_string_clear(&cookie.path);}
  assert(completed&&observed.size()==1&&!observed.begin()->second.has_value());
  std::cout<<"Native HTTP cookie projection: actual profile order, domain/path/expiry/loopback and partition isolation, actual creation metadata, ambiguity and retained-budget lifetime passed\n";
}
