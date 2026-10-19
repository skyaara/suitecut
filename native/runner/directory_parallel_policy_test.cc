// SPDX-License-Identifier: MIT
#include "directory_actor_timing.h"
#include "directory_source_coverage.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
template<class F>void Reject(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
struct Worker {unsigned worker_index;int64_t pid;};
int main(){
  // Controlled policy clocks and identities below are not kernel observations.
  sr::DirectoryActorTiming timing(1);timing.HostStarted(1,123,2);timing.Metadata(1,123,3);timing.Metadata(1,123,4);assert(timing.hosts[0].metadata==3);
  Reject([&]{timing.Complete(4);});Reject([&]{timing.Captured(2,123,4);});Reject([&]{timing.Captured(1,124,4);});Reject([&]{timing.Closing(1,123,2);});
  timing.Closing(1,123,5);timing.Captured(1,123,6);Reject([&]{timing.Captured(1,123,7);});
  timing.HostStarted(2,123,7);timing.Captured(2,123,8);timing.Complete(9);timing.Validate();assert(timing.hosts.size()==2);
  Reject([&]{timing.HostStarted(3,123,10);});Reject([&]{timing.Complete(10);});
  auto corrupt=timing;corrupt.hosts[0].captured=2;Reject([&]{corrupt.Validate();});
  sr::RetainedSourceLeases ledger;
  const sr::RetainedSourceLeases::Host h1{1,1,123,100},h2{2,2,124,101};
  const sr::RetainedSourceLeases::Owner o1{h1,{1,1,1,1,1}},o2{h2,{1,1,2,1,1}};
  const auto admission1=ledger.Reserve(o1,{1,11},100,1),admission2=ledger.Reserve(o2,{1,11},100,1);assert(admission1&&admission2);const auto t1=*admission1,t2=*admission2;
  sr::ReportRetainedSource r1;r1.ticket=t1;sr::ReportRetainedSource r2;r2.ticket=t2;
  const std::vector<Worker> first{{1,123}},second{{2,124}};
  Reject([&]{sr::ValidateDirectorySourceCoverage(ledger,first,{r1});});
  assert(ledger.Retire(t1,{h1,1,true,true,true},2));
  sr::ValidateDirectorySourceCoverage(ledger,first,{r1});assert(ledger.Outstanding()==1);
  // Scope coverage alone is not the separate global publication fence.
  Reject([&]{sr::ValidateDirectorySourceCoverage(ledger,first,{});});Reject([&]{sr::ValidateDirectorySourceCoverage(ledger,first,{r1,r1});});Reject([&]{sr::ValidateDirectorySourceCoverage(ledger,first,{r2});});
  assert(ledger.Retire(t2,{h2,2,true,true,true},3));sr::ValidateDirectorySourceCoverage(ledger,second,{r2});
  Reject([&]{sr::ValidateDirectorySourceCoverage(ledger,second,{r1});});Reject([&]{sr::ValidateDirectorySourceCoverage(ledger,std::vector<Worker>{{1,123},{1,124}},{r1});});
  const sr::RetainedSourceLeases::Host reused{3,3,123,102};const sr::RetainedSourceLeases::Owner o3{reused,{1,1,3,1,1}};
  const auto admission3=ledger.Reserve(o3,{1,11},100,4);assert(admission3);const auto t3=*admission3;assert(ledger.Retire(t3,{reused,3,true,true,true},5));
  sr::ReportRetainedSource r3;r3.ticket=t3;sr::ValidateDirectorySourceCoverage(ledger,std::vector<Worker>{{3,123}},{r3});Reject([&]{sr::ValidateDirectorySourceCoverage(ledger,std::vector<Worker>{{3,123}},{r1});});
  std::cout<<"Directory scoped Source coverage and native timing policy controls PASS\n";
}
