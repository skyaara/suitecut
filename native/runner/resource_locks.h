// SPDX-License-Identifier: MIT
#pragma once
#include "project_pool.h"
#include "json_text.h"
namespace suitecut::runner {
inline constexpr size_t kResourceLockUnits=1024;
// Canonical UTF16LE bytes preserve empty names, NUL and isolated code units.
// Empty lock name is a real name; an empty list declares no locks.
inline std::string ResourceLockName(std::string_view wire){
  if(wire=="-")return {};
  if(wire.empty()||wire.size()%4||wire.size()>kResourceLockUnits*4)
    throw std::invalid_argument("Invalid native resource-lock UTF16 name size");
  return DecodeHex(wire,kResourceLockUnits*2);
}
inline std::string ResourceLockJSON(std::string_view name){
  if(name.size()%2||name.size()>kResourceLockUnits*2)throw std::invalid_argument("Invalid native resource-lock UTF16 name");
  static constexpr char digits[]="0123456789abcdef";std::string json="\"";
  for(size_t at=0;at<name.size();at+=2){
    const auto low=static_cast<unsigned char>(name[at]),high=static_cast<unsigned char>(name[at+1]);
    json+="\\u";json+=digits[high>>4];json+=digits[high&15];json+=digits[low>>4];json+=digits[low&15];
  }
  json+='"';return json;
}
// One invocation, one already-validated native ProjectPool. Atomic named sets
// are acquired only for its exact real reservations. Waiting remains Pending,
// consuming no worker slot/test attempt and granting no fresh clock budget.
class ResourceLocks {
 public:
  using Owner=ProjectPool::Admission;
  enum class Receipt {Active,PhysicalRetired,NeverStarted};
  struct Record {Owner owner;Receipt receipt=Receipt::Active;};
  static constexpr size_t kNames=4096,kReferences=65536,kMetadata=8*1024*1024;
  ResourceLocks(const ProjectPool& pool,std::vector<std::vector<std::string>> names)
      :pool_(pool),names_(std::move(names)),records_(names_.size()){
    if(names_.size()!=pool.Graph().Declarations().size())throw std::invalid_argument("Native resource-lock project membership mismatch");
    std::set<std::string> unique;size_t references=0,bytes=0;
    for(auto& group:names_){
      std::sort(group.begin(),group.end());group.erase(std::unique(group.begin(),group.end()),group.end());
      if(group.size()>kNames||group.size()>kReferences-references)throw std::invalid_argument("Native resource-lock reference capacity exceeded");
      references+=group.size();
      for(const auto& name:group){
        if(name.size()%2||name.size()>kResourceLockUnits*2||name.size()+32>kMetadata-bytes)
          throw std::invalid_argument("Native resource-lock name/metadata capacity exceeded");
        bytes+=name.size()+32;unique.insert(name);
        if(unique.size()>kNames)throw std::invalid_argument("Native resource-lock namespace capacity4096 exceeded");
      }
    }
  }
  bool CanAcquire(size_t project)const{
    if(project>=names_.size()||records_[project]||active_>=ProjectPool::kWorkers)return false;
    for(const auto& name:names_[project])if(holders_.contains(name))return false;
    return true;
  }
  bool Acquire(const Owner& owner){
    if(!pool_.ReservedBeforeStart(owner)||!CanAcquire(owner.index))return false;
    // Stage every allocation before committing any part of an atomic set.
    auto staged=holders_;
    for(const auto& name:names_[owner.index])if(!staged.emplace(name,owner).second)
      throw std::runtime_error("Native atomic resource-lock admission changed during staging");
    holders_.swap(staged);records_[owner.index]=Record{owner};++active_;return true;
  }
  bool Release(const Owner& owner){
    if(owner.index>=records_.size()||!records_[owner.index]||records_[owner.index]->receipt!=Receipt::Active||!Same(records_[owner.index]->owner,owner))return false;
    const bool retired=pool_.Retired(owner),canceled=pool_.CanceledBeforeStart(owner);
    if(!retired&&!canceled)return false;
    auto staged=holders_;
    for(const auto& name:names_[owner.index]){
      const auto found=staged.find(name);
      if(found==staged.end()||!Same(found->second,owner))throw std::runtime_error("Native resource-lock physical retirement lost exact owner");
      staged.erase(found);
    }
    holders_.swap(staged);records_[owner.index]->receipt=retired?Receipt::PhysicalRetired:Receipt::NeverStarted;--active_;return true;
  }
  size_t Active()const{return active_;}
  const std::vector<std::string>& Names(size_t project)const{return names_.at(project);}
  const std::vector<std::optional<Record>>& Records()const{return records_;}
 private:
  static bool Same(const Owner& a,const Owner& b){return a.index==b.index&&a.generation==b.generation&&a.teardown==b.teardown;}
  const ProjectPool& pool_;std::vector<std::vector<std::string>> names_;
  std::vector<std::optional<Record>> records_;std::map<std::string,Owner> holders_;size_t active_=0;
};
}
