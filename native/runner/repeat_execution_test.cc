// SPDX-License-Identifier: MIT
#include "repeat_execution.h"
#include <cassert>
#include <iostream>
#include <functional>
namespace sr=suitecut::runner;
namespace {
size_t controls=0;
void Reject(const std::function<void()>& operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}assert(rejected);++controls;}
auto Tests(std::initializer_list<sr::NativeRepeatTest> values){return std::make_shared<const std::vector<sr::NativeRepeatTest>>(values);}
auto IDs(std::initializer_list<uint64_t> values){return std::make_shared<const std::vector<uint64_t>>(values);}
sr::NativeRepeatUnit Unit(uint64_t file=1,uint64_t project=1,uint64_t count=2){
 return {file,project,sr::NativeRepeatRole::Primary,{count,count,count?count:1},Tests({{1,100},{2,200}}),false,32};
}
auto Catalog(std::vector<sr::NativeRepeatUnit> units={Unit()}){return std::make_shared<const sr::NativeRepeatCatalog>(std::move(units),16384,4096,0);}
const sr::NativeRepeatSelectionLimits limits{16384,128,64*1024*1024};
}
int main(){
 // Numeric IDs/times here are pure native policy inputs, not actual actors,
 // waitpid, file/source receipts or proof of integrated Runner support.
 assert(sr::ParseNativeRepeatCount("0",0)==0&&sr::ParseNativeRepeatCount("01",0)==1&&sr::ParseNativeRepeatCount("18446744073709551615",0)==UINT64_MAX);++controls;
 for(const auto* bad:{"","-1","+1","1.5","NaN","Infinity","18446744073709551616","1 "," 1"})Reject([&]{(void)sr::ParseNativeRepeatCount(bad,0);});
 Reject([&]{(void)sr::ParseNativeRepeatCount("0",1);});
 using Role=sr::NativeRepeatRole;
 const sr::NativeRepeatConfiguration configured{3,1,2};
 assert((sr::ResolveNativeRepeatCounts(configured,Role::Primary,0)==sr::NativeRepeatCounts{1,2,2}));
 assert((sr::ResolveNativeRepeatCounts(configured,Role::Dependency,0)==sr::NativeRepeatCounts{1,1,1}));
 assert((sr::ResolveNativeRepeatCounts(configured,Role::Teardown,0)==sr::NativeRepeatCounts{1,1,1}));++controls;
 assert((sr::ResolveNativeRepeatCounts({},Role::Primary,0)==sr::NativeRepeatCounts{1,1,1}));
 assert((sr::ResolveNativeRepeatCounts({0,std::nullopt,std::nullopt},Role::Primary,0)==sr::NativeRepeatCounts{0,0,1}));++controls;
 const auto catalog=Catalog();assert(catalog->CandidateActors()==2&&catalog->CandidateLogicalTests()==4&&catalog->OwnedMetadataBytes()>0);++controls;
 const auto zero=Catalog({Unit(1,1,0)});assert(zero->CandidateActors()==1&&zero->CandidateLogicalTests()==2&&zero->Key(0,0).index==0);++controls;
 Reject([&]{(void)zero->Key(0,1);});
 const auto huge=Catalog({{1,1,Role::Primary,{UINT64_MAX,UINT64_MAX,UINT64_MAX},Tests({{1,100}}),false,32}});
 assert(huge->CandidateActors()==UINT64_MAX&&huge->CandidateLogicalTests()==UINT64_MAX&&huge->Units().size()==1);++controls;
 Reject([&]{(void)Catalog({Unit(1,1,UINT64_MAX)});});
 Reject([&]{auto bad=Unit();bad.counts.executions=0;(void)Catalog({bad});});
 Reject([&]{auto bad=Unit();bad.role=Role::Dependency;bad.counts.declared=1;(void)Catalog({bad});});
 Reject([&]{(void)Catalog({Unit(),Unit()});});
 Reject([&]{(void)Catalog({Unit(0)});});
 Reject([&]{(void)Catalog({Unit(1,0)});});
 Reject([&]{auto bad=Unit();bad.declarations=Tests({{1,1},{1,1}});(void)Catalog({bad});});
 Reject([&]{auto bad=Unit();bad.declarations=Tests({{1,0}});(void)Catalog({bad});});
 Reject([&]{auto bad=Unit();bad.declared_empty_actor=true;(void)Catalog({bad});});
 Reject([&]{(void)sr::NativeRepeatCatalog({Unit(),Unit(2)},16384,3,0);});
 Reject([&]{(void)Catalog({Unit(),Unit(1,2)});}); // same file/local ID cannot change project
 const auto separate=Catalog({Unit(),Unit(2)});assert(separate->CandidateLogicalTests()==8);++controls;
 assert(catalog->LogicalID(0,0,1)!=catalog->LogicalID(0,1,1));
 assert(catalog->LogicalID(0,1,1)==catalog->LogicalID(0,1,1));
 assert(separate->LogicalID(0,0,1)!=separate->LogicalID(1,0,1));++controls;
 Reject([&]{(void)catalog->LogicalID(0,2,1);});
 Reject([&]{(void)catalog->LogicalID(0,0,3);});
 sr::NativeRepeatSelectionUsage shared;
 const auto first=IDs({1}),second=IDs({2});
 sr::NativeRepeatSelection selected(catalog,{{0,0,first},{0,1,second}},limits,shared);
 assert(shared.slices==2&&shared.source_reports==2&&shared.logical_tests==2&&shared.metadata_bytes>300);
 assert(selected.Slices()[0].local_ids==first&&selected.Slices()[1].local_ids==second);
 assert(selected.LogicalID(0,1)==catalog->LogicalID(0,0,1));++controls;
 Reject([&]{(void)selected.LogicalID(0,2);}); // a different repeat's ID cannot cover this subset
 const auto saved=shared;
 const auto reject_selection=[&](std::vector<sr::NativeSelectedRepeatSlice> slices,sr::NativeRepeatSelectionLimits cap=limits){Reject([&]{sr::NativeRepeatSelection candidate(catalog,std::move(slices),cap,shared);});assert(shared==saved);};
 reject_selection({{0,0,first},{0,0,second}});
 reject_selection({{0,0,first},{0,2,first}});
 reject_selection({{0,0,first},{0,1,IDs({3})}});
 reject_selection({{0,0,IDs({1,1})}});
 reject_selection({{0,0,IDs({})}});
 reject_selection({{0,0,nullptr}});
 reject_selection({{0,0,first}},{16384,2,64*1024*1024});
 reject_selection({{0,0,first}},{16384,128,saved.metadata_bytes});
 sr::NativeRepeatSelectionUsage measure;
 {sr::NativeRepeatSelection a(catalog,{{0,0,first}},limits,measure);} // immutable planning charge does not vanish on destruction
 const auto exact=measure.metadata_bytes;sr::NativeRepeatSelectionUsage boundary;
 sr::NativeRepeatSelection accepted(catalog,{{0,0,first}},{1,1,exact},boundary);assert(boundary.metadata_bytes==exact);++controls;
 sr::NativeRepeatSelectionUsage under;
 Reject([&]{sr::NativeRepeatSelection a(catalog,{{0,0,first}},{1,1,exact-1},under);});assert(under==sr::NativeRepeatSelectionUsage{});
 const auto empty=Catalog({{1,1,Role::Dependency,{1,1,1},Tests({}),true,32}});sr::NativeRepeatSelectionUsage empty_usage;
 sr::NativeRepeatSelection empty_selection(empty,{{0,0,IDs({})}},limits,empty_usage);assert(empty_usage.logical_tests==0&&empty_usage.source_reports==1);++controls;
 const auto filtered=Catalog({{1,1,Role::Dependency,{1,1,1},Tests({}),false,32}});assert(filtered->CandidateActors()==0);++controls;
 sr::NativeRepeatSelectionUsage filtered_usage;Reject([&]{sr::NativeRepeatSelection a(filtered,{{0,0,IDs({})}},limits,filtered_usage);});
 sr::NativeRepeatSelectionUsage overflow{0,0,UINT64_MAX,0};const auto original=overflow;
 Reject([&]{sr::NativeRepeatSelection a(catalog,{{0,0,first}},{UINT64_MAX,UINT64_MAX,UINT64_MAX},overflow);});assert(overflow==original);
 // Explicit post-shard subsets keep original shared declarations; no policy
 // primitive guesses shard order or creates an execution/source receipt.
 assert(catalog->Units()[0].declarations->size()==2&&selected.Slices().size()==2);++controls;
 std::cout<<"native repeat compact policy "<<controls<<" controls PASS\n";
}
