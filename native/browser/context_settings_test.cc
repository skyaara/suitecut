// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "context_settings.h"
#include "context_auth_policy.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <iostream>
using Settings=suitecut::ContextSettings;
Settings::Dict Parse(const std::string&json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
class ObservedAuthenticationCallback final:public CefAuthCallback {
 public:
  int canceled=0,continued=0;std::string username,password;
  void Continue(const CefString&user,const CefString&secret)override{++continued;username=user;password=secret;}
  void Cancel()override{++canceled;}
 private:IMPLEMENT_REFCOUNTING(ObservedAuthenticationCallback);
};
int main(){
  cef_api_hash(CEF_API_VERSION,0);
  {Settings invalid;std::string error;assert(!Settings::Parse(Parse(R"({"permissions":["midi"]})"),invalid,error));assert(error==suitecut::NativePermissionError("midi"));}
  {Settings supported;std::string error;assert(Settings::Parse(Parse(R"({"permissions":["midi-sysex","idle-detection"]})"),supported,error));}

  const auto config=Parse(R"({"viewport":{"width":320,"height":240},"screen":{"width":390,"height":844},"deviceScaleFactor":2,"locale":"fr-FR","timezoneId":"America/New_York","userAgent":"Native Context","hasTouch":true,"isMobile":true,"colorScheme":"dark","reducedMotion":"reduce","forcedColors":"active","contrast":"more","geolocation":{"latitude":48.85,"longitude":2.35,"accuracy":10}})");
  Settings parsed;std::string error;assert(Settings::Parse(config,parsed,error));assert(parsed.RendererOverrides());assert(parsed.CaptureDimensions(1280,720,1,error));
  const auto main=parsed.Plan(true,1280,720,1),child=parsed.Plan(false,1280,720,1);assert(main.size()==7&&child.size()==6);
  assert(main[0].method=="Emulation.setLocaleOverride"&&main[0].params->GetString("locale")=="fr-FR");assert(main[4].method=="Emulation.setDeviceMetricsOverride");assert(main[4].params->GetInt("width")==320&&main[4].params->GetInt("screenWidth")==390&&main[4].params->GetDouble("deviceScaleFactor")==2);
  // Runtime resize must use its new viewport while retaining immutable native
  // context emulation. Repeated resizes must not reuse the launch viewport.
  const auto resized=parsed.ViewportMetrics({360,260},1);
  assert(resized->GetInt("width")==360&&resized->GetInt("height")==260);
  assert(resized->GetInt("screenWidth")==390&&resized->GetInt("screenHeight")==844);
  assert(resized->GetBool("mobile")&&resized->GetDouble("deviceScaleFactor")==2);
  const auto again=parsed.ViewportMetrics({300,200},3);
  assert(again->GetInt("width")==300&&again->GetBool("mobile")&&again->GetInt("screenWidth")==390);
  assert(parsed.viewport->width==320&&parsed.screen->width==390&&main[4].params->GetInt("width")==320);
  assert(parsed.ViewportMetrics(*parsed.viewport,1)->IsEqual(main[4].params));
  Settings desktop;const auto default_metrics=desktop.ViewportMetrics({640,480},1.5);
  assert(default_metrics->GetInt("screenWidth")==640&&default_metrics->GetInt("screenHeight")==480);
  assert(!default_metrics->GetBool("mobile")&&default_metrics->GetDouble("deviceScaleFactor")==1.5);
  desktop.screen=Settings::Size{1280,720};desktop.is_mobile=false;
  const auto desktop_metrics=desktop.ViewportMetrics({800,600},1);
  assert(!desktop_metrics->GetBool("mobile")&&desktop_metrics->GetInt("screenWidth")==1280&&desktop_metrics->GetInt("height")==600);
  for(const auto&step:child){assert(step.method!="Emulation.setDeviceMetricsOverride");}
  assert(parsed.Media()->GetList("features")->GetSize()==4);
  for(const auto&json:{R"({"locale":true})",R"({"timezoneId":""})",R"({"hasTouch":0})",R"({"viewport":{"width":320,"height":0}})",R"({"colorScheme":"null"})",R"({"contrast":"less"})",R"({"geolocation":{"latitude":91,"longitude":0}})",R"({"deviceScaleFactor":5})"}){Settings invalid;assert(!Settings::Parse(Parse(json),invalid,error));}
  Settings tls;assert(Settings::Parse(Parse(R"({"ignoreHTTPSErrors":true})"),tls,error));assert(tls.BootstrapRequired());const auto tls_plan=tls.Plan(true,320,240,1);assert(tls_plan.size()==1&&tls_plan[0].method=="Security.setIgnoreCertificateErrors"&&tls_plan[0].params->GetBool("ignore"));assert(tls.Plan(false,320,240,1).empty());
  Settings strict;assert(Settings::Parse(Parse(R"({"ignoreHTTPSErrors":false})"),strict,error));assert(strict.Plan(true,320,240,1).empty());
  Settings reset;assert(Settings::Parse(Parse(R"({"colorScheme":null,"viewport":null,"geolocation":null})"),reset,error));assert(reset.no_fixed_viewport);assert(reset.Media()->GetList("features")->GetDictionary(0)->GetString("value").empty());
  for(const auto&json:{R"({"viewport":null,"deviceScaleFactor":2})",R"({"viewport":null,"isMobile":true})",R"({"proxy":{"server":"socks5://localhost:1080","username":"u","password":"p"}})"}){Settings invalid;assert(!Settings::Parse(Parse(json),invalid,error));}
  Settings socks;assert(Settings::Parse(Parse(R"({"proxy":{"server":"socks5://localhost:1080"}})"),socks,error));assert(socks.proxy->host=="localhost"&&socks.proxy->port==1080&&socks.proxy->server=="socks5://localhost:1080");
  Settings big;assert(Settings::Parse(Parse(R"({"viewport":{"width":2000,"height":1200},"deviceScaleFactor":2})"),big,error));assert(!big.CaptureDimensions(1280,720,1,error));
  Settings authentication;assert(Settings::Parse(Parse(R"({"httpCredentials":[{"username":"fallback","password":"fallback-secret"},{"username":"specific","password":"specific-secret","origin":"http://example.test:80/path","send":"always"}]})"),authentication,error));
  assert(authentication.http_credentials_all.size()==2&&authentication.BootstrapRequired());
  assert(authentication.SelectHTTPCredential("http://example.test/actual")->username=="specific");
  assert(authentication.SelectHTTPCredential("https://example.test/actual")->username=="fallback");
  const auto policy=CefDictionaryValue::Create();authentication.WriteHTTPCredentials(policy);const auto rows=policy->GetList("credentials");assert(rows->GetSize()==2&&rows->GetDictionary(1)->GetString("origin")=="http://example.test"&&rows->GetDictionary(1)->GetString("send")=="always");
  const auto provider=suitecut::NativeContextAuthentication(std::make_shared<const Settings>(authentication));
  const auto challenge=Parse(R"({"request":{"url":"http://example.test/actual"},"authChallenge":{"source":"Server"}})");
  assert(provider(challenge)->GetString("username")=="specific");challenge->GetDictionary("request")->SetString("url","https://other.test/");assert(provider(challenge)->GetString("username")=="fallback");challenge->GetDictionary("authChallenge")->SetString("source","Proxy");assert(provider(challenge)->GetString("response")=="CancelAuth");
  Settings empty_credentials;assert(Settings::Parse(Parse(R"({"httpCredentials":[]})"),empty_credentials,error));assert(!empty_credentials.BootstrapRequired()&&!empty_credentials.SelectHTTPCredential("http://example.test/"));
  for(const auto&json:{R"({"httpCredentials":[{"username":"u","password":"p","send":"sometimes"}]})",R"({"httpCredentials":[{"username":"u"}]})",R"({"httpCredentials":[{"username":"u","password":"p","origin":"file:///tmp/"}]})"}){Settings invalid;assert(!Settings::Parse(Parse(json),invalid,error));}
  const auto live_snapshot=std::make_shared<suitecut::NativeContextAuthenticationSnapshot>(std::make_shared<const suitecut::NativeContextCredentials>(authentication.http_credentials_all));
  const auto live_provider=suitecut::NativeContextAuthentication(std::make_shared<const Settings>(authentication),live_snapshot);
  challenge->GetDictionary("authChallenge")->SetString("source","Server");challenge->GetDictionary("request")->SetString("url","http://example.test/");assert(live_provider(challenge)->GetString("username")=="specific");
  live_snapshot->Set(std::make_shared<const suitecut::NativeContextCredentials>(suitecut::NativeContextCredentials{{"updated","actual-secret","",false}}));assert(live_provider(challenge)->GetString("username")=="updated");live_snapshot->Set(nullptr);assert(live_provider(challenge)->GetString("response")=="CancelAuth");
  // Empty credentials remain an actual Fetch CancelAuth policy, including a
  // handler created before the first dynamic configuration update.
  const auto empty_config=std::make_shared<const Settings>(empty_credentials);
  const auto empty_snapshot=std::make_shared<suitecut::NativeContextAuthenticationSnapshot>(std::make_shared<const suitecut::NativeContextCredentials>());
  const auto default_provider=suitecut::NativeContextAuthentication(std::make_shared<const Settings>());assert(default_provider&&default_provider(challenge)->GetString("response")=="CancelAuth");
  const auto absent_provider=suitecut::NativeContextAuthentication(nullptr);assert(absent_provider&&absent_provider(challenge)->GetString("response")=="CancelAuth");
  const auto empty_provider=suitecut::NativeContextAuthentication(empty_config,empty_snapshot);assert(empty_provider&&empty_provider(challenge)->GetString("response")=="CancelAuth");
  empty_snapshot->Set(std::make_shared<const suitecut::NativeContextCredentials>(suitecut::NativeContextCredentials{{"later","secret","",false}}));assert(empty_provider(challenge)->GetString("username")=="later");
  empty_snapshot->Set(std::make_shared<const suitecut::NativeContextCredentials>());assert(empty_provider(challenge)->GetString("response")=="CancelAuth");
  const auto canceled=[&](std::shared_ptr<const Settings>config,suitecut::NativeContextAuthenticationControls::Snapshot snapshot,bool open,bool proxy){CefRefPtr<ObservedAuthenticationCallback>callback=new ObservedAuthenticationCallback;assert(suitecut::NativeContextAuthenticationCallback(std::move(config),std::move(snapshot),open,"http://example.test/",proxy,callback));assert(callback->canceled==1&&callback->continued==0);};
  canceled(empty_config,empty_snapshot,true,false);canceled(empty_config,empty_snapshot,true,true);canceled(empty_config,empty_snapshot,false,false);canceled(nullptr,nullptr,false,true);
  empty_snapshot->Set(nullptr);canceled(empty_config,empty_snapshot,true,false);
  const auto valid_snapshot=std::make_shared<suitecut::NativeContextAuthenticationSnapshot>(std::make_shared<const suitecut::NativeContextCredentials>(authentication.http_credentials_all));
  CefRefPtr<ObservedAuthenticationCallback>specific_callback=new ObservedAuthenticationCallback;assert(suitecut::NativeContextAuthenticationCallback(std::make_shared<const Settings>(authentication),valid_snapshot,true,"http://example.test/",false,specific_callback));assert(specific_callback->continued==1&&specific_callback->canceled==0&&specific_callback->username=="specific"&&specific_callback->password=="specific-secret");
  const auto unmatched_snapshot=std::make_shared<suitecut::NativeContextAuthenticationSnapshot>(std::make_shared<const suitecut::NativeContextCredentials>(suitecut::NativeContextCredentials{{"unused","unused","http://other.test",false}}));canceled(empty_config,unmatched_snapshot,true,false);
  assert(!suitecut::NativeContextAuthenticationCallback(empty_config,empty_snapshot,true,"http://example.test/",false,nullptr));
  Settings proxy_auth;assert(Settings::Parse(Parse(R"({"proxy":{"server":"http://proxy.test:8080","username":"proxy-user","password":"proxy-secret"}})"),proxy_auth,error));
  const auto proxy_snapshot=std::make_shared<suitecut::NativeContextAuthenticationSnapshot>(std::make_shared<const suitecut::NativeContextCredentials>());
  CefRefPtr<ObservedAuthenticationCallback>proxy_callback=new ObservedAuthenticationCallback;assert(suitecut::NativeContextAuthenticationCallback(std::make_shared<const Settings>(proxy_auth),proxy_snapshot,true,"http://example.test/",true,proxy_callback));assert(proxy_callback->continued==1&&proxy_callback->username=="proxy-user"&&proxy_callback->password=="proxy-secret");
  const auto proxy_provider=suitecut::NativeContextAuthentication(std::make_shared<const Settings>(proxy_auth),proxy_snapshot);challenge->GetDictionary("authChallenge")->SetString("source","Proxy");assert(proxy_provider(challenge)->GetString("username")=="proxy-user");proxy_snapshot->Set(nullptr);assert(proxy_provider(challenge)->GetString("response")=="CancelAuth");canceled(std::make_shared<const Settings>(proxy_auth),proxy_snapshot,true,true);
  suitecut::ContextSettingsRegistry registry;for(int id=1;id<=32;++id)assert(registry.Seed(id,config,error));assert(!registry.Seed(33,config,error));assert(!registry.Seed(1,config,error));registry.Drop(1);assert(!registry.Get(1));assert(registry.Seed(33,config,error));
  struct Call{std::string session,method;suitecut::ContextSettingsPage::Result done;};std::deque<Call>calls;
  auto page=std::make_shared<suitecut::ContextSettingsPage>([&](const std::string&session,const std::string&method,Settings::Dict,suitecut::ContextSettingsPage::Result done){calls.push_back({session,method,std::move(done)});});
  bool ready=false,detached=false,second=false;page->Attach("",main,[&](bool ok,const std::string&){assert(ok);ready=true;});assert(page->physical()&&calls.size()==1&&!ready);
  auto reply=[&](bool ok=true){assert(!calls.empty());auto call=std::move(calls.front());calls.pop_front();call.done(ok,ok?"":"Actual native override rejected");};
  for(size_t index=0;index<main.size();++index){assert(calls.front().method==main[index].method);reply();}assert(ready&&!page->physical()&&page->pending()==0);
  page->Attach("child-a",child,[&](bool ok,const std::string&){assert(!ok);detached=true;});page->Attach("child-b",child,[&](bool ok,const std::string&){assert(ok);second=true;});page->Detach("child-a");assert(detached&&page->physical()&&calls.size()==1&&!second);reply();for(size_t index=0;index<child.size();++index){assert(calls.front().session=="child-b");reply();}assert(second&&!page->physical());
  bool failure=false;page->Attach("child-c",child,[&](bool ok,const std::string&text){assert(!ok&&text=="Actual native override rejected");failure=true;});reply(false);assert(failure&&page->targets()==2);
  bool closed=false;page->Attach("child-d",child,[&](bool ok,const std::string&){assert(!ok);closed=true;});page->Clear();assert(closed&&page->targets()==0&&page->pending()==0&&!page->physical());reply();assert(calls.empty());
  std::cout<<"context settings: real CEF value validation, immutable capacity, native command plan, main-only metrics, physical ACK fence, target detach and close passed\n";
}
