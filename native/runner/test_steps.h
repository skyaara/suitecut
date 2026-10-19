// SPDX-License-Identifier: MIT
#pragma once
#include "json_text.h"
#include "step_location.h"
#include "source_callback_iteration_receipt.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace suitecut::runner {
// Native callback identity is supplied by the runner's actual invocation lease,
// never by declaration metadata or a host's proposed parent step alone.
struct StepOwner {
  uint64_t test_id=0,attempt_generation=0,callback_token=0,scope=0,scope_generation=0;
  unsigned worker_index=0;int64_t pid=0;std::string callback_kind="body";
  auto Key()const{return std::tuple(test_id,attempt_generation,callback_token,scope,scope_generation,worker_index,pid);}
  bool operator==(const StepOwner&)const=default;
};
enum class StepStatus {Running,Passed,Failed,TimedOut,Interrupted,Skipped};
struct StepSkipCause {uint64_t request=0;std::string reason_json="\"\"";bool operator==(const StepSkipCause&)const=default;};
struct LocalStepSkipCause {uint64_t request=0,source_step=0;std::string reason_json="\"\"";bool description_present=false;bool operator==(const LocalStepSkipCause&)const=default;};
inline const char* StepStatusName(StepStatus value){switch(value){case StepStatus::Running:return "running";case StepStatus::Passed:return "passed";case StepStatus::Failed:return "failed";case StepStatus::TimedOut:return "timedOut";case StepStatus::Interrupted:return "interrupted";case StepStatus::Skipped:return "skipped";}throw std::invalid_argument("Invalid native step status");}
enum class StepReceipt {Pending,Settled,WorkerExited,NotInvoked};
struct CallbackIterationOrigin {uint64_t assertion=0,invocation=0;bool operator==(const CallbackIterationOrigin&)const=default;};
struct StepRecord {
  uint64_t id=0,parent=0;StepOwner owner;std::string title_json;
  double started=0,deadline=0,own_deadline=0;std::optional<double> ended,received;
  StepStatus status=StepStatus::Running;StepReceipt receipt=StepReceipt::Pending;
  std::string error_json="\"\"",receipt_error_json="\"\"";
  unsigned depth=0;bool receipt_ok=false;uint64_t timeout_ms=0;
  std::optional<StepSkipCause> test_skip=std::nullopt;uint64_t receipt_test_skip=0;
  std::optional<LocalStepSkipCause> step_skip=std::nullopt;uint64_t receipt_step_skip=0;
  uint64_t declared_skip_request=0;
  std::optional<double> callback_received=std::nullopt;bool callback_ok=false;
  uint64_t callback_test_skip=0,callback_step_skip=0;
  std::optional<std::string> attachment_error_json=std::nullopt;
  std::optional<StepLocation> location=std::nullopt;
  StepBox box{};
  uint64_t originating_step=0;
  uint64_t soft_error_id=0;std::string soft_error_json="\"\"";
  std::optional<CallbackIterationOrigin> callback_iteration=std::nullopt;
  std::optional<double> callback_deadline=std::nullopt;
  std::optional<SourceCallbackIterationReceipt> source_fence=std::nullopt;
};
inline const StepLocation* StepErrorAttribution(const StepRecord& step){
  if(!step.box.enabled||(step.status!=StepStatus::Failed&&step.status!=StepStatus::TimedOut&&step.status!=StepStatus::Interrupted))return nullptr;
  return step.box.caller?&*step.box.caller:step.box.callsite?&*step.box.callsite:nullptr;
}
inline const char* StepErrorAttributionKind(const StepRecord& step){return step.box.caller?"boxedCaller":"boxedCallsite";}
class TestSteps {
 public:
  static constexpr size_t kPhysical=64,kRecords=8192,kOwners=64,kDepth=64,kMetadata=64*1024*1024;
  struct Budget {uint64_t sequence=0;size_t records=0,physical=0,reserved=0;};
  explicit TestSteps(Budget* budget=nullptr):budget_(budget){}
  struct Event {uint64_t id;bool abort;StepStatus status;};
  struct State {size_t owners,physical,records,events,metadata_reserved;};
  bool Open(const StepOwner& owner,double deadline,double now){
    Time(now);if(!ValidOwner(owner)||!std::isfinite(deadline)||deadline<=0||owners_.contains(owner.Key())||owners_.size()>=kOwners)return false;
    owners_.emplace(owner.Key(),Owner{deadline,true,0});return true;
  }
  std::optional<uint64_t> Begin(const StepOwner& owner,uint64_t parent,std::string title_json,uint64_t timeout,double now,std::optional<StepLocation> location=std::nullopt,StepBox box={}){
    Time(now);TickAt(now);const auto scope=owners_.find(owner.Key());
    if(!ValidStepBoxLocation(box,location)||(location&&!ValidStepLocation(*location))||scope==owners_.end()||!scope->second.accepting||now>=scope->second.deadline||timeout>120000||!JsonString(title_json,4096)||physical_>=kPhysical||records_.size()>=kRecords||sequence_==UINT64_MAX||(budget_&&(budget_->sequence==UINT64_MAX||budget_->records>=kRecords||budget_->physical>=kPhysical)))return std::nullopt;
    unsigned depth=1;double deadline=scope->second.deadline;
    if(parent){const auto* actual=Find(parent);if(!actual||actual->owner!=owner||actual->status!=StepStatus::Running||actual->receipt!=StepReceipt::Pending)return std::nullopt;depth=actual->depth+1;deadline=std::min(deadline,actual->deadline);}
    if(depth>kDepth)return std::nullopt;
    // Reserve both maximum-sized actual error scalars before invoking author
    // code; late physical receipt can never fail for lack of ledger memory.
    const size_t reservation=title_json.size()+4*(8192*6+2)+800+(location?location->file_json.size()+64:0)+StepBoxMetadata(box);
    if(reservation>kMetadata-reserved_||(budget_&&reservation>kMetadata-budget_->reserved))return std::nullopt;
    const double own=timeout?now+static_cast<double>(timeout):std::numeric_limits<double>::max();deadline=std::min(deadline,own);
    if(!std::isfinite(own)||!std::isfinite(deadline)||deadline<=now)return std::nullopt;
    const auto id=budget_?++budget_->sequence:++sequence_;
    records_.push_back(StepRecord{id,parent,owner,std::move(title_json),now,deadline,own,std::nullopt,std::nullopt,StepStatus::Running,StepReceipt::Pending,"\"\"","\"\"",depth,false,timeout});
    try{indices_.emplace(id,records_.size()-1);}catch(...){records_.pop_back();throw;}
    records_.back().location=std::move(location);records_.back().box=std::move(box);
    ++physical_;++scope->second.physical;reserved_+=reservation;if(budget_){++budget_->physical;++budget_->records;budget_->reserved+=reservation;}return id;
  }
  // Only the native retry controller uses this admission. One record denotes
  // one real author invocation, retaining the unchanged actual callback owner.
  std::optional<uint64_t> BeginCallbackIteration(const StepOwner& owner,uint64_t parent,uint64_t assertion,uint64_t invocation,std::optional<double> own_cap,double now){
    if(!invocation||(own_cap&&(!std::isfinite(*own_cap)||*own_cap<=now)))return std::nullopt;
    if(!assertion){if(invocation!=1)return std::nullopt;}
    else{
      const auto* first=Find(assertion);
      if(!first||first->owner!=owner||first->parent!=parent||!first->callback_iteration||first->callback_iteration->assertion!=assertion||first->callback_iteration->invocation!=1||first->callback_deadline!=own_cap)return std::nullopt;
    }
    const auto id=Begin(owner,parent,"\"expect.toPass invocation\"",0,now);if(!id)return std::nullopt;
    auto& actual=*Mutable(*id);actual.callback_iteration=CallbackIterationOrigin{assertion?assertion:*id,invocation};actual.callback_deadline=own_cap;
    if(own_cap)actual.deadline=std::min(actual.deadline,*own_cap);
    return id;
  }
  bool SetCallbackSourceFence(const StepOwner& owner,uint64_t id,const SourceCallbackIterationReceipt& receipt){
    auto* step=Mutable(id);
    if(!step||step->owner!=owner||!step->callback_iteration||!step->callback_received||step->receipt!=StepReceipt::Pending||step->source_fence||
       !receipt.observed||!receipt.acknowledged||!receipt.resumed||!receipt.host_ready||!receipt.host_resumed||
       !receipt.challenge.ticket||!receipt.challenge.nonce||receipt.resume_generation<=receipt.challenge.borrow_generation)return false;
    step->source_fence=receipt;return true;
  }
  bool CancelCallbackIteration(const StepOwner& owner,uint64_t id,const std::string& error,double now){
    Time(now);if(!JsonString(error,8192))return false;TickAt(now);auto* step=Mutable(id);
    if(!step||step->owner!=owner||!step->callback_iteration||step->receipt!=StepReceipt::Pending)return false;
    if(step->status==StepStatus::Running)Finish(*step,StepStatus::Interrupted,error,now,true);
    InterruptDescendants(id,now,error);return true;
  }
  // Dedicated native declaration admission: reserve a real step identity, then
  // confirm that author callback invocation was intentionally omitted. This is
  // not a successful raw callback settlement and does not emit an ordinary
  // STEP_RESULT for a callback which never existed.
  std::optional<uint64_t> DeclareSkipped(const StepOwner& owner,uint64_t parent,std::string title_json,uint64_t timeout,uint64_t request,double now,std::optional<StepLocation> location=std::nullopt,StepBox box={}){
    if(!request)return std::nullopt;
    const auto id=Begin(owner,parent,std::move(title_json),timeout,now,std::move(location),std::move(box));if(!id)return std::nullopt;
    auto* step=Mutable(*id);step->status=StepStatus::Skipped;step->ended=now;step->receipt=StepReceipt::NotInvoked;step->received=now;step->declared_skip_request=request;
    --physical_;if(budget_)--budget_->physical;--owners_.at(owner.Key()).physical;return id;
  }
  // Native raw ingress and joint physical settlement are separate receipts.
  // A real file fence can delay Complete without inventing a later callback.
  bool Receive(const StepOwner& owner,uint64_t id,bool ok,const std::string& error_json,double now,uint64_t test_skip=0,uint64_t local_skip=0,uint64_t originating_step=0){
    Time(now);if(!JsonString(error_json,8192)||(ok&&error_json!="\"\""))return false;
    TickAt(now);auto* step=Mutable(id);if(!step||step->owner!=owner||step->receipt!=StepReceipt::Pending||step->callback_received)return false;
    if(originating_step){
      if(ok||test_skip||local_skip||originating_step==id)return false;
      const auto* source=Find(originating_step);
      if(!source||source->owner!=owner||source->status!=StepStatus::Failed||!source->callback_received||source->callback_ok)return false;
      const auto* ancestor=source;
      while(ancestor&&ancestor->parent&&ancestor->parent!=id)ancestor=Find(ancestor->parent);
      if(!ancestor||ancestor->parent!=id)return false;
    }
    if(test_skip&&local_skip)return false;
    if(local_skip&&(!step->step_skip||step->step_skip->request!=local_skip||!ok||error_json!="\"\""))return false;
    if(test_skip&&(!step->test_skip||step->test_skip->request!=test_skip||!ok||error_json!="\"\""))return false;
    step->originating_step=originating_step;step->callback_received=now;step->callback_ok=ok;step->receipt_error_json=error_json;
    step->callback_test_skip=test_skip;step->callback_step_skip=local_skip;
    if(!ok&&step->status==StepStatus::Running)Finish(*step,StepStatus::Failed,error_json,now,false);
    InterruptDescendants(id,now,"\"Native parent step callback settled before its descendant\"");return true;
  }
  bool NoteSoftFailure(const StepOwner& owner,uint64_t id,uint64_t error_id,const std::string& error,double now){
    Time(now);if(!error_id||!JsonString(error,8192))return false;TickAt(now);
    const auto* source=Find(id);if(!source||source->owner!=owner||source->status!=StepStatus::Running||source->receipt!=StepReceipt::Pending)return false;
    for(const auto* step=source;step;step=step->parent?Find(step->parent):nullptr)if(step->owner!=owner||step->status!=StepStatus::Running||step->receipt!=StepReceipt::Pending)return false;
    for(auto* step=Mutable(id);step;step=step->parent?Mutable(step->parent):nullptr)if(!step->soft_error_id){step->soft_error_id=error_id;step->soft_error_json=error;}
    return true;
  }
  bool Complete(const StepOwner& owner,uint64_t id,double now,const std::optional<std::string>& attachment_error=std::nullopt){
    Time(now);if(attachment_error&&!JsonString(*attachment_error,8192))return false;
    TickAt(now);auto* step=Mutable(id);if(!step||step->owner!=owner||step->receipt!=StepReceipt::Pending||!step->callback_received)return false;
    step->attachment_error_json=attachment_error;step->receipt=StepReceipt::Settled;step->received=now;
    step->receipt_ok=step->callback_ok;step->receipt_test_skip=step->callback_test_skip;step->receipt_step_skip=step->callback_step_skip;
    --physical_;if(budget_)--budget_->physical;--owners_.at(owner.Key()).physical;
    if(step->status==StepStatus::Running)Finish(*step,(attachment_error||step->soft_error_id)?StepStatus::Failed:StepStatus::Passed,attachment_error.value_or(step->soft_error_id?step->soft_error_json:"\"\""),now,false);
    return true;
  }
  bool Settle(const StepOwner& owner,uint64_t id,bool ok,const std::string& error_json,double now,uint64_t test_skip=0,uint64_t local_skip=0,uint64_t originating_step=0){
    return Receive(owner,id,ok,error_json,now,test_skip,local_skip,originating_step)&&Complete(owner,id,now);
  }
  // An original test/hook deadline amendment is authoritative native state.
  // A step's own deadline and all actual ancestor deadlines remain caps.
  bool UpdateOwnerDeadline(const StepOwner& owner,double deadline,double now){
    Time(now);TickAt(now);const auto scope=owners_.find(owner.Key());if(scope==owners_.end()||!scope->second.accepting||now>=scope->second.deadline||!std::isfinite(deadline)||deadline<0)return false;
    scope->second.deadline=deadline;
    for(auto& step:records_)if(step.owner==owner&&step.status==StepStatus::Running){step.deadline=std::min(deadline,step.own_deadline);if(step.callback_deadline)step.deadline=std::min(step.deadline,*step.callback_deadline);if(step.parent)step.deadline=std::min(step.deadline,Find(step.parent)->deadline);}
    TickAt(now);return true;
  }
  bool Close(const StepOwner& owner,double now,const std::string& error_json="\"Native owning callback settled before its step\""){
    Time(now);if(!JsonString(error_json,8192))return false;TickAt(now);const auto scope=owners_.find(owner.Key());if(scope==owners_.end()||!scope->second.accepting)return false;scope->second.accepting=false;
    for(auto& step:records_)if(step.owner==owner&&step.status==StepStatus::Running)Finish(step,StepStatus::Interrupted,error_json,now,true);
    return true;
  }
  // Called only after the runner has accepted this exact native test-skip
  // mutation. The cause closes admission; it does not acknowledge raw work.
  bool SkipTestCallback(const StepOwner& owner,uint64_t request,const std::string& reason_json,double now){
    Time(now);if(!request||!JsonString(reason_json,8192))return false;TickAt(now);
    const auto scope=owners_.find(owner.Key());if(scope==owners_.end()||!scope->second.accepting||now>=scope->second.deadline)return false;
    scope->second.accepting=false;scope->second.test_skip=StepSkipCause{request,reason_json};
    for(auto& step:records_)if(step.owner==owner&&step.receipt==StepReceipt::Pending){step.test_skip=scope->second.test_skip;if(step.status==StepStatus::Running)Finish(step,StepStatus::Skipped,"\"\"",now,true);}
    return true;
  }
  // Native local-step skip never changes the owning test expectation or
  // sibling/ancestor admission. It cancels only this real admitted subtree.
  bool SkipStep(const StepOwner& owner,uint64_t id,uint64_t request,const std::string& reason_json,bool description_present,double now){
    Time(now);if(!request||!JsonString(reason_json,8192)||(!description_present&&reason_json!="\"\""))return false;TickAt(now);
    const auto scope=owners_.find(owner.Key());const auto* source=Find(id);
    if(scope==owners_.end()||!scope->second.accepting||now>=scope->second.deadline||!source||source->owner!=owner||source->status!=StepStatus::Running||source->receipt!=StepReceipt::Pending)return false;
    const LocalStepSkipCause cause{request,id,reason_json,description_present};
    for(auto& step:records_)if(step.owner==owner&&step.receipt==StepReceipt::Pending&&Within(step.id,id)&&!step.step_skip){step.step_skip=cause;if(step.status==StepStatus::Running)Finish(step,StepStatus::Skipped,"\"\"",now,true);}
    return true;
  }
  bool Retire(const StepOwner& owner){const auto scope=owners_.find(owner.Key());if(scope==owners_.end()||scope->second.accepting||scope->second.physical)return false;owners_.erase(scope);return true;}
  void Tick(double now){Time(now);TickAt(now);}
  // Call only after the actual waitpid-confirmed worker exit. Sending abort,
  // killing a PID, closing a channel or a logical timeout is not this receipt.
  bool ConfirmedWorkerExit(unsigned worker_index,int64_t pid,double now){
    Time(now);if(pid<=0)return false;TickAt(now);bool found=false;
    for(auto& step:records_)if(step.owner.worker_index==worker_index&&step.owner.pid==pid&&step.receipt==StepReceipt::Pending){
      found=true;if(step.status==StepStatus::Running)Finish(step,StepStatus::Interrupted,"\"Actual native worker exited before step callback receipt\"",now,false);
      step.receipt=StepReceipt::WorkerExited;step.received=now;--physical_;if(budget_)--budget_->physical;--owners_.at(step.owner.Key()).physical;
    }
    for(auto&[key,owner]:owners_){if(std::get<5>(key)==worker_index&&std::get<6>(key)==pid)owner.accepting=false;}
    return found;
  }
  const std::vector<StepRecord>& Records()const{return records_;}
  const StepRecord* Find(uint64_t id)const{const auto found=indices_.find(id);return found==indices_.end()?nullptr:&records_[found->second];}
  std::vector<Event> TakeEvents(){auto events=std::move(events_);events_.clear();return events;}
  State Inspect()const{return{owners_.size(),physical_,records_.size(),events_.size(),reserved_};}
 private:
  using Key=decltype(StepOwner{}.Key());struct Owner {double deadline;bool accepting;size_t physical;std::optional<StepSkipCause> test_skip=std::nullopt;};
  static bool ValidOwner(const StepOwner& owner){return owner.test_id&&owner.attempt_generation&&owner.scope&&owner.scope_generation&&owner.pid>0&&owner.pid<=INT32_MAX&&(owner.callback_kind=="body"||owner.callback_kind=="beforeEach"||owner.callback_kind=="afterEach"||owner.callback_kind=="beforeAll"||owner.callback_kind=="afterAll")&&(owner.callback_kind=="body"||owner.callback_token);}
  bool Within(uint64_t id,uint64_t root)const{for(const auto* node=Find(id);node;node=Find(node->parent))if(node->id==root)return true;return false;}
  void Time(double now){if(!std::isfinite(now)||now<0||now<last_now_)throw std::invalid_argument("Native step clock must be finite and monotonic");last_now_=now;}
  StepRecord* Mutable(uint64_t id){const auto found=indices_.find(id);return found==indices_.end()?nullptr:&records_[found->second];}
  void Finish(StepRecord& step,StepStatus status,const std::string& error,double now,bool abort){
    if(step.status!=StepStatus::Running)return;
    step.status=status;step.ended=now;step.error_json=error;events_.push_back({step.id,abort,status});
  }
  void TickAt(double now){for(auto& step:records_)if(step.status==StepStatus::Running&&now>=step.deadline){
    const bool own=step.own_deadline==step.deadline&&step.own_deadline<owners_.at(step.owner.Key()).deadline&&(!step.parent||step.own_deadline<Find(step.parent)->deadline);
    Finish(step,StepStatus::TimedOut,own?"\"Step timeout of "+std::to_string(step.timeout_ms)+"ms exceeded.\"":"\"Native owning callback or ancestor original deadline exceeded\"",now,true);
  }}
  void InterruptDescendants(uint64_t parent,double now,const std::string& reason){for(auto& step:records_)if(step.parent==parent){if(step.status==StepStatus::Running)Finish(step,StepStatus::Interrupted,reason,now,true);InterruptDescendants(step.id,now,reason);}}
  std::map<Key,Owner> owners_;std::vector<StepRecord> records_;std::vector<Event> events_;
  Budget* budget_;std::map<uint64_t,size_t> indices_;
  uint64_t sequence_=0;size_t physical_=0,reserved_=0;double last_now_=0;
};
} // namespace suitecut::runner
