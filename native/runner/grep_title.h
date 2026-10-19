// SPDX-License-Identifier: MIT
#pragma once
#include "grep_expression.h"
#include "report_merge_json.h"
#include <map>
#include <set>
namespace suitecut::runner {
struct GrepTitleComponent {
  std::u16string title;
  size_t expected_tags=0;
  std::vector<std::u16string> tags;
};
using GrepTitleComponents=std::map<uint64_t,std::vector<GrepTitleComponent>>;
inline std::u16string GrepUnits(std::string_view raw,size_t capacity=4096){
  if(!JsonString(raw,capacity))throw std::invalid_argument("Native grep metadata is not a bounded JSON string");
  return merge_detail::Units(raw);
}
inline std::string GrepJSON(std::u16string_view units){
  constexpr char hex[]="0123456789abcdef";std::string out="\"";
  for(const auto unit:units){out+="\\u";for(int shift=12;shift>=0;shift-=4)out+=hex[(unit>>shift)&15];}
  out+='"';return out;
}
inline std::u16string GrepUTF8(std::string_view bytes){
  std::string raw="\"";
  constexpr char hex[]="0123456789abcdef";
  for(const unsigned char byte:bytes){
    if(byte=='"'||byte=='\\'){raw+='\\';raw+=static_cast<char>(byte);}
    else if(byte<32){raw+="\\u00";raw+=hex[byte>>4];raw+=hex[byte&15];}
    else raw+=static_cast<char>(byte);
  }raw+='"';
  return GrepUnits(raw,32768);
}
inline void ValidateGrepComponents(const GrepTitleComponents& components,const std::vector<uint64_t>& declarations){
  if(components.size()!=declarations.size())throw std::invalid_argument("Native grep title components omit actual declarations");
  for(const auto id:declarations){const auto found=components.find(id);
    if(found==components.end()||found->second.empty()||found->second.size()>65)throw std::invalid_argument("Native grep title component ancestry rejected");
    for(const auto& part:found->second)if(part.tags.size()!=part.expected_tags)throw std::invalid_argument("Native grep title declared tags incomplete");
  }
}
inline std::u16string ComposeGrepTitle(std::u16string_view project,std::u16string_view file,const std::vector<GrepTitleComponent>& components,const std::vector<std::u16string>& global_tags={}){
  std::u16string title;
  const auto append=[&](std::u16string_view value){if(!title.empty())title+=' ';title+=value;
    if(title.size()>GrepPattern::kTitleUnits)throw std::invalid_argument("Native composed grep title capacity32768 exceeded");};
  if(!project.empty())append(project);
  append(file);
  for(const auto& tag:global_tags)append(tag);
  for(const auto& part:components){append(part.title);for(const auto& tag:part.tags)append(tag);}
  return title;
}
struct GrepPolicy {
  std::optional<std::string> include_argument,exclude_argument;
  std::optional<GrepExpression> include,exclude;
  void Compile(){
    if(include_argument)include.emplace(CompileCLIGrep(GrepUTF8(*include_argument)));
    if(exclude_argument)exclude.emplace(CompileCLIGrep(GrepUTF8(*exclude_argument)));
  }
  bool Active()const{return include.has_value()||exclude.has_value();}
  bool Matches(std::u16string_view title,GrepPattern::Work& work)const{
    return(!include||include->Matches(title,work))&&(!exclude||!exclude->Matches(title,work));
  }
};
}
