// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_timeout_runtime.h"
namespace suitecut::runner {
struct FixtureSourceSetupClock {
  double started;
  std::optional<double> deadline;
};
// An absent result rejects a nonlive owner. A live result with absent deadline
// is a real zero-disabled slot, not a rejected or already-retired callback.
inline std::optional<FixtureSourceSetupClock> InspectFixtureSourceSetupClock(
    FixtureTimeoutRuntime& runtime,const FixtureClockOwner& owner,double now,
    std::optional<double> original_suite_cap){
  const auto* record=runtime.Current(owner);
  if(!record||record->phase!=FixtureClockPhase::Setup||record->canceled||record->expired||runtime.DrainDeadline(owner))return std::nullopt;
  const auto deadline=runtime.SlotDeadline(owner,now,original_suite_cap);
  record=runtime.Current(owner);
  if(!record||record->phase!=FixtureClockPhase::Setup||record->canceled||record->expired||(deadline&&now>=*deadline))return std::nullopt;
  return FixtureSourceSetupClock{record->started,deadline};
}
inline double FixtureSourceSetupDeadline(const FixtureSourceSetupClock& slot){
  // Same native disabled-budget horizon as existing author clocks. Derive it
  // from actual admission, so repeated inspections cannot renew any budget.
  return slot.deadline.value_or(slot.started+3153600000000.0);
}
} // namespace suitecut::runner
