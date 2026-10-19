// SPDX-License-Identifier: MIT
#pragma once
#include "file_group_pool.h"
#include "resource_lock_declarations.h"
namespace suitecut::runner {
inline std::vector<std::vector<std::string>> FileGroupLockNames(const FileWorkGroups& groups,const ResourceLockDeclarations& declarations){
  std::vector<std::vector<std::string>> names;names.reserve(groups.Groups().size());size_t references=0,bytes=0;
  for(const auto& group:groups.Groups()){
    auto resolved=declarations.Resolve(std::set<uint64_t>(group.tests.begin(),group.tests.end()));
    if(resolved.size()>ResourceLocks::kReferences-references)throw std::invalid_argument("Native inherited file-group lock reference capacity exceeded");
    references+=resolved.size();
    for(const auto& name:resolved){
      if(name.size()+32>ResourceLocks::kMetadata-bytes)throw std::invalid_argument("Native inherited file-group lock metadata capacity exceeded");
      bytes+=name.size()+32;
    }
    names.push_back(std::move(resolved));
  }
  return names;
}
// Distinct file admission domain. Canonical names and their holder slots are
// allocated before native worker admission; acquire/release do not allocate.
class FileGroupLocks {
 public:
  using Owner=FileGroupPool::Admission;
  enum class Receipt {Active,PhysicalRetired,NeverStarted};
  struct Record {Owner owner;Receipt receipt=Receipt::Active;};
  FileGroupLocks(const FileGroupPool& pool,std::vector<std::vector<std::string>> names)
      :pool_(pool),names_(std::move(names)),records_(names_.size()),indices_(names_.size()){
    if(names_.size()!=pool.Plan().Groups().size())throw std::invalid_argument("Native file lock plan membership mismatch");
    std::map<std::string,size_t> unique;size_t references=0,bytes=0;
    for(size_t i=0;i<names_.size();++i){auto& group=names_[i];
      std::sort(group.begin(),group.end());group.erase(std::unique(group.begin(),group.end()),group.end());
      if(group.size()>ResourceLocks::kNames||group.size()>ResourceLocks::kReferences-references)
        throw std::invalid_argument("Native file-group lock reference capacity exceeded");
      references+=group.size();indices_[i].reserve(group.size());
      for(const auto& name:group){
        if(name.size()%2||name.size()>kResourceLockUnits*2||name.size()+32>ResourceLocks::kMetadata-bytes)
          throw std::invalid_argument("Native file-group lock name/metadata capacity exceeded");
        bytes+=name.size()+32;const auto [found,inserted]=unique.emplace(name,unique.size());
        if(inserted&&unique.size()>ResourceLocks::kNames)throw std::invalid_argument("Native file lock namespace capacity4096 exceeded");
        indices_[i].push_back(found->second);
      }
    }
    holders_.resize(unique.size());
  }
  bool CanAcquire(size_t group)const noexcept{
    if(group>=names_.size()||records_[group]||active_>=FileGroupPool::kWorkers)return false;
    for(const auto id:indices_[group])if(holders_[id])return false;
    return true;
  }
  bool Acquire(const Owner& owner)noexcept{
    if(!pool_.ReservedBeforeStart(owner)||!CanAcquire(owner.index))return false;
    for(const auto id:indices_[owner.index])holders_[id]=owner;
    records_[owner.index]=Record{owner};++active_;return true;
  }
  bool Release(const Owner& owner)noexcept{
    if(owner.index>=records_.size()||!records_[owner.index]||records_[owner.index]->receipt!=Receipt::Active||records_[owner.index]->owner!=owner)return false;
    const bool retired=pool_.Retired(owner),canceled=pool_.CanceledBeforeStart(owner);
    if(!retired&&!canceled)return false;
    for(const auto id:indices_[owner.index])if(!holders_[id]||*holders_[id]!=owner)return false;
    for(const auto id:indices_[owner.index])holders_[id].reset();
    records_[owner.index]->receipt=retired?Receipt::PhysicalRetired:Receipt::NeverStarted;--active_;return true;
  }
  size_t Active()const{return active_;}
  const std::vector<std::string>& Names(size_t group)const{return names_.at(group);}
  const std::vector<std::optional<Record>>& Records()const{return records_;}
 private:
  const FileGroupPool& pool_;std::vector<std::vector<std::string>> names_;
  std::vector<std::optional<Record>> records_;std::vector<std::vector<size_t>> indices_;
  std::vector<std::optional<Owner>> holders_;size_t active_=0;
};
}
