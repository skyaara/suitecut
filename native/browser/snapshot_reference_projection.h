// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <set>
#include <map>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "storage_utf16.h"

namespace suitecut {
// Validates native-assigned reference metadata without decoding author-shaped
// objects or changing original Unicode name/text values in the returned JSON.
inline bool SnapshotReferenceForests(const std::string& original,
                                    const std::map<std::string,size_t>& groups,std::string& error){
  const auto fail=[&](const char* message){error=message;return false;};
  if(original.size()>65536||groups.empty()||groups.size()>32)
    return fail("Native snapshot reference publication exceeds bounds");
  size_t total=0;for(const auto&[prefix,count]:groups){
    if(prefix.empty()||prefix.size()>160||count>4096||count>4096-total)
      return fail("Native snapshot reference publication exceeds bounds");
    total+=count;
    for(const auto&[other,unused]:groups)if(prefix!=other&&prefix.starts_with(other))
      return fail("Native snapshot reference prefixes overlap");
  }
  const auto value=CefParseJSON(StorageJSONValidationProjection(original),JSON_PARSER_RFC);
  auto forest=value?value->GetList():nullptr;
  if(!forest)return fail("Native snapshot reference forest is invalid");
  struct Level{CefRefPtr<CefListValue> nodes;size_t depth;};
  std::vector<Level> pending{{forest,0}};size_t visited=0;std::map<std::string,std::set<size_t>> used;
  while(!pending.empty()){
    const auto level=pending.back();pending.pop_back();
    if(level.depth>64||level.nodes->GetSize()>4096)
      return fail("Native snapshot reference forest exceeds traversal bounds");
    for(size_t index=0;index<level.nodes->GetSize();++index){
      const auto node=level.nodes->GetDictionary(index);
      if(!node||++visited>4096||node->GetType("role")!=VTYPE_STRING)
        return fail("Native snapshot reference forest contains an invalid node");
      if(node->HasKey("ref")){
        if(node->GetType("ref")!=VTYPE_STRING)return fail("Native snapshot reference token is invalid");
        const auto token=node->GetString("ref").ToString();
        auto owner=groups.end();for(auto it=groups.begin();it!=groups.end();++it)if(token.starts_with(it->first)){owner=it;break;}
        if(owner==groups.end())return fail("Native snapshot reference prefix does not own the token");
        const auto suffix=token.substr(owner->first.size());size_t reference=0;
        const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),reference);
        if(suffix.empty()||(suffix.size()>1&&suffix[0]=='0')||parsed.ec!=std::errc()||
           parsed.ptr!=suffix.data()+suffix.size()||reference>=owner->second)
          return fail("Native snapshot reference index is invalid");
        used[owner->first].insert(reference);
      }
      if(node->HasKey("children")){
        const auto children=node->GetList("children");
        if(!children)return fail("Native snapshot reference forest children are invalid");
        // The parent value remains owned by `value` throughout this synchronous
        // validation. No borrowed CEF list escapes this actual response owner.
        pending.push_back({children,level.depth+1});
      }
    }
  }
  for(const auto&[prefix,count]:groups)if(used[prefix].size()!=count)return fail("Native snapshot reference publication omits retained nodes");
  error.clear();return true;
}
// Stable references need exact ownership validation: an older ticket prefix
// alone no longer identifies the current retained array or element index.
inline bool SnapshotReferenceForests(const std::string& original,
                                    const std::vector<std::vector<std::string>>& groups,std::string& error){
  const auto fail=[&](const char* message){error=message;return false;};
  if(original.size()>65536||groups.empty()||groups.size()>32)
    return fail("Native snapshot reference publication exceeds bounds");
  std::set<std::string> allowed,used;size_t total=0;
  for(const auto& group:groups){if(group.size()>4096-total)return fail("Native snapshot reference publication exceeds bounds");total+=group.size();
    for(const auto& token:group)if(token.empty()||token.size()>192||!allowed.insert(token).second)
      return fail("Native snapshot reference ownership is invalid");}
  const auto value=CefParseJSON(StorageJSONValidationProjection(original),JSON_PARSER_RFC);
  const auto forest=value?value->GetList():nullptr;
  if(!forest)return fail("Native snapshot reference forest is invalid");
  struct Level{CefRefPtr<CefListValue> nodes;size_t depth;};
  std::vector<Level> pending{{forest,0}};size_t visited=0;
  while(!pending.empty()){
    const auto level=pending.back();pending.pop_back();
    if(level.depth>64||level.nodes->GetSize()>4096)return fail("Native snapshot reference forest exceeds traversal bounds");
    for(size_t index=0;index<level.nodes->GetSize();++index){
      const auto node=level.nodes->GetDictionary(index);
      if(!node||++visited>4096||node->GetType("role")!=VTYPE_STRING)return fail("Native snapshot reference forest contains an invalid node");
      if(node->HasKey("ref")){
        if(node->GetType("ref")!=VTYPE_STRING)return fail("Native snapshot reference token is invalid");
        const auto token=node->GetString("ref").ToString();
        if(!allowed.contains(token))return fail("Native snapshot reference token has no retained owner");
        used.insert(token);
      }
      if(node->HasKey("children")){const auto children=node->GetList("children");
        if(!children)return fail("Native snapshot reference forest children are invalid");
        pending.push_back({children,level.depth+1});}
    }
  }
  if(used.size()!=allowed.size())return fail("Native snapshot reference publication omits retained nodes");
  error.clear();return true;
}
inline bool SnapshotReferenceFrameIndices(const std::string& original,
                                         const std::vector<std::string>& references,
                                         std::vector<size_t>& frames,std::string& error){
  frames.clear();if(!SnapshotReferenceForests(original,std::vector<std::vector<std::string>>{references},error))return false;
  std::map<std::string,size_t> indices;for(size_t i=0;i<references.size();++i)indices.emplace(references[i],i);
  const auto parsed=CefParseJSON(StorageJSONValidationProjection(original),JSON_PARSER_RFC);
  const auto forest=parsed?parsed->GetList():nullptr;if(!forest)return false;
  std::vector<CefRefPtr<CefListValue>> pending{forest};std::set<size_t> used;
  while(!pending.empty()){
    const auto list=pending.back();pending.pop_back();
    for(size_t i=0;i<list->GetSize();++i){const auto node=list->GetDictionary(i);
      if(node->GetString("role")=="iframe"){
        const auto found=node->GetType("ref")==VTYPE_STRING?indices.find(node->GetString("ref").ToString()):indices.end();
        if(found==indices.end()||!used.insert(found->second).second){error="Native iframe snapshot identity is ambiguous";return false;}
        frames.push_back(found->second);
      }
      if(const auto children=node->GetList("children"))pending.push_back(children);
    }
  }
  return true;
}
inline bool SnapshotReferenceForest(const std::string& original,const std::string& prefix,
                                    size_t count,std::string& error){
  return SnapshotReferenceForests(original,{{prefix,count}},error);
}
// Reads only native role/reference metadata after full forest validation. Names
// and text remain in the original JSON; the validation projection is never data.
inline bool SnapshotReferenceFrameIndices(const std::string& original,const std::string& prefix,
                                         size_t count,std::vector<size_t>& frames,std::string& error){
  frames.clear();if(!SnapshotReferenceForest(original,prefix,count,error))return false;
  const auto parsed=CefParseJSON(StorageJSONValidationProjection(original),JSON_PARSER_RFC);
  const auto forest=parsed?parsed->GetList():nullptr;if(!forest)return false;
  std::vector<CefRefPtr<CefListValue>> pending{forest};std::set<size_t> used;
  while(!pending.empty()){
    const auto list=pending.back();pending.pop_back();
    for(size_t i=0;i<list->GetSize();++i){const auto node=list->GetDictionary(i);
      if(node->GetString("role")=="iframe"){
        if(node->GetType("ref")!=VTYPE_STRING){error="Native iframe snapshot lacks retained identity";return false;}
        const auto token=node->GetString("ref").ToString();const auto suffix=token.substr(prefix.size());size_t index=0;
        const auto result=std::from_chars(suffix.data(),suffix.data()+suffix.size(),index);
        if(result.ec!=std::errc()||!used.insert(index).second){error="Native iframe snapshot identity is ambiguous";return false;}
        frames.push_back(index);
      }
      if(const auto children=node->GetList("children"))pending.push_back(children);
    }
  }
  return true;
}
}
