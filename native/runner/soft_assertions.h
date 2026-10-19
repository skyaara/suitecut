// SPDX-License-Identifier: MIT
#pragma once
#include "test_steps.h"
#include <functional>
namespace suitecut::runner {
enum class SoftRawOutcome {Passed,RecognizedMismatch,RecognizedDeadline,OtherFailure};
enum class SoftReceipt {Mismatch,AcceptedDeadline,OtherFailure,Late,Canceled,WorkerExited};
inline bool IsAcceptedSoftFailure(SoftReceipt receipt)noexcept{return receipt==SoftReceipt::Mismatch||receipt==SoftReceipt::AcceptedDeadline;}
struct SoftAssertionRecord {
  uint64_t id=0,request=0,step=0;StepOwner owner;
  std::optional<StepLocation> location;
  double admitted=0,received=0;
  SoftRawOutcome raw=SoftRawOutcome::Passed;SoftReceipt receipt=SoftReceipt::OtherFailure;
  std::string error_json="\"\"",cancel_reason_json="\"\"";
  bool cleanup=false;
};
// Raw matcher leases, not a matcher implementation or host scheduling policy.
// Begin precedes author matcher execution. Only an actual matcher settlement or
// a runner-supplied exact waitpid receipt retires the native operation lease.
class SoftAssertions {
 public:
  static constexpr size_t kInflight=64,kPerAttempt=32,kRecords=8192,kMetadata=16*1024*1024,kError=8192*6+2;
  struct Budget {uint64_t sequence=0;size_t physical=0,records=0,metadata=0,reserved=0;std::map<uint64_t,size_t> failures,pending_by_attempt;std::map<uint64_t,std::tuple<uint64_t,unsigned,int64_t>> attempt_owners;};
  enum class Verdict {Passed,Mismatch,AcceptedDeadline,OtherFailure,Late,Canceled};
  static bool IsAcceptedFailure(Verdict verdict)noexcept{return verdict==Verdict::Mismatch||verdict==Verdict::AcceptedDeadline;}
  struct Admission {uint64_t id;double deadline,remaining;};
  explicit SoftAssertions(std::function<const StepRecord*(uint64_t)> step={},Budget* budget=nullptr)
    :step_(std::move(step)),budget_(budget?budget:&local_){records_.reserve(kRecords);}
  void Open(const StepOwner& owner,double deadline,double now){
    Time(now);if(!Valid(owner)||!std::isfinite(deadline)||deadline<=now||contexts_.size()>=64||contexts_.contains(owner.Key()))throw std::invalid_argument("Native soft callback context rejected");
    if(!budget_->failures.contains(owner.attempt_generation)&&budget_->failures.size()>=kRecords)throw std::runtime_error("Native soft attempt catalog capacity exceeded");
    const auto identity=std::tuple(owner.test_id,owner.worker_index,owner.pid);const auto known=budget_->attempt_owners.find(owner.attempt_generation);
    if(known!=budget_->attempt_owners.end()&&known->second!=identity)throw std::invalid_argument("Native soft attempt generation has another actual owner");
    budget_->attempt_owners.try_emplace(owner.attempt_generation,identity);budget_->failures.try_emplace(owner.attempt_generation,0);budget_->pending_by_attempt.try_emplace(owner.attempt_generation,0);contexts_.emplace(owner.Key(),Context{owner,deadline,true});
  }
  Admission Begin(const StepOwner& owner,uint64_t request,uint64_t step,std::optional<StepLocation> location,double now){
    Time(now);auto& context=Owned(owner);
    if(!request||!context.open||now>=context.deadline||(location&&(!ValidStepLocation(*location)||location->explicit_location)))throw std::invalid_argument("Native soft admission is stale, expired or has invalid captured location");
    for(const auto&[id,lease]:leases_){(void)id;if(lease.owner.worker_index==owner.worker_index&&lease.owner.pid==owner.pid&&lease.request==request)throw std::invalid_argument("Native soft request already has an actual lease");}
    double deadline=context.deadline;
    if(step){const auto* actual=step_?step_(step):nullptr;if(!actual||actual->id!=step||actual->owner!=owner||actual->status!=StepStatus::Running||actual->receipt!=StepReceipt::Pending||actual->callback_received)throw std::invalid_argument("Native soft admission step is foreign or not running");deadline=std::min(deadline,actual->deadline);}
    if(deadline<=now)throw std::invalid_argument("Native soft step original deadline expired");
    const size_t reservation=2*kError+1024+(location?location->file_json.size():0)+owner.callback_kind.size();
    if(budget_->sequence==UINT64_MAX||budget_->physical>=kInflight||budget_->records+budget_->physical>=kRecords||budget_->failures.at(owner.attempt_generation)+budget_->pending_by_attempt.at(owner.attempt_generation)>=kPerAttempt||budget_->metadata>kMetadata-budget_->reserved||reservation>kMetadata-budget_->reserved-budget_->metadata)throw std::runtime_error("Native soft journal capacity exceeded");
    Lease lease;lease.id=budget_->sequence+1;lease.request=request;lease.step=step;lease.owner=owner;lease.location=std::move(location);lease.admitted=now;lease.reservation=reservation;lease.error_json.reserve(kError);lease.cancel_reason_json.reserve(kError);
    const auto id=lease.id;leases_.emplace(id,std::move(lease));++budget_->sequence;++budget_->physical;++budget_->pending_by_attempt.at(owner.attempt_generation);budget_->reserved+=reservation;
    return{id,deadline,deadline-now};
  }
  void CloseAdmission(const StepOwner& owner){Owned(owner).open=false;}
  void Amend(const StepOwner& owner,double deadline,double now){
    Time(now);auto& context=Owned(owner);if(!context.open||now>=context.deadline||!std::isfinite(deadline)||deadline<0)throw std::invalid_argument("Native soft original deadline amendment rejected");context.deadline=deadline;
  }
  template<class Abort>void Tick(double now,Abort&& abort){
    Time(now);
    for(auto&[id,lease]:leases_)if(!lease.aborted&&!lease.received){
      const auto* step=lease.step&&step_?step_(lease.step):nullptr;
      if(now>=Deadline(lease)||(lease.step&&(!step||step->status!=StepStatus::Running))){
        lease.aborted=true;lease.cancel_reason_json="\"Native soft original callback or step deadline/cancellation\"";abort(id,lease.owner,lease.step,lease.cancel_reason_json);
      }
    }
  }
  template<class Abort>void Cancel(const StepOwner& owner,const std::string& reason,double now,Abort&& abort){
    Time(now);if(!JsonString(reason,8192))throw std::invalid_argument("Native soft cancel reason rejected");Owned(owner).open=false;
    for(auto&[id,lease]:leases_)if(lease.owner==owner&&!lease.aborted&&!lease.received){lease.aborted=true;lease.cancel_reason_json=reason;abort(id,owner,lease.step,reason);}
  }
  Verdict Settled(const StepOwner& owner,uint64_t id,SoftRawOutcome raw,const std::string& error,double now){
    Time(now);auto found=leases_.find(id);if(found==leases_.end()||found->second.owner!=owner||found->second.received||!JsonString(error,8192)||
      (raw!=SoftRawOutcome::Passed&&raw!=SoftRawOutcome::RecognizedMismatch&&raw!=SoftRawOutcome::RecognizedDeadline&&raw!=SoftRawOutcome::OtherFailure)||(raw==SoftRawOutcome::Passed&&error!="\"\""))throw std::invalid_argument("Native soft matcher receipt is forged, duplicate or invalid");
    auto& lease=found->second;const auto deadline=Deadline(lease);const auto* actual_step=lease.step&&step_?step_(lease.step):nullptr;
    if(lease.step&&actual_step&&actual_step->status!=StepStatus::Running&&!lease.aborted){lease.aborted=true;lease.cancel_reason_json="\"Actual native step ended before matcher receipt\"";}
    lease.received=now;lease.raw=raw;lease.error_json=error;
    const auto verdict=now>=deadline?Verdict::Late:lease.aborted?Verdict::Canceled:raw==SoftRawOutcome::RecognizedMismatch?Verdict::Mismatch:raw==SoftRawOutcome::RecognizedDeadline?Verdict::AcceptedDeadline:raw==SoftRawOutcome::OtherFailure?Verdict::OtherFailure:Verdict::Passed;
    if(verdict!=Verdict::Passed){
      const auto receipt=verdict==Verdict::Late?SoftReceipt::Late:verdict==Verdict::Canceled?SoftReceipt::Canceled:verdict==Verdict::Mismatch?SoftReceipt::Mismatch:verdict==Verdict::AcceptedDeadline?SoftReceipt::AcceptedDeadline:SoftReceipt::OtherFailure;
      Store(lease,receipt);if(IsAcceptedFailure(verdict))++budget_->failures.at(owner.attempt_generation);
    }
    Retire(found);return verdict;
  }
  // Caller must already have the real waitpid receipt for this exact host.
  void ConfirmedWorkerExit(unsigned worker,int64_t pid,double now){
    Time(now);if(!worker||pid<=0)throw std::invalid_argument("Native soft worker exit identity rejected");
    for(auto it=leases_.begin();it!=leases_.end();){auto& lease=it->second;if(lease.owner.worker_index!=worker||lease.owner.pid!=pid){++it;continue;}
      if(!lease.received){lease.received=now;lease.error_json="\"Actual native host exited before soft matcher settlement\"";lease.raw=SoftRawOutcome::OtherFailure;}
      Store(lease,SoftReceipt::WorkerExited);auto retired=it++;Retire(retired);
    }
    for(auto&[key,context]:contexts_){(void)key;if(context.owner.worker_index==worker&&context.owner.pid==pid)context.open=false;}
  }
  bool Pending(const StepOwner& owner)const{for(const auto&[id,lease]:leases_){(void)id;if(lease.owner==owner)return true;}return false;}
  bool PendingStep(const StepOwner& owner,uint64_t root)const{for(const auto&[id,lease]:leases_){(void)id;if(lease.owner==owner&&Within(lease.step,root))return true;}return false;}
  bool FailedAttempt(uint64_t generation)const{const auto found=budget_->failures.find(generation);return found!=budget_->failures.end()&&found->second;}
  const SoftAssertionRecord* FirstFailure(const StepOwner& owner)const{for(const auto& record:records_)if(record.owner==owner&&IsAcceptedSoftFailure(record.receipt))return &record;return nullptr;}
  void RetireContext(const StepOwner& owner){const auto& context=Owned(owner);if(context.open||Pending(owner))throw std::runtime_error("Native soft callback still owns admission or raw matcher work");contexts_.erase(owner.Key());}
  size_t Physical()const{return leases_.size();}
  bool IdleReady()const{return leases_.empty()&&contexts_.empty();}
  const std::vector<SoftAssertionRecord>& Records()const{return records_;}
 private:
  using Key=decltype(StepOwner{}.Key());
  struct Context {StepOwner owner;double deadline;bool open;};
  struct Lease {uint64_t id=0,request=0,step=0;StepOwner owner;std::optional<StepLocation> location;double admitted=0;std::optional<double> received;SoftRawOutcome raw=SoftRawOutcome::Passed;bool aborted=false;size_t reservation=0;std::string error_json="\"\"",cancel_reason_json="\"\"";};
  void Time(double now){if(!std::isfinite(now)||now<last_now_)throw std::invalid_argument("Native soft observation time rejected");last_now_=now;}
  static bool Valid(const StepOwner& owner){return owner.test_id&&owner.attempt_generation&&owner.scope&&owner.scope_generation&&owner.worker_index&&owner.pid>0&&owner.pid<=INT32_MAX&&(owner.callback_kind=="body"||owner.callback_kind=="beforeEach"||owner.callback_kind=="afterEach")&&(owner.callback_kind=="body"||owner.callback_token);}
  Context& Owned(const StepOwner& owner){const auto it=contexts_.find(owner.Key());if(it==contexts_.end()||it->second.owner!=owner)throw std::invalid_argument("Native soft callback context is stale or foreign");return it->second;}
  double Deadline(const Lease& lease)const{const auto context=contexts_.find(lease.owner.Key());if(context==contexts_.end())throw std::logic_error("Native soft physical lease lost callback owner");double deadline=context->second.deadline;if(lease.step){const auto* actual=step_?step_(lease.step):nullptr;if(!actual||actual->owner!=lease.owner)throw std::logic_error("Native soft physical lease lost actual step owner");deadline=std::min(deadline,actual->deadline);}return deadline;}
  bool Within(uint64_t id,uint64_t root)const{const auto* step=id&&step_?step_(id):nullptr;for(size_t depth=0;step&&depth<=TestSteps::kDepth;++depth,step=step->parent&&step_?step_(step->parent):nullptr)if(step->id==root)return true;return false;}
  void Store(const Lease& lease,SoftReceipt receipt){
    SoftAssertionRecord record{lease.id,lease.request,lease.step,lease.owner,lease.location,lease.admitted,*lease.received,lease.raw,receipt,lease.error_json,lease.cancel_reason_json,lease.owner.callback_kind=="afterEach"};
    const size_t charge=record.error_json.capacity()+record.cancel_reason_json.capacity()+record.owner.callback_kind.capacity()+(record.location?record.location->file_json.capacity():0)+1024;
    if(charge>lease.reservation||charge>kMetadata-budget_->metadata||records_.size()>=kRecords)throw std::runtime_error("Native soft exact error record capacity exceeded; raw receipt retained");
    records_.push_back(std::move(record));++budget_->records;budget_->metadata+=charge;
  }
  void Retire(std::map<uint64_t,Lease>::iterator it){budget_->reserved-=it->second.reservation;--budget_->physical;--budget_->pending_by_attempt.at(it->second.owner.attempt_generation);leases_.erase(it);}
  std::function<const StepRecord*(uint64_t)> step_;Budget local_;Budget* budget_;
  std::map<Key,Context> contexts_;std::map<uint64_t,Lease> leases_;std::vector<SoftAssertionRecord> records_;double last_now_=0;
};
}
