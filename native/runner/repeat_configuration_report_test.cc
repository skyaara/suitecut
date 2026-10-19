// SPDX-License-Identifier: MIT
#include "repeat_configuration_report.h"
#include "repeat_configuration_json.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
  sr::RepeatConfigurationReport metadata;Reject([&]{metadata.Validate();});
  metadata.root=0;metadata.resolved={{0,sr::NativeRepeatRole::Primary,{0,0,1}}};metadata.Validate();auto bytes=metadata.JSON();auto json=sr::merge_detail::JSONParser(bytes).Parse();assert(sr::merge_detail::UInt(json,"root")==0);assert(sr::merge_detail::Get(json,"primaryOverride").kind==sr::merge_detail::JSON::Null);const auto& zero=sr::merge_detail::Rows(json,"resolved")[0];assert(sr::merge_detail::UInt(zero,"declaredRepeatEach")==0&&sr::merge_detail::UInt(zero,"effectiveRepeatEach")==0&&sr::merge_detail::UInt(zero,"executions")==1);
  assert(sr::merge_detail::ReadRepeatConfiguration(json).JSON()==bytes);assert(bytes.size()<=metadata.MetadataBytes());metadata.ValidateAdmission(0,0,sr::NativeRepeatRole::Primary,{0,0,1});
  Reject([&]{metadata.ValidateAdmission(0,0,sr::NativeRepeatRole::Primary,{1,1,1});});Reject([&]{metadata.ValidateAdmission(0,1,sr::NativeRepeatRole::Primary,{0,0,1});});
  for(const auto raw:{R"({"primaryOverride":null,"root":1.5,"projectDeclarations":[],"resolved":[]})",R"({"primaryOverride":null,"root":-1,"projectDeclarations":[],"resolved":[]})",R"({"primaryOverride":null,"root":0,"projectDeclarations":[{"project":1,"repeatEach":0},{"project":1,"repeatEach":0}],"resolved":[]})",R"({"primaryOverride":null,"root":0,"projectDeclarations":[],"resolved":[{"project":0,"role":"primary","declaredRepeatEach":0,"effectiveRepeatEach":0,"executions":0}]})"})Reject([&]{(void)sr::merge_detail::ReadRepeatConfiguration(sr::merge_detail::JSONParser(raw).Parse());});
  for(const auto name:{"pid","sourceId","attempts","nativeRetired"})assert(bytes.find(name)==bytes.npos);
  auto invalid=metadata;invalid.resolved[0].counts.executions=0;Reject([&]{invalid.Validate();});invalid=metadata;invalid.resolved.push_back(invalid.resolved[0]);Reject([&]{invalid.Validate();});invalid=metadata;invalid.resolved[0].project=129;Reject([&]{invalid.Validate();});invalid=metadata;invalid.resolved[0].role=static_cast<sr::NativeRepeatRole>(9);Reject([&]{invalid.Validate();});
  metadata.root=3;metadata.primary_override=2;metadata.project_declarations={{0,1},{2,1}};metadata.resolved={{1,sr::NativeRepeatRole::Dependency,{1,1,1}},{2,sr::NativeRepeatRole::Primary,{3,2,2}},{3,sr::NativeRepeatRole::Teardown,{1,1,1}}};metadata.Validate();bytes=metadata.JSON();json=sr::merge_detail::JSONParser(bytes).Parse();assert(sr::merge_detail::Rows(json,"projectDeclarations").size()==2&&sr::merge_detail::Rows(json,"resolved").size()==3);assert(sr::merge_detail::UInt(sr::merge_detail::Rows(json,"projectDeclarations")[0],"project")==1);assert(sr::merge_detail::UInt(sr::merge_detail::Rows(json,"resolved")[1],"effectiveRepeatEach")==2);
  auto source_metadata=metadata;source_metadata.resolved={metadata.resolved[1]};source_metadata.ValidateAdmission(2,3,sr::NativeRepeatRole::Primary,{3,2,2});Reject([&]{source_metadata.ValidateAdmission(2,3,sr::NativeRepeatRole::Dependency,{3,3,3});});Reject([&]{metadata.ValidateAdmission(2,3,sr::NativeRepeatRole::Primary,{3,2,2});});source_metadata.project_declarations[7]=1;Reject([&]{source_metadata.ValidateAdmission(2,3,sr::NativeRepeatRole::Primary,{3,2,2});});
  invalid=metadata;invalid.resolved[0].counts.effective=2;invalid.resolved[0].counts.executions=2;Reject([&]{invalid.Validate();});invalid=metadata;invalid.resolved[1].counts.declared=2;Reject([&]{invalid.Validate();});invalid=metadata;invalid.project_declarations[128]=1;Reject([&]{invalid.Validate();});
  metadata.primary_override=0;metadata.resolved[1].counts={3,0,1};metadata.Validate();assert(metadata.JSON().find("\"primaryOverride\":0")!=bytes.npos);
  metadata.resolved.clear();metadata.Validate(); // Actual configuration can precede successful selection.
  sr::RepeatConfigurationReport large;large.root=UINT64_MAX;large.resolved={{0,sr::NativeRepeatRole::Primary,{UINT64_MAX,UINT64_MAX,UINT64_MAX}}};large.Validate();assert(large.JSON().find("18446744073709551615")!=bytes.npos); // Raw facts do not admit expansion.
  std::cout<<"Repeat configuration: native raw/effective/actual counts, role resolution, bounds and metadata-only projection PASS\n";
}
