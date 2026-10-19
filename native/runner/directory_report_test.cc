// SPDX-License-Identifier: MIT
#include "directory_report.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
void FileRowChecks(){
  sr::CollectedSuiteFile first;first.file={71,4,9};first.path="/controlled/rows-a.spec.mjs";
  first.catalog.projects={{"one",{},std::nullopt,true},{"two",{},std::nullopt,true}};first.catalog.project_metadata={"{}","{}"};
  first.catalog.tests={{91,100,0,false,"\"A\\ud800\"",{}},{7,100,0,false,"\"B\"",{}},{800,100,0,false,"\"C\"",{}}};first.catalog.project_tests={{91,0},{7,1},{800,0}};
  auto empty=first;empty.file.id=72;empty.path="/controlled/empty.spec.mjs";empty.catalog.tests.clear();empty.catalog.project_tests.clear();
  auto last=first;last.file.id=73;last.path="/controlled/rows-b.spec.mjs";last.catalog.tests={{7,100,0,false,"\"D\"",{}}};last.catalog.project_tests={{7,1}};
  sr::DirectoryReport report;report.catalog=std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{first,empty,last});report.selection_resolved=true;report.selection=report.catalog->Select();
  // Reordered and duplicate memberships retain boolean selection semantics;
  // IDs placed only in a foreign project's list stay excluded.
  report.selection.project_tests[0]={3,1,3,4};report.selection.project_tests[1]={};
  const auto json=report.JSON();
  const auto a=json.find("\"localTestId\":\"91\",\"globalProject\":0,\"title\":\"A\\ud800\",\"selected\":true");
  const auto b=json.find("\"localTestId\":\"7\",\"globalProject\":1,\"title\":\"B\",\"selected\":false");
  const auto c=json.find("\"localTestId\":\"800\",\"globalProject\":0,\"title\":\"C\",\"selected\":true");
  const auto d=json.find("\"localTestId\":\"7\",\"globalProject\":1,\"title\":\"D\",\"selected\":false");
  assert(a!=json.npos&&a<b&&b<c&&c<d&&d!=json.npos&&json.find("\"tests\":[]")!=json.npos);
  report.selection_resolved=false;assert(report.JSON().find("\"selected\":true")==json.npos&&report.JSON().find("\"selected\":null")!=json.npos);
  report.selection_resolved=true;report.selection.project_tests.clear();Reject([&]{(void)report.JSON();});
  report.catalog.reset();assert(report.JSON().find("\"files\":[]")!=json.npos);
}
void GroupChecks(){
  sr::CollectedSuiteFile file;file.file={51,4,9};file.path="/controlled/groups.spec.mjs";file.catalog.protocol="Controlled original grouping catalog\n";file.catalog.fully_parallel=true;
  file.catalog.tests={{1,100,0,false,"\"A\"",{}},{2,100,0,false,"\"B\"",{}}};
  auto catalog=std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{file});const auto project=catalog->AnonymousProject();
  auto declarations=std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::vector<sr::NativeRepeatTest>{{1,100},{2,100}});
  auto repeat_catalog=std::make_shared<const sr::NativeRepeatCatalog>(std::vector<sr::NativeRepeatUnit>{{51,project+1,sr::NativeRepeatRole::Primary,{2,2,2},declarations,false,100}},128,4096,0);
  auto ids=std::make_shared<const std::vector<uint64_t>>(std::vector<uint64_t>{1,2});sr::NativeRepeatSelectionUsage usage;const sr::NativeRepeatSelectionLimits limits{128,128,64*1024*1024};
  auto repeats=std::make_shared<const sr::NativeRepeatSelection>(repeat_catalog,std::vector<sr::NativeSelectedRepeatSlice>{{0,0,ids},{0,1,ids}},limits,usage);
  auto groups=std::make_shared<const sr::NativeRepeatGroupPartition>(repeats,catalog,limits,usage);assert(groups->Groups().size()==4);
  sr::DirectoryReport report;report.catalog=catalog;report.selection=catalog->Select();report.selection_resolved=true;report.original_start=1;report.coordinator_pid=321;report.repeat_selection=repeats;report.repeat_groups=groups;
  sr::DirectoryCollectionReport collection;collection.file=0;collection.identity=file.file;collection.path=file.path;collection.observed=true;report.collections={collection};
  std::map<std::string,std::set<uint64_t>> actual;std::map<std::string,bool> success;
  for(const auto& group:groups->Groups()){
    sr::DirectorySegmentReport row;row.segment=group.index;row.file=0;row.project=project;row.generation=group.index+1;row.selected_local_ids=*group.local_ids;row.source_id="controlled-group-"+std::to_string(group.index);row.state=sr::MultiFilePlan::State::Passed;row.native_retired=true;row.repeat_slice=group.slice;row.repeat_group=group.index;
    actual[*row.source_id]=std::set<uint64_t>(group.local_ids->begin(),group.local_ids->end());success[*row.source_id]=false;report.segments.push_back(std::move(row));
  }
  report.Validate(actual,success);assert(!report.RunFailed());const auto json=report.JSON();assert(json.find("\"repeatGroups\":[{\"index\":0,\"slice\":0,\"mode\":\"parallel\"")!=json.npos&&json.find("\"repeatGroup\":")!=json.npos);
  auto wrong=report;wrong.repeat_groups.reset();Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[1].repeat_group=0;Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[0].repeat_slice=1;Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[0].repeat_group=99;Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[0].repeat_group.reset();Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[0].selected_local_ids={1,2};Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments.pop_back();auto missing=actual;missing.erase("controlled-group-3");Reject([&]{wrong.Validate(missing,success);});
  auto cloned_catalog=std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{file});auto fresh_usage=repeats->AcceptedSharedUsage();wrong=report;wrong.repeat_groups=std::make_shared<const sr::NativeRepeatGroupPartition>(repeats,cloned_catalog,limits,fresh_usage);Reject([&]{wrong.Validate(actual,success);});
  wrong=report;auto& unstarted=wrong.segments.back();unstarted.state=sr::MultiFilePlan::State::NotRun;unstarted.source_id.reset();unstarted.generation=0;unstarted.native_retired=false;unstarted.reason="Actual stop before group admission";wrong.Validate(missing,success);assert(wrong.RunFailed());
  wrong=report;wrong.listing=true;wrong.segments.clear();wrong.Validate({},{});assert(wrong.JSON().find("\"segments\":[]")!=json.npos);
}
void RepeatChecks(){
  sr::CollectedSuiteFile file;file.file={41,4,9};file.path="/controlled/repeats.spec.mjs";file.catalog.protocol="Native original collection\n";
  file.catalog.tests={{1,100,0,false,"\"A\\ud800\"",{}},{2,100,0,false,"\"B\"",{}}};
  auto catalog=std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{file});
  const auto project=catalog->AnonymousProject();
  auto declarations=std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::vector<sr::NativeRepeatTest>{{1,100},{2,100}});
  auto make_selection=[&](sr::NativeRepeatCounts counts,std::vector<sr::NativeSelectedRepeatSlice> slices){
    auto repeat_catalog=std::make_shared<const sr::NativeRepeatCatalog>(std::vector<sr::NativeRepeatUnit>{{41,project+1,sr::NativeRepeatRole::Primary,counts,declarations,false,100}},128,4096,0);
    sr::NativeRepeatSelectionUsage usage;return std::make_shared<const sr::NativeRepeatSelection>(repeat_catalog,std::move(slices),sr::NativeRepeatSelectionLimits{128,128,64*1024*1024},usage);
  };
  auto ids=[](std::vector<uint64_t> value){return std::make_shared<const std::vector<uint64_t>>(std::move(value));};
  sr::DirectoryReport report;report.catalog=catalog;report.selection=catalog->Select();report.selection_resolved=true;report.original_start=1;report.coordinator_pid=321;
  sr::DirectoryCollectionReport collection;collection.file=0;collection.identity=file.file;collection.path=file.path;collection.observed=true;report.collections={collection};
  report.repeat_selection=make_selection({3,3,3},{{0,0,ids({1,2})},{0,1,ids({1})}});
  sr::DirectorySegmentReport first;first.segment=0;first.file=0;first.project=project;first.generation=1;first.selected_local_ids={1,2};first.source_id="repeat-zero";first.state=sr::MultiFilePlan::State::Passed;first.native_retired=true;first.repeat_slice=0;
  auto second=first;second.segment=1;second.generation=2;second.selected_local_ids={1};second.source_id="repeat-one";second.repeat_slice=1;report.segments={first,second};
  const std::map<std::string,std::set<uint64_t>> actual{{"repeat-zero",{1,2}},{"repeat-one",{1}}};const std::map<std::string,bool> success{{"repeat-zero",false},{"repeat-one",false}};
  report.Validate(actual,success);assert(!report.RunFailed());const auto json=report.JSON();assert(json.find("native-repeat:41:")!=json.npos&&json.find("\"repeatEachIndex\":1")!=json.npos);
  auto wrong=report;wrong.segments.pop_back();Reject([&]{wrong.Validate({{"repeat-zero",{1,2}}},{{"repeat-zero",false}});});
  wrong=report;wrong.segments[1].repeat_slice=0;Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[1].repeat_slice.reset();Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[1].repeat_slice=9;Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[1].selected_local_ids={1,2};Reject([&]{wrong.Validate(actual,success);});
  Reject([&]{report.Validate({{"repeat-zero",{1,2}},{"repeat-one",{2}}},success);});
  wrong=report;wrong.selection.project_tests[project]={1};Reject([&]{wrong.Validate(actual,success);});
  wrong=report;wrong.segments[1].state=sr::MultiFilePlan::State::NotRun;wrong.segments[1].source_id.reset();wrong.segments[1].generation=0;wrong.segments[1].native_retired=false;wrong.segments[1].reason="Stopped before this repeat admitted";wrong.Validate({{"repeat-zero",{1,2}}},{{"repeat-zero",false}});assert(wrong.RunFailed());
  wrong.segments[1].generation=2;Reject([&]{wrong.Validate({{"repeat-zero",{1,2}}},{{"repeat-zero",false}});});
  wrong=report;wrong.listing=true;Reject([&]{wrong.Validate(actual,success);});wrong.segments.clear();wrong.Validate({},{});assert(wrong.JSON().find("\"segments\":[]")!=json.npos);
  report.repeat_selection=make_selection({0,0,1},{{0,0,ids({1,2})}});report.segments={first};report.Validate({{"repeat-zero",{1,2}}},{{"repeat-zero",false}});const auto zero=report.JSON();assert(zero.find("\"effectiveRepeatEach\":0,\"executions\":1")!=zero.npos);
  wrong=report;wrong.repeat_selection.reset();Reject([&]{wrong.Validate({{"repeat-zero",{1,2}}},{{"repeat-zero",false}});});
}
int main(){
  FileRowChecks();
  GroupChecks();
  RepeatChecks();
  sr::CollectedSuiteFile f;f.file={1,4,9};f.path="/controlled/actual.spec.mjs";f.catalog.protocol="Exact controlled native collection\n";f.catalog.tests.push_back({1,100,0,false,"\"controlled declaration\"",{}});
  auto catalog=std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{f});sr::DirectoryReport d;d.catalog=catalog;d.selection=catalog->Select();d.selection_resolved=true;d.original_start=1;d.coordinator_pid=321;
  sr::DirectoryCollectionReport c;c.file=0;c.identity=f.file;c.path=f.path;c.observed=true;c.workers.push_back({1,123,false,0,"actual controlled stdout",""});d.collections.push_back(c);
  sr::DirectorySegmentReport s;s.file=0;s.project=catalog->AnonymousProject();s.generation=1;s.selected_local_ids={1};s.source_id="original-source";s.state=sr::MultiFilePlan::State::Passed;s.native_retired=true;d.segments.push_back(s);
  const std::map<std::string,std::set<uint64_t>> actual{{"original-source",{1}}};const std::map<std::string,bool> success{{"original-source",false}};
  d.Validate(actual,success);assert(!d.RunFailed());d.collections[0].identity.inode=10;Reject([&]{d.Validate(actual,success);});d.collections[0]=c;d.collections[0].path="/controlled/foreign.spec.mjs";Reject([&]{d.Validate(actual,success);});d.collections[0]=c;const auto json=d.JSON();assert(json.find("collectionProtocolUTF8Hex")!=json.npos&&json.find("globalProject")!=json.npos&&json.find("native coroutine cohorts")!=json.npos);
  auto timing=std::make_shared<sr::DirectoryActorTiming>(1);timing->HostStarted(1,123,2);timing->Metadata(1,123,3);timing->Closing(1,123,4);timing->Captured(1,123,5);timing->Complete(6);d.collections[0].timing=timing;d.Validate(actual,success);assert(d.JSON().find("metadataDecodedMonotonicMs")!=std::string::npos);
  d.active_file_limit=2;Reject([&]{d.Validate(actual,success);});d.workers=2;d.Validate(actual,success);d.resource_locks_serialize_files=true;Reject([&]{d.Validate(actual,success);});d.active_file_limit=1;d.Validate(actual,success);
  Reject([&]{d.Validate({{"original-source",{}}},success);});Reject([&]{d.Validate({{"original-source",{2}}},success);});Reject([&]{d.Validate(actual,{{"original-source",true}});});
  d.segments[0].native_retired=false;Reject([&]{d.Validate(actual,success);});d.segments[0].native_retired=true;
  d.segments[0].state=sr::MultiFilePlan::State::Failed;d.Validate({{"original-source",{}}},{{"original-source",true}});assert(d.RunFailed());
  d.segments[0]=s;d.collections[0].workers[0].signaled=true;d.collections[0].workers[0].exit_code=9;d.Validate(actual,success);assert(d.RunFailed());
  d.collections[0]=c;d.segments[0].state=sr::MultiFilePlan::State::NotRun;d.segments[0].generation=0;d.segments[0].source_id.reset();d.segments[0].native_retired=false;d.segments[0].reason="Actual global stop before admission";d.Validate({},{});assert(d.RunFailed());
  d.segments[0].generation=1;Reject([&]{d.Validate({},{});});
  sr::DirectoryReport failed;failed.original_start=1;failed.coordinator_pid=321;d.coordinator_pid=321;failed.collections={c};failed.collections[0].observed=false;failed.errors={"\"actual collection failed\""};failed.Validate({},{});assert(failed.RunFailed());
  sr::DirectoryReport empty;empty.original_start=1;empty.coordinator_pid=321;Reject([&]{empty.Validate({},{});});empty.pass_with_no_tests=true;empty.empty_shard=true;empty.Validate({},{});assert(!empty.RunFailed());
  sr::DirectoryJSONBuffer bounded;bounded.bytes.resize(sr::DirectoryJSONBuffer::kBytes-1);Reject([&]{bounded.Put("xx");});
  std::cout<<"Directory report: controlled coverage/receipt/serialization checks PASS\n";
}
