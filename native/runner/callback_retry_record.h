// SPDX-License-Identifier: MIT
#pragma once
#include "test_steps.h"
#include <optional>
#include <string>

namespace suitecut::runner {
enum class CallbackAssertionStatus {Passed,TimedOut,Canceled,Infrastructure};
enum class CallbackAssertionReceipt {Settled,WorkerExited};
// One final native assertion summary. Actual invocation history is retained in
// StepRecord; this record must never duplicate or reconstruct that history.
struct CallbackAssertionRecord {
  uint64_t id=0,request=0,parent=0,attempts=0,last_invocation=0;
  StepOwner owner;
  double started=0,deadline=0,received=0;
  std::optional<double> own_deadline;
  CallbackAssertionStatus status=CallbackAssertionStatus::Canceled;
  CallbackAssertionReceipt receipt=CallbackAssertionReceipt::Settled;
  std::string error_json="\"\"";
};
inline const char* CallbackAssertionStatusName(CallbackAssertionStatus status){
  switch(status){
    case CallbackAssertionStatus::Passed:return "passed";
    case CallbackAssertionStatus::TimedOut:return "timedOut";
    case CallbackAssertionStatus::Canceled:return "canceled";
    case CallbackAssertionStatus::Infrastructure:return "infrastructure";
  }
  throw std::invalid_argument("Invalid native callback assertion status");
}
inline const char* CallbackAssertionReceiptName(CallbackAssertionReceipt receipt){
  switch(receipt){
    case CallbackAssertionReceipt::Settled:return "settled";
    case CallbackAssertionReceipt::WorkerExited:return "workerExited";
  }
  throw std::invalid_argument("Invalid native callback assertion receipt");
}
}
