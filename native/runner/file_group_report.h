// SPDX-License-Identifier: MIT
#pragma once
#include "file_group_locks.h"
#include "test_attachments.h"
#include "work_group_report.h"
namespace suitecut::runner {
struct ReportSuiteFile {
  SuiteFileToken owner;std::string path,status;
  std::vector<uint64_t> tests,selected;
  double opened=0,retired=0;
};
struct ReportFileWorkGroup {
  uint64_t id=0,file_id=0,suite=0;std::optional<uint64_t> generation;
  WorkGroupMode mode=WorkGroupMode::Default;std::string status,receipt;
  std::vector<uint64_t> tests;std::vector<unsigned> workers;
};
struct ReportFileResourceLocks {
  uint64_t file_id=0,group_id=0,generation=0;std::string receipt;std::vector<std::string> names;
};
template<class Report>void ValidateFileGroupReports(const Report& report){
  if(report.files.empty()&&report.file_work_groups.empty()&&report.file_resource_locks.empty())return;
  if(report.files.size()!=1||report.file_work_groups.empty()||report.file_work_groups.size()>NativeTestGroupTree::kGroups||
     !report.projects.empty()||!report.work_groups.empty()||!report.resource_locks.empty())
    throw std::invalid_argument("Native anonymous file report requires one actual file and no fabricated project");
  const auto& file=report.files[0];
  if(!file.owner.id||file.path.empty()||file.path.front()!='/'||file.path.find('\0')!=std::string::npos||file.tests.size()>NativeTestGroupTree::kTests||
     !std::isfinite(file.opened)||!std::isfinite(file.retired)||file.opened<0||file.retired<file.opened)
    throw std::invalid_argument("Native anonymous file identity or close receipt rejected");
  (void)AttachmentPathJSON(file.path);
  std::set<uint64_t> all,selected,grouped,generations;std::set<unsigned> workers;
  for(const auto id:file.tests)if(!id||!all.insert(id).second)throw std::invalid_argument("Native file declaration identity rejected");
  for(const auto id:file.selected)if(!all.contains(id)||!selected.insert(id).second)throw std::invalid_argument("Native file selection identity rejected");
  bool failed=false,interrupted=false,not_run=false;
  for(size_t i=0;i<report.file_work_groups.size();++i){const auto& group=report.file_work_groups[i];
    (void)WorkGroupModeName(group.mode);
    if(group.id!=i+1||group.file_id!=file.owner.id||(group.mode==WorkGroupMode::Serial&&!group.suite)||
       (group.mode==WorkGroupMode::Parallel&&group.tests.size()!=1)||group.tests.size()>selected.size()-grouped.size()||
       group.workers.size()>8192-workers.size()||(group.tests.empty()&&!selected.empty()))
      throw std::invalid_argument("Native file work-group identity/capacity rejected");
    if(group.generation&&(!*group.generation||*group.generation>NativeTestGroupTree::kGroups||!generations.insert(*group.generation).second))
      throw std::invalid_argument("Native file group admission generation rejected");
    for(const auto id:group.tests)if(!selected.contains(id)||!grouped.insert(id).second)
      throw std::invalid_argument("Native file group test ownership rejected");
    if(group.receipt=="physicalRetired"||group.receipt=="idleTransferred"){
      if((group.receipt=="idleTransferred"&&report.worker_reuse.borrows.empty())||!group.generation||group.workers.empty()||(group.status!="passed"&&group.status!="failed"&&group.status!="interrupted"))
        throw std::invalid_argument("Native file group lacks actual physical retirement");
    }else if(group.receipt=="neverStarted"){
      if(!group.generation||!group.workers.empty()||group.status!="notRun")throw std::invalid_argument("Native file group fabricated cancellation worker");
    }else if(group.receipt=="notAdmitted"){
      if(group.generation||!group.workers.empty()||group.status!="notRun")throw std::invalid_argument("Native file group fabricated admission");
    }else throw std::invalid_argument("Native file group has no terminal receipt");
    for(const auto index:group.workers){const auto worker=std::find_if(report.workers.begin(),report.workers.end(),[&](const auto& value){return value.worker_index==index;});
      if(!index||(!workers.insert(index).second&&report.worker_reuse.borrows.empty())||worker==report.workers.end()||worker->exit_code<0)
        throw std::invalid_argument("Native file group lacks unique actual worker exit");
    }
    failed|=group.status=="failed";interrupted|=group.status=="interrupted";not_run|=group.status=="notRun";
  }
  if(grouped!=selected||workers.size()!=report.workers.size()||file.status!=(failed?"failed":interrupted?"interrupted":not_run?"notRun":"passed"))
    throw std::invalid_argument("Native file aggregate disagrees with genuine group receipts");
  const auto owning_group=[&](uint64_t id){return std::find_if(report.file_work_groups.begin(),report.file_work_groups.end(),[&](const auto& group){return std::find(group.tests.begin(),group.tests.end(),id)!=group.tests.end();});};
  for(const auto& attempt:report.attempts){const auto group=owning_group(attempt.test_id);
    if(group==report.file_work_groups.end()||std::find(group->workers.begin(),group->workers.end(),attempt.worker_index)==group->workers.end())
      throw std::invalid_argument("Actual native attempt has no owning file group worker");
  }
  for(const auto& row:report.unrun)if(!selected.contains(row.test_id))throw std::invalid_argument("Native file report contains foreign unrun declaration");
  for(const auto& row:report.skipped)if(!selected.contains(row.test_id))throw std::invalid_argument("Native file report contains foreign skipped declaration");
  for(const auto& row:report.runtime_skipped)if(!selected.contains(row.test_id))throw std::invalid_argument("Native file runtime skip contains foreign declaration");
  std::set<uint64_t> locked;std::set<std::string> namespace_names;size_t references=0,bytes=0;
  for(const auto& lock:report.file_resource_locks){
    if(lock.file_id!=file.owner.id||!lock.group_id||lock.group_id>report.file_work_groups.size()||!locked.insert(lock.group_id).second||lock.names.empty()||lock.names.size()>ResourceLocks::kReferences-references)
      throw std::invalid_argument("Native file lock report identity/capacity rejected");
    const auto& group=report.file_work_groups[lock.group_id-1];
    if(!group.generation||lock.generation!=*group.generation||lock.receipt!=group.receipt)
      throw std::invalid_argument("Native file lock lacks exact terminal file group receipt");
    references+=lock.names.size();std::set<std::string> unique;
    for(const auto& name:lock.names){
      if(name.size()%2||name.size()>kResourceLockUnits*2||name.size()+32>ResourceLocks::kMetadata-bytes||!unique.insert(name).second)
        throw std::invalid_argument("Native file lock report name/metadata rejected");
      bytes+=name.size()+32;namespace_names.insert(name);if(namespace_names.size()>ResourceLocks::kNames)throw std::invalid_argument("Native file lock report namespace capacity exceeded");
    }
  }
}
template<class Writer>void PutFileReports(Writer& out,const std::vector<ReportSuiteFile>& files){
  out.Put("[");bool first=true;
  for(const auto& file:files){if(!first)out.Put(",");first=false;
    out.Put("{\"id\":\"");out.NumberValue(file.owner.id);out.Put("\",\"device\":\"");out.NumberValue(file.owner.device);out.Put("\",\"inode\":\"");out.NumberValue(file.owner.inode);
    out.Put("\",\"path\":");out.Put(AttachmentPathJSON(file.path));out.Put(",\"status\":\"");out.Put(file.status);out.Put("\",\"receipt\":\"nativeFileClosed\",\"openedMs\":");out.NumberValue(file.opened);out.Put(",\"retiredMs\":");out.NumberValue(file.retired);
    out.Put(",\"testIds\":[");bool first_id=true;for(const auto id:file.tests){if(!first_id)out.Put(",");first_id=false;out.Put("\"");out.NumberValue(id);out.Put("\"");}
    out.Put("],\"selectedTestIds\":[");first_id=true;for(const auto id:file.selected){if(!first_id)out.Put(",");first_id=false;out.Put("\"");out.NumberValue(id);out.Put("\"");}out.Put("]}");
  }out.Put("]");
}
template<class Writer>void PutFileGroupReports(Writer& out,const std::vector<ReportFileWorkGroup>& groups){
  out.Put("[");bool first=true;
  for(const auto& group:groups){if(!first)out.Put(",");first=false;
    out.Put("{\"id\":\"");out.NumberValue(group.id);out.Put("\",\"fileId\":\"");out.NumberValue(group.file_id);out.Put("\",\"generation\":");
    if(group.generation){out.Put("\"");out.NumberValue(*group.generation);out.Put("\"");}else out.Put("null");
    out.Put(",\"suiteId\":");if(group.suite){out.Put("\"");out.NumberValue(group.suite);out.Put("\"");}else out.Put("null");
    out.Put(",\"mode\":\"");out.Put(WorkGroupModeName(group.mode));out.Put("\",\"status\":\"");out.Put(group.status);out.Put("\",\"receipt\":\"");out.Put(group.receipt);out.Put("\",\"testIds\":[");
    bool first_id=true;for(const auto id:group.tests){if(!first_id)out.Put(",");first_id=false;out.Put("\"");out.NumberValue(id);out.Put("\"");}
    out.Put("],\"workerIndices\":[");first_id=true;for(const auto index:group.workers){if(!first_id)out.Put(",");first_id=false;out.NumberValue(index);}out.Put("]}");
  }out.Put("]");
}
template<class Writer>void PutFileLockReports(Writer& out,const std::vector<ReportFileResourceLocks>& locks){
  out.Put("[");bool first=true;
  for(const auto& lock:locks){if(!first)out.Put(",");first=false;
    out.Put("{\"fileId\":\"");out.NumberValue(lock.file_id);out.Put("\",\"fileWorkGroupId\":\"");out.NumberValue(lock.group_id);out.Put("\",\"generation\":\"");out.NumberValue(lock.generation);out.Put("\",\"receipt\":\"");out.Put(lock.receipt);out.Put("\",\"names\":[");
    bool first_name=true;for(const auto& name:lock.names){if(!first_name)out.Put(",");first_name=false;out.Put(ResourceLockJSON(name));}out.Put("]}");
  }out.Put("]");
}
}
