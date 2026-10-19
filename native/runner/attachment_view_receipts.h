// SPDX-License-Identifier: MIT
#pragma once
#include "test_attachments.h"
#include <functional>

namespace suitecut::runner {
// Native callback metadata fence only. A real writer's COMMITTED/waitpid/drain
// receipts remain entirely in RunnerAttachments and do not wait in this ledger.
class AttachmentViewReceipts {
 public:
  static constexpr size_t kRecords=128;
  static constexpr double kCleanupGrace=6000;
  enum class End { Acknowledged, WorkerExited };
  struct Record {
    uint64_t ticket=0,artifact=0;StepOwner owner;uint64_t step_id=0;
    double issued=0,deadline=0,cleanup_deadline=0;
    bool expired=false,close_requested=false;
    std::optional<End> end;std::optional<double> received;
    uint64_t mutation_request=0;bool accepted=false;
    AttachmentRetryProvenance callback_retry{};
  };
  struct Budget {uint64_t sequence=0;size_t records=0,pending=0;};
  struct Hooks {
    std::function<void(const Record&)> expired;
    std::function<void(const Record&)> close_owner;
  };
  explicit AttachmentViewReceipts(Hooks hooks,Budget* budget=nullptr):hooks_(std::move(hooks)),budget_(budget){
    if(!hooks_.expired||!hooks_.close_owner)throw std::invalid_argument("Native attachment view receipt hooks missing");
    records_.reserve(kRecords);
  }
  static double OriginalDeadline(const AttachmentRecord& actual,double callback_deadline,const StepRecord* step=nullptr){
    ValidateNativeAttachment(actual);
    if(!std::isfinite(callback_deadline)||callback_deadline<=actual.received)
      throw std::invalid_argument("Native attachment publication callback deadline expired");
    if(!actual.step_id){if(step)throw std::invalid_argument("Native attachment publication has a foreign step cap");return callback_deadline;}
    if(!step||step->id!=actual.step_id||step->owner!=actual.owner||!std::isfinite(step->deadline)||step->deadline<=actual.received)
      throw std::invalid_argument("Native attachment publication has no current original step deadline");
    return std::min(callback_deadline,step->deadline);
  }
  uint64_t Issue(const AttachmentRecord& actual,double original_deadline,double now){
    ValidateNativeAttachment(actual);Time(now);
    if(!std::isfinite(original_deadline)||original_deadline<=now||actual.received!=now||
       !std::isfinite(original_deadline+kCleanupGrace)||records_.size()>=kRecords||sequence_==UINT64_MAX||
       (budget_&&(budget_->records>=kRecords||budget_->pending>=kRecords||budget_->sequence==UINT64_MAX)))
      throw std::invalid_argument("Native attachment view publication admission rejected");
    for(const auto& record:records_)if(record.artifact==actual.id)
      throw std::invalid_argument("Native attachment view publication already owns this artifact");
    Record next{budget_?budget_->sequence+1:sequence_+1,actual.id,actual.owner,actual.step_id,
                now,original_deadline,original_deadline+kCleanupGrace,false,false,std::nullopt,std::nullopt,0,false,actual.callback_retry};
    records_.push_back(std::move(next));++sequence_;++pending_;
    if(budget_){++budget_->sequence;++budget_->records;++budget_->pending;}
    return records_.back().ticket;
  }
  const Record& Pending(uint64_t ticket,const StepOwner& actual,uint64_t artifact)const{
    const auto* record=Find(ticket);
    if(!record||record->end||record->owner!=actual||record->artifact!=artifact)
      throw std::invalid_argument("Native attachment view ACK ticket is stale or foreign");
    return *record;
  }
  void Acknowledge(uint64_t ticket,const StepOwner& actual,uint64_t artifact,uint64_t mutation_request,bool native_accepted,double now){
    Time(now);const auto& valid=Pending(ticket,actual,artifact);
    if(!mutation_request||now<valid.issued)throw std::invalid_argument("Native attachment view ACK has no actual mutation request");
    if(native_accepted&&(valid.expired||now>=valid.deadline))throw std::invalid_argument("Native attachment view cannot accept an expired publication");
    auto* record=Mutable(ticket);record->end=End::Acknowledged;record->received=now;record->mutation_request=mutation_request;record->accepted=native_accepted;
    --pending_;if(budget_)--budget_->pending;
  }
  void WorkerExited(unsigned index,int64_t pid,double now){
    Time(now);
    for(const auto& record:records_)if(!record.end&&record.owner.worker_index==index&&record.owner.pid==pid&&now<record.issued)
      throw std::invalid_argument("Native attachment view host exit preceded publication");
    for(auto& record:records_)if(!record.end&&record.owner.worker_index==index&&record.owner.pid==pid){
      record.end=End::WorkerExited;record.received=now;--pending_;if(budget_)--budget_->pending;
    }
  }
  bool PendingOwner(const StepOwner& owner)const{
    for(const auto& record:records_)if(!record.end&&record.owner==owner)return true;
    return false;
  }
  template<class Within>bool PendingStep(const StepOwner& owner,uint64_t step,Within&& within)const{
    for(const auto& record:records_)if(!record.end&&record.owner==owner&&((record.step_id&&within(record.step_id,step))||(!record.callback_retry.Empty()&&within(record.callback_retry.iteration_step,step))))return true;
    return false;
  }
  void Tick(double now){
    Time(now);
    // Bound iteration to ingress-existing tickets. Hooks may synchronously
    // deliver genuine worker death; re-find the record after each callback.
    const auto count=records_.size();
    for(size_t index=0;index<count;++index){
      auto& record=records_[index];if(record.end)continue;
      const auto ticket=record.ticket;
      if(!record.expired&&now>=record.deadline){
        record.expired=true;const auto observed=record;hooks_.expired(observed);
      }
      auto* current=Mutable(ticket);
      if(current&&!current->end&&!current->close_requested&&now>=current->cleanup_deadline){
        current->close_requested=true;const auto observed=*current;hooks_.close_owner(observed);
      }
    }
  }
  size_t PendingCount()const{return pending_;}
  bool NeedsTick()const{for(const auto& record:records_)if(!record.end&&!record.close_requested)return true;return false;}
  const std::vector<Record>& Records()const{return records_;}
  const Record* Find(uint64_t ticket)const{for(const auto& record:records_)if(record.ticket==ticket)return &record;return nullptr;}
 private:
  static void Time(double now){if(!std::isfinite(now)||now<0)throw std::invalid_argument("Invalid native attachment view receipt time");}
  Record* Mutable(uint64_t ticket){for(auto& record:records_)if(record.ticket==ticket)return &record;return nullptr;}
  Hooks hooks_;Budget* budget_;uint64_t sequence_=0;size_t pending_=0;std::vector<Record> records_;
};
} // namespace suitecut::runner
