// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "network/network_publication_results.h"
int main(){using Results=suitecut::NativeNetworkPublicationResults;Results results;
 assert(results.Hold(1,11,3,"original renderer result")==Results::Admission::Queued);
 assert(results.Hold(1,11,999,"duplicate future result")==Results::Admission::Duplicate);
 assert(results.Hold(2,22,4,"canceled author result")==Results::Admission::Queued);
 auto current=[](int,uint64_t){return true;};assert(results.Ready(2,current).empty());results.Cancel(2);
 const auto ready=results.Ready(3,current);assert(ready.size()==1&&ready[0].owner==1&&ready[0].generation==11&&ready[0].cursor==3&&ready[0].json=="original renderer result"&&results.bytes()==0);
 assert(results.Hold(1,33,5,"obsolete generation")==Results::Admission::Queued);assert(results.Ready(100,[](int,uint64_t){return false;}).empty()&&results.size()==0);
 // No count or byte cap: only invalid owners/generations/cursors are refused.
 for(int i=1;i<=200;++i)assert(results.Hold(i,i,10,"held")==Results::Admission::Queued);
 assert(results.size()==200&&results.Hold(0,1,10,"x")==Results::Admission::Invalid);results.Clear();
 assert(results.Hold(1,1,1,std::string(16777217,'x'))==Results::Admission::Queued);assert(results.Hold(2,2,1,"x")==Results::Admission::Queued);results.Cancel(1);results.Cancel(2);assert(results.bytes()==0);
}
