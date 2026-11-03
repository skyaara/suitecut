// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <set>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "include/cef_parser.h"
#include "storage_utf16.h"

namespace suitecut {
// Stable references need exact ownership validation: an older ticket prefix
// alone no longer identifies the current retained array or element index.
// The forest is JSON.stringify output from the utility world. JSON escapes
// every quote inside a string, so `"ref":"` occurs only as a node's key and a
// single scan finds every token; no 100,000-node CEF value tree is built.
inline bool SnapshotReferenceForests(const std::string& original,
                                    const std::vector<std::vector<std::string>>& groups,std::string& error){
  const auto fail=[&](const char* message){error=message;return false;};
  if(groups.empty())return fail("Native snapshot reference publication has no groups");
  std::unordered_set<std::string_view> allowed,used;
  for(const auto& group:groups)for(const auto& token:group)
    if(token.empty()||token.size()>192||!allowed.insert(token).second)
      return fail("Native snapshot reference ownership is invalid");
  if(original.empty()||original.front()!='['||original.back()!=']')
    return fail("Native snapshot reference forest is invalid");
  constexpr std::string_view key="\"ref\":\"";
  for(auto at=original.find(key);at!=std::string::npos;at=original.find(key,at)){
    at+=key.size();const auto end=original.find('"',at);
    if(end==std::string::npos)return fail("Native snapshot reference token is invalid");
    const std::string_view token(original.data()+at,end-at);
    if(!allowed.contains(token))return fail("Native snapshot reference token has no retained owner");
    used.insert(token);at=end;
  }
  if(used.size()!=allowed.size())return fail("Native snapshot reference publication omits retained nodes");
  error.clear();return true;
}
// Finds the iframe nodes the frame batch must descend into. Only a forest that
// contains an iframe role is parsed.
inline bool SnapshotReferenceFrameIndices(const std::string& original,
                                         const std::vector<std::string>& references,
                                         std::vector<size_t>& frames,std::string& error){
  frames.clear();if(!SnapshotReferenceForests(original,std::vector<std::vector<std::string>>{references},error))return false;
  if(original.find("\"role\":\"iframe\"")==std::string::npos)return true;
  std::unordered_map<std::string,size_t> indices;for(size_t i=0;i<references.size();++i)indices.emplace(references[i],i);
  const auto parsed=CefParseJSON(StorageJSONValidationProjection(original),JSON_PARSER_RFC);
  const auto forest=parsed?parsed->GetList():nullptr;if(!forest){error="Native snapshot reference forest is invalid";return false;}
  std::vector<CefRefPtr<CefListValue>> pending{forest};std::set<size_t> used;
  while(!pending.empty()){
    const auto list=pending.back();pending.pop_back();
    for(size_t i=0;i<list->GetSize();++i){const auto node=list->GetDictionary(i);if(!node)continue;
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
}
