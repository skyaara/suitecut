// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
// Logical one-observation scope only. SelectorWaitControls independently keeps
// real Inspector commands and groups alive through their actual release ACKs.
class VisibilitySnapshotControls {
 public:
  struct Scope{int owner=0,context=0;uint64_t generation=0;std::string frame,session,loader,identity;bool validated=false;};
  struct Current{std::string session,loader,main_identity,utility_identity;int main_context=0,utility_context=0;};
  using Identity=std::pair<int,uint64_t>;
  bool Admit(int owner,uint64_t generation){
    if(owner<=0||!generation)return false;
    const auto found=states_.find(owner);if(found!=states_.end())return found->second.generation==generation;
    if(states_.size()>=64)return false;
    Scope scope;scope.owner=owner;scope.generation=generation;states_.emplace(owner,std::move(scope));return true;
  }
  bool Bind(int owner,uint64_t generation,const std::string& frame,const std::string& session,const std::string& loader,int context,const std::string& identity){
    const auto found=states_.find(owner);
    if(found==states_.end()||found->second.generation!=generation||frame.empty()||frame.size()>128||session.size()>256||loader.size()>256||identity.size()>256||context<0||(context>0&&identity.empty()))return false;
    const bool validated=found->second.validated;found->second={owner,context,generation,frame,session,loader,identity,validated};return true;
  }
  std::optional<Scope> Get(int owner)const{const auto found=states_.find(owner);if(found==states_.end())return{};return found->second;}
  bool Validated(int owner,uint64_t generation)const{const auto found=states_.find(owner);return found!=states_.end()&&found->second.generation==generation&&found->second.validated;}
  bool MarkValidated(int owner,uint64_t generation){const auto found=states_.find(owner);if(found==states_.end()||found->second.generation!=generation)return false;found->second.validated=true;return true;}
  bool Contains(int owner)const{return states_.contains(owner);}
  std::vector<Identity> InFrame(const std::string& frame)const{
    std::vector<Identity> result;for(const auto&[owner,scope]:states_)if(scope.frame==frame)result.emplace_back(owner,scope.generation);return result;
  }
  std::vector<Identity> InContext(const std::string& session,int context=0,const std::string& identity="")const{
    std::vector<Identity> result;for(const auto&[owner,scope]:states_)if(!scope.frame.empty()&&scope.session==session&&(!context||(scope.context==context&&!identity.empty()&&scope.identity==identity)))result.emplace_back(owner,scope.generation);return result;
  }
  static bool Gone(const Scope& scope,const std::optional<Current>& current){
    if(scope.frame.empty())return false;
    if(!current)return true;
    if(scope.session!=current->session||scope.loader!=current->loader)return true;
    return scope.context>0&&!((scope.context==current->main_context&&scope.identity==current->main_identity)||(scope.context==current->utility_context&&scope.identity==current->utility_identity));
  }
  void Remove(int owner,uint64_t generation){const auto found=states_.find(owner);if(found!=states_.end()&&found->second.generation==generation)states_.erase(found);}
  void Clear(){states_.clear();}size_t Size()const{return states_.size();}
 private:std::map<int,Scope> states_;
};
}
