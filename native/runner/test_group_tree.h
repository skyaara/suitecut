// SPDX-License-Identifier: MIT
#pragma once
#include "suite_tree.h"
namespace suitecut::runner {
enum class WorkGroupMode {Default,Parallel,Serial};
struct SelectedTestGroup {
  WorkGroupMode mode=WorkGroupMode::Default;
  uint64_t suite=0;
  std::vector<uint64_t> tests;
};
// Owner-neutral declaration policy: project and file identities belong to the
// caller. Selection must already reflect focus, explicit IDs and native shard.
class NativeTestGroupTree {
 public:
  static constexpr size_t kTests=4096,kGroups=8192;
  NativeTestGroupTree(std::vector<uint64_t> tests,const std::vector<SuiteDeclaration>& suites,
      const std::vector<SuiteTestDeclaration>& members,std::map<uint64_t,WorkGroupMode> modes)
      :order_(std::move(tests)),modes_(std::move(modes)){
    if(order_.size()>kTests)throw std::invalid_argument("Native work-group test capacity4096 exceeded");
    for(const auto id:order_)if(!id||!tests_.insert(id).second)
      throw std::invalid_argument("Native work-group test identity rejected");
    if(suites.empty()){
      if(!members.empty()||!modes_.empty())throw std::invalid_argument("Native work-group suite mode has no actual tree");
      return;
    }
    NativeSuiteTree tree(suites,members);
    if(std::set<uint64_t>(tree.TestOrder().begin(),tree.TestOrder().end())!=tests_)
      throw std::invalid_argument("Native work-group suite membership disagrees with declared tests");
    std::map<uint64_t,uint64_t> parents;
    for(const auto& suite:suites)parents.emplace(suite.id,suite.parent);
    for(const auto& [suite,mode]:modes_){
      if(!parents.contains(suite)||(mode!=WorkGroupMode::Default&&mode!=WorkGroupMode::Parallel&&mode!=WorkGroupMode::Serial))
        throw std::invalid_argument("Native work-group mode or suite owner rejected");
      if(mode==WorkGroupMode::Parallel){
        auto parent=parents.at(suite);
        while(parent){
          const auto found=modes_.find(parent);
          if(found!=modes_.end()&&found->second==WorkGroupMode::Serial)
            throw std::invalid_argument("Native parallel suite cannot be nested inside a serial suite");
          parent=parents.at(parent);
        }
      }
    }
    order_=tree.TestOrder();
    for(const auto& route:tree.Plan(tests_))ancestors_.emplace(route.test,route.ancestors);
  }
  std::vector<SelectedTestGroup> Resolve(const std::vector<uint64_t>& ids,bool fully_parallel)const{
    std::set<uint64_t> selected;
    for(const auto id:ids)if(!tests_.contains(id)||!selected.insert(id).second)
      throw std::invalid_argument("Native work-group selection has unknown or duplicate test identity");
    std::vector<SelectedTestGroup> result;
    std::map<std::pair<WorkGroupMode,uint64_t>,size_t> groups;
    for(const auto id:order_)if(selected.contains(id)){
      auto mode=fully_parallel?WorkGroupMode::Parallel:WorkGroupMode::Default;
      uint64_t boundary=0;
      if(const auto route=ancestors_.find(id);route!=ancestors_.end())for(const auto suite:route->second){
        const auto declared=modes_.find(suite);if(declared==modes_.end())continue;
        if(declared->second==WorkGroupMode::Serial){
          if(mode==WorkGroupMode::Parallel){mode=WorkGroupMode::Serial;boundary=suite;}
          break;
        }
        if(declared->second==WorkGroupMode::Parallel){mode=WorkGroupMode::Parallel;boundary=suite;}
        else{if(mode==WorkGroupMode::Parallel)boundary=suite;mode=WorkGroupMode::Default;}
      }
      const auto [position,inserted]=groups.emplace(std::make_pair(mode,mode==WorkGroupMode::Parallel?id:boundary),result.size());
      if(inserted){
        if(result.size()>=kGroups)throw std::invalid_argument("Native work-group capacity8192 exceeded");
        result.push_back({mode,boundary,{}});
      }
      result[position->second].tests.push_back(id);
    }
    // Empty selected owners retain genuine worker admission/cleanup semantics;
    // this row declares no test attempt or author callback.
    if(result.empty())result.push_back({WorkGroupMode::Default,0,{}});
    return result;
  }
 private:
  std::vector<uint64_t> order_;
  std::set<uint64_t> tests_;
  std::map<uint64_t,WorkGroupMode> modes_;
  std::map<uint64_t,std::vector<uint64_t>> ancestors_;
};
}
