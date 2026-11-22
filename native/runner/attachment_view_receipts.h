// SPDX-License-Identifier: MIT
#pragma once
#include "test_attachments.h"
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace suitecut::runner {
// Native callback metadata fence only. A real writer's COMMITTED/waitpid/drain
// receipts remain entirely in RunnerAttachments and do not wait in this ledger.
class AttachmentViewReceipts {
 public:
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
  struct Budget {uint64_t sequence=0;size_t pending=0;};
  struct Hooks {
    std::function<void(const Record&)> expired;
    std::function<void(const Record&)> close_owner;
  };
  explicit AttachmentViewReceipts(Hooks hooks,Budget* budget=nullptr):hooks_(std::move(hooks)),budget_(budget){
    if(!hooks_.expired||!hooks_.close_owner)throw std::invalid_argument("Native attachment view receipt hooks missing");
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
       !std::isfinite(original_deadline+kCleanupGrace)||sequence_==UINT64_MAX||
       (budget_&&budget_->sequence==UINT64_MAX))
      throw std::invalid_argument("Native attachment view publication admission rejected");
    if(artifacts_.contains(actual.id))
      throw std::invalid_argument("Native attachment view publication already owns this artifact");
    Record next{budget_?budget_->sequence+1:sequence_+1,actual.id,actual.owner,actual.step_id,
                now,original_deadline,original_deadline+kCleanupGrace,false,false,std::nullopt,std::nullopt,0,false,actual.callback_retry};
    records_.push_back(std::move(next));const auto ticket=records_.back().ticket;
    try{tickets_.emplace(ticket,records_.size()-1);artifacts_.insert(actual.id);open_.insert(ticket);}
    catch(...){tickets_.erase(ticket);artifacts_.erase(actual.id);records_.pop_back();throw;}
    ++sequence_;++pending_;
    if(budget_){++budget_->sequence;++budget_->pending;}
    return ticket;
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
    auto* record=Mutable(ticket);record->end=End::Acknowledged;open_.erase(ticket);record->received=now;record->mutation_request=mutation_request;record->accepted=native_accepted;
    --pending_;if(budget_)--budget_->pending;
  }
  void WorkerExited(unsigned index,int64_t pid,double now){
    Time(now);
    std::vector<uint64_t> exited;
    for(const auto ticket:open_){const auto& record=*Find(ticket);if(record.owner.worker_index==index&&record.owner.pid==pid){if(now<record.issued)throw std::invalid_argument("Native attachment view host exit preceded publication");exited.push_back(ticket);}}
    for(const auto ticket:exited){auto& record=*Mutable(ticket);
      record.end=End::WorkerExited;record.received=now;open_.erase(ticket);--pending_;if(budget_)--budget_->pending;
    }
  }
  bool PendingOwner(const StepOwner& owner)const{
    for(const auto ticket:open_)if(Find(ticket)->owner==owner)return true;
    return false;
  }
  template<class Within>bool PendingStep(const StepOwner& owner,uint64_t step,Within&& within)const{
    for(const auto ticket:open_)if(const auto& record=*Find(ticket);record.owner==owner&&((record.step_id&&within(record.step_id,step))||(!record.callback_retry.Empty()&&within(record.callback_retry.iteration_step,step))))return true;
    return false;
  }
  void Tick(double now){
    Time(now);
    // Bound iteration to ingress-existing tickets. Hooks may synchronously
    // deliver genuine worker death; re-find the record after each callback.
    const std::vector<uint64_t> open(open_.begin(),open_.end());
    for(const auto ticket:open){
      auto& record=*Mutable(ticket);if(record.end)continue;
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
  bool NeedsTick()const{for(const auto ticket:open_)if(!Find(ticket)->close_requested)return true;return false;}
  const std::vector<Record>& Records()const{return records_;}
  const Record* Find(uint64_t ticket)const{const auto found=tickets_.find(ticket);return found==tickets_.end()?nullptr:&records_[found->second];}
 private:
  static void Time(double now){if(!std::isfinite(now)||now<0)throw std::invalid_argument("Invalid native attachment view receipt time");}
  Record* Mutable(uint64_t ticket){const auto found=tickets_.find(ticket);return found==tickets_.end()?nullptr:&records_[found->second];}
  Hooks hooks_;Budget* budget_;uint64_t sequence_=0;size_t pending_=0;std::vector<Record> records_;
  // Run-lifetime ledger indexes: ticket, published artifact, unended tickets in issue order.
  std::map<uint64_t,size_t> tickets_;std::set<uint64_t> artifacts_,open_;
};
} // namespace suitecut::runner
