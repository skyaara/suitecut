// SPDX-License-Identifier: MIT
#ifndef SUITECUT_CONSOLE_ARRAY_PREVIEW_H
#define SUITECUT_CONSOLE_ARRAY_PREVIEW_H
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SuiteCutConsoleArrayProperty { std::string name, value; };
inline std::optional<std::uint64_t> SuiteCutConsoleArrayIndex(std::string_view text) {
  if(text.empty() || (text.size()>1 && text.front()=='0')) return std::nullopt;
  std::uint64_t value=0;
  const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
  if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || value>=4294967295ULL) return std::nullopt;
  return value;
}
inline std::optional<std::uint64_t> SuiteCutConsoleArrayLength(std::string_view description) {
  if(!description.starts_with("Array(") || !description.ends_with(')')) return std::nullopt;
  const auto number=description.substr(6,description.size()-7);
  if(number.empty() || (number.size()>1 && number.front()=='0')) return std::nullopt;
  std::uint64_t value=0;
  const auto parsed=std::from_chars(number.data(),number.data()+number.size(),value);
  if(parsed.ec!=std::errc{} || parsed.ptr!=number.data()+number.size() || value>4294967295ULL) return std::nullopt;
  return value;
}
// Only actual preview indices and Chromium's actual Array(length) description
// determine gaps. Overflow never implies that unseen tail indices are holes.
inline std::string SuiteCutFormatConsoleArray(const std::string& description,
    const std::vector<SuiteCutConsoleArrayProperty>& properties,bool overflow) {
  std::string output="[";
  bool first=true;
  const auto append=[&](const std::string& text) {
    if(!first) output+=", ";
    first=false;
    output.append(text,0,std::min(text.size(),std::size_t(65534)-std::min(output.size(),std::size_t(65534))));
  };
  const auto holes=[&](std::uint64_t count) {
    if(count) append(count==1 ? "empty" : "empty x "+std::to_string(count));
  };
  std::uint64_t next=0, preceding=0;
  bool ordered=true, seen=false;
  for(std::size_t i=0;i<properties.size() && i<32;++i) {
    const auto index=SuiteCutConsoleArrayIndex(properties[i].name);
    if(!index) continue;
    if(seen && *index<=preceding) ordered=false;
    seen=true; preceding=*index;
  }
  for(std::size_t i=0;i<properties.size() && i<32 && output.size()<65530;++i) {
    const auto& property=properties[i];
    const auto index=SuiteCutConsoleArrayIndex(property.name);
    if(index) {
      if(ordered && *index>next) holes(*index-next);
      append(property.value);
      next=std::max(next,*index+1);
    } else append(property.name+": "+property.value);
  }
  if(overflow || properties.size()>32) append("…");
  else if(ordered) {
    const auto length=SuiteCutConsoleArrayLength(description);
    if(length && *length>next) holes(*length-next);
  }
  if(output.size()>65535) output.resize(65535);
  return output+"]";
}
#endif
