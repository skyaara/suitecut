// SPDX-License-Identifier: MIT
#include "report_merge.h"
#include <cassert>
#include <iostream>
#include <unistd.h>
namespace sr=suitecut::runner;
namespace suitecut::runner {void WriteRepeatReportFixture(const RunnerReport&,const std::filesystem::path&);}
std::string Read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
void GroupMergeChecks(const std::filesystem::path& folder,int64_t pid){
  std::filesystem::create_directories(folder);
  sr::CollectedSuiteFile file;file.file={191,4,9};file.path="/controlled/grouped-repeat.spec.mjs";file.catalog.protocol="Controlled native tree for group projection\n";file.catalog.fully_parallel=true;file.catalog.tests={{1,100,0,false,"\"A\\ud800\"",{}},{2,100,0,false,"\"B\"",{}}};
  auto catalog=std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{file});const auto project=catalog->AnonymousProject();
  auto declarations=std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::vector<sr::NativeRepeatTest>{{1,100},{2,100}});
  auto repeat_catalog=std::make_shared<const sr::NativeRepeatCatalog>(std::vector<sr::NativeRepeatUnit>{{191,project+1,sr::NativeRepeatRole::Primary,{2,2,2},declarations,false,100}},128,4096,0);
  auto ids=std::make_shared<const std::vector<uint64_t>>(std::vector<uint64_t>{1,2});const sr::NativeRepeatSelectionLimits limits{128,128,64*1024*1024};sr::NativeRepeatSelectionUsage usage;
  auto repeats=std::make_shared<const sr::NativeRepeatSelection>(repeat_catalog,std::vector<sr::NativeSelectedRepeatSlice>{{0,0,ids},{0,1,ids}},limits,usage);
  auto groups=std::make_shared<const sr::NativeRepeatGroupPartition>(repeats,catalog,limits,usage);assert(groups->Groups().size()==4);
  auto directory=std::make_shared<sr::DirectoryReport>();directory->catalog=catalog;directory->selection=catalog->Select();directory->selection_resolved=true;directory->original_start=1;directory->coordinator_pid=pid;directory->repeat_selection=repeats;directory->repeat_groups=groups;
  sr::DirectoryCollectionReport collection;collection.file=0;collection.identity=file.file;collection.path=file.path;collection.observed=true;directory->collections={collection};
  std::vector<sr::ReportMergeInput> inputs;std::vector<sr::RunnerReport> originals;
  for(const auto& group:groups->Groups()){
    const auto id=group.local_ids->at(0);sr::RunnerReport report;const auto worker=static_cast<unsigned>(group.index+1);report.workers={{worker,pid,false,0,"controlled original report writer",""}};report.attempts={{id,group.index+1,0,worker,pid,id==1?"\"A\\ud800\"":"\"B\"","\"\"","passed",true,1}};
    const auto source="repeat-"+std::to_string(group.slice)+"-group-"+std::to_string(group.index);const auto path=folder/(source+".json");sr::WriteRepeatReportFixture(report,path);inputs.push_back({source,path});originals.push_back(std::move(report));
    sr::DirectorySegmentReport row;row.segment=group.index;row.file=0;row.project=project;row.generation=group.index+1;row.selected_local_ids=*group.local_ids;row.source_id=source;row.state=sr::MultiFilePlan::State::Passed;row.native_retired=true;row.repeat_slice=group.slice;row.repeat_group=group.index;directory->segments.push_back(std::move(row));
  }
  auto merged=sr::MergedRunnerReport::ReadDirectory(inputs,directory);assert(!merged.RunFailed());merged.WriteJSON(folder/"merged.json");merged.WriteJUnit(folder/"merged.xml");merged.WriteHTML(folder/"merged.html");
  auto bytes=Read(folder/"merged.json");auto document=sr::merge_detail::JSONParser(bytes).Parse();assert(sr::merge_detail::UInt(sr::merge_detail::Get(document,"summary"),"tests")==4);assert(sr::merge_detail::Rows(document,"sources").size()==4&&sr::merge_detail::Rows(document,"tests").size()==4);
  std::set<uint64_t> actual_groups;for(const auto& row:sr::merge_detail::Rows(document,"tests")){const auto& context=sr::merge_detail::Get(row,"repeatGroup");actual_groups.insert(sr::merge_detail::UInt(context,"index"));assert(!sr::merge_detail::Find(context,"selectedLocalIds"));}assert(actual_groups==std::set<uint64_t>({0,1,2,3}));
  for(size_t n=0;n<inputs.size();++n){const auto& source=sr::merge_detail::Rows(document,"sources")[n];const auto original=Read(inputs[n].path);const auto parsed_original=sr::merge_detail::JSONParser(original).Parse();assert(sr::merge_detail::Get(source,"report").raw==parsed_original.raw&&bytes.find(original)!=bytes.npos);assert(sr::merge_detail::UInt(sr::merge_detail::Get(source,"repeatGroup"),"index")==n);}
  assert(Read(folder/"merged.xml").find("native.repeatGroup")!=bytes.npos&&Read(folder/"merged.html").find("Native selected group")!=bytes.npos);
  auto partial=std::make_shared<sr::DirectoryReport>(*directory);auto& last=partial->segments.back();last.state=sr::MultiFilePlan::State::NotRun;last.generation=0;last.source_id.reset();last.native_retired=false;last.reason="Actual stop before group admission";auto partial_inputs=inputs;partial_inputs.pop_back();auto missing=sr::MergedRunnerReport::ReadDirectory(partial_inputs,partial);missing.WriteJSON(folder/"partial.json");missing.WriteJUnit(folder/"partial.xml");missing.WriteHTML(folder/"partial.html");
  auto partial_bytes=Read(folder/"partial.json");auto partial_doc=sr::merge_detail::JSONParser(partial_bytes).Parse();assert(sr::merge_detail::UInt(sr::merge_detail::Get(partial_doc,"summary"),"tests")==4&&sr::merge_detail::UInt(sr::merge_detail::Get(partial_doc,"summary"),"didNotRun")==1);const auto& unrun=sr::merge_detail::Rows(partial_doc,"tests").back();assert(sr::merge_detail::Get(unrun,"sourceId").kind==sr::merge_detail::JSON::Null&&sr::merge_detail::Bool(sr::merge_detail::Get(unrun,"nativeSelectionOnly"))&&sr::merge_detail::UInt(sr::merge_detail::Get(unrun,"repeatGroup"),"index")==3&&sr::merge_detail::Rows(unrun,"attemptIndices").empty());
  // An unrun row from a sibling group in the SAME repeat must be rejected too.
  auto foreign=originals[0];foreign.unrun={{2,"\"B\"","\"foreign sibling group\""}};sr::WriteRepeatReportFixture(foreign,folder/"foreign.json");auto forged_inputs=inputs;forged_inputs[0].path=folder/"foreign.json";auto failed=std::make_shared<sr::DirectoryReport>(*directory);failed->segments[0].state=sr::MultiFilePlan::State::Failed;Reject([&]{(void)sr::MergedRunnerReport::ReadDirectory(forged_inputs,failed);});
  auto listed=std::make_shared<sr::DirectoryReport>(*directory);listed->listing=true;listed->segments.clear();auto listing=sr::MergedRunnerReport::ReadDirectory({},listed);listing.WriteJSON(folder/"list.json");auto list_bytes=Read(folder/"list.json");auto list_doc=sr::merge_detail::JSONParser(list_bytes).Parse();assert(sr::merge_detail::Rows(list_doc,"sources").empty()&&sr::merge_detail::UInt(sr::merge_detail::Get(list_doc,"summary"),"didNotRun")==0);for(const auto& row:sr::merge_detail::Rows(list_doc,"tests")){assert(sr::merge_detail::Text(sr::merge_detail::Get(row,"outcome"))=="selected");assert(sr::merge_detail::Find(row,"repeatGroup"));}
}

int main(int argc,char** argv){
  assert(argc==2);const std::filesystem::path folder=argv[1];std::filesystem::create_directories(folder);const auto pid=static_cast<int64_t>(getpid());
  GroupMergeChecks(folder/"group-projection",pid);
  sr::CollectedSuiteFile file;file.file={91,4,9};file.path="/controlled/repeated.spec.mjs";file.catalog.protocol="Controlled immutable collection\n";file.catalog.tests={{1,100,0,false,"\"same \\ud800 title\"",{}},{2,100,0,false,"\"second\"",{}}};
  auto catalog=std::make_shared<const sr::MultiFileCatalog>(std::vector<sr::CollectedSuiteFile>{file});const auto project=catalog->AnonymousProject();
  auto declarations=std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::vector<sr::NativeRepeatTest>{{1,100},{2,100}});
  auto repeat_catalog=std::make_shared<const sr::NativeRepeatCatalog>(std::vector<sr::NativeRepeatUnit>{{91,project+1,sr::NativeRepeatRole::Primary,{2,2,2},declarations,false,100}},128,4096,0);
  auto ids=std::make_shared<const std::vector<uint64_t>>(std::vector<uint64_t>{1,2});sr::NativeRepeatSelectionUsage usage;
  auto selected=std::make_shared<const sr::NativeRepeatSelection>(repeat_catalog,std::vector<sr::NativeSelectedRepeatSlice>{{0,0,ids},{0,1,ids}},sr::NativeRepeatSelectionLimits{128,128,64*1024*1024},usage);
  auto directory=std::make_shared<sr::DirectoryReport>();directory->catalog=catalog;directory->selection=catalog->Select();directory->selection_resolved=true;directory->original_start=1;directory->coordinator_pid=pid;directory->repeat_selection=selected;
  sr::DirectoryCollectionReport collection;collection.file=0;collection.identity=file.file;collection.path=file.path;collection.observed=true;directory->collections={collection};
  sr::DirectorySegmentReport segment;segment.file=0;segment.project=project;segment.generation=1;segment.selected_local_ids={1,2};segment.source_id="repeat0";segment.state=sr::MultiFilePlan::State::Passed;segment.native_retired=true;segment.repeat_slice=0;
  auto second=segment;second.segment=1;second.generation=2;second.source_id="repeat1";second.repeat_slice=1;directory->segments={segment,second};
  sr::RunnerReport zero;zero.workers={{1,pid,false,0,"controlled writer",""}};zero.attempts={{1,1,0,1,pid,"\"same \\ud800 title\"","\"first repeat fails\"","failed",false,1},{2,2,0,1,pid,"\"second\"","\"\"","passed",true,1}};sr::WriteRepeatReportFixture(zero,folder/"zero.json");directory->segments[0].state=sr::MultiFilePlan::State::Failed;
  sr::RunnerReport one;one.workers={{2,pid,false,0,"",""}};one.attempts={{1,3,0,2,pid,"\"same \\ud800 title\"","\"retry first\"","failed",false,1},{1,4,1,2,pid,"\"same \\ud800 title\"","\"\"","passed",true,1},{2,5,0,2,pid,"\"second\"","\"\"","passed",true,1}};sr::WriteRepeatReportFixture(one,folder/"one.json");const auto original_zero=Read(folder/"zero.json"),original_one=Read(folder/"one.json");
  const std::vector<sr::ReportMergeInput> inputs{{"repeat0",folder/"zero.json"},{"repeat1",folder/"one.json"}};
  auto merged=sr::MergedRunnerReport::ReadDirectory(inputs,directory);assert(merged.RunFailed());merged.WriteJSON(folder/"merged.json");merged.WriteJUnit(folder/"merged.xml");merged.WriteHTML(folder/"merged.html");
  auto json=Read(folder/"merged.json");auto parsed=sr::merge_detail::JSONParser(json).Parse();const auto& summary=sr::merge_detail::Get(parsed,"summary");assert(sr::merge_detail::UInt(summary,"tests")==4&&sr::merge_detail::UInt(summary,"attempts")==5&&sr::merge_detail::UInt(summary,"unexpected")==1&&sr::merge_detail::UInt(summary,"flaky")==1);
  assert(json.find(original_zero)!=json.npos&&json.find(original_one)!=json.npos&&json.find("native-repeat:91:")!=json.npos);assert(Read(folder/"merged.xml").find("native.repeat")!=json.npos&&Read(folder/"merged.html").find("Native repeat context")!=json.npos);assert(Read(folder/"zero.json")==original_zero&&Read(folder/"one.json")==original_one);
  auto plain=sr::MergedRunnerReport::Read(inputs);plain.WriteJSON(folder/"plain.json");plain.WriteJUnit(folder/"plain.xml");plain.WriteHTML(folder/"plain.html");
  auto stopped=std::make_shared<sr::DirectoryReport>(*directory);auto& last=stopped->segments[1];last.state=sr::MultiFilePlan::State::NotRun;last.source_id.reset();last.generation=0;last.native_retired=false;last.reason="Actual native stop before repeat admission";
  auto partial=sr::MergedRunnerReport::ReadDirectory({inputs[0]},stopped);partial.WriteJSON(folder/"partial.json");partial.WriteJUnit(folder/"partial.xml");partial.WriteHTML(folder/"partial.html");auto partial_bytes=Read(folder/"partial.json");auto partial_doc=sr::merge_detail::JSONParser(partial_bytes).Parse();const auto& partial_summary=sr::merge_detail::Get(partial_doc,"summary");assert(sr::merge_detail::UInt(partial_summary,"tests")==4&&sr::merge_detail::UInt(partial_summary,"didNotRun")==2&&sr::merge_detail::Rows(partial_doc,"sources").size()==1);assert(partial_bytes.find("\"sourceId\":null")!=partial_bytes.npos&&partial_bytes.find("\"nativeSelectionOnly\":true")!=partial_bytes.npos);assert(Read(folder/"partial.xml").find("native.selectionOnly")!=json.npos&&Read(folder/"partial.html").find("Selected repeats without a source outcome")!=json.npos);
  // A failed source may contain real unrun rows, but never foreign logical IDs.
  auto foreign=zero;foreign.unrun={{99,"\"foreign\"","\"not started\""}};sr::WriteRepeatReportFixture(foreign,folder/"foreign.json");Reject([&]{(void)sr::MergedRunnerReport::ReadDirectory({{"repeat0",folder/"foreign.json"},inputs[1]},directory);});
  // Retry zero from a different repeat is valid only in its own actual source.
  auto invalid=one;invalid.attempts[1].retry=0;Reject([&]{sr::WriteRepeatReportFixture(invalid,folder/"invalid.json");});
  auto listed=std::make_shared<sr::DirectoryReport>(*directory);listed->listing=true;listed->segments.clear();auto listing=sr::MergedRunnerReport::ReadDirectory({},listed);listing.WriteJSON(folder/"list.json");auto listing_bytes=Read(folder/"list.json");auto listing_doc=sr::merge_detail::JSONParser(listing_bytes).Parse();assert(sr::merge_detail::Rows(listing_doc,"sources").empty()&&sr::merge_detail::Rows(listing_doc,"tests").size()==4);
  std::cout<<"Repeat merge: immutable sources, per-repeat retries, exact logical coverage and selection-only unrun PASS\n";
}
