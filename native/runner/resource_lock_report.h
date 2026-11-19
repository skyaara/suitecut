// SPDX-License-Identifier: MIT
#pragma once
#include "resource_locks.h"
#include "work_group_report.h"
namespace suitecut::runner {
struct ReportResourceLockGroup {
  uint64_t project_id=0,generation=0;
  std::vector<std::string> names; // canonical UTF16LE code-unit bytes
  std::string receipt;
  std::optional<uint64_t> work_group_id=std::nullopt;
};
// Cross-check native lock retirement metadata against actual selected project
// identities and confirmed OS worker exits. No elapsed-time/absence inference.
template<class Report>void ValidateResourceLockReports(const Report& report){
  std::set<uint64_t> projects,generations,groups;
  for(const auto& group:report.resource_locks){
    if(!group.project_id||!group.generation||!generations.insert(group.generation).second||
       group.names.empty()||
       (group.receipt!="physicalRetired"&&group.receipt!="neverStarted"))throw std::invalid_argument("Invalid native resource-lock report identity/receipt");
    if(!std::is_sorted(group.names.begin(),group.names.end())||std::adjacent_find(group.names.begin(),group.names.end())!=group.names.end())throw std::invalid_argument("Native lock report names are not a canonical atomic set");
    for(const auto& name:group.names)if(name.size()%2)throw std::invalid_argument("Native lock report name is not UTF16");
    const auto project=std::find_if(report.projects.begin(),report.projects.end(),[&](const auto& p){return p.id==group.project_id;});
    if(project==report.projects.end()||project->selection_role=="notSelected")throw std::invalid_argument("Native lock report lacks actual selected project identity");
    if(group.work_group_id){
      const auto* owner=FindWorkGroup(report,*group.work_group_id);
      if(!groups.insert(*group.work_group_id).second||!owner||owner->project_id!=group.project_id||owner->generation!=group.generation||owner->receipt!=group.receipt)
        throw std::invalid_argument("Native lock report lacks exact work-group admission/retirement");
      continue;
    }
    if(!report.work_groups.empty()||!projects.insert(group.project_id).second)throw std::invalid_argument("Native project lock report mixed with work-group identity");
    if(group.receipt=="neverStarted"){
      if(project->status!="notRun"||!project->workers.empty())throw std::invalid_argument("Native never-started lock receipt invented actual process retirement");
    }else{
      if(project->workers.empty()||(project->status!="passed"&&project->status!="failed"&&project->status!="interrupted"))throw std::invalid_argument("Native lock retirement lacks physically completed project");
      for(const auto index:project->workers){
        const auto worker=std::find_if(report.workers.begin(),report.workers.end(),[&](const auto& w){return w.worker_index==index;});
        if(worker==report.workers.end()||worker->exit_code<0)throw std::invalid_argument("Native resource lock released without actual OS worker exit receipt");
      }
    }
  }
}
template<class File>void PutResourceLockReports(File& file,const std::vector<ReportResourceLockGroup>& groups){
  file.Put("[");bool first=true;
  for(const auto& group:groups){
    if(!first)file.Put(",");
    first=false;file.Put("{\"projectId\":\"");file.NumberValue(group.project_id);
    if(group.work_group_id){file.Put("\",\"workGroupId\":\"");file.NumberValue(*group.work_group_id);file.Put("\",\"workGroupGeneration\":\"");}
    else file.Put("\",\"projectGeneration\":\"");
    file.NumberValue(group.generation);file.Put("\",\"receipt\":\"");file.Put(group.receipt);file.Put("\",\"names\":[");bool first_name=true;
    for(const auto& name:group.names){if(!first_name)file.Put(",");first_name=false;file.Put(ResourceLockJSON(name));}
    file.Put("]}");
  }
  file.Put("]");
}
}
