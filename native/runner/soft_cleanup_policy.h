// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <optional>
#include <string_view>
namespace suitecut::runner {
// HookPhases already admits cleanup under its own original phase deadline.
// A soft journal must neither replace that clock nor prevent the real CALL.
inline double SoftCallbackDeadline(std::string_view kind,double phase_deadline,std::optional<double> suite_deadline={},std::optional<double> cleanup_cap={}){
  return kind=="afterEach"?std::min(phase_deadline,cleanup_cap.value_or(phase_deadline)):std::min(phase_deadline,suite_deadline.value_or(phase_deadline));
}
inline bool SoftAdmissionAllowed(std::string_view kind,bool stopped,bool cleanup_admitted){
  return !stopped||(cleanup_admitted&&kind=="afterEach");
}
}
