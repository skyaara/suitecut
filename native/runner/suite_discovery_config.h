// SPDX-License-Identifier: MIT
#pragma once
#include "suite_discovery.h"
#include "report_merge_json.h"
namespace suitecut::runner {
inline std::string NativeDiscoveryDirectory(const std::string& value){
  if(value.empty()||value.front()!='/'||value.size()>16384||value.find('\0')!=value.npos||!JsonString(listing_detail::Quote(value),16384))throw std::invalid_argument("Native testDir must be bounded absolute filesystem UTF8");
  return value;
}
inline void NativeDiscoveryConfiguration(SuiteDiscoveryOptions& options,const std::string& key,const std::string& wire){
  if(wire.size()>1024*1024)throw std::invalid_argument("Native discovery configuration capacity1MiB exceeded");
  using namespace merge_detail;const auto data=JSONParser(wire).Parse();if(data.kind!=JSON::Array||data.items.size()>128)throw std::invalid_argument("Native discovery requires bounded pattern arrays");
  if(key=="--file-filters-json"){for(const auto& item:data.items)options.file_filters.push_back(GrepUTF8(Text(item)));return;}
  std::vector<SuitePathPattern> patterns;for(const auto& item:data.items){
    if(item.kind==JSON::String)patterns.push_back({SuitePathPattern::Kind::Glob,Units(item.raw),false});
    else {const auto kind=Text(Get(item,"kind"));const auto& source=Get(item,"source");if(source.kind!=JSON::String)throw std::invalid_argument("Native discovery source is not a string");
      if(kind=="glob"&&item.fields.size()==2)patterns.push_back({SuitePathPattern::Kind::Glob,Units(source.raw),false});
      else {const auto flags=Text(Get(item,"flags"));if(kind!="regex"||item.fields.size()!=3)throw std::invalid_argument("Native file regex declaration shape rejected");patterns.push_back({SuitePathPattern::Kind::Regex,Units(source.raw),false,flags});}
    }
  }
  if(key=="--test-match-json")options.match=std::move(patterns);else options.ignore=std::move(patterns);
}
}
