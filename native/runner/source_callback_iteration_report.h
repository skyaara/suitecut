// SPDX-License-Identifier: MIT
#pragma once
#include "test_steps.h"
#include "retained_source_report.h"
#include "source_callback_iteration_receipt.h"
#include <cmath>
#include <stdexcept>
namespace suitecut::runner {
namespace source_callback_report_detail {
// Step is dependent while the StepRecord/source_fence producer is composed.
// A successful cut is historical work evidence, including when that same
// iteration subsequently records a genuine worker exit. It grants no Source
// process-lifetime retirement and need not belong to the fixture's first test.
template<class Step,class Report>size_t Validate(const Step& step,const Report& report){
  if(!step.source_fence)return 0;
  const auto& value=*step.source_fence;
  using Wire=RetainedSourceWorkWire;
  const auto challenge=Wire::Decode(Wire::Encode(value.challenge));
  const auto cuts=Wire::Decode(Wire::Encode(Wire::Kind::Acknowledged,value.cuts));
  const Wire::Resume resume{value.challenge.ticket,value.challenge.nonce,value.resume_generation};
  const auto resumed=Wire::Decode(Wire::Resumed(resume));
  if(!step.callback_iteration||!step.callback_iteration->assertion||!step.callback_iteration->invocation||
     !step.received||!std::isfinite(*step.received)||!step.callback_received||!std::isfinite(*step.callback_received)||
     challenge.challenge!=value.challenge||cuts.cuts!=value.cuts||
     value.cuts.ticket!=value.challenge.ticket||value.cuts.nonce!=value.challenge.nonce||
     resumed.resume.ticket!=resume.ticket||resumed.resume.nonce!=resume.nonce||resumed.resume.generation!=resume.generation||
     value.resume_generation<=value.challenge.borrow_generation||
     !value.observed||!value.acknowledged||!value.resumed||!value.host_ready||!value.host_resumed||
     !std::isfinite(value.observed_at)||!std::isfinite(value.acknowledged_at)||!std::isfinite(value.resumed_at)||
     !std::isfinite(value.host_ready_at)||!std::isfinite(value.host_resumed_at)||
     value.observed_at<=0||value.observed_at<step.started||value.observed_at<*step.callback_received||
     value.observed_at>value.acknowledged_at||value.acknowledged_at>value.host_ready_at||
     value.host_ready_at>value.resumed_at||value.resumed_at>value.host_resumed_at||
     value.host_resumed_at>=value.challenge.deadline||value.host_resumed_at>*step.received)
    throw std::invalid_argument("Native callback Source report lacks genuine ordered cut/resume receipts");
  const ReportRetainedSource* source=nullptr;
  for(const auto& row:report.retained_sources){
    if(row.ticket.id!=value.challenge.ticket)continue;
    if(source)throw std::invalid_argument("Native callback Source report has duplicate ticket authority");
    source=&row;
  }
  if(!source||!source->startup||source->ticket.owner.host.lease!=value.challenge.borrow_lease||
     source->ticket.owner.host.worker!=step.owner.worker_index||source->ticket.owner.host.pid!=step.owner.pid)
    throw std::invalid_argument("Native callback Source report has no genuine Source startup/host association");
  ValidateRetainedSource(*source);
  // Fixed-size wire fields only: conservative serialized metadata charge.
  return 1024;
}
} // namespace source_callback_report_detail
template<class Report>size_t ValidateSourceCallbackIterationReceipt(const StepRecord& step,const Report& report){
  return source_callback_report_detail::Validate(step,report);
}
template<class Writer>void PutSourceCallbackIterationReceipt(Writer& out,const SourceCallbackIterationReceipt& value){
  out.Put("{\"ticketId\":\"");out.NumberValue(value.challenge.ticket);
  out.Put("\",\"nonce\":\"");out.NumberValue(value.challenge.nonce);
  out.Put("\",\"epoch\":\"");out.NumberValue(value.challenge.borrow_generation);
  out.Put("\",\"resumeEpoch\":\"");out.NumberValue(value.resume_generation);
  out.Put("\",\"cleanupDeadlineMonotonicMs\":");out.NumberValue(value.challenge.deadline);
  const auto cut=[&](const auto& item){out.Put("{\"instance\":\"");out.NumberValue(item.instance);out.Put("\",\"sequence\":\"");out.NumberValue(item.sequence);out.Put("\",\"nonce\":\"");out.NumberValue(item.nonce);out.Put("\"}");};
  out.Put(",\"cuts\":{\"reader\":");cut(value.cuts.reader);out.Put(",\"writer\":");cut(value.cuts.writer);out.Put("}");
  out.Put(",\"decoderObserved\":");out.Put(value.observed?"true":"false");
  out.Put(",\"acknowledged\":");out.Put(value.acknowledged?"true":"false");
  out.Put(",\"resumed\":");out.Put(value.resumed?"true":"false");
  out.Put(",\"hostReady\":");out.Put(value.host_ready?"true":"false");
  out.Put(",\"hostResumed\":");out.Put(value.host_resumed?"true":"false");
  out.Put(",\"observedMonotonicMs\":");out.NumberValue(value.observed_at);
  out.Put(",\"acknowledgedMonotonicMs\":");out.NumberValue(value.acknowledged_at);
  out.Put(",\"resumedMonotonicMs\":");out.NumberValue(value.resumed_at);
  out.Put(",\"hostReadyMonotonicMs\":");out.NumberValue(value.host_ready_at);
  out.Put(",\"hostResumedMonotonicMs\":");out.NumberValue(value.host_resumed_at);out.Put("}");
}
} // namespace suitecut::runner
