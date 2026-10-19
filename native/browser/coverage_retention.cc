// SPDX-License-Identifier: MIT
#include "coverage_retention.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include "coverage_ranges.h"
#include "devtools_utf16.h"
namespace suitecut {
namespace {
using Dict=CoverageRetention::Dict;using List=CefRefPtr<CefListValue>;
constexpr uint64_t kCount=9007199254740991ULL;
constexpr size_t kScripts=2048,kRanges=16384,kUnits=8*1048576;
bool Number(Dict value,const char* field,uint64_t maximum,uint64_t& output){
  if(!value||(value->GetType(field)!=VTYPE_INT&&value->GetType(field)!=VTYPE_DOUBLE))return false;
  const double n=value->GetType(field)==VTYPE_INT?double(value->GetInt(field)):value->GetDouble(field);
  if(!std::isfinite(n)||n<0||n>double(maximum)||n!=std::floor(n))return false;
  output=uint64_t(n);return true;
}
bool Text(Dict value,const char* field,std::string& output,size_t& units){
  if(!value){return false;}auto boxed=value->GetValue(field);std::u16string tagged;
  if(!boxed||(boxed->GetType()!=VTYPE_STRING&&!DevToolsReadTag(boxed,tagged)))return false;
  output=DevToolsString(boxed);units=boxed->GetType()==VTYPE_STRING?boxed->GetString().ToString16().size():tagged.size();return true;
}
bool Fail(std::string& error,const char* message){error=message;return false;}
struct Range{int start,end;uint64_t count;};
struct Function{std::string name;bool blocks=false;int start=0,end=0;uint64_t root=0;std::vector<std::vector<Range>> samples;};
struct Script{int context=0;std::string id,url,source;size_t units=0;std::map<std::pair<int,int>,Function> functions;};
// Each sample's innermost nested interval owns its count. Flatten a sample only
// after validating a laminar interval tree; crossing or duplicate ranges fail.
bool Segments(const std::vector<Range>& input,std::vector<Range>& output,std::string& error){
  const auto& root=input.front();std::vector<Range> ranges=input;
  std::sort(ranges.begin(),ranges.end(),[](const auto& a,const auto& b){return a.start<b.start||(a.start==b.start&&a.end>b.end);});
  std::vector<Range> stack;std::set<std::pair<int,int>> identities;
  for(const auto& range:ranges){
    if(!identities.insert({range.start,range.end}).second)return Fail(error,"Retained JavaScript ranges duplicate");
    while(!stack.empty()&&range.start>=stack.back().end)stack.pop_back();
    if(!stack.empty()&&(range.end>stack.back().end||(range.start==stack.back().start&&range.end==stack.back().end)))return Fail(error,"Retained JavaScript ranges cross or duplicate");
    stack.push_back(range);
  }
  struct Event{int offset,index;bool open;};std::vector<Event> events;
  for(size_t i=0;i<ranges.size();++i)if(ranges[i].start!=ranges[i].end){events.push_back({ranges[i].start,int(i),true});events.push_back({ranges[i].end,int(i),false});}
  std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.offset<b.offset;});
  std::set<std::tuple<int,int,int>> active;size_t position=0;
  while(position<events.size()){
    const int start=events[position].offset;
    while(position<events.size()&&events[position].offset==start){const auto event=events[position++];const auto& r=ranges[event.index];const auto key=std::make_tuple(r.start,-r.end,event.index);if(event.open)active.insert(key);else active.erase(key);}
    if(position==events.size()||active.empty())continue;
    const auto count=ranges[std::get<2>(*active.rbegin())].count;const int end=events[position].offset;
    if(!output.empty()&&output.back().end==start&&output.back().count==count)output.back().end=end;else output.push_back({start,end,count});
  }
  if(root.start!=root.end&&(output.empty()||output.front().start!=root.start||output.back().end!=root.end))return Fail(error,"Retained JavaScript ranges do not cover their function");
  return true;
}
bool Merge(const Function& function,List& output,size_t& total,std::string& error){
  struct Change{int offset;uint64_t count;bool add;};std::vector<Change> changes;
  for(const auto& sample:function.samples){std::vector<Range> segments;if(!Segments(sample,segments,error))return false;for(const auto& range:segments){changes.push_back({range.start,range.count,true});changes.push_back({range.end,range.count,false});}}
  std::sort(changes.begin(),changes.end(),[](const auto& a,const auto& b){return a.offset<b.offset;});
  std::vector<Range> merged;uint64_t count=0;size_t index=0;
  while(index<changes.size()){
    const int start=changes[index].offset;size_t end=index;while(end<changes.size()&&changes[end].offset==start)++end;
    for(size_t n=index;n<end;++n)if(!changes[n].add){if(changes[n].count>count)return Fail(error,"Invalid retained JavaScript interval balance");count-=changes[n].count;}
    for(size_t n=index;n<end;++n)if(changes[n].add){if(changes[n].count>kCount-count)return Fail(error,"Retained JavaScript execution count overflow");count+=changes[n].count;}
    index=end;if(index==changes.size()||count==function.root)continue;
    const int finish=changes[index].offset;
    if(!merged.empty()&&merged.back().end==start&&merged.back().count==count)merged.back().end=finish;else merged.push_back({start,finish,count});
  }
  if(count)return Fail(error,"Unbalanced retained JavaScript intervals");
  if(merged.size()+1>kRanges-total)return Fail(error,"Merged retained JavaScript range capacity exceeded");
  output=CefListValue::Create();const auto append=[&](int start,int end,uint64_t value){auto range=CefDictionaryValue::Create();range->SetInt("startOffset",start);range->SetInt("endOffset",end);range->SetDouble("count",double(value));output->SetDictionary(output->GetSize(),range);};
  append(function.start,function.end,function.root);for(const auto& range:merged)append(range.start,range.end,range.count);total+=merged.size()+1;return true;
}
}
bool CoverageRetention::JavaScript(Dict payload,const SelectScript& select,Dict& output,std::string& error){
  output=nullptr;auto chunks=payload?payload->GetList("result"):nullptr;if(!chunks||!select||!chunks->GetSize()||chunks->GetSize()>2048)return Fail(error,"Missing or excessive retained JavaScript chunks");
  std::map<std::pair<int,std::string>,Script> scripts;size_t occurrences=0,range_count=0,source_units=0;double last_time=-1;std::set<int> destroyed_contexts;
  for(size_t n=0;n<chunks->GetSize();++n){
    auto chunk=chunks->GetDictionary(n);uint64_t sequence=0,destroyed=0;
    if(!Number(chunk,"sequence",2048,sequence)||sequence!=n+1||!Number(chunk,"destroyedContextId",2147483647,destroyed)||((n+1==chunks->GetSize())!=(destroyed==0)))return Fail(error,"Retained JavaScript final receipt or sequence is invalid");
    if(chunk->GetType("timestamp")!=VTYPE_DOUBLE&&chunk->GetType("timestamp")!=VTYPE_INT)return Fail(error,"Missing retained JavaScript timestamp");
    const double timestamp=chunk->GetType("timestamp")==VTYPE_DOUBLE?chunk->GetDouble("timestamp"):double(chunk->GetInt("timestamp"));if(!std::isfinite(timestamp)||timestamp<0||timestamp<last_time)return Fail(error,"Invalid retained JavaScript timestamp order");last_time=timestamp;
    auto measured=chunk->GetList("result");if(!measured||measured->GetSize()>kScripts-occurrences)return Fail(error,"Retained JavaScript script capacity exceeded");occurrences+=measured->GetSize();
    for(size_t s=0;s<measured->GetSize();++s){
      auto snapshot=measured->GetDictionary(s);auto coverage=snapshot?snapshot->GetDictionary("coverage"):nullptr;uint64_t context=0;std::string id,url,source;size_t id_units=0,url_units=0,units=0;
      if(!Number(snapshot,"executionContextId",2147483647,context)||!context||!Text(coverage,"scriptId",id,id_units)||id.empty()||id_units>256||!Text(coverage,"url",url,url_units)||url_units>8192||!Text(snapshot,"source",source,units)||units>kUnits-source_units||url_units>kUnits-source_units-units){return Fail(error,"Invalid retained JavaScript source identity or capacity");}source_units+=units+url_units;
      if(destroyed_contexts.contains(int(context)))return Fail(error,"Retained JavaScript context reappeared after its destruction receipt");
      auto functions=coverage->GetList("functions");if(!functions||functions->GetSize()>kRanges)return Fail(error,"Invalid retained JavaScript functions");
      const bool selected=select(int(context),id);Script* script=nullptr;
      if(selected){auto [it,inserted]=scripts.try_emplace({int(context),id});script=&it->second;if(inserted){script->context=int(context);script->id=id;script->url=url;script->source=source;script->units=units;}else if(script->url!=url||script->source!=source)return Fail(error,"Retained JavaScript source identity mutated");}
      std::set<std::pair<int,int>> sample_functions;
      for(size_t f=0;f<functions->GetSize();++f){
        auto entry=functions->GetDictionary(f);auto ranges=entry?entry->GetList("ranges"):nullptr;std::string name;size_t name_units=0;
        if(!Text(entry,"functionName",name,name_units)||name_units>8192||name_units>kUnits-source_units||entry->GetType("isBlockCoverage")!=VTYPE_BOOL||!ranges||!ranges->GetSize()||ranges->GetSize()>kRanges-range_count){return Fail(error,"Invalid retained JavaScript function metadata or ranges");}source_units+=name_units;range_count+=ranges->GetSize();std::vector<Range> sample;
        for(size_t r=0;r<ranges->GetSize();++r){auto range=ranges->GetDictionary(r);uint64_t start=0,end=0,count=0;if(!Number(range,"startOffset",2147483647,start)||!Number(range,"endOffset",2147483647,end)||start>end||end>units||!Number(range,"count",kCount,count))return Fail(error,"Invalid retained JavaScript interval");if(r&&(start< uint64_t(sample.front().start)||end>uint64_t(sample.front().end)))return Fail(error,"Retained JavaScript block escapes its function");sample.push_back({int(start),int(end),count});}
        const auto key=std::make_pair(sample.front().start,sample.front().end);if(!sample_functions.insert(key).second)return Fail(error,"Duplicate retained JavaScript function identity");
        // Validate excluded/native ranges too; malformed payloads cannot hide in
        // a realm the caller has elected to exclude.
        std::vector<Range> checked;if(!Segments(sample,checked,error))return false;
        if(script){auto [it,inserted]=script->functions.try_emplace(key);auto& function=it->second;if(inserted){function.name=name;function.blocks=entry->GetBool("isBlockCoverage");function.start=key.first;function.end=key.second;}else if(function.name!=name||function.blocks!=entry->GetBool("isBlockCoverage"))return Fail(error,"Retained JavaScript function identity mutated");if(sample.front().count>kCount-function.root)return Fail(error,"Retained JavaScript call count overflow");function.root+=sample.front().count;function.samples.push_back(std::move(sample));}
      }
    }
    if(destroyed&&!destroyed_contexts.insert(int(destroyed)).second)return Fail(error,"Duplicate retained JavaScript destruction receipt");
  }
  auto entries=CefListValue::Create();size_t merged_ranges=0;
  for(const auto& [identity,script]:scripts){auto entry=CefDictionaryValue::Create();entry->SetInt("executionContextId",script.context);entry->SetString("scriptId",script.id);DevToolsSetString(entry,"url",script.url);DevToolsSetString(entry,"source",script.source);auto functions=CefListValue::Create();for(const auto& [key,function]:script.functions){List ranges;if(!Merge(function,ranges,merged_ranges,error))return false;auto item=CefDictionaryValue::Create();DevToolsSetString(item,"functionName",function.name);item->SetBool("isBlockCoverage",function.blocks);item->SetList("ranges",ranges);functions->SetDictionary(functions->GetSize(),item);}entry->SetList("functions",functions);entries->SetDictionary(entries->GetSize(),entry);}
  output=CefDictionaryValue::Create();output->SetList("result",entries);return true;
}
bool CoverageRetention::CSS(Dict payload,Dict& output,std::string& error){
  output=nullptr;auto sheets=payload?payload->GetList("styleSheets"):nullptr;auto rules=payload?payload->GetList("ruleUsage"):nullptr;if(!sheets||sheets->GetSize()>kScripts||!rules||rules->GetSize()>8192)return Fail(error,"Missing or excessive retained CSS sources/rules");
  struct Sheet{std::string url,text;size_t units=0;bool author=false;std::vector<CSSCoverageRule> rules;};std::map<std::string,Sheet> sources;size_t source_units=0;
  for(size_t n=0;n<sheets->GetSize();++n){auto snapshot=sheets->GetDictionary(n);auto header=snapshot?snapshot->GetDictionary("header"):nullptr;std::string id,url,text;size_t id_units=0,url_units=0,units=0;
    if(!Text(header,"styleSheetId",id,id_units)||id.empty()||id_units>256||!Text(header,"sourceURL",url,url_units)||url_units>32768||!Text(snapshot,"text",text,units)||units>kUnits-source_units||url_units>kUnits-source_units-units||header->GetType("origin")!=VTYPE_STRING){return Fail(error,"Invalid retained CSS source identity or capacity");}source_units+=units+url_units;
    if(!sources.emplace(id,Sheet{url,text,units,header->GetString("origin")=="regular"&&!url.empty(),{}}).second)return Fail(error,"Duplicate retained CSS source identity");
  }
  for(size_t n=0;n<rules->GetSize();++n){auto rule=rules->GetDictionary(n);std::string id;size_t ignored=0;uint64_t start=0,end=0;if(!Text(rule,"styleSheetId",id,ignored)||!Number(rule,"startOffset",2147483647,start)||!Number(rule,"endOffset",2147483647,end)||start>end||rule->GetType("used")!=VTYPE_BOOL)return Fail(error,"Invalid retained CSS rule");auto found=sources.find(id);if(found==sources.end()||end>found->second.units)return Fail(error,"Retained CSS rule has no exact source snapshot");found->second.rules.push_back({int(start),int(end),rule->GetBool("used")});}
  auto entries=CefListValue::Create();for(const auto& [id,source]:sources){if(!source.author)continue;std::vector<CSSCoverageRange> projected;if(!CSSCoverageRanges(source.rules,projected,error))return false;auto ranges=CefListValue::Create();for(const auto& range:projected){auto item=CefDictionaryValue::Create();item->SetInt("start",range.start);item->SetInt("end",range.end);ranges->SetDictionary(ranges->GetSize(),item);}auto entry=CefDictionaryValue::Create();entry->SetString("styleSheetId",id);DevToolsSetString(entry,"url",source.url);DevToolsSetString(entry,"text",source.text);entry->SetList("ranges",ranges);entries->SetDictionary(entries->GetSize(),entry);}
  output=CefDictionaryValue::Create();output->SetList("result",entries);return true;
}
} // namespace suitecut
