// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "api_cookie_jar_cef.h"
#include "cookie_controls.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include <cassert>
#include <deque>
#include <iostream>
using Dict=CefRefPtr<CefDictionaryValue>;
Dict Parse(const char* json){const auto value=CefParseJSON(json,JSON_PARSER_RFC);assert(value&&value->GetType()==VTYPE_DICTIONARY);return value->GetDictionary();}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  const suitecut::NativeAPICookieJar::Clock clock{[]{return 100.0;},1792022400,200,false};
  suitecut::NativeAPICookieJar jar(std::make_shared<suitecut::NativeAPICookieJar::Budget>());
  auto seed=Parse(R"({"cookies":[{"name":"latin","value":"\u00e9","domain":"one.com","path":"/"},{"name":"session","value":"v","domain":"two.com","path":"/api","secure":true,"partitionKey":"https://top.example","partitionKeyHasCrossSiteAncestor":false},{"name":"","value":"nameless","domain":"one.com","path":"/"}]})");
  std::vector<suitecut::NativeAPICookieJar::Input> input;std::string error;
  assert(suitecut::NativeAPIJarInputs(seed->GetList("cookies"),4096,input,error));assert(input[0].value==std::string(1,static_cast<char>(0xe9)));assert(jar.Import(input,clock,error));std::string header;assert(jar.Header("http://one.com/",clock,header,error));assert(header==std::string("latin=")+static_cast<char>(0xe9)+"; =nameless");assert(jar.Header("http://sub.one.com/",clock,header,error));assert(header.empty());
  std::vector<suitecut::NativeAPICookieJar::Cookie> cookies;assert(jar.Cookies({},clock,cookies,error));auto published=suitecut::NativeAPIJarCookies(cookies);assert(published->GetDictionary(0)->GetString("value").ToString16()==u"\u00e9");assert(!published->GetDictionary(0)->HasKey("nativeIdentity"));assert(published->GetDictionary(1)->GetString("partitionKey")=="https://top.example");assert(!published->GetDictionary(1)->GetBool("partitionKeyHasCrossSiteAncestor"));
  struct Call{suitecut::CookieMethod method;Dict params;double deadline;suitecut::CookieReply reply;};std::deque<Call> calls;
  auto matcher=std::make_shared<suitecut::CookieControls>([&](int owner,int context,suitecut::CookieMethod method,Dict params,double deadline,suitecut::CookieReply reply){assert(owner>0&&context==7);calls.push_back({method,params->Copy(false),deadline,std::move(reply)});});
  int completed=0;Dict selected;auto done=[&](bool ok,Dict result,const std::string& failure){assert(ok&&failure.empty());++completed;selected=result;};
  matcher->MatchOwned(1,7,Parse(R"({"name":""})"),suitecut::NativeAPIJarCookies(cookies,true),200,done);assert(completed==1&&calls.empty()&&matcher->Physical()==0);assert(selected->GetList("matches")->GetSize()==1);assert(selected->GetList("matches")->GetDictionary(0)->GetString("nativeIdentity")==std::to_string(cookies[2].identity));
  const auto filter=CefDictionaryValue::Create();const auto pattern=CefDictionaryValue::Create();pattern->SetString("sourceJSON",R"("one\\.com$")");pattern->SetString("flags","g");filter->SetDictionary("domain",pattern);
  matcher->MatchOwned(2,7,filter,suitecut::NativeAPIJarCookies(cookies,true),200,done);assert(calls.size()==1&&matcher->Physical()==1);auto call=std::move(calls.front());calls.pop_front();assert(call.method==suitecut::CookieMethod::Filter&&call.deadline==200);assert(!call.params->GetList("cookies")->GetDictionary(0)->HasKey("nativeIdentity"));assert(!call.params->GetList("cookies")->GetDictionary(0)->HasKey("value"));call.reply(true,Parse(R"({"result":{"value":{"matches":[0,2]}}})"),"");assert(completed==2&&calls.empty()&&matcher->Physical()==0);assert(selected->GetList("matches")->GetSize()==2);assert(selected->GetList("matches")->GetDictionary(0)->GetString("nativeIdentity")==std::to_string(cookies[0].identity));
  matcher->MatchOwned(3,7,filter,suitecut::NativeAPIJarCookies(cookies,true),200,done);assert(matcher->Physical()==1);matcher->Cancel(3);assert(matcher->Physical()==1);call=std::move(calls.front());calls.pop_front();call.reply(true,Parse(R"({"result":{"value":{"matches":[0]}}})"),"");assert(completed==2&&matcher->Physical()==0&&calls.empty());
  int closed=0;matcher->MatchOwned(4,7,filter,suitecut::NativeAPIJarCookies(cookies,true),200,[&](bool ok,Dict,const std::string& failure){assert(!ok&&failure=="Native cookie context closed");++closed;});matcher->Closed(7);assert(closed==1&&matcher->Physical()==1);call=std::move(calls.front());calls.pop_front();call.reply(false,nullptr,"actual closed channel");assert(matcher->Physical()==0);
  auto invalid=seed->Copy(false);invalid->GetList("cookies")->GetDictionary(0)->SetString("unknown","must reject");assert(!suitecut::NativeAPIJarInputs(invalid->GetList("cookies"),4096,input,error));assert(jar.Size()==3);
  std::cout<<"native API cookie CEF bridge: Latin-1/partition/identity projection and owned matcher ACK/cancel/close passed\n";
}
