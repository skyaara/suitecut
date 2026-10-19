// SPDX-License-Identifier: MIT
#pragma once
#include "multi_file_plan.h"
#include "directory_json.h"
#include "directory_actor_timing.h"
#include "repeat_execution.h"
#include "repeat_group_partition.h"
#include <array>
#include <memory>
#include <tuple>

namespace suitecut::runner {
// An in-process native admission is not a subprocess. Only workers below carry
// actual Node waitpid/stream receipts, preserved in their original reports.
struct DirectoryCollectionReport {size_t file=0;SuiteFileToken identity;std::string path,protocol;bool observed=false;std::vector<ReportWorker> workers;std::vector<std::string> errors;std::shared_ptr<const DirectoryActorTiming> timing;};
struct DirectorySegmentReport {
  size_t segment=0,file=0,project=0;uint64_t generation=0;
  std::vector<uint64_t> selected_local_ids;std::optional<std::string> source_id;
  MultiFilePlan::State state=MultiFilePlan::State::NotRun;std::string reason;
  bool native_retired=false;std::shared_ptr<const DirectoryActorTiming> timing;
  std::optional<size_t> repeat_slice;
  std::optional<size_t> repeat_group;
};
struct DirectoryReport {
  static constexpr size_t kSegments=128*128,kDTOBytes=64*1024*1024;
  std::shared_ptr<const MultiFileCatalog> catalog;MultiFileSelection selection;std::vector<SerialProjectPlan::State> project_states;
  std::shared_ptr<const NativeRepeatSelection> repeat_selection;
  std::shared_ptr<const NativeRepeatGroupPartition> repeat_groups;
  std::vector<DirectoryCollectionReport> collections;std::vector<DirectorySegmentReport> segments;
  std::vector<std::string> errors;double original_start=0;uint64_t global_timeout=0;
  int64_t coordinator_pid=0;size_t attachment_declarations=0,attachment_bytes=0,step_records=0,soft_records=0,output_records=0,source_records=0,physical_workers=0,physical_io=0,physical_steps=0,physical_soft=0,physical_sources=0,publication_pending=0;
  unsigned workers=1,max_failures=0,active_file_limit=1;bool resource_locks_serialize_files=false;bool listing=false,selection_resolved=false,empty_shard=false,pass_with_no_tests=false;
  bool RunFailed()const {
    if(!errors.empty())return true;
    for(const auto& row:collections){if(!row.observed||!row.errors.empty())return true;for(const auto& w:row.workers)if(w.exit_code<0||w.signaled||w.exit_code)return true;}
    for(const auto& row:segments)if(row.state!=MultiFilePlan::State::Passed)return true;
    return false;
  }
  void Validate(const std::map<std::string,std::set<uint64_t>>& actual,const std::map<std::string,bool>& failed)const {
    if(coordinator_pid<=0||attachment_declarations>128||attachment_bytes>64*1024*1024||step_records>8192||soft_records>8192||output_records>8192||source_records>256||physical_workers||physical_io||physical_steps||physical_soft||physical_sources||publication_pending||!std::isfinite(original_start)||original_start<=0||global_timeout>2147483647||!workers||workers>8||!active_file_limit||active_file_limit>workers||(resource_locks_serialize_files&&active_file_limit!=1)||max_failures>4096||segments.size()>kSegments||collections.size()>128||errors.size()>256||(selection_resolved&&!catalog)||(!selection_resolved&&(!segments.empty()||!actual.empty())))throw std::invalid_argument("Native directory report configuration rejected");
    size_t bytes=0;auto charge=[&](size_t n){if(n>kDTOBytes-bytes)throw std::length_error("Native directory report capacity64MiB exceeded");bytes+=n;};
    const auto timing=[&](const auto& t){if(t){t->Validate();charge(128+256*t->hosts.size());}};
    std::set<size_t> files,segments_seen,repeat_slices_seen,repeat_groups_seen;std::set<std::string> sources;std::set<std::pair<size_t,uint64_t>> covered;
    std::set<std::tuple<size_t,size_t,uint64_t,uint64_t>> repeated_covered;
    for(const auto& c:collections){timing(c.timing);if(!c.identity.id||!c.identity.inode||c.path.empty()||c.path.front()!='/'||!JsonString(listing_detail::Quote(c.path),16384)||c.protocol.size()>16*1024*1024)throw std::invalid_argument("Native collection actual file receipt rejected");charge(512+6*c.path.size()+2*c.protocol.size());if(!files.insert(c.file).second||c.workers.size()>128||c.errors.size()>256)throw std::invalid_argument("Native collection report repeated/bounded receipt rejected");for(const auto& w:c.workers){if(!w.worker_index||w.pid<=0||w.exit_code<0||w.exit_code>255||(w.signaled&&!w.exit_code))throw std::invalid_argument("Native collection worker lacks actual exit receipt");charge(256+2*w.stdout_bytes.size()+2*w.stderr_bytes.size());}for(const auto& e:c.errors){if(!JsonString(e,8192))throw std::invalid_argument("Native collection error JSON rejected");charge(e.size());}}
    for(const auto& e:errors){if(!JsonString(e,8192))throw std::invalid_argument("Native directory error JSON rejected");charge(e.size());}
    if(repeat_groups&&(!repeat_selection||!catalog||&repeat_groups->Selection()!=repeat_selection.get()||&repeat_groups->Catalog()!=catalog.get()))throw std::invalid_argument("Native repeat group report lost exact catalog/selection ownership");
    if(!selection_resolved){if(repeat_selection||repeat_groups)throw std::invalid_argument("Unresolved directory report invented repeat selection");if(!(empty_shard&&pass_with_no_tests)&&errors.empty()&&std::none_of(collections.begin(),collections.end(),[](const auto& c){return !c.observed||!c.errors.empty();}))throw std::invalid_argument("Unresolved directory report lacks failure evidence");return;}
    if(collections.size()!=catalog->Files().size()||selection.project_tests.size()!=catalog->Graph().Declarations().size())throw std::invalid_argument("Native directory collection coverage rejected");
    for(size_t f=0;f<catalog->Files().size();++f){if(!files.contains(f))throw std::invalid_argument("Native directory omitted actual collected file");const auto& file=catalog->Files()[f];charge(512+6*file.path.size()+2*file.catalog.protocol.size());for(const auto& test:file.catalog.tests)charge(256+test.title_json.size());}
    for(const auto& c:collections){if(c.file>=catalog->Files().size()||c.identity!=catalog->Files()[c.file].file||c.path!=catalog->Files()[c.file].path)throw std::invalid_argument("Native directory collection identity differs from immutable actual file");}
    if(repeat_selection){
      const auto& units=repeat_selection->Catalog().Units();
      if(units.size()>kSegments||repeat_selection->Slices().size()>kSegments||selection.roles.size()!=selection.project_tests.size()||selection.projects.selected.size()!=selection.project_tests.size())throw std::invalid_argument("Native repeat report selection shape rejected");
      std::map<std::pair<uint64_t,uint64_t>,size_t> declarations;
      std::map<std::pair<uint64_t,uint64_t>,uint64_t> global_ids;
      std::vector<std::set<uint64_t>> selected_union(selection.project_tests.size());
      for(const auto& test:catalog->Tests())declarations.emplace(std::pair{catalog->Files()[test.file].file.id,test.local_id},test.project);
      for(const auto& test:catalog->Tests())global_ids.emplace(std::pair{catalog->Files()[test.file].file.id,test.local_id},test.selection_id);
      for(const auto& unit:units){
        if(unit.project>selection.project_tests.size()||std::none_of(catalog->Files().begin(),catalog->Files().end(),[&](const auto& f){return f.file.id==unit.file;}))throw std::invalid_argument("Native repeat unit has no actual collected owner");
        charge(256+48*unit.declarations->size());
        for(const auto& test:*unit.declarations){const auto found=declarations.find({unit.file,test.local_id});if(found==declarations.end()||found->second+1!=unit.project)throw std::invalid_argument("Native repeat declaration differs from actual file/project");}
        if(unit.declared_empty_actor&&std::any_of(declarations.begin(),declarations.end(),[&](const auto& entry){return entry.first.first==unit.file&&entry.second+1==unit.project;}))throw std::invalid_argument("Native repeat empty actor concealed actual declarations");
      }
      for(const auto& slice:repeat_selection->Slices()){
        const auto& unit=units.at(slice.unit);const auto project=unit.project-1;
        if(!selection.projects.selected.at(project))throw std::invalid_argument("Native repeat selected an inactive project");
        const auto role=unit.role==NativeRepeatRole::Primary?"primary":unit.role==NativeRepeatRole::Dependency?"dependency":"teardown";
        if(selection.roles.at(project)!=role)throw std::invalid_argument("Native repeat project role differs from actual selection");
        charge(256+192*slice.local_ids->size());
        for(auto id:*slice.local_ids)selected_union[project].insert(global_ids.at({unit.file,id}));
      }
      for(size_t p=0;p<selected_union.size();++p){const auto& ids=selection.project_tests[p];if(std::set<uint64_t>(ids.begin(),ids.end())!=selected_union[p]||ids.size()!=selected_union[p].size())throw std::invalid_argument("Native repeat declaration union differs from logical selection");}
      if(listing&&!segments.empty())throw std::invalid_argument("Native repeat listing invented actor receipts");
    }
    if(repeat_groups){
      if(repeat_groups->Groups().size()>kSegments)throw std::invalid_argument("Native repeat report group bound rejected");
      for(size_t index=0;index<repeat_groups->Groups().size();++index){const auto& group=repeat_groups->Groups()[index];if(group.index!=index||group.slice>=repeat_selection->Slices().size()||!group.local_ids)throw std::invalid_argument("Native repeat report group identity rejected");charge(192+24*group.local_ids->size());}
    }
    for(const auto& s:segments){timing(s.timing);
      if(!segments_seen.insert(s.segment).second||s.file>=catalog->Files().size()||s.project>=selection.project_tests.size()||!selection.projects.selected.at(s.project)||s.reason.size()>8192)throw std::invalid_argument("Native directory segment selection identity rejected");
      uint64_t repeat_index=0;
      if(repeat_selection){
        if(!s.repeat_slice)throw std::invalid_argument("Native repeated segment omitted selected slice");
        const auto& slice=repeat_selection->Slices().at(*s.repeat_slice);const auto& unit=repeat_selection->Catalog().Units().at(slice.unit);repeat_index=slice.index;
        if(unit.file!=catalog->Files()[s.file].file.id||unit.project!=s.project+1)throw std::invalid_argument("Native repeated segment differs from exact logical selection");
        if(repeat_groups){
          if(!s.repeat_group||!repeat_groups_seen.insert(*s.repeat_group).second)throw std::invalid_argument("Native repeated segment omitted or reused selected group");
          const auto& group=repeat_groups->Groups().at(*s.repeat_group);
          if(group.slice!=*s.repeat_slice||s.selected_local_ids!=*group.local_ids)throw std::invalid_argument("Native repeated segment differs from exact native group partition");
          repeat_slices_seen.insert(*s.repeat_slice);
        }else if(s.repeat_group||!repeat_slices_seen.insert(*s.repeat_slice).second||s.selected_local_ids!=*slice.local_ids)throw std::invalid_argument("Native repeated segment omitted or reused exact selected slice");
      }else if(s.repeat_slice||s.repeat_group||s.selected_local_ids!=catalog->LocalTests(selection,s.project,s.file))throw std::invalid_argument("Native directory segment selection identity rejected");
      charge(512+6*s.reason.size()+24*s.selected_local_ids.size());std::set<uint64_t> selected;
      for(auto id:s.selected_local_ids)if(!selected.insert(id).second||(repeat_selection?!repeated_covered.emplace(s.file,s.project,repeat_index,id).second:!covered.emplace(s.file,id).second))throw std::invalid_argument("Native directory repeated selected identity");
      if(s.state==MultiFilePlan::State::Pending||s.state==MultiFilePlan::State::Reserved||s.state==MultiFilePlan::State::Running)throw std::invalid_argument("Native directory actor still active");
      if(s.state==MultiFilePlan::State::NotRun){if(s.generation||s.source_id||s.native_retired||s.reason.empty())throw std::invalid_argument("Native unstarted actor invented execution");continue;}
      if(s.source_id){if(!s.generation||!s.native_retired||!sources.insert(*s.source_id).second||!actual.contains(*s.source_id)||!failed.contains(*s.source_id))throw std::invalid_argument("Native source lacks real retired actor");for(auto id:actual.at(*s.source_id))if(!selected.contains(id))throw std::invalid_argument("Native source contains foreign selected identity");if(s.state==MultiFilePlan::State::Passed&&(failed.at(*s.source_id)||actual.at(*s.source_id)!=selected))throw std::invalid_argument("Passed native actor lacks genuine terminal coverage");}
      else if(s.state==MultiFilePlan::State::Passed||s.reason.empty()||s.generation||s.native_retired)throw std::invalid_argument("Native refused actor invented successful execution");
    }
    if(sources.size()!=actual.size())throw std::invalid_argument("Native directory omitted source report");
    if(!listing&&repeat_selection){
      if(repeat_slices_seen.size()!=repeat_selection->Slices().size()||(repeat_groups&&repeat_groups_seen.size()!=repeat_groups->Groups().size()))throw std::invalid_argument("Native directory logical repeat/group left uncovered");
      for(const auto& slice:repeat_selection->Slices()){const auto& unit=repeat_selection->Catalog().Units().at(slice.unit);const auto file=std::find_if(catalog->Files().begin(),catalog->Files().end(),[&](const auto& actual_file){return actual_file.file.id==unit.file;});for(auto id:*slice.local_ids)if(!repeated_covered.contains({static_cast<size_t>(file-catalog->Files().begin()),static_cast<size_t>(unit.project-1),slice.index,id}))throw std::invalid_argument("Native selected repeat group union left declaration uncovered");}
    }
    else if(!listing)for(const auto& ids:selection.project_tests)for(auto id:ids){if(!id||id>catalog->Tests().size())throw std::invalid_argument("Native global selected identity rejected");const auto& test=catalog->Tests()[id-1];if(!covered.contains({test.file,test.local_id}))throw std::invalid_argument("Native directory selected identity left uncovered");}
  }
  const DirectorySegmentReport* RepeatForSource(std::string_view source)const {
    if(repeat_selection)for(const auto& segment:segments)if(segment.source_id&&*segment.source_id==source)return &segment;
    return nullptr;
  }
  const std::string& RepeatTitle(size_t slice,uint64_t id)const {
    (void)repeat_selection->LogicalID(slice,id);
    const auto& unit=repeat_selection->Catalog().Units().at(repeat_selection->Slices().at(slice).unit);
    for(const auto& file:catalog->Files())if(file.file.id==unit.file)
      for(const auto& test:file.catalog.tests)if(test.id==id)return test.title_json;
    throw std::invalid_argument("Native repeat selected title has no original declaration");
  }
  template<class Sink>void WriteRepeatContext(Sink& out,size_t selected_slice)const {
    const auto& slice=repeat_selection->Slices().at(selected_slice);const auto& unit=repeat_selection->Catalog().Units().at(slice.unit);
    out.Put("{\"slice\":");out.NumberValue(selected_slice);out.Put(",\"fileId\":\"");out.NumberValue(unit.file);out.Put("\",\"globalProject\":");out.NumberValue(unit.project-1);out.Put(",\"declaredRepeatEach\":");out.NumberValue(unit.counts.declared);out.Put(",\"effectiveRepeatEach\":");out.NumberValue(unit.counts.effective);out.Put(",\"executions\":");out.NumberValue(unit.counts.executions);out.Put(",\"repeatEachIndex\":");out.NumberValue(slice.index);out.Put("}");
  }
  template<class Sink>void WriteRepeatGroupContext(Sink& out,size_t group_index,bool include_ids=false)const {
    const auto& group=repeat_groups->Groups().at(group_index);
    out.Put("{\"index\":");out.NumberValue(group.index);out.Put(",\"slice\":");out.NumberValue(group.slice);out.Put(",\"mode\":\"");out.Put(group.mode==WorkGroupMode::Parallel?"parallel":group.mode==WorkGroupMode::Serial?"serial":"default");out.Put("\",\"suiteId\":\"");out.NumberValue(group.suite);out.Put("\"");if(include_ids){out.Put(",\"selectedLocalIds\":");IDs(out,*group.local_ids);}out.Put("}");
  }
  template<class Sink,class Values>static void IDs(Sink& out,const Values& ids){bool first=true;out.Put("[");for(auto id:ids){if(!first)out.Put(",");first=false;out.Put("\"");out.NumberValue(id);out.Put("\"");}out.Put("]");}
  template<class Sink,class Values>static void Strings(Sink& out,const Values& values){bool first=true;out.Put("[");for(const auto& value:values){if(!first)out.Put(",");first=false;out.Put(listing_detail::Quote(value));}out.Put("]");}
  template<class Sink>static void Timing(Sink& out,const std::shared_ptr<const DirectoryActorTiming>& t){
    if(!t){out.Put("null");return;}out.Put("{\"actorAdmittedMonotonicMs\":");out.DecimalValue(t->admitted);out.Put(",\"actorCompletedMonotonicMs\":");out.DecimalValue(*t->completed);out.Put(",\"hosts\":[");bool first=true;
    for(const auto& h:t->hosts){if(!first)out.Put(",");first=false;out.Put("{\"workerIndex\":");out.NumberValue(h.worker_index);out.Put(",\"pid\":");out.NumberValue(h.pid);out.Put(",\"admittedMonotonicMs\":");out.DecimalValue(h.admitted);for(const auto& [name,value]:{std::pair<const char*,std::optional<double>>{"metadataDecodedMonotonicMs",h.metadata},{"closeStartedMonotonicMs",h.close_started},{"waitpidCapturedMonotonicMs",h.captured}}){out.Put(",\"");out.Put(name);out.Put("\":");if(value)out.DecimalValue(*value);else out.Put("null");}out.Put("}");}out.Put("]}");
  }
  std::string JSON()const{DirectoryJSONBuffer sink;Write(sink);return std::move(sink.bytes);}
  template<class Sink>void Write(Sink& out)const {
    // MultiFileCatalog appends global Tests in Files()/catalog.tests order.
    // Reuse that immutable correspondence and index selected native IDs once;
    // a foreign project's list must never select another project's test.
    std::array<unsigned char,4097> selected_ids{};
    if(catalog&&selection_resolved)for(size_t project=0;project<selection.project_tests.size();++project)
      for(auto id:selection.project_tests[project])if(id&&id<=catalog->Tests().size()&&catalog->Tests()[id-1].project==project)selected_ids[id]=1;
    size_t global_test=0;
    auto quote=listing_detail::Quote;out.Put("{\"version\":1,\"kind\":\"suitecut-native-directory\",\"execution\":\"native coroutine cohorts; shared run-global budgets\",\"selectionResolved\":");out.Put(selection_resolved?"true":"false");out.Put(",\"runFailed\":");out.Put(RunFailed()?"true":"false");out.Put(",\"coordinatorPID\":");out.NumberValue(coordinator_pid);out.Put(",\"nativeBudgetReceipt\":{\"attachmentDeclarations\":");out.NumberValue(attachment_declarations);out.Put(",\"retainedAttachmentBytes\":");out.NumberValue(attachment_bytes);out.Put(",\"stepRecords\":");out.NumberValue(step_records);out.Put(",\"softRecords\":");out.NumberValue(soft_records);out.Put(",\"outputRecords\":");out.NumberValue(output_records);out.Put(",\"sourceRecords\":");out.NumberValue(source_records);out.Put(",\"physicalWorkers\":");out.NumberValue(physical_workers);out.Put(",\"physicalIO\":");out.NumberValue(physical_io);out.Put(",\"physicalSteps\":");out.NumberValue(physical_steps);out.Put(",\"physicalSoft\":");out.NumberValue(physical_soft);out.Put(",\"physicalSources\":");out.NumberValue(physical_sources);out.Put(",\"publicationPending\":");out.NumberValue(publication_pending);out.Put("},\"requestedWorkers\":");out.NumberValue(workers);out.Put(",\"activeFileLimit\":");out.NumberValue(active_file_limit);out.Put(",\"resourceLocksSerializeFiles\":");out.Put(resource_locks_serialize_files?"true":"false");out.Put(",\"originalStartMonotonicMs\":");out.DecimalValue(original_start);out.Put(",\"globalTimeoutMs\":");out.NumberValue(global_timeout);out.Put(",\"maxFailures\":");out.NumberValue(max_failures);out.Put(",\"listing\":");out.Put(listing?"true":"false");out.Put(",\"emptyShard\":");out.Put(empty_shard?"true":"false");out.Put(",\"files\":[");bool first=true;
    if(catalog)for(size_t f=0;f<catalog->Files().size();++f){if(!first)out.Put(",");first=false;const auto& file=catalog->Files()[f];out.Put("{\"index\":");out.NumberValue(f);out.Put(",\"path\":");out.Put(quote(file.path));out.Put(",\"fileId\":\"");out.NumberValue(file.file.id);out.Put("\",\"device\":\"");out.NumberValue(file.file.device);out.Put("\",\"inode\":\"");out.NumberValue(file.file.inode);out.Put("\",\"collectionProtocolUTF8Hex\":\"");out.Hex(file.catalog.protocol);out.Put("\",\"tests\":[");bool ft=true;for(const auto& test:file.catalog.tests){const auto& t=catalog->Tests().at(global_test++);if(!ft)out.Put(",");ft=false;out.Put("{\"selectionId\":\"");out.NumberValue(t.selection_id);out.Put("\",\"localTestId\":\"");out.NumberValue(t.local_id);out.Put("\",\"globalProject\":");out.NumberValue(t.project);out.Put(",\"title\":");out.Put(test.title_json);out.Put(",\"selected\":");if(selection_resolved){(void)selection.project_tests.at(t.project);out.Put(selected_ids.at(t.selection_id)?"true":"false");}else out.Put("null");out.Put("}");}out.Put("]}");}
    out.Put("],\"projects\":[");first=true;if(catalog)for(size_t p=0;p<catalog->Graph().Declarations().size();++p){if(!first)out.Put(",");first=false;const auto& d=catalog->Graph().Declarations()[p];out.Put("{\"index\":");out.NumberValue(p);out.Put(",\"nativeAnonymous\":");out.Put(p==catalog->AnonymousProject()?"true":"false");out.Put(",\"name\":");out.Put(quote(d.name));out.Put(",\"dependencies\":");Strings(out,d.dependencies);out.Put(",\"teardown\":");out.Put(d.teardown?quote(*d.teardown):"null");out.Put(",\"status\":");if(p<project_states.size()){using S=SerialProjectPlan::State;const auto state=project_states[p];out.Put(quote(state==S::Passed?"passed":state==S::Failed?"failed":state==S::Interrupted?"interrupted":state==S::Blocked?"blocked":state==S::NotRun?"notRun":state==S::NotSelected?"notSelected":"pending"));}else out.Put("null");out.Put(",\"role\":");out.Put(selection_resolved?quote(selection.roles.at(p)):"null");out.Put(",\"selectedGlobalIds\":");if(selection_resolved)IDs(out,selection.project_tests.at(p));else out.Put("null");out.Put("}");}if(repeat_selection){
      out.Put("],\"repeatSelection\":{\"units\":[");bool first_unit=true;
      for(const auto& unit:repeat_selection->Catalog().Units()){
        if(!first_unit){out.Put(",");}first_unit=false;out.Put("{\"fileId\":\"");out.NumberValue(unit.file);out.Put("\",\"globalProject\":");out.NumberValue(unit.project-1);out.Put(",\"declaredRepeatEach\":");out.NumberValue(unit.counts.declared);out.Put(",\"effectiveRepeatEach\":");out.NumberValue(unit.counts.effective);out.Put(",\"executions\":");out.NumberValue(unit.counts.executions);out.Put("}");
      }
      out.Put("],\"selectedSlices\":[");bool first_slice=true;
      for(size_t n=0;n<repeat_selection->Slices().size();++n){
        if(!first_slice){out.Put(",");}first_slice=false;out.Put("{\"context\":");WriteRepeatContext(out,n);out.Put(",\"tests\":[");bool first_test=true;
        for(auto id:*repeat_selection->Slices()[n].local_ids){if(!first_test)out.Put(",");first_test=false;out.Put("{\"localTestId\":\"");out.NumberValue(id);out.Put("\",\"logicalId\":");out.Put(quote(repeat_selection->LogicalID(n,id)));out.Put("}");}out.Put("]}");
      }
      out.Put("]}");
    }else out.Put("]");
    if(repeat_groups){out.Put(",\"repeatGroups\":[");for(size_t index=0;index<repeat_groups->Groups().size();++index){if(index)out.Put(",");WriteRepeatGroupContext(out,index,true);}out.Put("]");}
    out.Put(",\"collections\":[");first=true;for(const auto& c:collections){if(!first)out.Put(",");first=false;out.Put("{\"file\":");out.NumberValue(c.file);out.Put(",\"path\":");out.Put(quote(c.path));out.Put(",\"fileId\":\"");out.NumberValue(c.identity.id);out.Put("\",\"device\":\"");out.NumberValue(c.identity.device);out.Put("\",\"inode\":\"");out.NumberValue(c.identity.inode);out.Put("\",\"collectionProtocolUTF8Hex\":\"");out.Hex(c.protocol);out.Put("\",\"timing\":");Timing(out,c.timing);out.Put(",\"observed\":");out.Put(c.observed?"true":"false");out.Put(",\"workers\":[");bool fw=true;for(const auto& w:c.workers){if(!fw)out.Put(",");fw=false;out.Put("{\"workerIndex\":");out.NumberValue(w.worker_index);out.Put(",\"pid\":");out.NumberValue(w.pid);out.Put(",\"signaled\":");out.Put(w.signaled?"true":"false");out.Put(",\"exitCode\":");out.NumberValue(w.exit_code);out.Put(",\"stdoutHex\":\"");out.Hex(w.stdout_bytes);out.Put("\",\"stderrHex\":\"");out.Hex(w.stderr_bytes);out.Put("\"}");}out.Put("],\"errors\":[");bool fe=true;for(const auto& e:c.errors){if(!fe)out.Put(",");fe=false;out.Put(e);}out.Put("]}");}
    out.Put("],\"segments\":[");first=true;for(const auto& s:segments){if(!first)out.Put(",");first=false;out.Put("{\"segment\":");out.NumberValue(s.segment);out.Put(",\"file\":");out.NumberValue(s.file);out.Put(",\"project\":");out.NumberValue(s.project);out.Put(",\"generation\":\"");out.NumberValue(s.generation);out.Put("\",\"selectedLocalIds\":");IDs(out,s.selected_local_ids);out.Put(",\"sourceId\":");out.Put(s.source_id?quote(*s.source_id):"null");out.Put(",\"nativeRetired\":");out.Put(s.native_retired?"true":"false");if(s.repeat_slice){out.Put(",\"repeat\":");WriteRepeatContext(out,*s.repeat_slice);}if(s.repeat_group){out.Put(",\"repeatGroup\":");WriteRepeatGroupContext(out,*s.repeat_group);}out.Put(",\"status\":");const char* status=s.state==MultiFilePlan::State::Passed?"passed":s.state==MultiFilePlan::State::Failed?"failed":s.state==MultiFilePlan::State::Interrupted?"interrupted":"notRun";out.Put(quote(status));out.Put(",\"timing\":");Timing(out,s.timing);out.Put(",\"reason\":");out.Put(quote(s.reason));out.Put("}");}
    out.Put("],\"errors\":[");first=true;for(const auto& e:errors){if(!first)out.Put(",");first=false;out.Put(e);}out.Put("]}");
  }
};
}
