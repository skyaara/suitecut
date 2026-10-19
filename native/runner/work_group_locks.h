// SPDX-License-Identifier: MIT
#pragma once
#include "work_group_pool.h"
#include "resource_lock_declarations.h"
namespace suitecut::runner {
inline std::vector<std::vector<std::string>> WorkGroupLockNames(const TestWorkGroups& groups,const ResourceLockDeclarations& declarations){
  std::vector<std::vector<std::string>> names;names.reserve(groups.Groups().size());size_t references=0,bytes=0;
  for(const auto& group:groups.Groups()){
    auto resolved=declarations.Resolve(std::set<uint64_t>(group.tests.begin(),group.tests.end()));
    if(resolved.size()>ResourceLocks::kReferences-references)throw std::invalid_argument("Native inherited group-lock reference capacity exceeded");
    references+=resolved.size();
    for(const auto& name:resolved){
      if(name.size()+32>ResourceLocks::kMetadata-bytes)throw std::invalid_argument("Native inherited group-lock metadata capacity exceeded");
      bytes+=name.size()+32;
    }
    names.push_back(std::move(resolved));
  }
  return names;
}
// A separate typed receipt domain: a work-group lease is never represented as a
// project lease. The proven ordinary project ResourceLocks path stays intact.
class WorkGroupLocks {
 public:
  using Owner=WorkGroupPool::Admission;
  enum class Receipt {Active,PhysicalRetired,NeverStarted};
  struct Record {Owner owner;Receipt receipt=Receipt::Active;};
  WorkGroupLocks(const WorkGroupPool& pool,std::vector<std::vector<std::string>> names)
      :pool_(pool),names_(std::move(names)),records_(names_.size()){
    if(names_.size()!=pool.Plan().Groups().size())throw std::invalid_argument("Native group-lock plan membership mismatch");
    std::set<std::string> unique;size_t references=0,bytes=0;
    for(auto& group:names_){
      std::sort(group.begin(),group.end());group.erase(std::unique(group.begin(),group.end()),group.end());
      if(group.size()>ResourceLocks::kNames||group.size()>ResourceLocks::kReferences-references)throw std::invalid_argument("Native group-lock reference capacity exceeded");
      references+=group.size();
      for(const auto& name:group){
        if(name.size()%2||name.size()>kResourceLockUnits*2||name.size()+32>ResourceLocks::kMetadata-bytes)
          throw std::invalid_argument("Native group-lock name/metadata capacity exceeded");
        bytes+=name.size()+32;unique.insert(name);
        if(unique.size()>ResourceLocks::kNames)throw std::invalid_argument("Native group-lock namespace capacity4096 exceeded");
      }
    }
  }
  bool CanAcquire(size_t group)const{
    if(group>=names_.size()||records_[group]||active_>=WorkGroupPool::kWorkers)return false;
    for(const auto& name:names_[group])if(holders_.contains(name))return false;
    return true;
  }
  bool Acquire(const Owner& owner){
    if(!pool_.ReservedBeforeStart(owner)||!CanAcquire(owner.index))return false;
    auto staged=holders_;
    for(const auto& name:names_[owner.index])if(!staged.emplace(name,owner).second)throw std::runtime_error("Native atomic group-lock admission changed during staging");
    holders_.swap(staged);records_[owner.index]=Record{owner};++active_;return true;
  }
  bool Release(const Owner& owner){
    if(owner.index>=records_.size()||!records_[owner.index]||records_[owner.index]->receipt!=Receipt::Active||!Same(records_[owner.index]->owner,owner))return false;
    const bool retired=pool_.Retired(owner),canceled=pool_.CanceledBeforeStart(owner);
    if(!retired&&!canceled)return false;
    auto staged=holders_;
    for(const auto& name:names_[owner.index]){
      const auto found=staged.find(name);
      if(found==staged.end()||!Same(found->second,owner))throw std::runtime_error("Native group-lock retirement lost exact group/project owner");
      staged.erase(found);
    }
    holders_.swap(staged);records_[owner.index]->receipt=retired?Receipt::PhysicalRetired:Receipt::NeverStarted;--active_;return true;
  }
  size_t Active()const{return active_;}
  const std::vector<std::string>& Names(size_t group)const{return names_.at(group);}
  const std::vector<std::optional<Record>>& Records()const{return records_;}
 private:
  static bool Same(const Owner& a,const Owner& b){return a.index==b.index&&a.project==b.project&&a.generation==b.generation&&a.teardown==b.teardown;}
  const WorkGroupPool& pool_;std::vector<std::vector<std::string>> names_;std::vector<std::optional<Record>> records_;
  std::map<std::string,Owner> holders_;size_t active_=0;
};
}
