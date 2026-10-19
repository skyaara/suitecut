// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "credential_page.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <deque>
#include <iostream>
struct Call{std::string method;suitecut::CredentialPage::Dict params;suitecut::CredentialPage::Result done;};
int main(){
  cef_api_hash(CEF_API_VERSION,0);
  std::deque<Call>calls;bool ready=false;auto page=std::make_shared<suitecut::CredentialPage>([&](const std::string&session,const std::string&method,suitecut::CredentialPage::Dict params,suitecut::CredentialPage::Result done){assert(session.empty());calls.push_back({method,params,std::move(done)});},[&](bool value){ready=value;});
  std::string error;suitecut::VirtualCredential first,second;assert(suitecut::GenerateVirtualCredential("localhost",0,first,error));assert(suitecut::GenerateVirtualCredential("localhost",0,second,error));
  auto config=std::make_shared<suitecut::CredentialPage::Config>();config->version=1;config->installed=true;config->credentials={first};bool completed=false;
  page->SessionAttached("",config,[&](bool ok,const std::string&){assert(ok);completed=true;});
  auto reply=[&](const std::string&expected,const std::string&json="{}"){assert(!calls.empty());auto call=std::move(calls.front());calls.pop_front();assert(call.method==expected);call.done(true,json);};
  reply("WebAuthn.enable");assert(!ready&&!completed);reply("WebAuthn.addVirtualAuthenticator","{\"authenticatorId\":\"native-component-device\"}");reply("WebAuthn.clearCredentials");assert(!completed);
  assert(page->OwnsImportEvent("",first));
  assert(!page->OwnsImportEvent("child",first)&&!page->OwnsImportEvent("",second));
  auto asserted=first;asserted.sign_count=1;assert(!page->OwnsImportEvent("",asserted));
  reply("WebAuthn.addCredential");assert(completed&&ready&&!page->OwnsImportEvent("",first));
  auto replacement=std::make_shared<suitecut::CredentialPage::Config>();replacement->version=2;replacement->installed=true;replacement->credentials={second};bool canceled=false,rollback=false,waited=false;
  page->Apply(77,replacement,[&](bool ok,const std::string&){assert(!ok);canceled=true;});assert(!ready);
  page->WhenReady([&](bool ok,const std::string&){assert(ok);waited=true;});
  page->Cancel(77);auto restored=std::make_shared<suitecut::CredentialPage::Config>(*config);restored->version=3;
  page->Apply(0,restored,[&](bool ok,const std::string&){assert(ok);rollback=true;});
  reply("WebAuthn.clearCredentials");assert(canceled&&!rollback&&!waited);reply("WebAuthn.clearCredentials");assert(!rollback);reply("WebAuthn.addCredential");assert(rollback&&waited&&ready);
  const auto diagnostics=CefDictionaryValue::Create();page->Diagnostics(diagnostics);assert(diagnostics->GetInt("credentialDevices")==1);assert(diagnostics->GetInt("credentialPageOperations")==0);assert(diagnostics->GetInt("credentialPageWaiters")==0);
  auto disabled=std::make_shared<suitecut::CredentialPage::Config>();disabled->version=4;disabled->installed=false;bool cleared=false;page->Apply(99,disabled,[&](bool ok,const std::string&){assert(ok);cleared=true;});reply("WebAuthn.removeVirtualAuthenticator");assert(!cleared&&!ready);reply("WebAuthn.disable");assert(cleared&&ready);page->Diagnostics(diagnostics);assert(diagnostics->GetInt("credentialEnabledDomains")==0);
  bool invalid=false;page->SessionAttached("child",disabled,[&](bool ok,const std::string&){assert(!ok);invalid=true;});assert(invalid);
  auto installing=std::make_shared<suitecut::CredentialPage::Config>(*config);installing->version=5;
  bool interrupted=false,repaired=false;
  page->Apply(100,installing,[&](bool ok,const std::string&){assert(!ok);interrupted=true;});
  page->Cancel(100);auto repair=std::make_shared<suitecut::CredentialPage::Config>(*disabled);repair->version=6;
  page->Apply(0,repair,[&](bool ok,const std::string&){assert(ok);repaired=true;});
  reply("WebAuthn.enable");assert(interrupted&&!repaired);assert(calls.size()==1);reply("WebAuthn.disable");assert(repaired&&ready);assert(calls.empty());
  // Logical cancellation and a newer desired config cannot erase the identity
  // of an already submitted physical import before its actual command reply.
  auto in_flight=std::make_shared<suitecut::CredentialPage::Config>(*config);in_flight->version=7;
  bool import_canceled=false,import_repaired=false;
  page->Apply(200,in_flight,[&](bool ok,const std::string&){assert(!ok);import_canceled=true;});
  reply("WebAuthn.enable");reply("WebAuthn.addVirtualAuthenticator","{\"authenticatorId\":\"native-component-reinstalled\"}");reply("WebAuthn.clearCredentials");
  assert(page->OwnsImportEvent("",first));page->Cancel(200);
  auto final_repair=std::make_shared<suitecut::CredentialPage::Config>(*disabled);final_repair->version=8;
  page->Apply(0,final_repair,[&](bool ok,const std::string&){assert(ok);import_repaired=true;});
  assert(page->OwnsImportEvent("",first)&&!import_canceled&&!import_repaired);
  reply("WebAuthn.addCredential");assert(import_canceled&&!page->OwnsImportEvent("",first)&&!import_repaired);
  reply("WebAuthn.removeVirtualAuthenticator");reply("WebAuthn.disable");assert(import_repaired&&ready);
  page->Clear();page->Diagnostics(diagnostics);assert(diagnostics->GetInt("credentialDevices")==0&&diagnostics->GetInt("credentialPageOperations")==0&&diagnostics->GetInt("credentialTargets")==0);
  std::cout<<"native credential page component: actual CEF dictionaries, physical ack ordering, cancel/rollback/latest readiness, device removal and cleanup passed\n";
}
