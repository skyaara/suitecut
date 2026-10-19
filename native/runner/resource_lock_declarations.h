// SPDX-License-Identifier: MIT
#pragma once
#include "resource_locks.h"
#include "suite_tree.h"
namespace suitecut::runner {
// Declarative inheritance only. JavaScript reports literal names and owner IDs;
// C++ resolves selected test/suite/file unions before any callback admission.
class ResourceLockDeclarations {
 public:
  using Names=std::vector<std::string>;using ByOwner=std::map<uint64_t,Names>;
  ResourceLockDeclarations(std::vector<uint64_t> tests,std::vector<SuiteDeclaration> suites,
      std::vector<SuiteTestDeclaration> memberships,ByOwner test_names,ByOwner suite_names,Names file_names={})
      :tests_(tests.begin(),tests.end()),test_names_(std::move(test_names)),suite_names_(std::move(suite_names)),file_names_(std::move(file_names)){
    if(tests.size()>4096||tests_.size()!=tests.size()||tests_.contains(0))throw std::invalid_argument("Invalid native resource-lock test identities");
    std::set<uint64_t> suite_ids;for(const auto& suite:suites)if(!suite.id||!suite_ids.insert(suite.id).second)throw std::invalid_argument("Invalid native resource-lock suite identities");
    if(suites.empty()){if(!memberships.empty()||!suite_names_.empty())throw std::invalid_argument("Native resource-lock suite declaration has no actual tree");}
    else{
      tree_.emplace(std::move(suites),std::move(memberships));
      if(std::set<uint64_t>(tree_->TestOrder().begin(),tree_->TestOrder().end())!=tests_)throw std::invalid_argument("Native resource-lock tree disagrees with actual test declarations");
    }
    size_t references=0,bytes=0;std::set<std::string> names;
    const auto check=[&](const Names& declared){
      if(declared.size()>64||declared.size()>ResourceLocks::kReferences-references)throw std::invalid_argument("Native resource-lock declaration/reference capacity exceeded");
      references+=declared.size();
      for(const auto& name:declared){
        if(name.size()%2||name.size()>kResourceLockUnits*2||name.size()+32>ResourceLocks::kMetadata-bytes)throw std::invalid_argument("Native resource-lock declaration name/metadata capacity exceeded");
        bytes+=name.size()+32;names.insert(name);if(names.size()>ResourceLocks::kNames)throw std::invalid_argument("Native resource-lock namespace capacity exceeded");
      }
    };
    check(file_names_);
    for(const auto& [id,declared]:test_names_){if(!tests_.contains(id))throw std::invalid_argument("Unknown native resource-lock test owner");check(declared);}
    for(const auto& [id,declared]:suite_names_){if(!suite_ids.contains(id))throw std::invalid_argument("Unknown native resource-lock suite owner");check(declared);}
  }
  Names Resolve(const std::set<uint64_t>& selected)const{
    for(const auto id:selected)if(!tests_.contains(id))throw std::invalid_argument("Unknown native resource-lock selected test");
    if(selected.empty())return {};
    std::set<std::string> names(file_names_.begin(),file_names_.end());
    for(const auto id:selected)if(const auto found=test_names_.find(id);found!=test_names_.end())names.insert(found->second.begin(),found->second.end());
    if(tree_){
      // Visit each suite's declarations once, avoiding a selected-test × depth ×
      // lock-name expansion while preserving exact inherited union semantics.
      std::set<uint64_t> owners;
      for(const auto& route:tree_->Plan(selected))owners.insert(route.ancestors.begin(),route.ancestors.end());
      for(const auto id:owners)if(const auto found=suite_names_.find(id);found!=suite_names_.end())names.insert(found->second.begin(),found->second.end());
    }
    return {names.begin(),names.end()};
  }
 private:
  std::set<uint64_t> tests_;std::optional<NativeSuiteTree> tree_;ByOwner test_names_,suite_names_;Names file_names_;
};
}
