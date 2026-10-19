// SPDX-License-Identifier: MIT
#pragma once
#include "multi_file_catalog.h"
#include "repeat_execution.h"
#include <functional>

namespace suitecut::runner {
// Immutable native planning only. This partition owns no actor/process/credit
// retirement API. Its actual actors share the original coordinator and fences.
class NativeRepeatGroupPartition {
 public:
  struct Group {
    size_t slice=0,index=0;WorkGroupMode mode=WorkGroupMode::Default;uint64_t suite=0;
    std::shared_ptr<const std::vector<uint64_t>> local_ids;
  };
  NativeRepeatGroupPartition(std::shared_ptr<const NativeRepeatSelection> selection,
      std::shared_ptr<const MultiFileCatalog> catalog,const NativeRepeatSelectionLimits& limits,
      NativeRepeatSelectionUsage& shared,const std::function<void()>& stopped={})
      :selection_(std::move(selection)),catalog_(std::move(catalog)){
    if(!selection_||!catalog_||shared!=selection_->AcceptedSharedUsage()||shared.source_reports<selection_->Slices().size())
      throw std::invalid_argument("Native repeated groups lost original catalog/selection planning ownership");
    repeat_detail::Within(shared,limits);auto next=shared;
    next.source_reports-=selection_->Slices().size();
    std::map<uint64_t,size_t> files;std::vector<std::unique_ptr<NativeTestGroupTree>> trees(catalog_->Files().size());
    std::vector<std::set<uint64_t>> original_ids(catalog_->Files().size());
    for(size_t f=0;f<catalog_->Files().size();++f){
      files.emplace(catalog_->Files()[f].file.id,f);
      for(const auto& test:catalog_->Files()[f].catalog.tests){if(stopped)stopped();original_ids[f].insert(test.id);}
    }
    uint64_t extra_actor_bytes=0;
    for(size_t slice_index=0;slice_index<selection_->Slices().size();++slice_index){
      if(stopped)stopped();
      const auto& slice=selection_->Slices()[slice_index];const auto& unit=selection_->Catalog().Units().at(slice.unit);
      const auto found=files.find(unit.file);
      if(found==files.end()||!unit.project||unit.project>catalog_->Graph().Declarations().size())throw std::invalid_argument("Native repeat group has no actual collected file/project");
      const auto file=found->second;const auto project=static_cast<size_t>(unit.project-1);const auto& original=catalog_->Files()[file].catalog;
      std::optional<size_t> local;
      if(project==catalog_->AnonymousProject()){
        if(!original.projects.empty())throw std::invalid_argument("Native anonymous repeat group has named project declarations");
      }else{
        const auto& name=catalog_->Graph().Declarations()[project].name;
        for(size_t p=0;p<original.projects.size();++p)if(original.projects[p].name==name)local=p;
        if(!local)throw std::invalid_argument("Native repeat group lacks original local project");
      }
      for(const auto id:*slice.local_ids){
        if(stopped)stopped();
        if(!original_ids[file].contains(id)||
            (local&&original.project_tests.at(id)!=*local))throw std::invalid_argument("Native repeat group selected foreign original declaration");
      }
      if(slice.local_ids->empty()&&(!unit.declared_empty_actor||std::any_of(original.tests.begin(),original.tests.end(),[&](const auto& t){return !local||original.project_tests.at(t.id)==*local;})))
        throw std::invalid_argument("Native repeat group invented a filtered-empty actor");
      if(!trees[file]){
        std::vector<uint64_t> ids;for(const auto& t:original.tests)ids.push_back(t.id);
        trees[file]=std::make_unique<NativeTestGroupTree>(std::move(ids),original.suites,original.members,original.suite_modes);
      }
      bool parallel=original.fully_parallel.value_or(false);
      if(local)if(const auto mode=original.project_parallel.find(*local);mode!=original.project_parallel.end())parallel=mode->second;
      auto groups=trees[file]->Resolve(*slice.local_ids,parallel);
      next.source_reports=repeat_detail::Add(next.source_reports,groups.size());repeat_detail::Within(next,limits);
      // Existing selection reserves one actor per slice. Replace that source
      // reservation rather than multiplying a per-actor source allowance.
      if(groups.size()>1)extra_actor_bytes=repeat_detail::Add(extra_actor_bytes,repeat_detail::Multiply(groups.size()-1,unit.actor_metadata_bytes));
      std::set<uint64_t> covered;
      for(auto& group:groups){
        for(const auto id:group.tests)if(!covered.insert(id).second)throw std::invalid_argument("Native repeated group partition overlaps a logical test");
        auto ids=std::make_shared<const std::vector<uint64_t>>(std::move(group.tests));
        groups_.push_back({slice_index,groups_.size(),group.mode,group.suite,std::move(ids)});
      }
      if(covered!=std::set<uint64_t>(slice.local_ids->begin(),slice.local_ids->end()))throw std::invalid_argument("Native repeated group partition omitted selected logical tests");
    }
    next.metadata_bytes=repeat_detail::Add(next.metadata_bytes,repeat_detail::Add(OwnedMetadataBytes(),extra_actor_bytes));
    repeat_detail::Within(next,limits);accepted_=next;shared=next;
  }
  const NativeRepeatSelection& Selection()const{return *selection_;}
  const MultiFileCatalog& Catalog()const{return *catalog_;}
  const std::vector<Group>& Groups()const{return groups_;}
  const NativeRepeatSelectionUsage& AcceptedSharedUsage()const{return accepted_;}
  uint64_t OwnedMetadataBytes()const{
    uint64_t bytes=repeat_detail::Add(sizeof(*this),repeat_detail::Multiply(groups_.capacity(),sizeof(Group)));
    for(const auto& group:groups_)bytes=repeat_detail::Add(bytes,repeat_detail::Add(sizeof(std::vector<uint64_t>),repeat_detail::Multiply(group.local_ids->capacity(),sizeof(uint64_t))));
    return bytes;
  }
 private:
  std::shared_ptr<const NativeRepeatSelection> selection_;std::shared_ptr<const MultiFileCatalog> catalog_;
  std::vector<Group> groups_;NativeRepeatSelectionUsage accepted_;
};
} // namespace suitecut::runner
