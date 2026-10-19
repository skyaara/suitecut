// SPDX-License-Identifier: MIT
#pragma once
#include "test_listing.h"

namespace suitecut::runner {
struct CollectedSuiteFile {
  SuiteFileToken file;
  std::string path;
  TestListingCatalog catalog;
  // Computed by the existing native grep policy on this actual file catalog.
  // nullopt and an observed empty eligible set have different meanings.
  std::optional<std::set<uint64_t>> config_eligible;
  std::optional<std::set<uint64_t>> cli_eligible;
};
struct GlobalSuiteTest {
  uint64_t selection_id=0;
  size_t file=0;
  uint64_t local_id=0;
  size_t project=0;
};
struct MultiFileSelection {
  ProjectPlanSelection projects;
  std::vector<std::vector<uint64_t>> project_tests;
  std::vector<std::string> roles;
};
// IDs in this catalog are native selection identities, never replacements for
// the actual host's local callback/test/fixture IDs. Reports retain (source,
// original local ID). All files are collected before this immutable view exists.
class MultiFileCatalog {
 public:
  explicit MultiFileCatalog(std::vector<CollectedSuiteFile> files):files_(std::move(files)){
    if(files_.empty()||files_.size()>128)throw std::invalid_argument("Native multi-file collection requires1..128 file receipts");
    std::set<uint64_t> file_ids;std::set<std::string> paths;size_t bytes=0;
    for(const auto& file:files_){
      if(!file.file.id||!file.file.inode||file.path.empty()||file.path.front()!='/'||!file_ids.insert(file.file.id).second||!paths.insert(file.path).second)
        throw std::invalid_argument("Native multi-file collection identity rejected");
      if(file.catalog.protocol.size()>16*1024*1024-bytes)throw std::invalid_argument("Native multi-file collection transcript capacity16MiB exceeded");
      bytes+=file.catalog.protocol.size();
      if(file.catalog.projects.empty()){
        if(!file.catalog.project_tests.empty())throw std::invalid_argument("Native anonymous file has forged project membership");
      }else{
        if(file.catalog.project_metadata.size()!=file.catalog.projects.size()||file.catalog.project_tests.size()!=file.catalog.tests.size())
          throw std::invalid_argument("Native multi-file project metadata incomplete");
        std::set<std::string> local_names;
        for(const auto& project:file.catalog.projects){
          if(!local_names.insert(project.name).second)throw std::invalid_argument("Native file repeats a project declaration");
          const auto found=project_names_.find(project.name);
          if(found==project_names_.end()){
            if(projects_.size()>=127)throw std::invalid_argument("Native multi-file authored project capacity127 exceeded");
            project_names_.emplace(project.name,projects_.size());projects_.push_back(project);
          }else if(!Same(projects_[found->second],project))throw std::invalid_argument("Native files disagree on project dependencies/teardown/default: "+project.name);
        }
      }
    }
    // One explicitly native scheduling owner covers anonymous files. It is
    // excluded from all public project-name matching and never sent as authored
    // PROJECT_CONTEXT or emitted as an authored project's report identity.
    std::string internal="suitecut-native-anonymous";
    while(project_names_.contains(internal))internal+='-';
    anonymous_project_=projects_.size();projects_.push_back({std::move(internal),{},std::nullopt,true});
    graph_.emplace(projects_);all_.resize(projects_.size());
    for(size_t f=0;f<files_.size();++f){const auto& file=files_[f];std::set<uint64_t> local;
      std::set<uint64_t> focused(file.catalog.focused.begin(),file.catalog.focused.end());
      if(focused.size()!=file.catalog.focused.size())throw std::invalid_argument("Native file repeats focused test identity");
      if(file.catalog.has_suite_focus){
        if(file.catalog.projects.empty())suite_focus_projects_.insert(anonymous_project_);
        else for(const auto project:ProjectSuiteFocus(file.catalog.projects.size(),file.catalog.suites,file.catalog.suite_modifiers))
          suite_focus_projects_.insert(project_names_.at(file.catalog.projects.at(project).name));
      }
      for(const auto& test:file.catalog.tests){
        if(!test.id||!local.insert(test.id).second||tests_.size()>=4096)throw std::invalid_argument("Native multi-file test identity/capacity4096 rejected");
        size_t project=anonymous_project_;
        if(!file.catalog.projects.empty()){
          const auto membership=file.catalog.project_tests.find(test.id);
          if(membership==file.catalog.project_tests.end()||membership->second>=file.catalog.projects.size())throw std::invalid_argument("Native file test has no genuine project owner");
          project=project_names_.at(file.catalog.projects[membership->second].name);
        }
        const uint64_t id=tests_.size()+1;tests_.push_back({id,f,test.id,project});
        if(!file.config_eligible||file.config_eligible->contains(test.id))all_[project].push_back(id);
        if(focused.contains(test.id))focused_.insert(id);
        if((!file.config_eligible||file.config_eligible->contains(test.id))&&(!file.cli_eligible||file.cli_eligible->contains(test.id)))eligible_.insert(id);
      }
      for(const auto id:focused)if(!local.contains(id))throw std::invalid_argument("Native file focus references an absent test");
      for(const auto* eligibility:{&file.config_eligible,&file.cli_eligible})if(*eligibility)for(const auto id:**eligibility)if(!local.contains(id))throw std::invalid_argument("Native file grep eligibility references an absent test");
    }
  }
  MultiFileSelection Select(TestSelection selection={},const std::vector<std::string>& patterns={},bool no_dependencies=false)const{
    std::vector<uint64_t> ids;for(const auto& test:tests_)ids.push_back(test.selection_id);
    const std::vector<uint64_t> focus(focused_.begin(),focused_.end());
    // Validate caller's exact IDs and global focus before any worker admission.
    (void)SelectTestIndices(ids,selection,focus,!suite_focus_projects_.empty());
    if(patterns.size()>128)throw std::invalid_argument("Native multi-file project selector capacity128 exceeded");
    std::vector<bool> roots(projects_.size());
    if(patterns.empty()){
      for(size_t p=0;p<projects_.size();++p)roots[p]=projects_[p].default_enabled&&!graph_->IsTeardown(p);
    }else for(const auto& pattern:patterns){ProjectPattern matcher(pattern);bool matched=false;
      for(size_t p=0;p<anonymous_project_;++p)if(matcher.Matches(projects_[p].name)){roots[p]=true;matched=true;}
      if(!matched&&!matcher.Wildcard())throw std::invalid_argument("Native multi-file project selector not found: "+pattern);
    }
    if(!patterns.empty()&&std::none_of(roots.begin(),roots.end(),[](bool value){return value;}))throw std::invalid_argument("No native multi-file projects matched");
    std::set<uint64_t> requested(selection.ids.begin(),selection.ids.end());
    bool has_focus=false;
    for(const auto& test:tests_)if(roots[test.project]&&focused_.contains(test.selection_id))has_focus=true;
    for(const auto project:suite_focus_projects_)if(roots[project])has_focus=true;
    std::vector<std::vector<uint64_t>> primary(projects_.size());std::vector<bool> candidates(projects_.size()),support(projects_.size());
    for(const auto& test:tests_)if(roots[test.project]&&eligible_.contains(test.selection_id)&&(!selection.eligible||selection.eligible->contains(test.selection_id))&&(requested.empty()||requested.contains(test.selection_id))&&(!has_focus||focused_.contains(test.selection_id)))primary[test.project].push_back(test.selection_id);
    const bool unfiltered=selection.ids.empty()&&!selection.eligible&&!has_focus&&selection.shard_total==1&&eligible_.size()==tests_.size();
    for(size_t p=0;p<projects_.size();++p)candidates[p]=roots[p]&&(!primary[p].empty()||(unfiltered&&all_[p].empty()&&p!=anonymous_project_));
    if(!no_dependencies){std::function<void(size_t)> add=[&](size_t p){if(support[p])return;support[p]=true;for(const auto dep:graph_->Dependencies(p))add(dep);if(const auto teardown=graph_->Teardown(p))add(*teardown);};
      for(size_t p=0;p<projects_.size();++p)if(candidates[p]){for(const auto dep:graph_->Dependencies(p))add(dep);if(const auto teardown=graph_->Teardown(p))add(*teardown);}
    }
    std::vector<uint64_t> shard_ids;
    for(const auto& test:tests_)if(candidates[test.project]&&!support[test.project]&&std::find(primary[test.project].begin(),primary[test.project].end(),test.selection_id)!=primary[test.project].end())shard_ids.push_back(test.selection_id);
    TestSelection shard;shard.shard_current=selection.shard_current;shard.shard_total=selection.shard_total;
    MultiFileSelection result{{std::vector<bool>(projects_.size()),no_dependencies},std::vector<std::vector<uint64_t>>(projects_.size()),std::vector<std::string>(projects_.size(),"notSelected")};
    std::vector<bool> selected_roots(projects_.size());
    for(const auto position:SelectTestIndices(shard_ids,shard)){const auto id=shard_ids[position];const auto p=tests_[id-1].project;selected_roots[p]=true;result.project_tests[p].push_back(id);}
    if(unfiltered)for(size_t p=0;p<projects_.size();++p)if(candidates[p]&&!support[p]&&all_[p].empty())selected_roots[p]=true;
    result.projects.selected=graph_->Closure(selected_roots,no_dependencies);
    for(size_t p=0;p<projects_.size();++p)if(result.projects.selected[p]){
      if(selected_roots[p])result.roles[p]="primary";
      else{result.roles[p]=graph_->IsTeardown(p)?"teardown":"dependency";result.project_tests[p]=all_[p];}
    }
    return result;
  }
  const std::vector<CollectedSuiteFile>& Files()const{return files_;}
  const std::vector<GlobalSuiteTest>& Tests()const{return tests_;}
  const ProjectGraph& Graph()const{return *graph_;}
  size_t AnonymousProject()const{return anonymous_project_;}
  std::vector<uint64_t> LocalTests(const MultiFileSelection& selection,size_t project,size_t file)const{
    std::vector<uint64_t> result;
    for(const auto id:selection.project_tests.at(project)){const auto& test=tests_.at(id-1);if(test.file==file)result.push_back(test.local_id);}
    return result;
  }
 private:
  static bool Same(const ProjectDeclaration& a,const ProjectDeclaration& b){return a.name==b.name&&a.dependencies==b.dependencies&&a.teardown==b.teardown&&a.default_enabled==b.default_enabled;}
  std::vector<CollectedSuiteFile> files_;std::vector<ProjectDeclaration> projects_;std::map<std::string,size_t> project_names_;
  size_t anonymous_project_=0;std::optional<ProjectGraph> graph_;std::vector<GlobalSuiteTest> tests_;
  std::vector<std::vector<uint64_t>> all_;std::set<uint64_t> focused_,eligible_;std::set<size_t> suite_focus_projects_;
};
}
