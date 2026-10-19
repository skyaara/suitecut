// SPDX-License-Identifier: MIT
#pragma once
#include "project_metadata.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace suitecut::runner {
struct ReportProject {
  uint64_t id=0;
  std::string name_json="\"\"",metadata_json="{}",status="notSelected";
  std::vector<uint64_t> test_ids{},dependencies{},selected_test_ids{};
  std::string selection_role="notSelected";bool dependencies_ignored=false;
  std::optional<uint64_t> teardown=std::nullopt,failed_dependency=std::nullopt;
  std::vector<unsigned> workers{};bool default_enabled=true;
};
inline const ReportProject* ProjectForTest(const std::vector<ReportProject>& projects,uint64_t test){for(const auto& project:projects)for(const auto id:project.test_ids)if(id==test)return &project;return nullptr;}
inline void ValidateProjectReports(const std::vector<ReportProject>& projects){
  if(projects.size()>128)throw std::invalid_argument("Native project report capacity128 exceeded");
  std::set<uint64_t> ids,tests;std::set<unsigned> workers;size_t total_tests=0,total_edges=0;
  for(const auto& project:projects){
    if(!project.id||!ids.insert(project.id).second||(project.name_json=="\"\""||!JsonString(project.name_json,256))||!ProjectMetadataJSON(project.metadata_json)||project.test_ids.size()>4096-total_tests||project.dependencies.size()>4096-total_edges||project.workers.size()>8192)
      throw std::invalid_argument("Invalid native project report declaration or capacity");
    total_tests+=project.test_ids.size();total_edges+=project.dependencies.size();
    if(project.status!="notSelected"&&project.status!="passed"&&project.status!="failed"&&project.status!="blocked"&&project.status!="interrupted"&&project.status!="notRun")throw std::invalid_argument("Native project report requires terminal native state");
    for(const auto test:project.test_ids)if(!test||!tests.insert(test).second)throw std::invalid_argument("Native project test identity duplicated");
    if(project.selection_role!="notSelected"&&project.selection_role!="primary"&&project.selection_role!="dependency"&&project.selection_role!="teardown")throw std::invalid_argument("Invalid native project selection role");
    std::set<uint64_t> selected;for(const auto test:project.selected_test_ids)if(!selected.insert(test).second||std::find(project.test_ids.begin(),project.test_ids.end(),test)==project.test_ids.end())throw std::invalid_argument("Invalid native selected project test identity");
    if(project.dependencies_ignored&&(project.selection_role=="dependency"||project.selection_role=="teardown"||project.status=="blocked"))throw std::invalid_argument("Native ignored dependencies acquired support role or blocked state");
    for(const auto worker:project.workers)if(!worker||!workers.insert(worker).second)throw std::invalid_argument("Native project worker identity duplicated");
    if((project.status=="notSelected"||project.status=="blocked"||project.status=="notRun")&&!project.workers.empty())throw std::invalid_argument("Non-admitted native project acquired an actual worker");
    if((project.status=="passed"||project.status=="failed"||project.status=="interrupted")&&project.workers.empty())throw std::invalid_argument("Admitted native project has no actual worker");
  }
  for(const auto& project:projects){std::set<uint64_t> deps;
    for(const auto id:project.dependencies)if(!ids.contains(id)||id==project.id||!deps.insert(id).second)throw std::invalid_argument("Invalid native project report dependency");
    if(project.teardown&&(!ids.contains(*project.teardown)||*project.teardown==project.id))throw std::invalid_argument("Invalid native project report teardown");
    if((project.status=="blocked")!=project.failed_dependency.has_value()||(project.failed_dependency&&!deps.contains(*project.failed_dependency)))throw std::invalid_argument("Invalid native project blocked cause");
  }
}
template<class Writer>void PutProjectReports(Writer& file,const std::vector<ReportProject>& projects){
  file.Put("[");bool first=true;
  for(const auto& project:projects){if(!first)file.Put(",");first=false;file.Put("{\"id\":\"");file.NumberValue(project.id);file.Put("\",\"name\":");file.Put(project.name_json);file.Put(",\"metadata\":");file.Put(project.metadata_json);file.Put(",\"status\":\"");file.Put(project.status);file.Put("\",\"default\":");file.Put(project.default_enabled?"true":"false");
    const auto ids=[&](const auto& values){bool first_id=true;file.Put("[");for(const auto id:values){if(!first_id)file.Put(",");first_id=false;file.Put("\"");file.NumberValue(id);file.Put("\"");}file.Put("]");};
    file.Put(",\"selectionRole\":\"");file.Put(project.selection_role);file.Put("\",\"dependenciesIgnored\":");file.Put(project.dependencies_ignored?"true":"false");file.Put(",\"selectedTestIds\":");ids(project.selected_test_ids);
    file.Put(",\"testIds\":");ids(project.test_ids);file.Put(",\"dependencies\":");ids(project.dependencies);file.Put(",\"workerIndices\":[");bool first_worker=true;for(const auto worker:project.workers){if(!first_worker)file.Put(",");first_worker=false;file.NumberValue(worker);}file.Put("]");
    if(project.teardown){file.Put(",\"teardown\":\"");file.NumberValue(*project.teardown);file.Put("\"");}
    if(project.failed_dependency){file.Put(",\"failedDependency\":\"");file.NumberValue(*project.failed_dependency);file.Put("\"");}file.Put("}");
  }file.Put("]");
}
}
