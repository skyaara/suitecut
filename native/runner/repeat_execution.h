// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace suitecut::runner {
// Pure planning facts. These types admit no host, callback, Source or IO and
// cannot retire a physical credit. Actual actors must retain their coordinator
// reservation through their original complete physical retirement fence.
enum class NativeRepeatRole {Primary,Dependency,Teardown};
struct NativeRepeatConfiguration {
  std::optional<uint64_t> root,project,primary_override;
};
struct NativeRepeatCounts {
  uint64_t declared=1,effective=1,executions=1;
  bool operator==(const NativeRepeatCounts&)const=default;
};
inline uint64_t ParseNativeRepeatCount(std::string_view value,uint64_t minimum){
  if(minimum>1||value.empty())throw std::invalid_argument("Native repeat count domain is invalid");
  uint64_t result=0;const auto read=std::from_chars(value.data(),value.data()+value.size(),result);
  if(read.ec!=std::errc{}||read.ptr!=value.data()+value.size()||result<minimum)
    throw std::invalid_argument("Native repeat count must be an exact admitted unsigned integer");
  return result;
}
inline NativeRepeatCounts ResolveNativeRepeatCounts(const NativeRepeatConfiguration& config,
    NativeRepeatRole role,uint64_t minimum){
  if(minimum>1||(role!=NativeRepeatRole::Primary&&role!=NativeRepeatRole::Dependency&&role!=NativeRepeatRole::Teardown))
    throw std::invalid_argument("Native repeat role/count domain is invalid");
  for(const auto* value:{&config.root,&config.project,&config.primary_override})
    if(*value&&**value<minimum)throw std::invalid_argument("Native repeat declaration is outside admitted count domain");
  const auto declared=config.project.value_or(config.root.value_or(1));
  const auto effective=role==NativeRepeatRole::Primary?config.primary_override.value_or(declared):declared;
  return {declared,effective,effective?effective:1};
}
struct NativeRepeatTest {
  uint64_t local_id=0,projected_attempt_bytes=0;
};
struct NativeRepeatUnit {
  // Native positive owner handles. The adapter maps zero-based project indices
  // explicitly (index+1); file is the original positive SuiteFileToken.id.
  uint64_t file=0,project=0;
  NativeRepeatRole role=NativeRepeatRole::Primary;
  NativeRepeatCounts counts;
  std::shared_ptr<const std::vector<NativeRepeatTest>> declarations;
  // True only for a genuine declared-empty native segment. A config-filtered
  // no-work project must not fabricate an actor to satisfy this flag.
  bool declared_empty_actor=false;
  uint64_t actor_metadata_bytes=0;
};
struct NativeRepeatSliceKey {
  uint64_t file=0,project=0,index=0;
  bool operator==(const NativeRepeatSliceKey&)const=default;
};
// Native post-selection fact: logical sharding may select different original
// IDs for different repeats. It is deliberately not the old blanket file mask.
struct NativeSelectedRepeatSlice {
  size_t unit=0;uint64_t index=0;
  std::shared_ptr<const std::vector<uint64_t>> local_ids;
};
struct NativeRepeatSelectionLimits {
  uint64_t slices=0,source_reports=0,metadata_bytes=0;
};
struct NativeRepeatSelectionUsage {
  uint64_t slices=0,source_reports=0,metadata_bytes=0,logical_tests=0;
  bool operator==(const NativeRepeatSelectionUsage&)const=default;
};
namespace repeat_detail {
inline uint64_t Add(uint64_t a,uint64_t b){
  if(b>std::numeric_limits<uint64_t>::max()-a)throw std::length_error("Native repeat expansion arithmetic overflow");
  return a+b;
}
inline uint64_t Multiply(uint64_t a,uint64_t b){
  if(a&&b>std::numeric_limits<uint64_t>::max()/a)throw std::length_error("Native repeat expansion arithmetic overflow");
  return a*b;
}
inline void Within(const NativeRepeatSelectionUsage& usage,const NativeRepeatSelectionLimits& limit){
  if(usage.slices>limit.slices||usage.source_reports>limit.source_reports||usage.metadata_bytes>limit.metadata_bytes)
    throw std::length_error("Native repeat selection exceeds original shared planning budget");
}
}
class NativeRepeatCatalog {
 public:
  NativeRepeatCatalog(std::vector<NativeRepeatUnit> units,uint64_t actual_unit_limit,
      uint64_t actual_declaration_limit,uint64_t minimum):units_(std::move(units)){
    if(minimum>1||units_.size()>actual_unit_limit)throw std::invalid_argument("Native repeat original catalog bound rejected");
    std::set<std::pair<uint64_t,uint64_t>> owners,declarations;
    for(const auto& unit:units_){
      if(!unit.file||!unit.project||!owners.emplace(unit.file,unit.project).second||!unit.declarations||
          unit.declarations->size()>actual_declaration_limit||unit.counts.declared<minimum||unit.counts.effective<minimum||unit.counts.executions!=(unit.counts.effective?unit.counts.effective:1)||
          (unit.role!=NativeRepeatRole::Primary&&unit.role!=NativeRepeatRole::Dependency&&unit.role!=NativeRepeatRole::Teardown)||
          (unit.declared_empty_actor&&!unit.declarations->empty())||
          (unit.role!=NativeRepeatRole::Primary&&unit.counts.declared!=unit.counts.effective))
        throw std::invalid_argument("Native repeat unit lacks actual immutable declaration identity");
      std::set<uint64_t> ids;
      for(const auto& test:*unit.declarations){
        if(!test.local_id||!test.projected_attempt_bytes||!ids.insert(test.local_id).second||!declarations.emplace(unit.file,test.local_id).second)
          throw std::invalid_argument("Native repeat selected declaration is empty, repeated or has conflicting project ownership");
        if(declarations.size()>actual_declaration_limit)throw std::length_error("Native repeat original global declaration bound exceeded");
      }
      if(!unit.declarations->empty()||unit.declared_empty_actor){
        candidate_actors_=repeat_detail::Add(candidate_actors_,unit.counts.executions);
        candidate_tests_=repeat_detail::Add(candidate_tests_,repeat_detail::Multiply(unit.counts.executions,unit.declarations->size()));
      }
    }
  }
  const std::vector<NativeRepeatUnit>& Units()const{return units_;}
  // Charge this new compact container once in the existing native catalog
  // metadata owner. Referenced immutable declaration storage is already charged
  // there and never copied by this policy.
  uint64_t OwnedMetadataBytes()const{return repeat_detail::Add(sizeof(*this),repeat_detail::Multiply(units_.capacity(),sizeof(NativeRepeatUnit)));}
  uint64_t CandidateActors()const{return candidate_actors_;}
  uint64_t CandidateLogicalTests()const{return candidate_tests_;}
  NativeRepeatSliceKey Key(size_t unit,uint64_t index)const{
    const auto& owner=units_.at(unit);if(index>=owner.counts.executions)throw std::invalid_argument("Native repeat index has no configured logical position");
    return {owner.file,owner.project,index};
  }
  const NativeRepeatTest& Declaration(size_t unit,uint64_t id)const{
    for(const auto& test:*units_.at(unit).declarations)if(test.local_id==id)return test;
    throw std::invalid_argument("Native repeat logical identity has no original selected declaration");
  }
  std::string LogicalID(size_t unit,uint64_t index,uint64_t local_id)const{
    const auto owner=Key(unit,index);(void)Declaration(unit,local_id);
    return "native-repeat:"+std::to_string(owner.file)+":"+std::to_string(owner.project)+":"+std::to_string(local_id)+":"+std::to_string(index);
  }
 private:
  std::vector<NativeRepeatUnit> units_;uint64_t candidate_actors_=0,candidate_tests_=0;
};
class NativeRepeatSelection {
 public:
  NativeRepeatSelection(std::shared_ptr<const NativeRepeatCatalog> catalog,
      std::vector<NativeSelectedRepeatSlice> slices,const NativeRepeatSelectionLimits& limits,
      NativeRepeatSelectionUsage& shared):catalog_(std::move(catalog)),slices_(std::move(slices)){
    if(!catalog_)throw std::invalid_argument("Native repeat selection lost immutable catalog ownership");
    repeat_detail::Within(shared,limits);auto next=shared;
    // All checks/allocations occur before committing the shared planning usage.
    next.slices=repeat_detail::Add(next.slices,slices_.size());
    next.source_reports=repeat_detail::Add(next.source_reports,slices_.size());
    next.metadata_bytes=repeat_detail::Add(next.metadata_bytes,repeat_detail::Multiply(slices_.capacity(),sizeof(NativeSelectedRepeatSlice)));
    repeat_detail::Within(next,limits);
    std::set<std::pair<size_t,uint64_t>> chosen;std::set<const std::vector<uint64_t>*> charged_ids;
    for(const auto& slice:slices_){
      (void)catalog_->Key(slice.unit,slice.index);const auto& unit=catalog_->Units().at(slice.unit);
      if(!slice.local_ids||!chosen.emplace(slice.unit,slice.index).second||
          (slice.local_ids->empty()&&!unit.declared_empty_actor))throw std::invalid_argument("Native repeat selection is duplicate or has no actual actor work");
      std::set<uint64_t> ids;uint64_t bytes=unit.actor_metadata_bytes;
      if(charged_ids.insert(slice.local_ids.get()).second)bytes=repeat_detail::Add(bytes,repeat_detail::Add(sizeof(std::vector<uint64_t>),repeat_detail::Multiply(slice.local_ids->capacity(),sizeof(uint64_t))));
      for(const auto id:*slice.local_ids){
        if(!ids.insert(id).second)throw std::invalid_argument("Native repeat logical selection repeats a declaration");
        bytes=repeat_detail::Add(bytes,catalog_->Declaration(slice.unit,id).projected_attempt_bytes);
      }
      next.logical_tests=repeat_detail::Add(next.logical_tests,slice.local_ids->size());
      next.metadata_bytes=repeat_detail::Add(next.metadata_bytes,bytes);repeat_detail::Within(next,limits);
    }
    accepted_=next;shared=next;
  }
  const NativeRepeatCatalog& Catalog()const{return *catalog_;}
  const std::vector<NativeSelectedRepeatSlice>& Slices()const{return slices_;}
  const NativeRepeatSelectionUsage& AcceptedSharedUsage()const{return accepted_;}
  std::string LogicalID(size_t slice,uint64_t id)const{
    const auto& selected=slices_.at(slice);
    if(!selected.local_ids)throw std::invalid_argument("Native repeat selection lost admitted local IDs");
    bool present=false;for(const auto local:*selected.local_ids)present|=local==id;
    if(!present)throw std::invalid_argument("Native repeat public identity is not in this exact selected execution");
    return catalog_->LogicalID(selected.unit,selected.index,id);
  }
 private:
  std::shared_ptr<const NativeRepeatCatalog> catalog_;
  std::vector<NativeSelectedRepeatSlice> slices_;NativeRepeatSelectionUsage accepted_;
};
} // namespace suitecut::runner
