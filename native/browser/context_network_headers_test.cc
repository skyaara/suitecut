// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "context_network_headers.h"
int main(){assert(cef_api_hash(CEF_API_VERSION,0));using Policy=suitecut::NativeContextNetworkHeaders;using Dict=Policy::Dict;Policy policy;auto headers=CefDictionaryValue::Create();headers->SetString("X-Actual","first");assert(policy.Seed(1,headers));std::vector<Policy::Result>acks;std::vector<Dict>sent;int ready=0;auto apply=[&](Dict actual,Policy::Result done){sent.push_back(actual->Copy(false));acks.push_back(std::move(done));};assert(policy.Attach(1,"actual-page:1",apply,[&](bool ok,const std::string&){assert(ok);++ready;}));assert(ready==0&&sent[0]->GetString("X-Actual")=="first");acks[0](true,"");assert(ready==1);
 auto changed=CefDictionaryValue::Create();changed->SetString("x-actual","updated");int updates=0;assert(policy.Set(1,1,changed,21,[&](bool ok,const std::string&){assert(ok);++updates;}));assert(policy.Attach(1,"actual-broker:2",apply,[&](bool ok,const std::string&){assert(ok);++ready;}));assert(acks.size()==3&&updates==0);acks[1](true,"");assert(updates==0);acks[2](true,"");assert(updates==1&&ready==2&&policy.pending()==0);
 auto invalid=CefDictionaryValue::Create();invalid->SetString("x-invalid","real\nnewline");assert(!policy.Set(2,1,invalid,31,[](bool,const std::string&){assert(false);}));int expired=0;assert(policy.Set(2,1,headers,31,[&](bool ok,const std::string&error){assert(!ok&&error=="Native context HTTP headers original deadline exceeded");++expired;}));policy.Tick(31);assert(expired==1&&policy.pending()==0);acks[3](true,"");acks[4](true,"");assert(expired==1);
 auto empty=CefDictionaryValue::Create();assert(policy.Set(3,1,empty,41,[&](bool ok,const std::string&){assert(ok);++updates;}));assert(sent.back()->GetSize()==0);policy.Detach("actual-broker:2");acks[6](true,"");assert(updates==2);acks[5](true,"");assert(updates==2);policy.Closed(1);assert(policy.targets()==0);
}
