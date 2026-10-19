// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "credential_controls.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <cstdio>
#include <deque>
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
 using Controls=suitecut::CredentialControls;using Config=suitecut::CredentialPage::Config;
 Controls::Hooks hooks;std::vector<int>owners;std::deque<suitecut::CredentialPage::Result>pending;int legacy=0,replies=0;
 hooks.open=[](int context){return context==1;};hooks.pages=[](int){return std::vector<int>{1};};
 hooks.publish=[&](int,std::shared_ptr<const Config>){++legacy;return false;};
 hooks.publish_owned=[&](int context,int owner,std::shared_ptr<const Config>){assert(context==1);owners.push_back(owner);return true;};
 hooks.apply=[&](int,int,std::shared_ptr<const Config>,auto done){pending.push_back(std::move(done));};hooks.cancel=[](int,int){};
 auto controls=std::make_shared<Controls>(hooks);controls->ContextCreated(1);
 controls->Handle(19,1,"install",CefDictionaryValue::Create(),[&](bool ok,auto,const auto&){assert(ok);++replies;});
 assert((owners==std::vector<int>{19})&&pending.size()==1&&replies==0&&legacy==0);
 auto done=std::move(pending.front());pending.pop_front();done(true,"");assert(replies==1&&controls->Config(1)->installed);
 controls->Restore(20,1,CefListValue::Create(),false,[&](bool,auto,const auto&){++replies;});assert(owners.back()==20&&pending.size()==1);
 controls->Cancel(20);done=std::move(pending.front());pending.pop_front();done(true,"");assert(owners.back()==0&&pending.size()==1);
 done=std::move(pending.front());pending.pop_front();done(true,"");assert(replies==1&&legacy==0&&controls->Config(1)->installed);controls->Clear();
 std::puts("Credential snapshot original owner and separate rollback cleanup: 2 controlled groups PASS");
}
