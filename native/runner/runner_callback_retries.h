// SPDX-License-Identifier: MIT
#pragma once
#include "callback_retry_record.h"
#include "callback_retry_schedule.h"
#include "step_wire.h"
#include "test_attachments.h"
#include <charconv>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <sstream>

namespace suitecut::runner {
// Actual host callback adapter. Native scheduling, managed steps and joint
// physical fences are independent from the SDK's authored Promise outcome.
class RunnerCallbackRetries {
 public:
  using Schedule=CallbackRetrySchedule;
  enum class SourceFence {Pending,Ready,Failed};
  struct Hooks {
    std::function<void(const std::string&)> send;
    std::function<uint64_t()> next_step;
    std::function<std::optional<uint64_t>(const StepOwner&,uint64_t,uint64_t,uint64_t,std::optional<double>,double)> begin;
    std::function<void(const StepOwner&,uint64_t,uint64_t,bool,const std::string&,uint64_t,std::function<void(const std::optional<std::string>&)>,double)> settled;
    std::function<void(const StepOwner&,uint64_t,const std::string&,double)> cancel;
    std::function<void(const StepOwner&,const AttachmentRetryProvenance&,std::function<void(const std::optional<std::string>&)>,double)> files;
    std::function<SourceFence(const StepOwner&,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,double,double)> source;
    std::function<const StepRecord*(uint64_t)> step;
    std::function<void(const StepOwner&,const std::string&)> hard;
    std::function<void(const std::string&)> terminate;
    std::function<double()> now;
    std::function<bool(const StepOwner&)> infrastructure;
  };
  RunnerCallbackRetries(Hooks hooks,TestSteps::Budget& budget):hooks_(std::move(hooks)),budget_(budget){}
  void WorkerStarted(){if(!IdleReady())throw std::runtime_error("Native callback retries still own worker callbacks");nonce_=0;}
  void Open(const StepOwner& owner,double deadline,double now){
    if(contexts_.size()>=TestSteps::kOwners||contexts_.contains(Key(owner)))throw std::runtime_error("Native callback retry context capacity/identity rejected");
    if(owner.callback_kind!="body"&&owner.callback_kind!="beforeEach"&&owner.callback_kind!="afterEach")return;
    if(!std::isfinite(deadline)||deadline<=now)throw std::runtime_error("Native callback retry context original cap rejected");
    contexts_.emplace(Key(owner),Context{owner,deadline});
    hooks_.send("TOPASS_CONTEXT "+Identity(owner)+" "+N(owner.test_id)+" "+N(owner.attempt_generation)+" "+N(owner.worker_index)+" "+N(static_cast<uint64_t>(owner.pid))+" "+owner.callback_kind);
  }
  void Amend(const StepOwner& owner,double deadline,double now){
    auto* c=Find(owner);if(!c||c->closed)return;
    if(!std::isfinite(deadline)||now>=c->deadline)throw std::runtime_error("Native callback retry owner amendment expired");
    c->deadline=deadline;
    for(auto&[id,op]:operations_)if(op.record.owner==owner){(void)id;op.schedule->AmendOwner(OwnerDeadline(op),now);}
  }
  void Cancel(const StepOwner& owner,double now){for(auto&[id,op]:operations_)if(op.record.owner==owner){op.schedule->Cancel(now);ObserveLogical(id,now);}}
  void CancelAll(double now){for(auto&[id,op]:operations_){op.schedule->Cancel(now);ObserveLogical(id,now);}}
  void Fence(const StepOwner& owner,std::function<void(const std::optional<std::string>&)> forward,double now){
    auto* c=Find(owner);if(!c){forward(std::nullopt);return;}
    if(c->forward)throw std::runtime_error("Native callback retry outer raw receipt is duplicate");
    c->closed=true;c->forward=std::move(forward);
    for(auto&[id,op]:operations_)if(op.record.owner==owner){
      if(op.schedule->Result()==Schedule::Verdict::Pending&&!c->error)c->error="\"Owning callback settled before its native toPass operation\"";
      op.schedule->Cancel(now);ObserveLogical(id,now);
    }
    DrainContexts();
  }
  void FenceStep(const StepOwner& owner,uint64_t step,std::function<void(const std::optional<std::string>&)> forward,double now){
    const auto* actual=hooks_.step(step);
    if(!forward||!actual||actual->owner!=owner||actual->callback_iteration||step_fences_.contains(step))throw std::runtime_error("Native callback retry parent fence is foreign or duplicate");
    if(!PendingWithin(owner,step)){forward(std::nullopt);return;}
    if(step_fences_.size()>=TestSteps::kPhysical)throw std::runtime_error("Native callback retry parent fence capacity exceeded");
    auto [entry,inserted]=step_fences_.emplace(step,StepFence{owner,std::move(forward)});(void)inserted;
    for(auto&[id,op]:operations_)if(op.record.owner==owner&&Within(op.record.parent,step)){
      if(op.schedule->Result()==Schedule::Verdict::Pending&&!entry->second.error)entry->second.error="\"Owning step settled before its native toPass operation\"";
      op.schedule->Cancel(now);ObserveLogical(id,now);
    }
    DrainStepFences();
  }
  bool CanSkip(const StepOwner& owner,uint64_t step=0)const{return !PendingWithin(owner,step);}
  bool HasSourceFenceOwner(const StepOwner& owner,uint64_t request,uint64_t assertion,uint64_t invocation,uint64_t iteration,uint64_t parent)const{
    const auto found=operations_.find(assertion);if(found==operations_.end())return false;
    const auto& op=found->second;const auto& actual=op.schedule->Actual();const auto* step=hooks_.step(iteration);
    return op.record.owner==owner&&op.record.request==request&&op.record.parent==parent&&op.schedule->Physical()&&actual.raw_received&&
      actual.invocation.nonce==invocation&&actual.invocation.step==iteration&&!op.front_pending&&op.files_ready&&
      step&&step->owner==owner&&step->callback_iteration&&step->callback_iteration->assertion==assertion&&
      step->callback_iteration->invocation==invocation&&step->callback_received&&step->receipt==StepReceipt::Pending;
  }
  // Called by the actual managed-step physical hook, after descendant raw
  // step receipts. The original raw receipt was already recorded separately.
  void FenceIteration(const StepOwner& owner,uint64_t step,std::function<void(const std::optional<std::string>&)> forward,double now){
    auto* op=Iteration(owner,step);
    if(!op||!op->schedule->Actual().raw_received||op->forward)throw std::runtime_error("Native callback retry joint fence lacks exact raw invocation");
    // Raw/child/file callbacks may run inside an older pinned ingress event.
    // The coordinator's fresh physical stage owns advancement, not this latch.
    op->forward=std::move(forward);(void)now;
  }
  bool OwnsIteration(const StepOwner& owner,uint64_t step)const{for(const auto&[id,op]:operations_){(void)id;if(op.record.owner==owner&&op.schedule->Actual().invocation.step==step&&op.schedule->Physical())return true;}return false;}
  bool ValidateRetry(const StepOwner& owner,uint64_t public_step,const AttachmentRetryProvenance& p,double now,bool continuation)const{
    const auto found=operations_.find(p.assertion_id);if(found==operations_.end())return false;
    const auto& op=found->second;const auto& actual=op.schedule->Actual();
    if(op.record.owner!=owner||!op.schedule->Physical()||actual.invocation.nonce!=p.invocation_nonce||actual.invocation.step!=p.iteration_step)return false;
    const auto* native=hooks_.step(p.iteration_step);
    if(!native||native->owner!=owner||!native->callback_iteration||native->callback_iteration->assertion!=p.assertion_id||native->callback_iteration->invocation!=p.invocation_nonce||native->receipt!=StepReceipt::Pending)return false;
    if(public_step){const auto* published=hooks_.step(public_step);if(!published||published->owner!=owner||published->receipt!=StepReceipt::Pending)return false;}
    if(continuation)return true;
    const auto context=contexts_.find(Key(owner));
    return context!=contexts_.end()&&!context->second.closed&&!actual.raw_received&&op.schedule->Result()==Schedule::Verdict::Pending&&now<op.schedule->Deadline()&&native->status==StepStatus::Running;
  }
  void FileFailure(const StepOwner& owner,const AttachmentRetryProvenance& p,const std::string& error,bool infrastructure,double now){
    if(!ValidateRetry(owner,0,p,now,true)||!JsonString(error,8192))throw std::runtime_error("Native callback retry file failure has stale ownership");
    auto& op=operations_.at(p.assertion_id);
    if(infrastructure){op.schedule->FailInfrastructure(now);HardCause(op,error);ObserveLogical(op.record.id,now);}
    else if(!op.file_error)op.file_error=error;
  }
  bool Packet(const std::vector<std::string>& f,double now,bool stopped,bool cleanup_admitted=false){
    if(f.empty()||!f[0].starts_with("TOPASS_"))return false;
    if(f[0]=="TOPASS_BEGIN"){Begin(f,now,stopped,cleanup_admitted);return true;}
    if(f.size()<6)throw std::runtime_error("Native callback retry receipt shape invalid");
    auto& op=Exact(f);
    if(f[0]=="TOPASS_CANCEL"){
      Size(f,6);op.schedule->Cancel(now);ObserveLogical(op.record.id,now);return true;
    }
    if(f.size()<8||UInt(f[6])!=op.schedule->Actual().invocation.nonce||UInt(f[7])!=op.schedule->Actual().invocation.step||!op.schedule->Physical())throw std::runtime_error("Native callback retry invocation receipt is stale");
    if(f[0]=="TOPASS_PRODUCER_FAILURE"){
      Size(f,10);SetSource(op,UInt(f[8]));op.schedule->FailInfrastructure(now);HardCause(op,RuntimeSkipReason(f[9]));ObserveLogical(op.record.id,now);
    }else if(f[0]=="TOPASS_OPERATIONS_RETIRED"){
      Size(f,9);if(!op.schedule->Actual().raw_received||!op.front_pending)throw std::runtime_error("Native callback retry operation retirement is duplicate or has no pending operations");
      SetSource(op,UInt(f[8]));op.front_pending=0;
    }else if(f[0]=="TOPASS_RAW"){
      Size(f,13);const auto raw=f[8]=="P"?Schedule::Raw::Returned:f[8]=="E"?Schedule::Raw::Threw:f[8]=="I"?Schedule::Raw::InfrastructureFailure:throw std::runtime_error("Native callback retry raw outcome rejected");
      const auto error=RuntimeSkipReason(f[9]);if(raw==Schedule::Raw::Returned&&error!="\"\"")throw std::runtime_error("Native callback retry raw pass has an error");
      const auto source=UInt(f[10]);SetSource(op,UInt(f[11]));op.front_pending=UInt(f[12]);if(op.front_pending>128)throw std::runtime_error("Native callback retry captured operation capacity exceeded");
      op.schedule->RawSettled(op.schedule->Actual().invocation,raw,now);CleanupOrigin(op,now);
      if(raw==Schedule::Raw::InfrastructureFailure)HardCause(op,error);
      else if(raw==Schedule::Raw::Threw&&!op.hard_error)op.error=error;
      const auto id=op.record.id;ObserveLogical(id,now);
      hooks_.settled(op.record.owner,op.schedule->Actual().invocation.step,op.schedule->Actual().invocation.nonce,raw==Schedule::Raw::Returned,error,source,[this,id](const auto& failure){Physical(id,failure,hooks_.now());},now);
    }else throw std::runtime_error("Unknown native callback retry packet");
    return true;
  }
  void Tick(double now){
    // A snapshot of IDs permits synchronous physical callbacks to retire an
    // operation; it is bounded by the existing real native step admission cap.
    std::array<uint64_t,TestSteps::kPhysical> ids{};size_t count=0;
    for(const auto&[id,op]:operations_){(void)op;if(count==ids.size())throw std::runtime_error("Native callback retry physical admission invariant failed");ids[count++]=id;}
    bool dispatched=false;
    const auto split=static_cast<size_t>(std::upper_bound(ids.begin(),ids.begin()+count,last_dispatched_)-ids.begin());
    for(size_t i=0;i<count;++i){const auto id=ids[(split+i)%count];auto found=operations_.find(id);if(found==operations_.end())continue;auto& op=found->second;
      if(hooks_.infrastructure&&hooks_.infrastructure(op.record.owner))op.schedule->FailInfrastructure(now);
      const auto deadline=OwnerDeadline(op);if(deadline!=op.owner_deadline){op.schedule->AmendOwner(deadline,now);op.owner_deadline=deadline;}
      op.schedule->Tick(now);ObserveLogical(id,now);Progress(id,now);
      found=operations_.find(id);if(found==operations_.end())continue;
      if(found->second.cleanup_end&&found->second.schedule->Physical()&&now>=*found->second.cleanup_end&&!found->second.termination_requested){found->second.termination_requested=true;hooks_.terminate("Native toPass raw/descendant/Source physical retirement deadline expired");}
      if(!dispatched&&found->second.schedule->Ready(now)){Dispatch(found->second,now);dispatched=true;}
      else if(found->second.schedule->Result()!=Schedule::Verdict::Pending&&!found->second.schedule->Physical())Finalize(id,now,CallbackAssertionReceipt::Settled);
    }
    DrainContexts();
  }
  std::optional<double> NextWake()const{
    std::optional<double> wake;for(const auto&[id,op]:operations_){(void)id;const auto next=op.schedule->NextWake();if(next)wake=wake?std::min(*wake,*next):next;if(op.cleanup_end&&op.schedule->Physical())wake=wake?std::min(*wake,*op.cleanup_end):op.cleanup_end;}return wake;
  }
  void WorkerExited(unsigned worker,int64_t pid,double now){
    for(auto it=contexts_.begin();it!=contexts_.end();)if(it->second.owner.worker_index==worker&&it->second.owner.pid==pid)it=contexts_.erase(it);else ++it;
    for(auto it=step_fences_.begin();it!=step_fences_.end();)if(it->second.owner.worker_index==worker&&it->second.owner.pid==pid)it=step_fences_.erase(it);else ++it;
    for(auto it=operations_.begin();it!=operations_.end();){const auto id=it->first;++it;auto& op=operations_.at(id);
      if(op.record.owner.worker_index!=worker||op.record.owner.pid!=pid)continue;
      if(!op.schedule->Attempts()){operations_.erase(id);continue;}
      const bool confirmed=op.schedule->ConfirmedWorkerExit(worker,pid,now);
      if(!confirmed&&(op.schedule->Physical()||op.schedule->Result()==Schedule::Verdict::Pending))throw std::runtime_error("Native callback retry actual exit left an unreconciled invocation");
      Finalize(id,now,CallbackAssertionReceipt::WorkerExited);
    }
  }
  bool Pending(const StepOwner& owner)const{for(const auto&[id,op]:operations_){(void)id;if(op.record.owner==owner)return true;}return false;}
  bool CleanupReady(uint64_t scope,uint64_t generation)const{
    for(const auto&[id,op]:operations_){(void)id;const auto& owner=op.record.owner;
      if(owner.scope==scope&&owner.scope_generation==generation&&
          (owner.callback_kind=="body"||owner.callback_kind=="beforeEach"))return false;
    }
    return true;
  }
  size_t Physical()const{return operations_.size();}
  bool IdleReady()const{return operations_.empty()&&contexts_.empty()&&step_fences_.empty();}
  void ThrowIfUnpublishable()const{if(publication_failure_)std::rethrow_exception(publication_failure_);}
  const std::vector<CallbackAssertionRecord>& Records()const{ThrowIfUnpublishable();return records_;}

 private:
  using KeyType=std::tuple<uint64_t,uint64_t,uint64_t>;
  struct Context {StepOwner owner;double deadline=0;bool closed=false;std::function<void(const std::optional<std::string>&)> forward={};std::optional<std::string> error=std::nullopt;};
  struct StepFence {StepOwner owner;std::function<void(const std::optional<std::string>&)> forward;std::optional<std::string> error=std::nullopt;};
  struct Operation {
    CallbackAssertionRecord record;std::optional<Schedule> schedule;double owner_deadline=0;
    std::string error="\"\"";std::optional<std::string> file_error;bool hard_error=false;uint64_t source_ticket=0,front_pending=0;
    std::optional<double> cleanup_end;bool result_sent=false,termination_requested=false,files_started=false,files_ready=false,source_failed=false;
    std::function<void(const std::optional<std::string>&)> forward;
  };
  static KeyType Key(const StepOwner& o){return{o.callback_token,o.scope,o.scope_generation};}
  static std::string N(uint64_t n){return std::to_string(n);}
  static std::string Identity(const StepOwner& o){return N(o.callback_token)+" "+N(o.scope)+" "+N(o.scope_generation);}
  static uint64_t UInt(std::string_view text){uint64_t value=0;const auto r=std::from_chars(text.data(),text.data()+text.size(),value);if(text.empty()||(text.size()>1&&text.front()=='0')||r.ec!=std::errc{}||r.ptr!=text.data()+text.size())throw std::runtime_error("Native callback retry integer rejected");return value;}
  static void Size(const auto& f,size_t size){if(f.size()!=size)throw std::runtime_error("Native callback retry packet arity rejected");}
  Context* Find(const StepOwner& o){const auto found=contexts_.find(Key(o));return found!=contexts_.end()&&found->second.owner==o?&found->second:nullptr;}
  Operation& Exact(const std::vector<std::string>& f){const auto found=operations_.find(UInt(f[5]));if(found==operations_.end()||found->second.record.request!=UInt(f[1])||Identity(found->second.record.owner)!=f[2]+" "+f[3]+" "+f[4])throw std::runtime_error("Native callback retry receipt owner rejected");return found->second;}
  Operation* Iteration(const StepOwner& owner,uint64_t step){for(auto&[id,op]:operations_){(void)id;if(op.record.owner==owner&&op.schedule->Actual().invocation.step==step&&op.schedule->Physical())return &op;}return nullptr;}
  bool Within(uint64_t parent,uint64_t root)const{for(size_t depth=0;parent&&depth<=TestSteps::kDepth;++depth){if(parent==root)return true;const auto* step=hooks_.step(parent);if(!step)return false;parent=step->parent;}return false;}
  bool PendingWithin(const StepOwner& owner,uint64_t root)const{for(const auto&[id,op]:operations_){(void)id;if(op.record.owner==owner&&(!root||Within(op.record.parent,root)))return true;}return false;}
  double OwnerDeadline(const Operation& op)const{const auto found=contexts_.find(Key(op.record.owner));if(found==contexts_.end())return 0;double deadline=found->second.deadline;if(op.record.parent){const auto* step=hooks_.step(op.record.parent);if(!step||step->owner!=op.record.owner)return 0;deadline=std::min(deadline,step->deadline);}return deadline;}
  void HardCause(Operation& op,const std::string& error){if(!op.hard_error){op.hard_error=true;op.error=error;hooks_.hard(op.record.owner,error);}}
  void Rejected(uint64_t request,const StepOwner& owner){hooks_.send("TOPASS_ADMITTED "+N(request)+" "+Identity(owner)+" 0 0 "+StepWire("\"Native toPass admission capacity exhausted\""));}
  static void CleanupOrigin(Operation& op,double now){if(!op.cleanup_end)op.cleanup_end=now+1000;}
  static void SetSource(Operation& op,uint64_t ticket){if(ticket&&op.source_ticket&&op.source_ticket!=ticket)throw std::runtime_error("Native callback retry used another Source registration");if(ticket)op.source_ticket=ticket;}
  void Begin(const std::vector<std::string>& f,double now,bool stopped,bool cleanup){
    Size(f,12);const auto request=UInt(f[1]);if(!request||nonce_==UINT64_MAX||request!=nonce_+1)throw std::runtime_error("Native callback retry request nonce rejected");++nonce_;
    const auto key=KeyType{UInt(f[2]),UInt(f[3]),UInt(f[4])};const auto context=contexts_.find(key);
    if(context==contexts_.end()||context->second.closed||(stopped&&!(cleanup&&context->second.owner.callback_kind=="afterEach")))throw std::runtime_error("Native callback retry admission has no live author scope");
    const auto owner=context->second.owner;const auto parent=UInt(f[5]);const auto timeout=f[6]=="-"?0:UInt(f[6]);
    std::array<uint64_t,Schedule::kIntervals> intervals{};size_t count=0;
    if(f[7]!="-"){std::string_view values=f[7];for(;;){if(count==intervals.size())throw std::runtime_error("Native callback retry interval count exceeded");const auto end=values.find(',');intervals[count++]=UInt(values.substr(0,end));if(end==values.npos)break;values.remove_prefix(end+1);}}
    (void)PrepareStepLocation(f[8],UInt(f[9]),UInt(f[10]),UInt(f[11]));
    if(timeout>Schedule::kSafeDuration||std::any_of(intervals.begin(),intervals.begin()+count,[](uint64_t value){return value>Schedule::kSafeDuration;}))throw std::runtime_error("Native callback retry safe duration exceeded");
    if(operations_.size()>=TestSteps::kPhysical){Rejected(request,owner);return;}
    const auto id=hooks_.next_step();if(!id){Rejected(request,owner);return;}
    Operation op;op.record.id=id;op.record.request=request;op.record.parent=parent;op.record.owner=owner;op.record.started=now;
    op.owner_deadline=OwnerDeadline(op);
    op.schedule.emplace(Schedule::Identity{id,parent,owner},Schedule::Options{timeout,f[7]=="-"?std::nullopt:std::make_optional(std::span<const uint64_t>(intervals.data(),count))},op.owner_deadline,now);
    op.record.own_deadline=op.schedule->OwnDeadline();
    const auto charge=2*(8192*6+2)+1024;
    if(budget_.reserved>TestSteps::kMetadata)throw std::runtime_error("Native callback retry shared metadata invariant failed");
    if(charge>TestSteps::kMetadata-budget_.reserved){Rejected(request,owner);return;}
    // Unused actors allocate no journal. Reserve its existing maximum before
    // any actual invocation so final physical publication cannot allocate.
    if(records_.capacity()<TestSteps::kRecords)records_.reserve(TestSteps::kRecords);
    op.error.reserve(8192*6+2);op.record.error_json.reserve(8192*6+2);
    auto actual=operations_.emplace(id,std::move(op));budget_.reserved+=charge;
    std::optional<uint64_t> step;
    try{step=hooks_.begin(owner,parent,0,1,actual.first->second.schedule->OwnDeadline(),now);}
    catch(...){if(!hooks_.step(id)){operations_.erase(id);budget_.reserved-=charge;}else if(!publication_failure_)publication_failure_=std::current_exception();throw;}
    if(!step||*step!=id){
      if(!step&&!hooks_.step(id)){operations_.erase(id);budget_.reserved-=charge;Rejected(request,owner);return;}
      try{throw std::runtime_error("Native callback retry actual first iteration admission failed");}catch(...){if(hooks_.step(id)&&!publication_failure_)publication_failure_=std::current_exception();throw;}
    }
    try{hooks_.send("TOPASS_ADMITTED "+N(request)+" "+Identity(owner)+" "+N(id)+" 1 -");Dispatch(actual.first->second,now,true);}
    catch(...){if(!publication_failure_)publication_failure_=std::current_exception();throw;}
  }
  void Dispatch(Operation& op,double now,bool first=false){
    const auto next=op.schedule->Attempts()+1;auto id=op.record.id;
    if(!first){
      const auto expected=hooks_.next_step();std::optional<uint64_t> admitted;
      try{admitted=hooks_.begin(op.record.owner,op.record.parent,op.record.id,next,op.schedule->OwnDeadline(),now);}
      catch(...){if(hooks_.step(expected)&&!publication_failure_)publication_failure_=std::current_exception();throw;}
      if(!admitted)throw std::runtime_error("Native callback retry actual iteration admission failed");
      id=*admitted;op.file_error.reset();op.files_started=op.files_ready=false;op.source_ticket=op.front_pending=0;op.cleanup_end.reset();
    }
    try{
      const auto invocation=op.schedule->Start(id,now);last_dispatched_=op.record.id;const auto remaining=static_cast<uint64_t>(std::max(1.0,std::min(static_cast<double>(Schedule::kSafeDuration),std::ceil(op.schedule->Deadline()-now))));
      hooks_.send("TOPASS_RUN "+N(op.record.request)+" "+Identity(op.record.owner)+" "+N(op.record.id)+" "+N(invocation.nonce)+" "+N(id)+" "+N(op.record.parent)+" "+N(remaining));
    }catch(...){if(!publication_failure_)publication_failure_=std::current_exception();throw;}
  }
  void ObserveLogical(uint64_t id,double now){
    auto found=operations_.find(id);if(found==operations_.end())return;auto& op=found->second;
    if(op.schedule->Result()==Schedule::Verdict::Pending||op.result_sent)return;
    op.result_sent=true;
    if(op.schedule->Physical()){CleanupOrigin(op,now);hooks_.cancel(op.record.owner,op.schedule->Actual().invocation.step,"\"Native toPass logical outcome closed its invocation\"",now);}
    const auto result=op.schedule->Result();const auto status=result==Schedule::Verdict::Passed?"passed":result==Schedule::Verdict::TimedOut?"timedOut":result==Schedule::Verdict::Canceled?"canceled":"infrastructure";
    const auto error=result==Schedule::Verdict::Passed?std::string("\"\""):op.error!="\"\""?op.error:result==Schedule::Verdict::TimedOut?std::string("\"Native toPass original assertion deadline expired\""):std::string("\"Native toPass owner canceled or infrastructure failed\"");
    hooks_.send("TOPASS_RESULT "+N(op.record.request)+" "+Identity(op.record.owner)+" "+N(id)+" "+status+" "+StepWire(error)+" "+N(op.schedule->Attempts()));
  }
  void Progress(uint64_t id,double now){
    auto found=operations_.find(id);if(found==operations_.end())return;auto& op=found->second;
    if(!op.forward||op.front_pending)return;
    if(!op.files_started){op.files_started=true;const auto p=AttachmentRetryProvenance{id,op.schedule->Actual().invocation.nonce,op.schedule->Actual().invocation.step};hooks_.files(op.record.owner,p,[this,id](const auto& failure){auto actual=operations_.find(id);if(actual==operations_.end())throw std::runtime_error("Native callback retry file fence lost its owner");actual->second.files_ready=true;if(failure)actual->second.file_error=failure;},now);}
    if(!op.files_ready)return;
    if(op.source_failed)return;
    if(op.source_ticket){const auto fence=hooks_.source(op.record.owner,op.record.request,id,op.schedule->Actual().invocation.nonce,op.schedule->Actual().invocation.step,op.source_ticket,*op.cleanup_end,now);if(fence==SourceFence::Pending)return;if(fence==SourceFence::Failed){op.source_failed=true;op.schedule->FailInfrastructure(now);HardCause(op,"\"Native toPass Source physical fence failed\"");ObserveLogical(id,now);return;}}
    auto forward=std::move(op.forward);forward(op.file_error);
  }
  void Physical(uint64_t id,const std::optional<std::string>& failure,double now){
    auto found=operations_.find(id);if(found==operations_.end())throw std::runtime_error("Native callback retry physical receipt lost operation");auto& op=found->second;
    if(failure&&!op.hard_error)op.error=*failure;
    const auto invocation=op.schedule->Actual().invocation;
    op.schedule->PhysicallySettled(invocation,now,failure?Schedule::Fence::AuthoredFailure:Schedule::Fence::Clean);
    hooks_.send("TOPASS_ITERATION_RETIRED "+N(op.record.request)+" "+Identity(op.record.owner)+" "+N(id)+" "+N(invocation.nonce)+" "+N(invocation.step));
    ObserveLogical(id,now);
    if(op.schedule->Result()!=Schedule::Verdict::Pending)Finalize(id,now,CallbackAssertionReceipt::Settled);
  }
  void Finalize(uint64_t id,double now,CallbackAssertionReceipt receipt){
    auto found=operations_.find(id);if(found==operations_.end())throw std::runtime_error("Native callback retry final receipt is duplicate");auto& op=found->second;
    if(op.schedule->Physical()||op.schedule->Result()==Schedule::Verdict::Pending)throw std::runtime_error("Native callback retry final publication still owns physical invocation");
    ObserveLogical(id,now);auto& r=op.record;r.attempts=r.last_invocation=op.schedule->Attempts();r.deadline=op.schedule->Deadline();r.received=now;r.receipt=receipt;
    const auto verdict=op.schedule->Result();r.status=receipt==CallbackAssertionReceipt::WorkerExited?CallbackAssertionStatus::Infrastructure:verdict==Schedule::Verdict::Passed?CallbackAssertionStatus::Passed:verdict==Schedule::Verdict::TimedOut?CallbackAssertionStatus::TimedOut:verdict==Schedule::Verdict::Canceled?CallbackAssertionStatus::Canceled:CallbackAssertionStatus::Infrastructure;
    r.error_json=verdict==Schedule::Verdict::Passed?"\"\"":op.error!="\"\""?op.error:verdict==Schedule::Verdict::TimedOut?"\"Native toPass original assertion deadline expired\"":"\"Native toPass owner canceled or infrastructure failed\"";
    const auto packet="TOPASS_RETIRED "+N(r.request)+" "+Identity(r.owner)+" "+N(id);records_.push_back(std::move(r));operations_.erase(found);hooks_.send(packet);DrainStepFences();DrainContexts();
  }
  void DrainStepFences(){
    if(draining_steps_)return;
    draining_steps_=true;
    try{for(;;){const auto found=std::find_if(step_fences_.begin(),step_fences_.end(),[this](const auto& row){return !PendingWithin(row.second.owner,row.first);});if(found==step_fences_.end())break;auto fence=std::move(found->second);step_fences_.erase(found);fence.forward(fence.error);}}
    catch(...){draining_steps_=false;throw;}
    draining_steps_=false;
  }
  void DrainContexts(){
    if(draining_contexts_)return;
    draining_contexts_=true;
    try{for(;;){const auto found=std::find_if(contexts_.begin(),contexts_.end(),[this](const auto& row){return row.second.forward&&!Pending(row.second.owner);});if(found==contexts_.end())break;auto c=std::move(found->second);contexts_.erase(found);hooks_.send("TOPASS_CONTEXT_RETIRED "+Identity(c.owner));c.forward(c.error);}}
    catch(...){draining_contexts_=false;throw;}
    draining_contexts_=false;
  }
  Hooks hooks_;TestSteps::Budget& budget_;std::map<KeyType,Context> contexts_;std::map<uint64_t,StepFence> step_fences_;std::map<uint64_t,Operation> operations_;std::vector<CallbackAssertionRecord> records_;uint64_t nonce_=0,last_dispatched_=0;bool draining_steps_=false,draining_contexts_=false;std::exception_ptr publication_failure_;
};
} // namespace suitecut::runner
