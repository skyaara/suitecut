// SPDX-License-Identifier: MIT
#pragma once
#include "test_work_groups.h"
namespace suitecut::runner {
struct ReportWorkGroup {
  uint64_t id=0,project_id=0,suite=0;
  std::optional<uint64_t> generation;
  WorkGroupMode mode=WorkGroupMode::Default;
  std::string status,receipt;
  std::vector<uint64_t> tests;
  std::vector<unsigned> workers;
};
inline const char* WorkGroupModeName(WorkGroupMode mode){
  switch(mode){case WorkGroupMode::Default:return "default";case WorkGroupMode::Parallel:return "parallel";case WorkGroupMode::Serial:return "serial";}
  throw std::invalid_argument("Invalid native work-group report mode");
}
template<class Report>const ReportWorkGroup* FindWorkGroup(const Report& report,uint64_t id){
  if(!id||id>report.work_groups.size()||report.work_groups[id-1].id!=id)return nullptr;
  return &report.work_groups[id-1];
}
template<class Report>void ValidateWorkGroupReports(const Report& report){
  if(report.work_groups.empty())return;
  if(report.work_groups.size()>TestWorkGroups::kGroups||report.projects.empty())throw std::invalid_argument("Native work-group report requires bounded genuine projects");
  std::set<uint64_t> tests,generations;std::set<unsigned> workers;std::map<uint64_t,std::vector<const ReportWorkGroup*>> projects;
  for(size_t index=0;index<report.work_groups.size();++index){
    const auto& group=report.work_groups[index];(void)WorkGroupModeName(group.mode);
    if(group.id!=index+1||!group.project_id||group.tests.size()>TestWorkGroups::kTests-tests.size()||group.workers.size()>8192-workers.size()||
       (group.mode==WorkGroupMode::Serial&&!group.suite)||(group.mode==WorkGroupMode::Parallel&&group.tests.size()!=1))throw std::invalid_argument("Invalid native work-group report identity or capacity");
    if(group.generation&&(!*group.generation||*group.generation>TestWorkGroups::kGroups||!generations.insert(*group.generation).second))throw std::invalid_argument("Invalid native work-group admission generation");
    const auto project=std::find_if(report.projects.begin(),report.projects.end(),[&](const auto& value){return value.id==group.project_id;});
    if(project==report.projects.end()||project->selection_role=="notSelected"||(group.tests.empty()&&!project->selected_test_ids.empty()))throw std::invalid_argument("Native group report has no original selected project");
    projects[group.project_id].push_back(&group);
    for(const auto id:group.tests)if(!id||!tests.insert(id).second||std::find(project->selected_test_ids.begin(),project->selected_test_ids.end(),id)==project->selected_test_ids.end())throw std::invalid_argument("Native group selected test identity duplicated or foreign");
    if(group.receipt=="physicalRetired"){
      if(!group.generation||group.workers.empty()||(group.status!="passed"&&group.status!="failed"&&group.status!="interrupted"))throw std::invalid_argument("Native group lacks actual admitted physical retirement");
    }else if(group.receipt=="neverStarted"){
      if(!group.generation||!group.workers.empty()||group.status!="notRun")throw std::invalid_argument("Native group cancellation fabricated an actual worker");
    }else if(group.receipt=="notAdmitted"){
      if(group.generation||!group.workers.empty()||(group.status!="notRun"&&group.status!="blocked"))throw std::invalid_argument("Native unadmitted group fabricated admission or worker identity");
    }else throw std::invalid_argument("Native group has no terminal receipt");
    for(const auto index:group.workers){
      const auto worker=std::find_if(report.workers.begin(),report.workers.end(),[&](const auto& value){return value.worker_index==index;});
      if(!index||!workers.insert(index).second||worker==report.workers.end()||worker->exit_code<0||std::find(project->workers.begin(),project->workers.end(),index)==project->workers.end())throw std::invalid_argument("Native work group lacks unique actual project worker exit");
    }
  }
  for(const auto& project:report.projects){
    if(project.selection_role=="notSelected")continue;
    if(!projects.contains(project.id))throw std::invalid_argument("Selected native project has no work-group plan");
    for(const auto id:project.selected_test_ids)if(!tests.contains(id))throw std::invalid_argument("Native group plan omitted selected project test");
    for(const auto worker:project.workers)if(!workers.contains(worker))throw std::invalid_argument("Native project worker has no actual work-group ownership");
    bool failed=false,interrupted=false,not_run=false,blocked=false,all_blocked=true,all_not_run=true;
    for(const auto* group:projects.at(project.id)){failed|=group->status=="failed";interrupted|=group->status=="interrupted";not_run|=group->status=="notRun";blocked|=group->status=="blocked";all_blocked&=group->status=="blocked";all_not_run&=group->status=="notRun";}
    if((project.status=="passed"&&(failed||interrupted||not_run||blocked))||(project.status=="failed"&&!failed)||
       (project.status=="blocked"&&!all_blocked)||(project.status=="notRun"&&!all_not_run)||
       (project.status=="interrupted"&&(failed||blocked||(!interrupted&&!not_run))))throw std::invalid_argument("Native project result disagrees with terminal work groups");
  }
  for(const auto& attempt:report.attempts){
    const auto group=std::find_if(report.work_groups.begin(),report.work_groups.end(),[&](const auto& value){return std::find(value.tests.begin(),value.tests.end(),attempt.test_id)!=value.tests.end();});
    if(group==report.work_groups.end()||std::find(group->workers.begin(),group->workers.end(),attempt.worker_index)==group->workers.end())throw std::invalid_argument("Actual native attempt has no owning work-group worker");
  }
}
template<class Writer>void PutWorkGroupReports(Writer& file,const std::vector<ReportWorkGroup>& groups){
  file.Put("[");bool first=true;
  for(const auto& group:groups){
    if(!first)file.Put(",");
    first=false;
    file.Put("{\"id\":\"");file.NumberValue(group.id);file.Put("\",\"projectId\":\"");file.NumberValue(group.project_id);file.Put("\",\"generation\":");
    if(group.generation){file.Put("\"");file.NumberValue(*group.generation);file.Put("\"");}else file.Put("null");
    file.Put(",\"suiteId\":");if(group.suite){file.Put("\"");file.NumberValue(group.suite);file.Put("\"");}else file.Put("null");
    file.Put(",\"mode\":\"");file.Put(WorkGroupModeName(group.mode));file.Put("\",\"status\":\"");file.Put(group.status);file.Put("\",\"receipt\":\"");file.Put(group.receipt);file.Put("\",\"testIds\":[");
    bool first_id=true;for(const auto id:group.tests){if(!first_id)file.Put(",");first_id=false;file.Put("\"");file.NumberValue(id);file.Put("\"");}
    file.Put("],\"workerIndices\":[");first_id=true;for(const auto index:group.workers){if(!first_id)file.Put(",");first_id=false;file.NumberValue(index);}file.Put("]}");
  }
  file.Put("]");
}
}
