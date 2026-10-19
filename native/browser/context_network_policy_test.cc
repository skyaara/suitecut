// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "context_network_policy.h"
int main(){
  using Policy=suitecut::NativeContextNetworkPolicy;Policy policy;assert(policy.Seed(1,false));assert(policy.Seed(2,true));
  Policy::Result initial;bool ready=false;assert(policy.Attach(2,"broker:10",[&](bool offline,Policy::Result done){initial=std::move(done);},[&](bool ok,const std::string&){ready=ok;}));assert(!ready);initial(true,"");assert(ready);
  std::vector<Policy::Result> replies;bool first_ready=false;assert(policy.Attach(1,"page:1",[&](bool offline,Policy::Result done){assert(offline);replies.push_back(std::move(done));},[&](bool ok,const std::string&){first_ready=ok;}));assert(first_ready && replies.empty());
  bool updated=false;assert(policy.Set(1,1,true,100,[&](bool ok,const std::string&){updated=ok;}));assert(!updated && replies.size()==1);
  bool inherited=false;assert(policy.Attach(1,"page:2",[&](bool offline,Policy::Result done){assert(offline);replies.push_back(std::move(done));},[&](bool ok,const std::string&){inherited=ok;}));assert(replies.size()==2 && !inherited);replies[0](true,"");assert(!updated);replies[1](true,"");assert(updated && inherited && policy.pending()==0);
  bool closed=false;assert(policy.Set(2,2,false,100,[&](bool ok,const std::string& error){closed=!ok&&!error.empty();}));policy.Closed(2);assert(closed);initial(true,"");assert(policy.targets()==2);
  bool expired=false;assert(policy.Set(3,1,true,10,[&](bool ok,const std::string& error){expired=!ok&&!error.empty();}));policy.Tick(10);assert(expired && policy.pending()==0);
  assert(policy.Set(4,1,true,0,[](bool,const std::string&){assert(false);}));policy.Cancel(4);policy.Tick(100);assert(policy.pending()==0);policy.Clear();assert(policy.targets()==0);
}
