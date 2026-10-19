// SPDX-License-Identifier: MIT
#pragma once
#include "repeat_configuration_report.h"
#include "report_merge_json.h"
namespace suitecut::runner::merge_detail {
inline RepeatConfigurationReport ReadRepeatConfiguration(const JSON& value){
  RepeatConfigurationReport result;
  const auto& override_value=Get(value,"primaryOverride");if(override_value.kind!=JSON::Null)result.primary_override=Integer(override_value);
  const auto& root=Get(value,"root");if(root.kind!=JSON::Null)result.root=Integer(root);
  const auto& declarations=Rows(value,"projectDeclarations");const auto& resolved=Rows(value,"resolved");
  if(declarations.size()>128||resolved.size()>128)Invalid("repeat configuration exceeds original native project bound");
  for(const auto& row:declarations){const auto project=UInt(row,"project");if(!project||project>128||!result.project_declarations.emplace(static_cast<size_t>(project-1),UInt(row,"repeatEach")).second)Invalid("invalid or duplicate repeat project declaration");}
  for(const auto& row:resolved){
    const auto role=Text(Get(row,"role"));if(role!="primary"&&role!="dependency"&&role!="teardown")Invalid("unknown repeat project role");
    result.resolved.push_back({UInt(row,"project"),role=="primary"?NativeRepeatRole::Primary:role=="dependency"?NativeRepeatRole::Dependency:NativeRepeatRole::Teardown,{UInt(row,"declaredRepeatEach"),UInt(row,"effectiveRepeatEach"),UInt(row,"executions")}});
  }
  result.Validate();return result;
}
} // namespace suitecut::runner::merge_detail
