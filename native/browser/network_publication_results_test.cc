// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "network_publication_results.h"
int main(){using Results=suitecut::NativeNetworkPublicationResults;Results results;
 assert(results.Hold(1,11,3,"original renderer result")==Results::Admission::Queued);
 assert(results.Hold(1,11,999,"duplicate future result")==Results::Admission::Duplicate);
 assert(results.Hold(2,22,4,"canceled author result")==Results::Admission::Queued);
 auto current=[](int,uint64_t){return true;};assert(results.Ready(2,current).empty());results.Cancel(2);
 const auto ready=results.Ready(3,current);assert(ready.size()==1&&ready[0].owner==1&&ready[0].generation==11&&ready[0].cursor==3&&ready[0].json=="original renderer result"&&results.bytes()==0);
 assert(results.Hold(1,33,5,"obsolete generation")==Results::Admission::Queued);assert(results.Ready(100,[](int,uint64_t){return false;}).empty()&&results.size()==0);
 for(int i=1;i<=64;++i)assert(results.Hold(i,i,10,"held")==Results::Admission::Queued);
 assert(results.Hold(65,65,10,"overflow")==Results::Admission::Full);results.Clear();
 assert(results.Hold(1,1,1,std::string(16777216,'x'))==Results::Admission::Queued);assert(results.Hold(2,2,1,"x")==Results::Admission::Full);results.Cancel(1);assert(results.bytes()==0&&results.Hold(2,2,1,"x")==Results::Admission::Queued);
}
