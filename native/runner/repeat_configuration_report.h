// SPDX-License-Identifier: MIT
#pragma once
#include "repeat_execution.h"
#include "directory_json.h"
#include <map>

namespace suitecut::runner {
// Native-collected configuration only. This record neither admits a callback
// nor proves an attempt, source, PID, output or physical retirement.
struct RepeatConfigurationReport {
  std::optional<uint64_t> primary_override,root;
  // Keys are actual zero-based local project indices; JSON uses index+1.
  std::map<size_t,uint64_t> project_declarations;
  struct Resolved {
    uint64_t project=0; // Zero is anonymous; authored local projects are 1-based.
    NativeRepeatRole role=NativeRepeatRole::Primary;
    NativeRepeatCounts counts;
  };
  std::vector<Resolved> resolved;
  void Validate()const {
    if((!primary_override&&!root&&project_declarations.empty())||project_declarations.size()>128||resolved.size()>128)
      throw std::invalid_argument("Native repeat configuration report lacks bounded explicit declarations");
    for(const auto& [project,count]:project_declarations){(void)count;if(project>=128)throw std::invalid_argument("Native repeat configuration project declaration rejected");}
    std::set<uint64_t> projects;
    for(const auto& row:resolved){
      if(row.project>128||!projects.insert(row.project).second)throw std::invalid_argument("Native repeat configuration resolved owner rejected");
      NativeRepeatConfiguration config;config.root=root;config.primary_override=primary_override;
      if(row.project)if(const auto found=project_declarations.find(static_cast<size_t>(row.project-1));found!=project_declarations.end())config.project=found->second;
      if(row.counts!=ResolveNativeRepeatCounts(config,row.role,0))throw std::invalid_argument("Native repeat configuration resolved counts differ from actual declarations");
    }
  }
  size_t MetadataBytes()const {Validate();return 512+256*(project_declarations.size()+resolved.size());}
  void ValidateAdmission(uint64_t local_project,size_t actual_project_count,NativeRepeatRole role,const NativeRepeatCounts& counts)const {
    Validate();
    if(actual_project_count>128||local_project>actual_project_count||(!local_project&&actual_project_count))throw std::invalid_argument("Native repeat configuration lacks actual admitted local project");
    for(const auto& [project,count]:project_declarations){(void)count;if(project>=actual_project_count)throw std::invalid_argument("Native repeat configuration declared a foreign collected project");}
    NativeRepeatConfiguration config;config.root=root;config.primary_override=primary_override;
    if(local_project)if(const auto found=project_declarations.find(static_cast<size_t>(local_project-1));found!=project_declarations.end())config.project=found->second;
    if(ResolveNativeRepeatCounts(config,role,0)!=counts)throw std::invalid_argument("Native source repeat configuration differs from admitted logical selection");
    for(const auto& row:resolved)if(row.project!=local_project||row.role!=role||row.counts!=counts)throw std::invalid_argument("Native source repeat resolution claimed foreign admission");
  }
  template<class Sink>void Write(Sink& out)const {
    Validate();out.Put("{\"primaryOverride\":");if(primary_override)out.NumberValue(*primary_override);else out.Put("null");out.Put(",\"root\":");if(root)out.NumberValue(*root);else out.Put("null");
    out.Put(",\"projectDeclarations\":[");bool first=true;
    for(const auto& [project,count]:project_declarations){if(!first)out.Put(",");first=false;out.Put("{\"project\":");out.NumberValue(project+1);out.Put(",\"repeatEach\":");out.NumberValue(count);out.Put("}");}
    out.Put("],\"resolved\":[");first=true;
    for(const auto& row:resolved){if(!first)out.Put(",");first=false;out.Put("{\"project\":");out.NumberValue(row.project);out.Put(",\"role\":\"");out.Put(row.role==NativeRepeatRole::Primary?"primary":row.role==NativeRepeatRole::Dependency?"dependency":"teardown");out.Put("\",\"declaredRepeatEach\":");out.NumberValue(row.counts.declared);out.Put(",\"effectiveRepeatEach\":");out.NumberValue(row.counts.effective);out.Put(",\"executions\":");out.NumberValue(row.counts.executions);out.Put("}");}out.Put("]}");
  }
  std::string JSON()const {DirectoryJSONBuffer out;Write(out);return std::move(out.bytes);}
};
} // namespace suitecut::runner
