// SPDX-License-Identifier: MIT
#pragma once
#include "callback_retry_record.h"
#include "source_callback_iteration_report.h"
#include "json_text.h"
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace suitecut::runner {
template<class Report>size_t ValidateCallbackAssertions(const Report& report,const std::map<uint64_t,const StepRecord*>& steps){
  if(report.callback_assertions.size()>TestSteps::kRecords)
    throw std::invalid_argument("Native callback assertion report capacity exceeded");
  if(report.callback_assertions.empty()){
    for(const auto& step:report.steps)if(step.callback_iteration||step.source_fence)
      throw std::invalid_argument("Native iteration step has no actual assertion owner");
    return 0;
  }
  // Index each existing history once. Validation does not rescan all prior
  // steps or attempts for each assertion or retain a second iteration journal.
  using Attempt=std::tuple<uint64_t,uint64_t,unsigned,int64_t>;
  std::map<Attempt,std::string_view> attempts;
  for(const auto& attempt:report.attempts)
    attempts.emplace(Attempt{attempt.test_id,attempt.generation,attempt.worker_index,attempt.pid},attempt.status);
  std::map<unsigned,int64_t> workers;
  for(const auto& worker:report.workers)if(worker.exit_code>=0)workers.emplace(worker.worker_index,worker.pid);
  struct Checked {const CallbackAssertionRecord* record;uint64_t count=0;const StepRecord* last=nullptr;};
  std::map<uint64_t,Checked> assertions;
  std::set<std::pair<unsigned,uint64_t>> requests;
  size_t metadata=0;
  for(const auto& record:report.callback_assertions){
    const auto& owner=record.owner;
    (void)CallbackAssertionStatusName(record.status);
    (void)CallbackAssertionReceiptName(record.receipt);
    const auto worker=workers.find(owner.worker_index);
    if(!record.id||!record.request||!record.attempts||record.attempts>TestSteps::kRecords||
       record.last_invocation!=record.attempts||!owner.test_id||!owner.attempt_generation||
       !owner.scope||!owner.scope_generation||!owner.worker_index||owner.pid<=0||owner.pid>INT32_MAX||
       (owner.callback_kind!="body"&&owner.callback_kind!="beforeEach"&&owner.callback_kind!="afterEach")||
       (owner.callback_kind!="body"&&owner.callback_token==0)||
       !assertions.emplace(record.id,Checked{&record}).second||
       !requests.emplace(owner.worker_index,record.request).second||
       !attempts.contains({owner.test_id,owner.attempt_generation,owner.worker_index,owner.pid})||
       worker==workers.end()||worker->second!=owner.pid||!std::isfinite(record.started)||record.started<0||
       !std::isfinite(record.deadline)||record.deadline<0||
       !std::isfinite(record.received)||record.received<record.started||
       !JsonString(record.error_json,8192))
      throw std::invalid_argument("Native callback assertion lacks actual owner, invocation or retirement metadata");
    if(record.own_deadline&&(!std::isfinite(*record.own_deadline)||*record.own_deadline<=record.started||record.deadline>*record.own_deadline))
      throw std::invalid_argument("Native callback assertion own deadline is invalid");
    if(record.status==CallbackAssertionStatus::TimedOut&&record.received<record.deadline)
      throw std::invalid_argument("Native callback assertion timed out before its native deadline");
    if(record.parent){
      const auto parent=steps.find(record.parent);
      if(parent==steps.end()||parent->second->owner!=owner||parent->second->started>record.started||
         !parent->second->received||*parent->second->received<record.received||parent->second->receipt==StepReceipt::NotInvoked)
        throw std::invalid_argument("Native callback assertion lacks actual parent physical ownership");
    }
    const size_t bytes=record.error_json.size()+owner.callback_kind.size()+512;
    if(bytes>TestSteps::kMetadata-metadata)
      throw std::invalid_argument("Native callback assertion report metadata exceeded");
    metadata+=bytes;
  }
  for(const auto& step:report.steps){
    const auto source_bytes=ValidateSourceCallbackIterationReceipt(step,report);
    if(source_bytes>TestSteps::kMetadata-metadata)
      throw std::invalid_argument("Native callback assertion report metadata exceeded");
    metadata+=source_bytes;
    if(!step.callback_iteration)continue;
    const auto& origin=*step.callback_iteration;
    const auto found=assertions.find(origin.assertion);
    if(found==assertions.end())throw std::invalid_argument("Native iteration step has no actual assertion owner");
    auto& checked=found->second;const auto& record=*checked.record;
    if(step.owner!=record.owner||step.parent!=record.parent||origin.invocation!=checked.count+1||
       (checked.count==0?step.id!=record.id:step.id<=checked.last->id)||
       (checked.count==0?step.started!=record.started:step.started<*checked.last->received)||
       step.callback_deadline!=record.own_deadline||
       !step.received||step.receipt==StepReceipt::Pending||step.receipt==StepReceipt::NotInvoked||
       *step.received>record.received||checked.count>=record.attempts)
      throw std::invalid_argument("Native callback assertion iteration history is foreign, incomplete or contradictory");
    // No fixed assertion cap is represented by a fabricated numeric deadline.
    // The adapter stores nullopt when timeout is zero or omitted.
    ++checked.count;checked.last=&step;
  }
  for(const auto& [id,checked]:assertions){
    (void)id;const auto& record=*checked.record;
    if(checked.count!=record.attempts||!checked.last)
      throw std::invalid_argument("Native callback assertion count has no actual invocation history");
    const auto& last=*checked.last;
    // Existing accepted soft failures still fail the owning test, even though
    // the actual callback returned. ValidateSoftAssertions checks that cause;
    // a successful toPass must neither erase it nor retry it toward green.
    const auto attempt_status=attempts.at({record.owner.test_id,record.owner.attempt_generation,record.owner.worker_index,record.owner.pid});
    const bool soft_return=last.status==StepStatus::Failed&&last.soft_error_id&&
      last.error_json==last.soft_error_json&&last.callback_received&&last.callback_ok&&
      !last.attachment_error_json&&attempt_status!="passed"&&attempt_status!="skipped";
    // An actual host exit during backoff retires the assertion, while its last
    // already-settled invocation remains unchanged historical evidence.
    if((last.receipt==StepReceipt::WorkerExited&&record.receipt!=CallbackAssertionReceipt::WorkerExited)||
       (record.receipt==CallbackAssertionReceipt::WorkerExited&&record.status!=CallbackAssertionStatus::Infrastructure)||
       (record.status==CallbackAssertionStatus::Passed&&
        (record.receipt!=CallbackAssertionReceipt::Settled||(last.status!=StepStatus::Passed&&!soft_return)||
         !last.receipt_ok||record.error_json!="\"\""||record.received>=record.deadline)))
      throw std::invalid_argument("Native callback assertion verdict contradicts its actual final receipt");
  }
  return metadata;
}

template<class Writer>void PutCallbackAssertions(Writer& file,const std::vector<CallbackAssertionRecord>& records){
  file.Put("[");bool first=true;
  for(const auto& record:records){
    if(!first)file.Put(",");
    first=false;const auto& owner=record.owner;
    const auto id=[&](const char* key,uint64_t value){file.Put(key);file.NumberValue(value);file.Put("\"");};
    file.Put("{");id("\"id\":\"",record.id);id(",\"requestId\":\"",record.request);
    id(",\"testId\":\"",owner.test_id);id(",\"generation\":\"",owner.attempt_generation);
    id(",\"callbackToken\":\"",owner.callback_token);id(",\"scopeId\":\"",owner.scope);
    id(",\"scopeGeneration\":\"",owner.scope_generation);id(",\"parentStepId\":\"",record.parent);
    file.Put(",\"workerIndex\":");file.NumberValue(owner.worker_index);
    file.Put(",\"pid\":");file.NumberValue(owner.pid);
    file.Put(",\"callbackKind\":\"");file.Put(owner.callback_kind);file.Put("\"");
    file.Put(",\"startedMonotonicMs\":");file.NumberValue(record.started);
    if(record.own_deadline){file.Put(",\"ownDeadlineMonotonicMs\":");file.NumberValue(*record.own_deadline);}
    file.Put(",\"deadlineMonotonicMs\":");file.NumberValue(record.deadline);
    file.Put(",\"attempts\":");file.NumberValue(record.attempts);
    if(record.last_invocation)id(",\"lastInvocation\":\"",record.last_invocation);
    file.Put(",\"status\":\"");file.Put(CallbackAssertionStatusName(record.status));file.Put("\"");
    file.Put(",\"error\":");file.Put(record.error_json);
    file.Put(",\"receiptMonotonicMs\":");file.NumberValue(record.received);
    file.Put(",\"receipt\":\"");file.Put(CallbackAssertionReceiptName(record.receipt));file.Put("\"}");
  }
  file.Put("]");
}
}
