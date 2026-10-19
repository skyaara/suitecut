// SPDX-License-Identifier: MIT
#pragma once
#include "fail_on_flaky_tests.h"
#include "report_merge_json.h"
namespace suitecut::runner::merge_detail {
inline FailOnFlakyTestsPolicy ReadFailOnFlakyPolicy(const JSON& value,size_t actual_flaky){
  FailOnFlakyTestsPolicy policy{Bool(Get(value,"failOnFlakyTests"))};
  policy.Validate(UInt(value,"flakyTests"),Bool(Get(value,"failed")),actual_flaky);return policy;
}
} // namespace suitecut::runner::merge_detail
