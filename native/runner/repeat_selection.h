// SPDX-License-Identifier: MIT
#pragma once
#include "repeat_execution.h"
#include <algorithm>
#include <functional>
#include <map>

namespace suitecut::runner {
struct NativeRepeatPrimarySelection {
  std::vector<NativeSelectedRepeatSlice> slices;
  uint64_t unsharded_logical_tests=0,first=0,last=0;
};
namespace repeat_detail {
inline void Poll(const std::function<void()>& stopped){if(stopped)stopped();}
inline void SliceCapacity(const std::vector<NativeSelectedRepeatSlice>& selected,
    const NativeRepeatSelectionLimits& limit,const NativeRepeatSelectionUsage& usage){
  Within(usage,limit);
  if(selected.size()>=limit.slices-usage.slices||selected.size()>=limit.source_reports-usage.source_reports)
    throw std::length_error("Native repeat selected sources exceed original shared planning budget");
}
}
// This is native logical selection, not physical actor admission. The caller
// supplies genuine ordered native group sizes after focus/config/CLI filtering;
// default file/repeat is one group, fullyParallel is individual tests except
// actual serial/native indivisible groups. No JS grouping or mode inference.
// support closure is resolved by the original single native project graph.
inline NativeRepeatPrimarySelection SelectNativeRepeatPrimary(
    const NativeRepeatCatalog& catalog,const std::vector<std::vector<size_t>>& native_group_sizes,
    uint32_t shard_current,uint32_t shard_total,const NativeRepeatSelectionLimits& limit,
    const NativeRepeatSelectionUsage& usage,const std::function<void()>& stopped={}){
  if(native_group_sizes.size()!=catalog.Units().size()||!shard_current||!shard_total||
      shard_current>shard_total||shard_total>65536)
    throw std::invalid_argument("Native repeat shard/group declaration is invalid");
  repeat_detail::Within(usage,limit);NativeRepeatPrimarySelection result;
  for(size_t unit=0;unit<catalog.Units().size();++unit){
    uint64_t covered=0;
    for(const auto size:native_group_sizes[unit]){repeat_detail::Poll(stopped);if(!size)throw std::invalid_argument("Native repeat shard group is empty");covered=repeat_detail::Add(covered,size);if(covered>catalog.Units()[unit].declarations->size())throw std::invalid_argument("Native repeat shard groups exceed original declarations");}
    if(covered!=catalog.Units()[unit].declarations->size())throw std::invalid_argument("Native repeat shard groups omit original declarations");
  }
  std::map<uint64_t,std::vector<size_t>> projects;
  for(size_t unit=0;unit<catalog.Units().size();++unit){repeat_detail::Poll(stopped);const auto& owner=catalog.Units()[unit];
    if(owner.role!=NativeRepeatRole::Primary||owner.declarations->empty())continue;
    result.unsharded_logical_tests=repeat_detail::Add(result.unsharded_logical_tests,
        repeat_detail::Multiply(owner.counts.executions,owner.declarations->size()));
    projects[owner.project].push_back(unit);
  }
  const auto base=result.unsharded_logical_tests/shard_total,remainder=result.unsharded_logical_tests%shard_total;
  const uint64_t prior=shard_current-1;
  result.first=repeat_detail::Add(repeat_detail::Multiply(prior,base),std::min(prior,remainder));
  result.last=repeat_detail::Add(result.first,base+(shard_current<=remainder?1:0));
  if(result.first==result.last)return result;
  uint64_t project_start=0,preview_bytes=usage.metadata_bytes;
  for(auto& [project,units]:projects){
    (void)project;repeat_detail::Poll(stopped);
    std::sort(units.begin(),units.end(),[&](size_t a,size_t b){return catalog.Units()[a].file<catalog.Units()[b].file;});
    uint64_t project_size=0,cycles=0;
    for(const auto unit:units){const auto& owner=catalog.Units()[unit];project_size=repeat_detail::Add(project_size,repeat_detail::Multiply(owner.counts.executions,owner.declarations->size()));cycles=std::max(cycles,owner.counts.executions);}
    const auto project_end=repeat_detail::Add(project_start,project_size);
    if(project_start>=result.last)break;
    if(project_end<=result.first){project_start=project_end;continue;}
    const auto prefix=[&](uint64_t cycle){uint64_t n=0;for(const auto unit:units){repeat_detail::Poll(stopped);const auto& owner=catalog.Units()[unit];n=repeat_detail::Add(n,repeat_detail::Multiply(std::min(cycle,owner.counts.executions),owner.declarations->size()));}return n;};
    const uint64_t offset=result.first>project_start?result.first-project_start:0;
    // Find the containing repeat cycle without iterating every preceding repeat.
    uint64_t low=0,high=cycles;
    while(low<high){repeat_detail::Poll(stopped);const auto mid=low+(high-low)/2;
      if(prefix(mid)<=offset)low=mid+1;else high=mid;
    }
    uint64_t cycle=low?low-1:0;
    for(;cycle<cycles;++cycle){
      repeat_detail::Poll(stopped);uint64_t cursor=repeat_detail::Add(project_start,prefix(cycle));
      if(cursor>=result.last)break;
      for(const auto unit:units){
        repeat_detail::Poll(stopped);const auto& owner=catalog.Units()[unit];if(cycle>=owner.counts.executions)continue;
        const auto next=repeat_detail::Add(cursor,owner.declarations->size());
        uint64_t begin=0,end=0,position=0;bool selected=false;
        for(const auto width:native_group_sizes[unit]){
          repeat_detail::Poll(stopped);const auto group_start=repeat_detail::Add(cursor,position);
          if(group_start>=result.first&&group_start<result.last){if(!selected)begin=position;end=repeat_detail::Add(position,width);selected=true;}
          position=repeat_detail::Add(position,width);
        }
        if(!selected){cursor=next;continue;}
        repeat_detail::SliceCapacity(result.slices,limit,usage);
        auto bytes=repeat_detail::Add(sizeof(NativeSelectedRepeatSlice),repeat_detail::Add(sizeof(std::vector<uint64_t>),repeat_detail::Multiply(end-begin,sizeof(uint64_t))));
        bytes=repeat_detail::Add(bytes,owner.actor_metadata_bytes);
        for(auto test=begin;test<end;++test)bytes=repeat_detail::Add(bytes,owner.declarations->at(static_cast<size_t>(test)).projected_attempt_bytes);
        preview_bytes=repeat_detail::Add(preview_bytes,bytes);
        if(preview_bytes>limit.metadata_bytes)throw std::length_error("Native repeat shard subset exceeds original metadata budget");
        auto ids=std::make_shared<std::vector<uint64_t>>();ids->reserve(static_cast<size_t>(end-begin));
        for(auto test=begin;test<end;++test)ids->push_back(owner.declarations->at(static_cast<size_t>(test)).local_id);
        result.slices.push_back({unit,cycle,std::move(ids)});cursor=next;
      }
    }
    project_start=project_end;
  }
  return result;
}
// Add only genuinely selected support projects after resolving the single
// original project graph closure. Their own configured counts remain effective;
// the primary CLI override never duplicates that graph once per body repeat.
inline void AppendNativeRepeatSupport(const NativeRepeatCatalog& catalog,
    const std::set<uint64_t>& selected_projects,std::vector<NativeSelectedRepeatSlice>& slices,
    const NativeRepeatSelectionLimits& limit,const NativeRepeatSelectionUsage& usage,
    const std::function<void()>& stopped={}){
  repeat_detail::Within(usage,limit);uint64_t preview_bytes=usage.metadata_bytes;
  for(const auto& slice:slices){
    const auto& owner=catalog.Units().at(slice.unit);uint64_t bytes=owner.actor_metadata_bytes;
    bytes=repeat_detail::Add(bytes,repeat_detail::Add(sizeof(NativeSelectedRepeatSlice),repeat_detail::Add(sizeof(std::vector<uint64_t>),repeat_detail::Multiply(slice.local_ids->size(),sizeof(uint64_t)))));
    for(const auto id:*slice.local_ids)bytes=repeat_detail::Add(bytes,catalog.Declaration(slice.unit,id).projected_attempt_bytes);
    preview_bytes=repeat_detail::Add(preview_bytes,bytes);
  }
  for(size_t unit=0;unit<catalog.Units().size();++unit){
    repeat_detail::Poll(stopped);const auto& owner=catalog.Units()[unit];
    if(owner.role==NativeRepeatRole::Primary||!selected_projects.contains(owner.project)||
        (owner.declarations->empty()&&!owner.declared_empty_actor))continue;
    const auto remaining=std::min(limit.slices-usage.slices,limit.source_reports-usage.source_reports);
    if(slices.size()>remaining||owner.counts.executions>remaining-slices.size())
      throw std::length_error("Native repeat support exceeds original shared source budget");
    uint64_t per_actor=repeat_detail::Add(sizeof(NativeSelectedRepeatSlice),owner.actor_metadata_bytes);
    for(const auto& test:*owner.declarations)per_actor=repeat_detail::Add(per_actor,test.projected_attempt_bytes);
    preview_bytes=repeat_detail::Add(preview_bytes,repeat_detail::Add(repeat_detail::Multiply(per_actor,owner.counts.executions),repeat_detail::Add(sizeof(std::vector<uint64_t>),repeat_detail::Multiply(owner.declarations->size(),sizeof(uint64_t)))));
    if(preview_bytes>limit.metadata_bytes)throw std::length_error("Native repeat support exceeds original metadata budget");
    auto ids=std::make_shared<std::vector<uint64_t>>();ids->reserve(owner.declarations->size());
    for(const auto& test:*owner.declarations)ids->push_back(test.local_id);
    for(uint64_t index=0;index<owner.counts.executions;++index){repeat_detail::Poll(stopped);slices.push_back({unit,index,ids});}
  }
}
} // namespace suitecut::runner
