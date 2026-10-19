// SPDX-License-Identifier: MIT
#include "repeat_selection.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
namespace {
const sr::NativeRepeatSelectionLimits limit{16384,128,64*1024*1024};
sr::NativeRepeatSelectionUsage usage;
auto Unit(uint64_t file,uint64_t project,uint64_t count,sr::NativeRepeatRole role=sr::NativeRepeatRole::Primary){
 return sr::NativeRepeatUnit{file,project,role,{count,count,count?count:1},std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::initializer_list<sr::NativeRepeatTest>{{1,100},{2,100}}),false,32};
}
auto Catalog(std::vector<sr::NativeRepeatUnit> units={Unit(1,1,3)}){return std::make_shared<const sr::NativeRepeatCatalog>(std::move(units),16384,4096,0);}
std::vector<std::pair<uint64_t,uint64_t>> Keys(const sr::NativeRepeatPrimarySelection& selection){std::vector<std::pair<uint64_t,uint64_t>> keys;for(const auto& slice:selection.slices)for(const auto id:*slice.local_ids)keys.push_back({slice.index,id});return keys;}
size_t controls=0;
template<class F>void Reject(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}assert(failed);++controls;}
}
int main(){
 const auto catalog=Catalog();
 auto first=sr::SelectNativeRepeatPrimary(*catalog,{{2}},1,2,limit,usage);
 auto second=sr::SelectNativeRepeatPrimary(*catalog,{{2}},2,2,limit,usage);
 assert((Keys(first)==std::vector<std::pair<uint64_t,uint64_t>>{{0,1},{0,2},{1,1},{1,2}}));
 assert((Keys(second)==std::vector<std::pair<uint64_t,uint64_t>>{{2,1},{2,2}}));assert(first.unsharded_logical_tests==6&&first.first==0&&first.last==3);++controls;
 first=sr::SelectNativeRepeatPrimary(*catalog,{{1,1}},1,2,limit,usage);
 second=sr::SelectNativeRepeatPrimary(*catalog,{{1,1}},2,2,limit,usage);
 assert((Keys(first)==std::vector<std::pair<uint64_t,uint64_t>>{{0,1},{0,2},{1,1}}));
 assert((Keys(second)==std::vector<std::pair<uint64_t,uint64_t>>{{1,2},{2,1},{2,2}}));++controls;
 sr::NativeRepeatSelection exact(catalog,std::move(second.slices),limit,usage);assert(exact.Slices().at(0).local_ids->size()==1&&exact.Slices().at(0).local_ids->at(0)==2);++controls;
 const sr::NativeRepeatSelectionUsage empty;
 auto all=sr::SelectNativeRepeatPrimary(*catalog,{{2}},1,1,limit,empty);assert(all.slices.size()==3&&Keys(all).size()==6);++controls;
 const auto zero=Catalog({Unit(1,1,0)});auto once=sr::SelectNativeRepeatPrimary(*zero,{{2}},1,1,limit,empty);assert(once.slices.size()==1&&once.slices[0].index==0&&Keys(once).size()==2);++controls;
 auto no_work=sr::SelectNativeRepeatPrimary(*catalog,{{2}},4,4,limit,empty);assert(no_work.slices.empty()&&no_work.unsharded_logical_tests==6);++controls;
 // Real public rule generalized over two actual file/repeat groups: native
 // project order, repeat index, file order. Input units need not be sorted.
 const auto files=Catalog({Unit(2,1,2),Unit(1,1,2)});auto ordered=sr::SelectNativeRepeatPrimary(*files,{{2},{2}},1,1,limit,empty);
 assert(ordered.slices.size()==4&&ordered.slices[0].unit==1&&ordered.slices[1].unit==0&&ordered.slices[2].index==1);++controls;
 // Different effective counts do not produce nonexistent repeat positions.
 const auto uneven=Catalog({Unit(1,1,1),Unit(2,1,3)});auto varying=sr::SelectNativeRepeatPrimary(*uneven,{{2},{2}},1,1,limit,empty);assert(varying.slices.size()==4&&varying.unsharded_logical_tests==8);++controls;
 const auto projects=Catalog({Unit(2,2,1),Unit(1,1,1)});auto project_order=sr::SelectNativeRepeatPrimary(*projects,{{2},{2}},1,1,limit,empty);assert(project_order.slices[0].unit==1&&project_order.slices[1].unit==0);++controls;
 // Explicit native indivisible serial groups remain together even when their
 // containing file otherwise uses fullyParallel grouping.
 auto four=Unit(1,1,2);four.declarations=std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::initializer_list<sr::NativeRepeatTest>{{1,100},{2,100},{3,100},{4,100}});
 const auto serial=Catalog({four});auto grouped=sr::SelectNativeRepeatPrimary(*serial,{{1,2,1}},1,3,limit,empty);assert((Keys(grouped)==std::vector<std::pair<uint64_t,uint64_t>>{{0,1},{0,2},{0,3}}));++controls;
 const auto dep=sr::NativeRepeatRole::Dependency,tear=sr::NativeRepeatRole::Teardown;
 const auto graph=Catalog({Unit(1,1,1,dep),Unit(2,2,2),Unit(3,3,1,tear)});
 auto primary=sr::SelectNativeRepeatPrimary(*graph,{{2},{2},{2}},1,1,limit,empty);assert(primary.unsharded_logical_tests==4&&primary.slices.size()==2);
 sr::AppendNativeRepeatSupport(*graph,{1,2,3},primary.slices,limit,empty);assert(primary.slices.size()==4&&primary.slices[2].unit==0&&primary.slices[3].unit==2);++controls;
 auto no_support=sr::SelectNativeRepeatPrimary(*graph,{{2},{2},{2}},1,1,limit,empty);sr::AppendNativeRepeatSupport(*graph,{2},no_support.slices,limit,empty);assert(no_support.slices.size()==2);++controls;
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{},1,2,limit,empty);});
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{1}},1,2,limit,empty);});
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{0,2}},1,2,limit,empty);});
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{3}},1,2,limit,empty);});
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{2}},0,2,limit,empty);});
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{2}},1,65537,limit,empty);});
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{2}},1,1,{16384,2,64*1024*1024},empty);});
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{2}},1,1,{16384,128,1},empty);});
 size_t polls=0;Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{{2}},1,1,limit,empty,[&]{if(++polls==4)throw std::runtime_error("original native suite stopped");});});assert(polls==4);
 // Huge count stays compact; skip preceding repeat positions arithmetically,
 // then fail original source bound instead of walking/materializing billions.
 auto one=Unit(1,1,UINT64_MAX);one.declarations=std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::initializer_list<sr::NativeRepeatTest>{{1,100}});const auto huge=Catalog({one});
 polls=0;Reject([&]{(void)sr::SelectNativeRepeatPrimary(*huge,{{1}},65536,65536,{16384,1,64*1024*1024},empty,[&]{++polls;});});assert(polls<1000);++controls;
 auto too_many=Catalog({Unit(1,1,129,dep)});std::vector<sr::NativeSelectedRepeatSlice> slices;Reject([&]{sr::AppendNativeRepeatSupport(*too_many,{1},slices,limit,empty);});assert(slices.empty());
 // A shard plan is not accepted source/physical work; the shared planning
 // usage changes only after constructing the exact immutable selection.
 assert(empty==sr::NativeRepeatSelectionUsage{});++controls;
 std::cout<<"native repeat expanded-group shard policy "<<controls<<" controls PASS\n";
}
