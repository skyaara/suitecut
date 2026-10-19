// SPDX-License-Identifier: MIT
#pragma once
#include "runtime_skip.h"
#include <optional>
namespace suitecut::runner {
// Author-declared or actual runtime callsite metadata, never a file-existence
// assertion. Native step admission pins the bounded scalar with its real owner.
struct StepLocation {
  std::string file_json;uint32_t line=0,column=0;bool explicit_location=false;
  bool operator==(const StepLocation&)const=default;
};
inline bool ValidStepLocation(const StepLocation& value){return JsonString(value.file_json,4096)&&value.file_json!="\"\""&&value.line&&value.column&&value.line<=2147483647&&value.column<=2147483647;}
inline std::optional<StepLocation> PrepareStepLocation(const std::string& file_wire,uint64_t line,uint64_t column,uint64_t origin){
  if(!origin){if(file_wire!="-"||line||column)throw std::invalid_argument("Absent native step location has metadata");return std::nullopt;}
  if(origin>2||line>2147483647||column>2147483647)throw std::invalid_argument("Invalid native step location bounds");
  StepLocation value{RuntimeSkipReason(file_wire),static_cast<uint32_t>(line),static_cast<uint32_t>(column),origin==2};
  if(!ValidStepLocation(value))throw std::invalid_argument("Invalid native step location");
  return value;
}
// Captured runtime frames remain distinct from any explicit author location.
// The native controller chooses attribution only from these bounded callsites.
struct StepBox {
  bool enabled=false;
  std::optional<StepLocation> callsite=std::nullopt,caller=std::nullopt;
  bool operator==(const StepBox&)const=default;
};
inline bool ValidStepBox(const StepBox& value){
  if(!value.enabled&&(value.callsite||value.caller))return false;
  for(const auto* location:{&value.callsite,&value.caller})if(*location&&(!ValidStepLocation(**location)||(*location)->explicit_location))return false;
  return !value.caller||value.callsite.has_value();
}
inline bool ValidStepBoxLocation(const StepBox& box,const std::optional<StepLocation>& location){
  return ValidStepBox(box)&&(!box.callsite||(location&&(location->explicit_location||*location==*box.callsite)));
}
inline size_t StepBoxMetadata(const StepBox& value){return (value.callsite?value.callsite->file_json.size()+64:0)+(value.caller?value.caller->file_json.size()+64:0);}
} // namespace suitecut::runner
