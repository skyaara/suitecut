// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "credential_controls.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <deque>
#include <iostream>
using namespace suitecut;
int main(){
 cef_api_hash(CEF_API_VERSION,0);
 struct Pending{int page,owner;std::shared_ptr<const CredentialPage::Config>config;CredentialPage::Result done;};
 std::deque<Pending>pending;std::vector<int>pages{1,2};int adopted=0,published=0,fatal=0;
 CredentialControls::Hooks hooks;hooks.open=[](int context){return context==1;};hooks.pages=[&](int){return pages;};hooks.cancel=[](int,int){};
 hooks.apply=[&](int page,int owner,std::shared_ptr<const CredentialPage::Config>config,CredentialPage::Result done){pending.push_back({page,owner,config,std::move(done)});};
 hooks.publish=[&](int,std::shared_ptr<const CredentialPage::Config>){++published;return true;};hooks.adopt_observed=[&](int page,std::shared_ptr<const CredentialPage::Config>){adopted=page;return true;};hooks.fatal=[&](int,const std::string&){++fatal;};
 auto controller=std::make_shared<CredentialControls>(hooks);controller->ContextCreated(1);auto params=CefDictionaryValue::Create();params->SetString("rpId","localhost");params->SetInt("signCount",41);CredentialControls::Dict created;controller->Handle(1,1,"create",params,[&](bool ok,auto result,const auto&){assert(ok);created=result;});assert(!created&&pending.size()==2);
 auto acknowledge=[&](bool ok=true){auto call=std::move(pending.front());pending.pop_front();call.done(ok,ok?"":"actual native command failure");};acknowledge();assert(!created);acknowledge();assert(created&&controller->Capture(1)->GetList("credentials")->GetSize()==1);
 controller->Handle(2,1,"install",CefDictionaryValue::Create(),[](bool ok,auto,const auto&){assert(ok);});acknowledge();acknowledge();assert(controller->Config(1)->installed);
 VirtualCredential observed;observed.id=created->GetString("id");observed.rp_id="localhost";observed.user_handle=created->GetString("userHandle");observed.private_key=created->GetString("privateKey");observed.public_key=created->GetString("publicKey");observed.sign_count=42;std::string error;
 assert(controller->Observed(1,1,observed,error));assert(adopted==1&&pending.size()==1&&pending.front().page==2);bool got=false;
 // The canonical counter is still41 until the first real fanout ACK. A second
 // actual event queues behind it, but must not clear/reimport the same state
 // after that ACK commits42.
 assert(controller->Observed(1,1,observed,error));assert(pending.size()==1);
 controller->Handle(3,1,"get",CefDictionaryValue::Create(),[&](bool ok,auto result,const auto&){assert(ok);assert(result->GetList("credentials")->GetDictionary(0)->GetDouble("signCount")==42);got=true;});assert(!got);acknowledge();assert(got);
 assert(pending.empty());
 bool canceled_reply=false;controller->Restore(4,1,CefListValue::Create(),false,[&](bool,auto,const auto&){canceled_reply=true;});assert(pending.size()==2&&pending.front().config->installed);controller->Cancel(4);auto diagnostics=CefDictionaryValue::Create();controller->Diagnostics(diagnostics);assert(diagnostics->GetInt("nativeCredentialOperations")==1&&diagnostics->GetDouble("nativeCredentialRetainedBytes")>0);acknowledge();acknowledge();assert(pending.size()==2&&!canceled_reply);acknowledge();acknowledge();assert(controller->Capture(1)->GetList("credentials")->GetSize()==1&&controller->Config(1)->installed&&!canceled_reply);
 controller->Restore(5,1,CefListValue::Create(),false,[](bool ok,auto,const auto&){assert(ok);});acknowledge();acknowledge();assert(controller->Capture(1)->GetList("credentials")->GetSize()==0&&controller->Config(1)->installed);
 controller->Clear();controller->Diagnostics(diagnostics);assert(!diagnostics->GetInt("nativeCredentialOperations")&&!diagnostics->GetInt("nativeCredentials")&&!diagnostics->GetDouble("nativeCredentialRetainedBytes")&&!fatal&&published>=6);
 std::cout<<"native credential controller: actual native key/store, page acknowledgment fanout, assertion counter synchronization, queued capture, physically retained cancellation/rollback and state replacement passed\n";
}
