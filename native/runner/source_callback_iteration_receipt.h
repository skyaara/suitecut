// SPDX-License-Identifier: MIT
#pragma once
#include "retained_source_work_wire.h"
namespace suitecut::runner {
// A snapshot of genuine successful transport receipts. The actual step owner
// is supplied by the caller's StepRecord; this DTO grants no admission itself.
struct SourceCallbackIterationReceipt {
  RetainedSourceWorkWire::Challenge challenge;
  RetainedSourceWorkWire::Cuts cuts;
  uint64_t resume_generation=0;
  bool observed=false,acknowledged=false,resumed=false,host_ready=false,host_resumed=false;
  double observed_at=0,acknowledged_at=0,resumed_at=0,host_ready_at=0,host_resumed_at=0;
};
} // namespace suitecut::runner
