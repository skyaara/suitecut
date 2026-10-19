// SPDX-License-Identifier: MIT
#include "repeat_projection.h"
#include "repeat_selection.h"
#include "test_listing.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
namespace {
using Test=sr::TestListingCatalog::Test;
size_t controls=0;
template<class F>void Reject(F operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}assert(rejected);++controls;}
void Exact(const sr::NativeRepeatProjection& actual,const std::vector<size_t>& groups,const std::vector<std::pair<uint64_t,uint64_t>>& rows){
 assert(actual.group_sizes==groups&&actual.declarations&&actual.declarations->size()==rows.size());
 for(size_t i=0;i<rows.size();++i)assert(actual.declarations->at(i).local_id==rows[i].first&&actual.declarations->at(i).projected_attempt_bytes==rows[i].second);
 ++controls;
}
struct CountingID {
 uint64_t value=0;static inline uint64_t comparisons=0;
 operator uint64_t()const{return value;}
 friend bool operator<(CountingID a,CountingID b){++comparisons;return a.value<b.value;}
 friend bool operator<(CountingID a,uint64_t b){++comparisons;return a.value<b;}
 friend bool operator==(CountingID a,CountingID b){++comparisons;return a.value==b.value;}
 friend bool operator==(CountingID a,uint64_t b){++comparisons;return a.value==b;}
};
struct CountingTest {CountingID id;unsigned retries=0;std::string title_json;std::vector<std::string> fixtures;};
}
int main(){
 const std::vector<Test> tests{{90,100,0,false,"\"A\"",{"x"}},{7,200,2,false,"\"B\"",{}},{UINT64_MAX,300,4,false,"\"C\"",{"abc","d"}}};
 sr::NativeRepeatProjectionIndex<Test> index(tests);
 // Sparse IDs are opaque native keys. Input/group order, fixture/name byte
 // reservations and per-test retries survive the sorted scratch index.
 Exact(index.Project({{sr::WorkGroupMode::Parallel,0,{UINT64_MAX}},{sr::WorkGroupMode::Default,0,{90,7}}},{7,UINT64_MAX,90}),{1,2},{{UINT64_MAX,42595},{90,8484},{7,25353}});
 // The outermost native serial group's maximum retry belongs to every member,
 // while an incomparable default/parallel group keeps its own retry policy.
 Exact(index.Project({{sr::WorkGroupMode::Serial,11,{90,7}},{sr::WorkGroupMode::Parallel,0,{UINT64_MAX}}},{90,7,UINT64_MAX}),{2,1},{{90,25452},{7,25353},{UINT64_MAX,42595}});
 Exact(index.Project({{sr::WorkGroupMode::Default,0,{}},{sr::WorkGroupMode::Parallel,0,{7}}},{7}),{1},{{7,25353}});
 assert(tests[0].id==90&&tests[0].retries==0&&tests[0].fixtures==std::vector<std::string>{"x"});++controls;
 const std::vector<Test> other{{7,100,0,false,"\"different original file\"",{}}};sr::NativeRepeatProjectionIndex<Test> other_index(other);
 const auto other_projection=other_index.Project({{sr::WorkGroupMode::Default,0,{7}}},{7});
 assert(other_projection.declarations->front().local_id==7&&other_projection.declarations->front().projected_attempt_bytes==other[0].title_json.size()+8448);++controls;
 const std::vector<Test> empty;sr::NativeRepeatProjectionIndex<Test> empty_index(empty);Exact(empty_index.Project({{sr::WorkGroupMode::Default,0,{}}},{}),{},{});
 Reject([&]{(void)index.Project({{sr::WorkGroupMode::Default,0,{90}}},{90,7});});
 Reject([&]{(void)index.Project({{sr::WorkGroupMode::Default,0,{8}}},{8});});
 Reject([&]{auto duplicate=tests;duplicate.push_back(tests.front());sr::NativeRepeatProjectionIndex<Test> invalid(duplicate);});
 Reject([&]{auto zero=tests;zero.front().id=0;sr::NativeRepeatProjectionIndex<Test> invalid(zero);});
 Reject([&]{const std::vector<Test> excessive(4097);sr::NativeRepeatProjectionIndex<Test> invalid(excessive);});
 // Native comparison operations are counted through the declaration ID type,
 // without a production metrics API or a timing-sensitive benchmark. The old
 // source's two full scans +find_if require41,945,088 comparisons for4096 IDs.
 std::vector<CountingTest> large;std::vector<uint64_t> ids;
 for(uint64_t i=0;i<4096;++i){const auto id=(4096-i)*17+3;large.push_back({{id},0,"\"x\"",{}});ids.push_back(id);}
 CountingID::comparisons=0;sr::NativeRepeatProjectionIndex<CountingTest> large_index(large);
 const std::vector<sr::SelectedTestGroup> groups{{sr::WorkGroupMode::Serial,31,ids}};
 const auto projected=large_index.Project(groups,ids);const auto comparisons=CountingID::comparisons;
 assert(comparisons<1000000&&projected.declarations->size()==4096&&projected.group_sizes==std::vector<size_t>{4096});
 for(size_t i=0;i<ids.size();++i){assert(projected.declarations->at(i).local_id==ids[i]&&projected.declarations->at(i).projected_attempt_bytes==8451);}++controls;
 // Original metadata/source caps and exact shared charge remain in the existing
 // selection policy: two full repeats exceed64MiB; one genuinely sharded actor
 // fits. Projection allocates no host and commits no usage on failure.
 const auto catalog=std::make_shared<const sr::NativeRepeatCatalog>(std::vector<sr::NativeRepeatUnit>{{1,1,sr::NativeRepeatRole::Primary,{2,2,2},projected.declarations,false,64}},16384,4096,0);
 const sr::NativeRepeatSelectionLimits limits{16384,128,64*1024*1024};sr::NativeRepeatSelectionUsage usage{0,0,13,0};const auto original=usage;
 Reject([&]{(void)sr::SelectNativeRepeatPrimary(*catalog,{projected.group_sizes},1,1,limits,usage);});assert(usage==original);
 auto shard=sr::SelectNativeRepeatPrimary(*catalog,{projected.group_sizes},1,2,limits,usage);assert(shard.slices.size()==1&&shard.slices[0].index==0);
 const auto expected_charge=usage.metadata_bytes+shard.slices.capacity()*sizeof(sr::NativeSelectedRepeatSlice)+sizeof(std::vector<uint64_t>)+shard.slices[0].local_ids->capacity()*sizeof(uint64_t)+64+uint64_t{8451}*4096;
 sr::NativeRepeatSelection accepted(catalog,std::move(shard.slices),limits,usage);assert(accepted.Slices().size()==1&&usage.slices==1&&usage.source_reports==1&&usage.logical_tests==4096&&usage.metadata_bytes==expected_charge);++controls;
 bool stop=false;size_t polls=0;sr::NativeRepeatProjectionIndex<CountingTest> stopped_index(large,[&]{if(stop&&++polls==2)throw std::runtime_error("Original native global stop");});
 stop=true;const auto charged=usage;Reject([&]{(void)stopped_index.Project(groups,ids);});assert(polls==2&&usage==charged);++controls;
 polls=0;Reject([&]{sr::NativeRepeatProjectionIndex<CountingTest> canceled(large,[&]{if(++polls==2)throw std::runtime_error("Original native global stop");});});assert(polls==2&&usage==charged);++controls;
 std::cout<<"Native repeat projection "<<controls<<" controls PASS; indexed declaration comparisons="<<comparisons<<"; baseline source-derived=41945088\n";
}
