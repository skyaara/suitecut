// SPDX-License-Identifier: MIT
#include "multi_file_plan.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
namespace {
const sr::NativeRepeatSelectionLimits limits{16384,128,64*1024*1024};
auto Catalog(bool parallel,bool serial=false){
 sr::CollectedSuiteFile file;file.file={11,1,11};file.path="/controlled/groups.mjs";file.catalog.protocol="controlled native declarations";file.catalog.fully_parallel=parallel;
 for(uint64_t id=1;id<=2;++id)file.catalog.tests.push_back({id,1000,0,false,"\"real declaration input\"",{}});
 if(serial){file.catalog.suites={{9,0,0,{}}};file.catalog.members={{1,9,0},{2,9,1}};file.catalog.suite_modes.emplace(9,sr::WorkGroupMode::Serial);}
 return std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{std::move(file)});
}
auto Selection(const sr::MultiFileCatalog& catalog,sr::NativeRepeatSelectionUsage& usage){
 auto tests=std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::initializer_list<sr::NativeRepeatTest>{{1,100},{2,100}});
 auto units=std::make_shared<const sr::NativeRepeatCatalog>(std::vector<sr::NativeRepeatUnit>{{11,catalog.AnonymousProject()+1,sr::NativeRepeatRole::Primary,{2,2,2},tests,false,64}},16384,4096,0);
 auto ids=std::make_shared<const std::vector<uint64_t>>(std::initializer_list<uint64_t>{1,2});
 return std::make_shared<const sr::NativeRepeatSelection>(units,std::vector<sr::NativeSelectedRepeatSlice>{{0,0,ids},{0,1,ids}},limits,usage);
}
size_t checks=0;
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);++checks;}
}
int main(){
 const auto parallel=Catalog(true);sr::NativeRepeatSelectionUsage usage;const auto repeats=Selection(*parallel,usage);const auto original=usage;
 const auto groups=std::make_shared<const sr::NativeRepeatGroupPartition>(repeats,parallel,limits,usage);
 assert(groups->Groups().size()==4&&usage.source_reports==4&&usage.slices==original.slices&&usage.logical_tests==original.logical_tests);++checks;
 assert(&groups->Selection()==repeats.get()&&&groups->Catalog()==parallel.get());++checks;
 for(size_t g=0;g<4;++g){const auto& row=groups->Groups()[g];assert(row.index==g&&row.slice==g/2&&row.mode==sr::WorkGroupMode::Parallel&&row.local_ids->size()==1&&row.local_ids->at(0)==g%2+1);}++checks;
 assert(repeats->LogicalID(0,1)=="native-repeat:11:1:1:0"&&repeats->LogicalID(1,1)=="native-repeat:11:1:1:1");++checks;
 const auto ordinary=Catalog(false);sr::NativeRepeatSelectionUsage default_usage;const auto ordinary_repeats=Selection(*ordinary,default_usage);
 sr::NativeRepeatGroupPartition defaults(ordinary_repeats,ordinary,limits,default_usage);assert(defaults.Groups().size()==2&&defaults.Groups()[0].local_ids->size()==2&&default_usage.source_reports==2);++checks;
 const auto serial=Catalog(true,true);sr::NativeRepeatSelectionUsage serial_usage;const auto serial_repeats=Selection(*serial,serial_usage);
 sr::NativeRepeatGroupPartition serial_groups(serial_repeats,serial,limits,serial_usage);assert(serial_groups.Groups().size()==2&&serial_groups.Groups()[0].mode==sr::WorkGroupMode::Serial&&serial_groups.Groups()[0].suite==9&&serial_groups.Groups()[0].local_ids->size()==2);++checks;
 auto failed=original;Reject([&]{sr::NativeRepeatGroupPartition denied(repeats,parallel,{16384,3,limits.metadata_bytes},failed);});assert(failed==original);++checks;
 failed=original;Reject([&]{sr::NativeRepeatGroupPartition denied(repeats,parallel,{16384,128,original.metadata_bytes+1},failed);});assert(failed==original);++checks;
 failed=original;size_t polls=0;Reject([&]{sr::NativeRepeatGroupPartition stopped(repeats,parallel,limits,failed,[&]{if(++polls==2)throw std::runtime_error("original run stopped");});});assert(failed==original);++checks;
 failed=original;++failed.slices;const auto wrong=failed;Reject([&]{sr::NativeRepeatGroupPartition denied(repeats,parallel,limits,failed);});assert(failed==wrong);++checks;
 // Controlled pure-policy process values below are NOT actual waitpid/Source
 // receipts. Integration must retain each real actor through its full fence.
 auto selected=parallel->Select();sr::MultiFilePlan plan(*parallel,selected,2,0,false,repeats,groups);
 const auto a=plan.Next(),b=plan.Next();assert(a&&b&&a->repeat_slice==b->repeat_slice&&a->repeat_group!=b->repeat_group&&!plan.Next());++checks;
 auto forged=*a;forged.repeat_group=b->repeat_group;assert(!plan.Started(forged,101));assert(plan.Started(*a,101)&&plan.Started(*b,102));++checks;
 assert(plan.Complete(*a,sr::MultiFilePlan::Outcome::Passed,0)&&plan.Occupied()==2&&!plan.Next());++checks;
 assert(plan.PhysicalExit(*a,101)&&plan.Next()&&plan.Occupied()==2);++checks;
 const auto other=Catalog(true);Reject([&]{sr::MultiFilePlan foreign(*other,other->Select(),2,0,false,repeats,groups);});
 std::cout<<"Native repeat groups: "<<checks<<" partition/global-credit/owner/retirement controls PASS\n";
}
