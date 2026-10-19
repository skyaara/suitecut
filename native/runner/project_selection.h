// SPDX-License-Identifier: MIT
#pragma once
#include "project_plan.h"
#include "suite_tree.h"
#include "test_selection.h"
namespace suitecut::runner {
struct ProjectTestDeclaration {uint64_t id;size_t project;};
struct ProjectTestSelection {
  ProjectPlanSelection plan;
  std::vector<std::vector<uint64_t>> tests;
  std::vector<std::string> roles;
};
// The host supplies declarations, never a selected list. Preserve original IDs
// and ordering; only primary tests consume a shard. Required support runs whole.
inline ProjectTestSelection SelectProjectTests(const ProjectGraph& graph,
    const std::vector<ProjectTestDeclaration>& declarations,const TestSelection& selection={},
    const std::vector<std::string>& patterns={},bool no_dependencies=false,
    const std::vector<uint64_t>& focused={},const std::vector<size_t>& suite_focus_projects={}) {
  if(declarations.size()>4096)throw std::invalid_argument("Native project test declaration capacity4096 exceeded");
  const auto count=graph.Declarations().size();std::vector<uint64_t> ids;
  std::vector<std::vector<uint64_t>> all(count);std::map<uint64_t,size_t> owners;
  for(const auto& test:declarations){if(test.project>=count)throw std::invalid_argument("Native project test owner rejected");ids.push_back(test.id);if(!selection.catalog_eligible||selection.catalog_eligible->contains(test.id))all[test.project].push_back(test.id);owners.emplace(test.id,test.project);}
  // Reuse the actual ID/shard/focus capacity and membership validator. Its
  // global focus result is deliberately not used to filter required support.
  (void)SelectTestIndices(ids,selection,focused,!suite_focus_projects.empty());
  const auto candidates=graph.Roots(patterns);
  std::set<uint64_t> focus(focused.begin(),focused.end()),requested(selection.ids.begin(),selection.ids.end());
  bool has_primary_focus=false;
  for(const auto id:focused)if(candidates[owners.at(id)])has_primary_focus=true;
  std::set<size_t> suite_focus;
  for(const auto project:suite_focus_projects){if(project>=count||!suite_focus.insert(project).second)throw std::invalid_argument("Native focused project identity rejected");if(candidates[project])has_primary_focus=true;}
  std::vector<std::vector<uint64_t>> primary(count);std::vector<bool> eligible(count),support(count);
  for(const auto& test:declarations)if(candidates[test.project]&&(!selection.catalog_eligible||selection.catalog_eligible->contains(test.id))&&(!selection.eligible||selection.eligible->contains(test.id))&&(requested.empty()||requested.contains(test.id))&&(!has_primary_focus||focus.contains(test.id)))primary[test.project].push_back(test.id);
  const bool unfiltered=!selection.catalog_eligible&&!selection.eligible&&selection.ids.empty()&&!has_primary_focus&&selection.shard_total==1;
  for(size_t project=0;project<count;++project)eligible[project]=candidates[project]&&(!primary[project].empty()||(unfiltered&&all[project].empty()));
  if(!no_dependencies){
    std::function<void(size_t)> include=[&](size_t project){if(support[project])return;support[project]=true;for(const auto dependency:graph.Dependencies(project))include(dependency);if(const auto teardown=graph.Teardown(project))include(*teardown);};
    for(size_t project=0;project<count;++project)if(eligible[project]){for(const auto dependency:graph.Dependencies(project))include(dependency);if(const auto teardown=graph.Teardown(project))include(*teardown);}
  }
  std::vector<uint64_t> shard_ids;
  for(const auto& test:declarations)if(eligible[test.project]&&!support[test.project]&&std::find(primary[test.project].begin(),primary[test.project].end(),test.id)!=primary[test.project].end())shard_ids.push_back(test.id);
  TestSelection shard;shard.shard_current=selection.shard_current;shard.shard_total=selection.shard_total;
  ProjectTestSelection result{{std::vector<bool>(count),no_dependencies},std::vector<std::vector<uint64_t>>(count),std::vector<std::string>(count,"notSelected")};
  std::vector<bool> roots(count);
  for(const auto position:SelectTestIndices(shard_ids,shard)){const auto id=shard_ids[position],project=owners.at(id);roots[project]=true;result.tests[project].push_back(id);}
  // Empty setup projects still form a valid dependency; an explicitly selected
  // empty root preserves the original unfiltered empty-project hook contract.
  if(unfiltered)for(size_t project=0;project<count;++project)if(eligible[project]&&!support[project]&&all[project].empty())roots[project]=true;
  result.plan.selected=graph.Closure(roots,no_dependencies);
  for(size_t project=0;project<count;++project)if(result.plan.selected[project]){
    if(roots[project])result.roles[project]="primary";
    else{result.roles[project]=graph.IsTeardown(project)?"teardown":"dependency";result.tests[project]=all[project];}
  }
  return result;
}
// Project suites are direct children of the single native synthetic root. Map
// even an empty focused suite to its project so excluded focus cannot leak in.
inline std::vector<size_t> ProjectSuiteFocus(size_t count,const std::vector<SuiteDeclaration>& suites,
    const std::map<uint64_t,SuiteModifiers>& modifiers) {
  std::map<uint64_t,const SuiteDeclaration*> nodes;uint64_t root=0;
  for(const auto& suite:suites){nodes.emplace(suite.id,&suite);if(!suite.parent)root=suite.id;}
  if(!root||count>128)throw std::invalid_argument("Native project suite root missing");
  std::set<size_t> projects,focused;
  for(const auto& suite:suites)if(suite.parent==root){if(suite.position>=count||!projects.insert(suite.position).second)throw std::invalid_argument("Native project suite identity rejected");}
  if(projects.size()!=count)throw std::invalid_argument("Native project suite membership incomplete");
  for(const auto& [id,modifier]:modifiers)if(modifier.only){
    if(id==root){for(size_t project=0;project<count;++project)focused.insert(project);continue;}
    auto current=nodes.find(id);size_t depth=0;
    while(current!=nodes.end()&&current->second->parent!=root&&++depth<=64)current=nodes.find(current->second->parent);
    if(current==nodes.end()||depth>64||current->second->position>=count)throw std::invalid_argument("Native focused suite project rejected");
    focused.insert(current->second->position);
  }
  return {focused.begin(),focused.end()};
}
}
