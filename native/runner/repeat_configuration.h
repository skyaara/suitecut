// SPDX-License-Identifier: MIT
#pragma once
#include "repeat_execution.h"
#include <map>
namespace suitecut::runner {
class NativeRepeatDeclarations {
 public:
  void Declare(uint64_t owner,size_t actual_projects,uint64_t value){
    if(frozen_||actual_projects>128||owner>actual_projects)throw std::invalid_argument("Native repeat config lacks actual declaration ownership");
    if(!owner){if(root_)throw std::invalid_argument("Duplicate native root repeat declaration");root_=value;}
    else if(!projects_.emplace(owner-1,value).second)throw std::invalid_argument("Duplicate native project repeat declaration");
  }
  void Freeze(size_t projects){
    if(frozen_||projects>128||(!projects_.empty()&&projects_.rbegin()->first>=projects))throw std::invalid_argument("Native repeat config cannot freeze unknown project declarations");
    project_count_=projects;frozen_=true;
  }
  NativeRepeatCounts Resolve(std::optional<size_t> project,NativeRepeatRole role,
      std::optional<uint64_t> primary_override={})const{
    if(!frozen_||(project&&*project>=project_count_)||(!project&&project_count_))throw std::invalid_argument("Native repeat config lacks frozen actual project membership");
    NativeRepeatConfiguration config;config.root=root_;config.primary_override=primary_override;
    if(project){const auto declared=projects_.find(*project);if(declared!=projects_.end())config.project=declared->second;}
    return ResolveNativeRepeatCounts(config,role,0);
  }
  const std::optional<uint64_t>& RootDeclaration()const{return root_;}
  const std::map<size_t,uint64_t>& ProjectDeclarations()const{return projects_;}
  bool Explicit()const{return root_.has_value()||!projects_.empty();}
 private:
  std::optional<uint64_t> root_;std::map<size_t,uint64_t> projects_;size_t project_count_=0;bool frozen_=false;
};
} // namespace suitecut::runner
