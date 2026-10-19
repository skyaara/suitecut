// SPDX-License-Identifier: MIT
#include "context_permission_scopes.h"
#include <cassert>
int main(){
  using Ledger=suitecut::NativePermissionScopes<int>;
  Ledger scopes(2);assert(scopes.Observe("first","first"));assert(scopes.entries().size()==1);assert(!scopes.Observe("second","first"));assert(scopes.entries().size()==1&&scopes.failed());assert(!scopes.Observe("first","first"));
  Ledger pairs(3);assert(pairs.Observe("first","first"));assert(pairs.Observe("second","first"));assert(pairs.entries().size()==3);assert(pairs.Observe("second","first"));assert(!pairs.Observe("first","second"));assert(pairs.entries().size()==3);
  Ledger zero(0);assert(!zero.Observe("first","first"));assert(zero.entries().empty());
  for(int i=0;i<100000;++i){Ledger cycle(3);assert(cycle.Observe("first",""));assert(cycle.Observe("second","first"));assert(!cycle.Observe("third","first"));assert(cycle.entries().size()==3);for(const auto&[scope,value]:cycle.entries()){assert(!scope.first.empty()&&!scope.second.empty());assert(value==0);}}
}
