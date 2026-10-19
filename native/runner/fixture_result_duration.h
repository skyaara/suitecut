// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_attempt_clocks.h"
namespace suitecut::runner {
// No associated Test execution ledger means no charged Test duration. A genuine
// independent beforeAll may already have run (and failed); actual wall and IO
// receipts remain separate and are never derived from this display duration.
inline double FixtureResultDuration(const FixtureAttemptClocks* timing,double now){
  if(!std::isfinite(now)||now<0)throw std::invalid_argument("Native fixture result observation rejected");
  return timing?timing->Observe(timing->Owner(),now).charged:0;
}
} // namespace suitecut::runner
