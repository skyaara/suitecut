// SPDX-License-Identifier: MIT
#pragma once
#include "repeat_execution.h"
#include "test_group_tree.h"
#include <algorithm>
#include <functional>
#include <set>

namespace suitecut::runner {
struct NativeRepeatProjection {
  std::vector<size_t> group_sizes;
  std::shared_ptr<const std::vector<NativeRepeatTest>> declarations;
};
// Bounded planning scratch over one immutable actual file catalog. This owns no
// declarations/actors, issues no identities and never changes shared budgets.
// Only the numeric projection escapes; pointers remain within the caller's
// immutable catalog lifetime. Original stop/deadline checks remain authoritative.
template<class Declaration>class NativeRepeatProjectionIndex {
 public:
  explicit NativeRepeatProjectionIndex(const std::vector<Declaration>& tests,
      std::function<void()> stopped={}):stopped_(std::move(stopped)){
    Poll();
    if(tests.size()>NativeTestGroupTree::kTests)throw std::length_error("Native repeat projection declaration capacity4096 exceeded");
    index_.reserve(tests.size());
    for(const auto& test:tests){Step();index_.push_back(&test);}
    std::sort(index_.begin(),index_.end(),[this](const auto* a,const auto* b){Step();return a->id<b->id;});
    for(size_t i=0;i<index_.size();++i){Step();if(!index_[i]->id||(i&&index_[i-1]->id==index_[i]->id))throw std::invalid_argument("Native repeat projection original declaration identity rejected");}
    Poll();
  }
  NativeRepeatProjection Project(const std::vector<SelectedTestGroup>& groups,
      const std::vector<uint64_t>& selected){
    Poll();
    if(selected.size()>NativeTestGroupTree::kTests||groups.size()>NativeTestGroupTree::kGroups)throw std::length_error("Native repeat projection group capacity exceeded");
    NativeRepeatProjection result;std::set<uint64_t> ordered,expected;
    for(const auto id:selected){Step();expected.insert(id);}
    size_t count=0,maximum=0;
    for(const auto& group:groups){
      Step();if(!group.tests.empty())result.group_sizes.push_back(group.tests.size());
      maximum=std::max(maximum,group.tests.size());
      for(const auto id:group.tests){Step();if(++count>NativeTestGroupTree::kTests)throw std::length_error("Native repeat projection selected declaration capacity exceeded");ordered.insert(id);}
    }
    if(ordered.size()!=expected.size())throw std::runtime_error("Native repeat groups lost actual selected declarations");
    auto original=expected.begin();for(const auto id:ordered){Step();if(id!=*original++)throw std::runtime_error("Native repeat groups lost actual selected declarations");}
    auto declarations=std::make_shared<std::vector<NativeRepeatTest>>();declarations->reserve(selected.size());
    std::vector<const Declaration*> references;references.reserve(maximum);
    for(const auto& group:groups){
      Step();references.clear();unsigned retries=0;
      for(const auto id:group.tests){const auto& test=Find(id);references.push_back(&test);retries=std::max(retries,test.retries);}
      for(const auto* test:references){
        Step();uint64_t bytes=repeat_detail::Add(test->title_json.size(),8448);
        for(const auto& fixture:test->fixtures){Step();bytes=repeat_detail::Add(bytes,fixture.size()+32);}
        const auto attempts=static_cast<uint64_t>(group.mode==WorkGroupMode::Serial?retries:test->retries)+1;
        declarations->push_back({test->id,repeat_detail::Multiply(bytes,attempts)});
      }
    }
    Poll();result.declarations=std::move(declarations);return result;
  }
 private:
  void Poll(){if(stopped_)stopped_();}
  void Step(){if((++work_&63)==0)Poll();}
  const Declaration& Find(uint64_t id){
    Step();const auto found=std::lower_bound(index_.begin(),index_.end(),id,[this](const auto* test,uint64_t key){Step();return test->id<key;});
    if(found==index_.end()||(*found)->id!=id)throw std::logic_error("Native repeat original selected test vanished");
    return **found;
  }
  std::vector<const Declaration*> index_;std::function<void()> stopped_;size_t work_=0;
};
}
