// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  using Dict=CefRefPtr<CefDictionaryValue>;
  auto project=[](Dict source){auto output=CefDictionaryValue::Create();suitecut::NativeResponseDetails(source,output);return output;};
  auto source=CefDictionaryValue::Create();assert(project(source)->GetSize()==0);
  source->SetString("remoteIPAddress","::1");source->SetInt("remotePort",443);
  auto output=project(source);assert(output->GetDictionary("serverAddr")->GetString("ipAddress")=="::1");assert(output->GetDictionary("serverAddr")->GetInt("port")==443);assert(!output->HasKey("securityDetails"));
  source->SetDouble("remotePort",65535);assert(project(source)->GetDictionary("serverAddr")->GetInt("port")==65535);
  for(double port:{0.,-1.,65536.,443.5}){source->SetDouble("remotePort",port);assert(!project(source)->HasKey("serverAddr"));}
  source->SetInt("remotePort",443);source->SetString("remoteIPAddress",std::string(129,'a'));assert(!project(source)->HasKey("serverAddr"));source->SetString("remoteIPAddress","");assert(!project(source)->HasKey("serverAddr"));source->SetString("remoteIPAddress","127.0.0.1");
  auto security=CefDictionaryValue::Create();security->SetString("issuer","Observed issuer");security->SetString("subjectName","native.test");security->SetString("protocol","TLS 1.3");security->SetInt("validFrom",1700000000);security->SetDouble("validTo",2200000000.);source->SetDictionary("securityDetails",security);
  output=project(source);source=nullptr;assert(output->GetDictionary("securityDetails")->GetString("issuer")=="Observed issuer");assert(output->GetDictionary("securityDetails")->GetDouble("validFrom")==1700000000.);assert(output->GetDictionary("securityDetails")->GetDouble("validTo")==2200000000.);
  source=CefDictionaryValue::Create();source->SetDictionary("securityDetails",output->GetDictionary("securityDetails")->Copy(false));security=source->GetDictionary("securityDetails");
  security->SetString("issuer",std::string(8193,'x'));assert(!project(source)->HasKey("securityDetails"));security->SetString("issuer","Observed issuer");
  // Real controller projection retains the native DTO and charges it to the
  // existing request metadata budget; no separate unaccounted metadata cache.
  using Controls=suitecut::NetworkControls;std::vector<Dict> events;
  Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict event){events.push_back(event);},[](const std::string&){assert(false);},[]{},[]{return 0.;});
  auto event=CefDictionaryValue::Create();event->SetString("requestId","observed-TLS");event->SetString("type","Document");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/");request->SetString("method","GET");request->SetDictionary("headers",CefDictionaryValue::Create());event->SetDictionary("request",request);controls.Event("Network.requestWillBeSent",event,"");
  auto response=CefDictionaryValue::Create();response->SetString("url","https://native.test/");response->SetInt("status",200);response->SetDictionary("headers",CefDictionaryValue::Create());event->SetDictionary("response",response);controls.Event("Network.responseReceived",event,"");auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);const auto before=diagnostics->GetDouble("networkRequestMetadataBytes");
  response=event->GetDictionary("response");response->SetString("remoteIPAddress","127.0.0.1");response->SetInt("remotePort",443);auto maximum=security->Copy(false);maximum->SetString("issuer",std::string(8192,'a'));response->SetDictionary("securityDetails",maximum);controls.Event("Network.responseReceived",event,"");controls.Diagnostics(diagnostics);assert(diagnostics->GetDouble("networkRequestMetadataBytes")>=before+8192);assert(events.back()->GetDictionary("response")->GetDictionary("securityDetails")->GetString("issuer").length()==8192);assert(events.back()->GetDictionary("response")->GetDictionary("serverAddr")->GetInt("port")==443);
  security->Remove("validFrom");assert(!project(source)->HasKey("securityDetails"));security->SetString("validFrom","not a numeric epoch");assert(!project(source)->HasKey("securityDetails"));security->SetDouble("validFrom",1700000000.);assert(project(source)->HasKey("securityDetails"));
  // Navigation replies and network events share this fresh bounded projection.
  // Actual Chromium TLS dictionaries include many additional protocol fields.
  auto raw=CefDictionaryValue::Create();raw->SetString("url","https://native.test/secure");
  raw->SetInt("status",200);raw->SetString("statusText","OK");raw->SetString("rawExtra","not a wire field");
  raw->SetString("remoteIPAddress","::1");raw->SetInt("remotePort",8443);
  raw->SetString("protocol","h2");raw->SetString("mimeType","text/html");raw->SetBool("fromServiceWorker",true);
  auto rawHeaders=CefDictionaryValue::Create();rawHeaders->SetString("content-type","text/html");raw->SetDictionary("headers",rawHeaders);
  auto rawTiming=CefDictionaryValue::Create();rawTiming->SetDouble("requestTime",12.5);raw->SetDictionary("timing",rawTiming);
  auto rawTLS=CefDictionaryValue::Create();rawTLS->SetString("issuer","Observed issuer");
  rawTLS->SetString("subjectName","native.test");rawTLS->SetString("protocol","TLS 1.3");
  rawTLS->SetInt("validFrom",1700000000);rawTLS->SetDouble("validTo",2200000000.);
  rawTLS->SetInt("certificateId",7);rawTLS->SetString("cipher","AES_256_GCM");
  rawTLS->SetList("sanList",CefListValue::Create());raw->SetDictionary("securityDetails",rawTLS);
  auto wire=suitecut::NativeResponseWireInfo(raw,"actual-request","actual-session","actual-frame","POST");
  assert(wire&&wire->GetString("requestId")=="actual-request"&&wire->GetString("session")=="actual-session");
  assert(wire->GetString("frameId")=="actual-frame"&&wire->GetString("method")=="POST");
  assert(wire->GetString("url")=="https://native.test/secure"&&wire->GetInt("status")==200);
  assert(wire->GetString("protocol")=="h2"&&wire->GetString("mimeType")=="text/html"&&wire->GetBool("fromServiceWorker"));
  assert(wire->GetDictionary("securityDetails")->GetSize()==5&&!wire->HasKey("rawExtra")&&!wire->HasKey("remoteIPAddress"));
  assert(raw->GetDictionary("securityDetails")->GetSize()==8&&raw->HasKey("rawExtra"));
  // The wire DTO owns detached children after source mutation/destruction.
  raw->GetDictionary("headers")->SetString("content-type","mutated");
  raw->GetDictionary("timing")->SetDouble("requestTime",99);
  raw->GetDictionary("securityDetails")->SetString("issuer","mutated");raw=nullptr;
  assert(wire->GetDictionary("headers")->GetString("content-type")=="text/html");
  assert(wire->GetDictionary("timing")->GetDouble("requestTime")==12.5);
  assert(wire->GetDictionary("securityDetails")->GetString("issuer")=="Observed issuer");
  auto absent=CefDictionaryValue::Create();auto empty=suitecut::NativeResponseWireInfo(absent,"r","","f","GET");
  assert(!empty->HasKey("securityDetails")&&!empty->HasKey("serverAddr"));
  absent->SetDictionary("securityDetails",wire->GetDictionary("securityDetails")->Copy(false));
  absent->GetDictionary("securityDetails")->SetString("issuer",std::string(8193,'x'));
  absent->SetString("remoteIPAddress","::1");absent->SetInt("remotePort",0);
  empty=suitecut::NativeResponseWireInfo(absent,"r","","f","GET");
  assert(!empty->HasKey("securityDetails")&&!empty->HasKey("serverAddr"));
  assert(!suitecut::NativeResponseWireInfo(nullptr,"r","","f","GET"));
}
