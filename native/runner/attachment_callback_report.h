// SPDX-License-Identifier: MIT
#pragma once
#include "test_attachments.h"
#include <algorithm>
namespace suitecut::runner {
// Metadata consistency only: production supplies native-admitted StepRecords.
// This never turns a report DTO into a process or callback settlement receipt.
template<class Report>void ValidateAttachmentCallbackRetry(const Report& report,const StepOwner& owner,const AttachmentRetryProvenance& retry,uint64_t public_step,double admitted,double terminal){
  if(retry.Empty())return;
  if(!retry.Complete()||(owner.callback_kind!="body"&&owner.callback_kind!="beforeEach"&&owner.callback_kind!="afterEach")||!std::isfinite(admitted)||!std::isfinite(terminal)||admitted<0||terminal<admitted)throw std::invalid_argument("Invalid native attachment callback assertion identity/time");
  if constexpr(requires{report.steps;report.steps.front().callback_iteration;}){
    const auto find=[&](uint64_t id){return std::find_if(report.steps.begin(),report.steps.end(),[&](const auto& step){return step.id==id;});};
    // Effective deadline is amended by live owner authority and is not an
    // admission-history timestamp. Keep immutable own caps and actual receipts.
    const auto immutable_admission=[&](uint64_t id){
      for(size_t depth=0;id&&depth<=TestSteps::kDepth;++depth){
        const auto step=find(id);
        if(step==report.steps.end()||step->owner!=owner||!std::isfinite(step->own_deadline)||admitted>=step->own_deadline)return false;
        if constexpr(requires{step->callback_deadline;})if(step->callback_deadline&&(!std::isfinite(*step->callback_deadline)||admitted>=*step->callback_deadline))return false;
        id=step->parent;
      }
      return !id;
    };
    const auto iteration=find(retry.iteration_step),first=find(retry.assertion_id);
    if(iteration==report.steps.end()||first==report.steps.end()||iteration->owner!=owner||first->owner!=owner||
       !iteration->callback_iteration||!first->callback_iteration||iteration->callback_iteration->assertion!=retry.assertion_id||
       iteration->callback_iteration->invocation!=retry.invocation_nonce||first->callback_iteration->assertion!=retry.assertion_id||first->callback_iteration->invocation!=1||
       iteration->parent!=first->parent||!std::isfinite(first->started)||first->started>iteration->started||
       (retry.invocation_nonce>1&&(!first->received||!std::isfinite(*first->received)||*first->received>iteration->started||first->receipt!=StepReceipt::Settled))||(retry.invocation_nonce==1?retry.iteration_step!=retry.assertion_id:retry.iteration_step<=retry.assertion_id)||
       !std::isfinite(iteration->started)||admitted<iteration->started||!immutable_admission(retry.iteration_step)||
       !iteration->received||!std::isfinite(*iteration->received)||*iteration->received<iteration->started||
       (iteration->callback_received&&(!std::isfinite(*iteration->callback_received)||admitted>*iteration->callback_received))||
       (iteration->receipt!=StepReceipt::Settled&&iteration->receipt!=StepReceipt::WorkerExited)||
       (iteration->receipt==StepReceipt::Settled&&(!iteration->callback_received||terminal>*iteration->received)))throw std::invalid_argument("Native attachment callback assertion lacks exact native iteration receipt");
    if(public_step){
      const auto actual=find(public_step);
      if(actual==report.steps.end()||actual->owner!=owner||!std::isfinite(actual->started)||admitted<actual->started||!immutable_admission(public_step)||
         (actual->callback_received&&(!std::isfinite(*actual->callback_received)||admitted>*actual->callback_received)))throw std::invalid_argument("Native attachment callback assertion has no original public step admission");
      if(!actual->received||!std::isfinite(*actual->received)||*actual->received<actual->started||
         (actual->receipt!=StepReceipt::Settled&&actual->receipt!=StepReceipt::WorkerExited)||
         (actual->receipt==StepReceipt::Settled&&(!actual->callback_received||terminal>*actual->received)))throw std::invalid_argument("Native attachment original public step lacks its independent physical receipt");
    }
  }else throw std::invalid_argument("Native attachment callback assertion has no native step journal");
}
template<class Writer>void PutAttachmentCallbackRetry(Writer& file,const AttachmentRetryProvenance& retry){
  if(retry.Empty())return;
  if(!retry.Complete())throw std::invalid_argument("Native attachment callback assertion identity incomplete");
  file.Put(",\"callbackAssertion\":{\"id\":\"");file.NumberValue(retry.assertion_id);file.Put("\",\"invocation\":\"");file.NumberValue(retry.invocation_nonce);file.Put("\",\"stepId\":\"");file.NumberValue(retry.iteration_step);file.Put("\"}");
}
} // namespace suitecut::runner
