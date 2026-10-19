// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace suitecut {
struct CSSCoverageRule {int start=0,end=0;bool used=false;};
struct CSSCoverageRange {int start=0,end=0;bool operator==(const CSSCoverageRange&)const=default;};
// Native CSS rule intervals can nest. The most specific containing rule owns
// each segment; a genuinely unused child excludes that segment from its parent.
// Identical observations accumulate usage. Offsets remain CSS UTF16 offsets.
inline bool CSSCoverageRanges(const std::vector<CSSCoverageRule>& input,std::vector<CSSCoverageRange>& output,std::string& error){
  if(input.size()>16384){error="Native CSS coverage range capacity exceeded";return false;}
  std::map<std::pair<int,int>,bool> rules;for(const auto& rule:input){if(rule.start<0||rule.end<rule.start){error="Invalid native CSS coverage interval";return false;}if(rule.start!=rule.end)rules[{rule.start,rule.end}]|=rule.used;}
  struct Boundary{int position,index;bool opening;};std::vector<Boundary> boundaries;std::vector<CSSCoverageRule> unique;
  for(const auto& [range,used]:rules){const auto index=int(unique.size());unique.push_back({range.first,range.second,used});boundaries.push_back({range.first,index,true});boundaries.push_back({range.second,index,false});}
  std::sort(boundaries.begin(),boundaries.end(),[](const auto& a,const auto& b){return a.position<b.position;});
  std::set<std::tuple<int,int,int>> active;std::vector<CSSCoverageRange> ranges;
  size_t index=0;while(index<boundaries.size()){
    const int position=boundaries[index].position;
    while(index<boundaries.size()&&boundaries[index].position==position){const auto& event=boundaries[index++];const auto key=std::make_tuple(unique[event.index].start,-unique[event.index].end,event.index);if(event.opening)active.insert(key);else active.erase(key);}
    if(index==boundaries.size()||active.empty())continue;
    const auto& rule=unique[std::get<2>(*active.rbegin())];if(!rule.used)continue;const int end=boundaries[index].position;
    if(!ranges.empty()&&ranges.back().end==position)ranges.back().end=end;else ranges.push_back({position,end});
  }
  output=std::move(ranges);return true;
}
} // namespace suitecut
