// SPDX-License-Identifier: MIT
#pragma once
#include "test_group_tree.h"
namespace suitecut::runner {
struct WorkGroupTest {uint64_t id;size_t project;};
struct TestWorkGroup {
  size_t project=0;
  WorkGroupMode mode=WorkGroupMode::Default;
  uint64_t suite=0;
  std::vector<uint64_t> tests;
};
// Selected declarations only. No callback, worker, deadline or JS grouping is
// admitted here. Default subgroups retain original order; the outermost serial
// ancestor always owns the entire selected serial unit.
class TestWorkGroups {
 public:
  static constexpr size_t kGroups=8192,kTests=4096,kProjects=128;
  TestWorkGroups(std::vector<WorkGroupTest> tests,std::vector<SuiteDeclaration> suites,
      std::vector<SuiteTestDeclaration> members,std::map<uint64_t,WorkGroupMode> modes,
      const std::vector<bool>& selected_projects,const std::vector<std::vector<uint64_t>>& selected_tests,
      const std::vector<bool>& fully_parallel):by_project_(selected_projects.size()){
    const auto count=selected_projects.size();
    if(!count||count>kProjects||selected_tests.size()!=count||fully_parallel.size()!=count||tests.size()>kTests)
      throw std::invalid_argument("Native test work-group project/test capacity or shape rejected");
    std::map<uint64_t,size_t> owners;std::vector<uint64_t> order;
    for(const auto& test:tests){
      if(!test.id||test.project>=count||!owners.emplace(test.id,test.project).second)
        throw std::invalid_argument("Native test work-group test/project identity rejected");
      order.push_back(test.id);
    }
    NativeTestGroupTree tree(std::move(order),suites,members,std::move(modes));
    std::set<uint64_t> all_selected;
    for(size_t project=0;project<count;++project){
      std::set<uint64_t> selected;
      for(const auto id:selected_tests[project])
        if(!selected_projects[project]||!owners.contains(id)||owners.at(id)!=project||!selected.insert(id).second||!all_selected.insert(id).second)
          throw std::invalid_argument("Native work-group selection has unknown, duplicate or foreign test identity");
      if(!selected_projects[project])continue;
      for(auto& group:tree.Resolve(selected_tests[project],fully_parallel[project])){
        if(groups_.size()>=kGroups)throw std::invalid_argument("Native work-group capacity8192 exceeded");
        by_project_[project].push_back(groups_.size());
        groups_.push_back({project,group.mode,group.suite,std::move(group.tests)});
      }
    }
  }
  const std::vector<TestWorkGroup>& Groups()const{return groups_;}
  const std::vector<std::vector<size_t>>& ByProject()const{return by_project_;}
 private:
  std::vector<TestWorkGroup> groups_;
  std::vector<std::vector<size_t>> by_project_;
};
}
