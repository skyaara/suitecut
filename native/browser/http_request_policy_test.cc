// SPDX-License-Identifier: MIT
#include "http_request_policy.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <iostream>
int main(){
  cef_api_hash(CEF_API_VERSION,0);
  using Policy=suitecut::NativeHTTPRequestPolicy;Policy policy;std::string error;
  auto options=CefDictionaryValue::Create();options->SetBool("ignoreHTTPSErrors",true);auto credentials=CefListValue::Create();
  auto fallback=CefDictionaryValue::Create();fallback->SetString("username","fallback");fallback->SetString("password","secret");credentials->SetDictionary(0,fallback);
  auto exact=CefDictionaryValue::Create();exact->SetString("username","specific");exact->SetString("password","private");exact->SetString("origin","https://EXAMPLE.com/path");exact->SetString("send","always");credentials->SetDictionary(1,exact);options->SetList("credentials",credentials);
  assert(Policy::Parse(options,policy,error));suitecut::NativeCurlTransport::Request request;request.url="https://example.com/other";policy.Apply(request);assert(request.username=="specific"&&request.preemptive_credentials&&request.ignore_https_errors);
  request={};request.url="https://other.test/";policy.Apply(request);assert(request.username=="fallback"&&!request.preemptive_credentials);
  auto proxy=CefDictionaryValue::Create();proxy->SetString("server","localhost:1234");proxy->SetString("username","native");proxy->SetString("password","password");options=CefDictionaryValue::Create();options->SetDictionary("proxy",proxy);assert(Policy::Parse(options,policy,error));assert(policy.proxy_server=="http://localhost:1234");
  for(const auto& server:{"file:///tmp/private","http://localhost:1234\r\ninjected","socks9://localhost:1234"}){options->GetDictionary("proxy")->SetString("server",server);assert(options->GetDictionary("proxy")->GetString("server")==server);assert(!Policy::Parse(options,policy,error));assert(policy.proxy_server=="http://localhost:1234");}
  auto replacement=CefDictionaryValue::Create();replacement->SetString("server","http://other-proxy.test:4321");auto replace_options=CefDictionaryValue::Create();replace_options->SetDictionary("proxy",replacement);assert(Policy::Parse(replace_options,policy,error));assert(policy.proxy_username.empty()&&policy.proxy_password.empty()&&policy.proxy_bypass.empty()&&policy.proxy_server=="http://other-proxy.test:4321");
  options=CefDictionaryValue::Create();options->SetString("ignoreHTTPSErrors","yes");assert(!Policy::Parse(options,policy,error));assert(policy.ignore_https_errors);
  options=CefDictionaryValue::Create();credentials=CefListValue::Create();for(int i=0;i<65;++i)credentials->SetDictionary(i,CefDictionaryValue::Create());options->SetList("credentials",credentials);assert(!Policy::Parse(options,policy,error));assert(policy.credentials.size()==2);
  exact=CefDictionaryValue::Create();exact->SetString("username","specific");exact->SetString("password","private");exact->SetString("origin","file:///tmp/private");credentials=CefListValue::Create();credentials->SetDictionary(0,exact);options->SetList("credentials",credentials);assert(!Policy::Parse(options,policy,error));
  auto trust=suitecut::NativeHTTPExtraCA::Parse({},error);assert(trust);policy.extra_ca=trust;
  request={};policy.Apply(request);assert(request.extra_ca==trust);
  auto ca_options=CefDictionaryValue::Create();ca_options->SetString("extraCACertificates","not-an-array");assert(!Policy::Parse(ca_options,policy,error)&&policy.extra_ca==trust);
  auto certs=CefListValue::Create();certs->SetInt(0,1);ca_options->SetList("extraCACertificates",certs);assert(!Policy::Parse(ca_options,policy,error)&&policy.extra_ca==trust);
  certs=CefListValue::Create();certs->SetString(0,std::string(32769,'x'));ca_options->SetList("extraCACertificates",certs);assert(!Policy::Parse(ca_options,policy,error)&&policy.extra_ca==trust);
  certs=CefListValue::Create();certs->SetString(0,"not-a-certificate");ca_options->SetList("extraCACertificates",certs);assert(!Policy::Parse(ca_options,policy,error)&&policy.extra_ca==trust);
  certs=CefListValue::Create();ca_options->SetList("extraCACertificates",certs);assert(Policy::Parse(ca_options,policy,error)&&!policy.extra_ca);
  std::cout<<"Native HTTP policy: actual origin selection, bounded credentials, proxy/TLS validation and atomic rejection passed\n";
}
