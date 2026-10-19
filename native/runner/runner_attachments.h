// SPDX-License-Identifier: MIT
#pragma once
#include "attachment_files.h"
#include "host_process.h"
#include "runtime_skip.h"
#include "step_wire.h"
#include "test_steps.h"
#include "test_attachments.h"
#include "attachment_declarations.h"
#include "attachment_append_source.h"
#include "attachment_mutations.h"
#include "attachment_view_receipts.h"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
namespace suitecut::runner {
// Native actor owns admission, bounded actual file processes, callback fences,
// authoritative phase deadlines, and only its own inode-identified artifacts.
class RunnerAttachments {
 public:
  static constexpr size_t kRecords=128,kPhysical=4,kBytes=64*1024*1024;
  struct Budget {uint64_t sequence=0;size_t accepted=0,reserved=0,physical=0;AttachmentDeclarations::Budget declarations{};AttachmentMutations::Budget mutations{};AttachmentViewReceipts::Budget publications{};};
  struct Hooks {std::function<void(const std::string&)> send;std::function<void(const std::string&)> failure;std::function<const StepRecord*(uint64_t)> step={};std::function<void(const StepOwner&,const std::string&)> close_owner={};std::function<void(const StepOwner&,uint64_t,const AttachmentRetryProvenance&,double,bool)> validate_retry={};std::function<void(const StepOwner&,const AttachmentRetryProvenance&,const std::string&,bool)> retry_failure={};};
  RunnerAttachments(std::filesystem::path parent,Hooks hooks,Budget* budget=nullptr):parent_(std::move(parent).lexically_normal()),hooks_(std::move(hooks)),budget_(budget),declarations_(budget?&budget->declarations:nullptr){records_.reserve(kRecords);}
  void WorkerStarted(){if(!jobs_.empty()||!callbacks_.empty()||!step_fences_.empty())throw std::runtime_error("Native attachment worker still owns physical jobs/callbacks");nonce_=0;}
  void BeginCallback(const StepOwner& owner,double deadline,double now){
    if(!Valid(owner)||!std::isfinite(deadline)||!std::isfinite(now)||deadline<=0||callbacks_.size()>=64||callbacks_.contains(Key(owner)))throw std::runtime_error("Native attachment callback admission failed");
    declarations_.Open(owner,deadline,now);callbacks_.emplace(Key(owner),Callback{owner,deadline,now});MutationOpen(owner,deadline,now);
    hooks_.send("ATTACH_CONTEXT "+std::to_string(owner.callback_token)+" "+std::to_string(owner.scope)+" "+std::to_string(owner.scope_generation)+" "+std::to_string(owner.test_id)+" "+std::to_string(owner.attempt_generation)+" "+std::to_string(owner.worker_index)+" "+std::to_string(owner.pid));
  }
  bool Pending(uint64_t token,uint64_t scope,uint64_t generation)const{
    for(const auto&[id,job]:jobs_){(void)id;if(Match(job.owner,token,scope,generation))return true;}
    for(const auto&[key,callback]:callbacks_){(void)key;if(Match(callback.owner,token,scope,generation)&&MutationPending(callback.owner))return true;}return false;
  }
  bool PendingStep(const StepOwner& owner,uint64_t id)const{
    for(const auto&[key,job]:jobs_){(void)key;if(job.owner==owner&&OwnsStep(job,id))return true;}return MutationPendingStep(owner,id);
  }
  bool PendingRetry(const StepOwner& owner,const AttachmentRetryProvenance& retry)const{
    if(!retry.Complete())throw std::runtime_error("Native attachment retry fence identity incomplete");
    for(const auto& [id,job]:jobs_){(void)id;if(job.owner==owner&&job.callback_retry==retry)return true;}
    if(mutation_receipts_)for(const auto& record:mutation_receipts_->Records())if(!record.end&&record.owner==owner&&record.callback_retry==retry)return true;
    return false;
  }
  void FenceRetry(const StepOwner& owner,const AttachmentRetryProvenance& retry,std::function<void(const std::optional<std::string>&)> forward,double now){
    if(!retry.Complete()||!hooks_.validate_retry)throw std::runtime_error("Native attachment retry fence has no native controller");
    hooks_.validate_retry(owner,0,retry,now,true);FenceStep(owner,retry.iteration_step,std::move(forward),now);
  }
  void FenceStep(const StepOwner& owner,uint64_t id,std::function<void(const std::optional<std::string>&)> forward,double now){
    const auto* step=hooks_.step?hooks_.step(id):nullptr;
    if(!std::isfinite(now)||!step||step->owner!=owner||step->receipt!=StepReceipt::Pending||!step->callback_received||step_fences_.contains(id))throw std::runtime_error("Native attachment step fence has no actual raw callback receipt");
    StepFence fence{owner,std::move(forward)};
    for(const auto&[key,job]:jobs_){(void)key;if(job.owner==owner&&OwnsStep(job,id)&&FailureAffects(job,id)&&!job.error.empty()&&!job.step_canceled&&!job.test_skip&&!job.local_skip)fence.error=MessageJSON(job.error);}
    step_fences_.emplace(id,std::move(fence));DrainSteps();
  }
  // Preflight the entire actual descendant file set before native step state
  // changes. A prior file error, cancellation, deadline or different cause may
  // not be hidden by a later local skip request.
  bool CanAcceptLocalSkip(const StepOwner& owner,uint64_t source,double now)const{
    const auto callback=callbacks_.find(Key(owner));const auto* step=hooks_.step?hooks_.step(source):nullptr;
    if(!std::isfinite(now)||callback==callbacks_.end()||callback->second.owner!=owner||callback->second.closed||callback->second.mutation_error||now>=callback->second.deadline||!step||step->owner!=owner||step->status!=StepStatus::Running||step->receipt!=StepReceipt::Pending||step->callback_received||step->test_skip||step->step_skip||now>=step->deadline)return false;
    for(const auto&[id,job]:jobs_){(void)id;if(job.owner==owner&&OwnsStep(job,source)){
      const auto* actual=hooks_.step(SkipStepID(job,source));
      if(!job.error.empty()||job.cleaning||job.test_skip||job.local_skip||job.step_canceled||now>=job.job_cap||StepExpired(job,now)||!actual||actual->status!=StepStatus::Running||actual->test_skip||actual->step_skip)return false;
    }}
    return true;
  }
  void AcceptLocalSkip(const StepOwner& owner,uint64_t source,uint64_t request,double now){
    const auto* step=hooks_.step?hooks_.step(source):nullptr;const auto callback=callbacks_.find(Key(owner));
    if(!request||!std::isfinite(now)||callback==callbacks_.end()||callback->second.owner!=owner||callback->second.closed||callback->second.mutation_error||now>=callback->second.deadline||!step||step->owner!=owner||step->status!=StepStatus::Skipped||step->receipt!=StepReceipt::Pending||!step->ended||*step->ended!=now||!step->step_skip||step->step_skip->source_step!=source||step->step_skip->request!=request)throw std::runtime_error("Native attachment local skip has no accepted actual step cause");
    // Validate every affected job before any PID is signaled. The accepted step
    // cause and this bounded file set are pinned on the same native ingress.
    for(const auto&[id,job]:jobs_){(void)id;if(job.owner==owner&&OwnsStep(job,source)){
      const auto* actual=hooks_.step(SkipStepID(job,source));
      if(!actual||!actual->step_skip||*actual->step_skip!=*step->step_skip||job.local_skip||job.test_skip||!job.error.empty()||job.cleaning)throw std::runtime_error("Native attachment local skip changed its preflighted descendant ownership");
    }}
    for(auto&[id,job]:jobs_){(void)id;if(job.owner==owner&&OwnsStep(job,source)){job.local_skip=LocalSkip{request,source};Stop(job,"Native attachment canceled by accepted local step skip",now);}}
  }
  void StepChanged(const StepRecord& step,double now){
    if(step.status==StepStatus::Running||step.status==StepStatus::Passed||(step.status==StepStatus::Skipped&&step.test_skip))return;
    for(auto&[key,job]:jobs_){(void)key;if(job.owner==step.owner&&(job.step_id==step.id||(!job.callback_retry.Empty()&&job.callback_retry.iteration_step==step.id))){if(step.status==StepStatus::Skipped&&step.step_skip&&job.local_skip&&job.local_skip->request==step.step_skip->request&&job.local_skip->source_step==step.step_skip->source_step)continue;job.step_canceled=true;Stop(job,"Native step "+std::to_string(step.id)+" "+StepStatusName(step.status)+" canceled its pending attachment",now);}}
  }
  bool CanAcceptTestSkip(uint64_t token,uint64_t scope,uint64_t generation,uint64_t request,double now)const{
    const auto found=callbacks_.find(Key(token,scope,generation));
    if(!request||!std::isfinite(now)||found==callbacks_.end()||found->second.closed||found->second.mutation_error||found->second.test_skip||now>=found->second.deadline||
       (found->second.owner.callback_kind!="body"&&found->second.owner.callback_kind!="beforeEach"))return false;
    for(const auto& declaration:declarations_.Records())if(declaration.owner==found->second.owner&&!declaration.retry_failure_disposed&&(declaration.receipt==AttachmentDeclarations::Receipt::Failed||declaration.receipt==AttachmentDeclarations::Receipt::Rejected))return false;
    for(const auto&[id,job]:jobs_){(void)id;if(Match(job.owner,token,scope,generation)){
      if(job.test_skip||now>=job.job_cap)return false;
      // An earlier accepted local skip is not an ordinary file error. A later
      // owning test skip can coexist while its genuine rollback remains owned.
      if(job.local_skip){if(!AuthenticLocalCancellation(job))return false;}
      else{const auto* actual=job.step_id&&hooks_.step?hooks_.step(job.step_id):nullptr;
        if(!job.error.empty()||job.cleaning||StepExpired(job,now)||(job.step_id&&(!actual||actual->owner!=job.owner||actual->status!=StepStatus::Running)))return false;
      }
    }}
    return true;
  }
  void AcceptTestSkip(uint64_t token,uint64_t scope,uint64_t generation,uint64_t request,const std::string& reason,double now){
    if(!JsonString(reason,8192)||!CanAcceptTestSkip(token,scope,generation,request,now))throw std::runtime_error("Native attachment accepted test-skip cause is stale or ineligible");
    auto& callback=callbacks_.at(Key(token,scope,generation));callback.test_skip=TestSkip{request,reason};declarations_.Close(callback.owner);MutationClose(callback.owner);callback.closed=true;
    for(auto&[id,job]:jobs_){(void)id;if(Match(job.owner,token,scope,generation)){job.test_skip=callback.test_skip;Stop(job,"Native attachment canceled by accepted runtime test skip",now);}}
  }
  void Amend(uint64_t token,uint64_t scope,uint64_t generation,double deadline,double now){
    const auto callback=callbacks_.find(Key(token,scope,generation));
    if(callback==callbacks_.end()||callback->second.closed||callback->second.deadline==deadline)return;
    if(!std::isfinite(deadline)||!std::isfinite(now)||now>=callback->second.deadline)throw std::runtime_error("Native attachment phase amendment expired");
    declarations_.Amend(callback->second.owner,deadline,now);MutationAmend(callback->second.owner,deadline,now);callback->second.deadline=deadline;Tick(now);
  }
  void CloseAdmission(uint64_t token,uint64_t scope,uint64_t generation){
    const auto callback=callbacks_.find(Key(token,scope,generation));if(callback==callbacks_.end())throw std::runtime_error("Native attachment callback close is stale");declarations_.Close(callback->second.owner);MutationClose(callback->second.owner);callback->second.closed=true;
  }
  void Fence(uint64_t token,uint64_t scope,uint64_t generation,std::function<void()> forward,double now){
    FenceWithAppendFailure(token,scope,generation,[forward=std::move(forward)](const auto&){forward();},now);
  }
  void FenceWithAppendFailure(uint64_t token,uint64_t scope,uint64_t generation,std::function<void(const std::optional<std::string>&)> forward,double now){
    const auto callback=callbacks_.find(Key(token,scope,generation));
    if(callback==callbacks_.end()||callback->second.forward)throw std::runtime_error("Native attachment callback fence is stale/duplicate");
    declarations_.Close(callback->second.owner);MutationClose(callback->second.owner);callback->second.closed=true;callback->second.forward=std::move(forward);Drain(now);
  }
  void Cancel(uint64_t token,uint64_t scope,uint64_t generation,const std::string& error,double now){
    const auto callback=callbacks_.find(Key(token,scope,generation));if(callback==callbacks_.end())return;
    declarations_.Close(callback->second.owner);MutationClose(callback->second.owner);callback->second.closed=true;
    for(auto&[id,job]:jobs_){(void)id;if(Match(job.owner,token,scope,generation))Stop(job,error,now);}
  }
  // Actual original host process exit is supplied by the native runner only.
  // Native writer children retain ownership until their own real waitpid exit.
  void WorkerExited(unsigned index,int64_t pid,double now){
    for(auto&[key,callback]:callbacks_){(void)key;if(callback.owner.worker_index==index&&callback.owner.pid==pid){declarations_.Close(callback.owner);MutationClose(callback.owner);callback.closed=true;callback.exited=true;callback.forward={};}}
    for(auto&[id,job]:jobs_){(void)id;if(job.owner.worker_index==index&&job.owner.pid==pid)Stop(job,"Native attachment owner exited before actual artifact completion",now);}
    if(mutation_receipts_)mutation_receipts_->WorkerExited(index,pid,now);
    Drain(now);
  }
  bool Packet(const std::vector<std::string>& f,double now,bool stopped){
    if(MutationPacket(f,now,stopped))return true;
    if(f.empty()||(f[0]!="ATTACH_BEGIN"&&f[0]!="ATTACH_STEP_BEGIN"&&f[0]!="ATTACH_APPEND_BEGIN"&&f[0]!="ATTACH_CHUNK"&&f[0]!="ATTACH_FINISH"))return false;
    if(f[0]=="ATTACH_APPEND_BEGIN"){
      if(f.size()!=10&&f.size()!=13)throw std::runtime_error("Invalid native append declaration packet");
      const auto request=attachment_files::UInt(f[1]),token=attachment_files::UInt(f[2]),scope=attachment_files::UInt(f[3]),generation=attachment_files::UInt(f[4]),expected=attachment_files::UInt(f[7]);
      if(!request||nonce_==UINT64_MAX||request!=++nonce_)throw std::runtime_error("Native append request nonce is stale");
      const auto callback=callbacks_.find(Key(token,scope,generation));
      if(callback==callbacks_.end())throw std::runtime_error("Native append declaration has no actual owning callback");
      const auto retry=ReadRetry(f,10,callback->second.owner,0,now,false);
      const auto name=RuntimeSkipReason(f[5]),type=RuntimeSkipReason(f[8]);
      const auto decoded=ReadAttachmentAppendSource(f[6],expected,RuntimeSkipReason(f[9]));const auto source=decoded.source;
      const auto rejection=(stopped||callback->second.closed||now>=callback->second.deadline)?std::make_optional(MessageJSON("Native append declaration rejected: owning callback closed, expired or stopped")):std::nullopt;
      const auto declaration_owner=callback->second.owner;const auto preparation_error=MessageJSON("Native append body admission preparation failed");
      const auto declaration=declarations_.Declare(callback->second.owner,request,name,type,source,decoded.author_path_json,decoded.expected_bytes,now,rejection,retry);
      try{
      if(rejection){hooks_.send("ATTACH_APPEND_REJECTED "+f[1]+" "+f[2]+" "+f[3]+" "+f[4]+" "+std::to_string(declaration)+" "+StepWire(*rejection)+(retry.Empty()?"":" O"));return true;}
      if(source!=AttachmentDeclarations::Source::Body){hooks_.send("ATTACH_REFERENCE_ACCEPTED "+f[1]+" "+f[2]+" "+f[3]+" "+f[4]+" "+std::to_string(declaration));return true;}
      const size_t reservation=expected;
      if(accepted_>=kRecords||jobs_.size()>=kPhysical||reservation>kBytes-reserved_||id_==UINT64_MAX||(budget_&&(budget_->sequence==UINT64_MAX||budget_->accepted>=kRecords||budget_->physical>=kPhysical||reservation>kBytes-budget_->reserved))){
        const auto error=MessageJSON("Native append body admission capacity exceeded");RetryFailure(callback->second.owner,retry,error,false);declarations_.Failed(callback->second.owner,declaration,error,now,!retry.Empty());hooks_.send("ATTACH_APPEND_REJECTED "+f[1]+" "+f[2]+" "+f[3]+" "+f[4]+" "+std::to_string(declaration)+" "+StepWire(error)+(retry.Empty()?"":" O"));return true;
      }
      Job job;job.id=budget_?++budget_->sequence:++id_;job.declaration_id=declaration;job.callback_retry=retry;job.request=request;job.owner=callback->second.owner;job.started=now;job.job_cap=now+120000;job.reservation=reservation;job.expected=expected;job.kind="B";job.name=name;job.type=type;job.basename=Prefix(name)+"-"+std::to_string(job.id)+Extension(job.type);
      declarations_.BindArtifact(job.owner,declaration,job.id);
      const auto packet="INIT "+std::to_string(job.id)+" B "+std::to_string(expected)+" "+attachment_files::Hex(parent_.string())+" "+attachment_files::Hex(job.basename)+" - "+std::to_string(job.job_cap)+" 0 0"+(retry.Empty()?"":" R");
      const auto admission_error=MessageJSON("Native append body writer admission failed");job.error.reserve(8192);
      const auto actual_id=job.id;const auto actual_owner=job.owner;
      auto [owned,inserted]=jobs_.emplace(actual_id,std::move(job));if(!inserted)throw std::runtime_error("Native append writer ID is duplicate");
      reserved_+=reservation;++accepted_;if(budget_){budget_->reserved+=reservation;++budget_->accepted;++budget_->physical;}
      try{
        owned->second.process=std::make_unique<DedicatedHostProcess>("/proc/self/exe",std::vector<std::string>{"--native-attachment-worker"});
        if(!owned->second.process->Send(packet))throw std::runtime_error("Native append body writer initialization failed");
      }catch(...){
        if(owned->second.process){Stop(owned->second,"Native append body writer admission failed",now);}
        else{reserved_-=reservation;if(budget_){budget_->reserved-=reservation;--budget_->physical;}jobs_.erase(owned);declarations_.Failed(actual_owner,declaration,admission_error,now);}
        throw;
      }
      }catch(...){
        const auto* record=declarations_.Find(declaration);const bool physical_owner=std::any_of(jobs_.begin(),jobs_.end(),[&](const auto& value){return value.second.declaration_id==declaration;});
        if(record&&record->receipt==AttachmentDeclarations::Receipt::Pending&&!physical_owner)declarations_.Failed(declaration_owner,declaration,preparation_error,now);
        throw;
      }
    }else if(f[0]=="ATTACH_BEGIN"||f[0]=="ATTACH_STEP_BEGIN"){
      const size_t extra=f[0]=="ATTACH_STEP_BEGIN"?1:0;
      if(f.size()!=10+extra&&f.size()!=13+extra)throw std::runtime_error("Invalid native attachment admission packet");
      const auto request=attachment_files::UInt(f[1]),token=attachment_files::UInt(f[2]),scope=attachment_files::UInt(f[3]),generation=attachment_files::UInt(f[4]),expected=attachment_files::UInt(f[7+extra]),step_id=extra?attachment_files::UInt(f[5]):0;
      if(!request||nonce_==UINT64_MAX||request!=++nonce_)throw std::runtime_error("Native attachment admission nonce is stale");
      const auto name=RuntimeSkipReason(f[5+extra]);const auto type=f[8+extra]=="~"?std::string():RuntimeSkipReason(f[8+extra]);
      if(!JsonString(name,4096)||(!type.empty()&&!JsonString(type,1024))||(f[6+extra]!="S"&&f[6+extra]!="B"&&f[6+extra]!="P")||expected>attachment_files::kBytes)throw std::runtime_error("Invalid native attachment metadata");
      const auto path=attachment_files::Unhex(f[9+extra],8192);if(path.find('\0')!=std::string::npos||(f[6+extra]!="P"&&!path.empty())||(f[6+extra]=="P"&&expected))throw std::runtime_error("Invalid native attachment source path");
      const auto callback=callbacks_.find(Key(token,scope,generation));
      const auto retry=ReadRetry(f,10+extra,callback==callbacks_.end()?StepOwner{}:callback->second.owner,step_id,now,false);
      const size_t reservation=f[6+extra]=="P"?attachment_files::kBytes:expected;
      const auto* step=step_id&&hooks_.step?hooks_.step(step_id):nullptr;
      if(stopped||(extra&&(!step_id||!step||callback==callbacks_.end()||step->owner!=callback->second.owner||step->status!=StepStatus::Running||step->receipt!=StepReceipt::Pending||step->callback_received||now>=step->deadline))||callback==callbacks_.end()||callback->second.closed||now>=callback->second.deadline||accepted_>=kRecords||jobs_.size()>=kPhysical||reservation>kBytes-reserved_||id_==UINT64_MAX||(budget_&&(budget_->sequence==UINT64_MAX||budget_->accepted>=kRecords||budget_->physical>=kPhysical||reservation>kBytes-budget_->reserved))){
        RetryFailure(callback==callbacks_.end()?StepOwner{}:callback->second.owner,retry,MessageJSON("Native attachment admission rejected: stale, expired, stopped or capacity exceeded"),false);
        hooks_.send(std::string(extra?"ATTACH_STEP_ADMITTED ":"ATTACH_ADMITTED ")+f[1]+" "+f[2]+" "+f[3]+" "+f[4]+" 0 "+(extra?std::to_string(step_id)+" ":"")+"0 "+Wire("Native attachment admission rejected: stale, expired, stopped or capacity exceeded")+(retry.Empty()?"":" O"));return true;
      }
      if(MutationFind(callback->second.owner))(void)MutationReceipts();
      Job job;job.id=budget_?++budget_->sequence:++id_;job.callback_retry=retry;job.step_id=step_id;job.request=request;job.owner=callback->second.owner;job.started=now;job.job_cap=now+120000;job.reservation=reservation;job.expected=expected;job.kind=f[6+extra];job.name=name;job.type=type.empty()?DefaultType(f[6+extra],path):type;job.basename=Prefix(name)+"-"+std::to_string(job.id)+Extension(job.type);
      job.process=std::make_unique<DedicatedHostProcess>("/proc/self/exe",std::vector<std::string>{"--native-attachment-worker"});
      const auto packet="INIT "+std::to_string(job.id)+" "+(job.kind=="P"?"P":"B")+" "+std::to_string(expected)+" "+attachment_files::Hex(parent_.string())+" "+attachment_files::Hex(job.basename)+" "+attachment_files::Hex(path)+" "+std::to_string(job.job_cap)+" "+"0 0"+(retry.Empty()?"":" R");
      if(!job.process->Send(packet))throw std::runtime_error("Native attachment worker initialization failed");
      reserved_+=reservation;++accepted_;if(budget_){budget_->reserved+=reservation;++budget_->accepted;++budget_->physical;}jobs_.emplace(job.id,std::move(job));
    }else{
      if(f.size()!=(f[0]=="ATTACH_CHUNK"?7:5))throw std::runtime_error("Invalid native attachment byte packet");
      const auto id=attachment_files::UInt(f[1]),token=attachment_files::UInt(f[2]),scope=attachment_files::UInt(f[3]),generation=attachment_files::UInt(f[4]);const auto found=jobs_.find(id);
      if(found==jobs_.end()){const auto done=completed_.find(id);if(done!=completed_.end()&&Match(done->second,token,scope,generation))return true;throw std::runtime_error("Native attachment byte packet has stale owner");}
      if(!Match(found->second.owner,token,scope,generation))throw std::runtime_error("Native attachment byte packet has stale owner");
      auto& job=found->second;
      if(!job.error.empty())return true;
      const auto callback=callbacks_.find(Key(job.owner));
      if(stopped||callback==callbacks_.end()||now>=callback->second.deadline||StepExpired(job,now)){Stop(job,"Native attachment original callback deadline expired or canceled",now);return true;}
      if(!job.admitted||job.kind=="P"||job.finish_sent||job.chunk_pending)throw std::runtime_error("Native attachment chunks exceed actual ACK admission");
      if(f[0]=="ATTACH_FINISH"){
        if(job.actual_bytes!=job.expected)throw std::runtime_error("Native attachment body finish size mismatch");
        job.finish_sent=true;SendJob(job,"FINISH "+f[1]);
      }else{
        const auto sequence=attachment_files::UInt(f[5]);const auto bytes=attachment_files::Unhex(f[6],attachment_files::kChunk);
        if(job.sequence==UINT64_MAX||sequence!=job.sequence+1||bytes.empty()||bytes.size()>job.expected-job.actual_bytes)throw std::runtime_error("Native attachment chunk size/sequence invalid");
        job.sequence=sequence;job.chunk_pending=bytes.size();SendJob(job,"CHUNK "+f[1]+" "+f[5]+" "+f[6]);
      }
    }
    return true;
  }
  void Tick(double now){
    if(!std::isfinite(now)||now<0)throw std::runtime_error("Invalid native attachment observation time");
    for(auto it=jobs_.begin();it!=jobs_.end();){auto& job=it->second;
      if(!job.process)throw std::logic_error("Native append staged file job escaped synchronous admission");
      auto callback=callbacks_.find(Key(job.owner));
      if(!job.cleaning&&job.error.empty()){
        const auto observe=[&](uint64_t id){if(!id)return;const auto* step=hooks_.step?hooks_.step(id):nullptr;if(!step||step->owner!=job.owner){job.retry_infrastructure=true;hooks_.failure("Native attachment lost its actual step owner");Stop(job,"Native attachment lost its actual step owner",now);}else StepChanged(*step,now);};
        observe(job.step_id);if(!job.callback_retry.Empty()&&job.callback_retry.iteration_step!=job.step_id)observe(job.callback_retry.iteration_step);
      }
      if(!job.cleaning&&job.error.empty()&&(callback==callbacks_.end()||now>=callback->second.deadline||StepExpired(job,now)||now>=job.job_cap))Stop(job,"Native attachment original callback/operation deadline expired",now);
      for(const auto& line:job.process->Poll(0))Receipt(job,attachment_files::Fields(line),DedicatedRealMilliseconds());
      const auto exit=job.process->Exited();
      if(!exit){if(job.stopped&&DedicatedRealMilliseconds()>=job.stopped+6000)throw std::runtime_error("Native attachment actual writer/cleanup exit remains unconfirmed after cancellation");++it;continue;}
      for(const auto& line:job.process->Poll(0))Receipt(job,attachment_files::Fields(line),DedicatedRealMilliseconds());
      const auto received=DedicatedRealMilliseconds();
      if(job.cleaning){
        if(!job.cleaned||exit->signaled||exit->code!=0||!job.process->Stdout().empty()||!job.process->Stderr().empty()){
          job.error+="; actual native artifact rollback failed";job.retry_infrastructure=true;job.skip_failed=true;StepFailure(job,job.error);hooks_.failure(job.error);
        }
      }else{
        if(!job.callback_retry.Empty()&&!job.error.empty()&&!job.test_skip&&!job.local_skip&&!job.step_canceled&&
           !(job.typed_error&&!exit->signaled&&exit->code==1)&&!(job.stopped&&exit->signaled&&exit->code==SIGKILL)){
          job.retry_infrastructure=true;hooks_.failure("Native attachment retry error has no genuine ordinary writer exit");
        }
        if(job.test_skip||job.local_skip){
          const bool canceled=exit->signaled&&exit->code==SIGKILL;
          const bool committed=job.committed&&!exit->signaled&&exit->code==0;
          if(!job.raw_error.empty()||(!canceled&&!committed)){
            job.skip_failed=true;job.error=!job.raw_error.empty()?job.raw_error:"Native attachment writer PID "+std::to_string(job.process->Pid())+(exit->signaled?" signal ":" exit ")+std::to_string(exit->code)+" without accepted cancellation or successful commit receipt";hooks_.failure(job.error);
          }
        }
        if(job.step_canceled&&!job.test_skip&&!job.local_skip){
          const bool canceled=exit->signaled&&exit->code==SIGKILL,committed=job.committed&&!exit->signaled&&exit->code==0;
          if(!job.raw_error.empty()||(!canceled&&!committed)){job.error=!job.raw_error.empty()?job.raw_error:"Native step attachment writer exit lacks actual cancellation or commit receipt";StepFailure(job,job.error);hooks_.failure(job.error);}
        }
        if(job.error.empty()&&(callback==callbacks_.end()||received>=callback->second.deadline||StepExpired(job,received)||received>=job.job_cap))job.error="Native attachment deadline expired before actual writer exit receipt";
        if(job.error.empty()&&(!job.committed||exit->signaled||exit->code!=0)){
          job.error="Native attachment writer PID "+std::to_string(job.process->Pid())+(exit->signaled?" signal ":" exit ")+std::to_string(exit->code)+" without successful commit receipt";
          job.retry_infrastructure=true;hooks_.failure(job.error);
        }
        if(!job.process->Stdout().empty()||!job.process->Stderr().empty()){job.error="Native attachment writer emitted unexpected native output";job.retry_infrastructure=true;job.skip_failed=true;hooks_.failure(job.error);}
        if(job.error.empty()){
          AttachmentRecord actual{job.id,job.request,job.owner,job.name,job.type,job.kind=="P"?"path":job.kind=="S"?"string":"bytes",job.path,job.actual_bytes,job.device,job.inode,job.process->Pid(),exit->signaled,exit->code,job.started,job.committed_at,received,job.step_id,job.callback_retry};
          uint64_t view_ticket=0;
          const bool view_enabled=MutationFind(job.owner)!=nullptr;
          if(view_enabled&&!job.declaration_id){
            const auto* original_step=job.step_id&&hooks_.step?hooks_.step(job.step_id):nullptr;
            view_ticket=MutationReceipts().Issue(actual,AttachmentViewReceipts::OriginalDeadline(actual,callback->second.deadline,original_step),received);
          }
          static_assert(std::is_nothrow_move_constructible_v<AttachmentRecord>);
          records_.push_back(std::move(actual)); // complete catalog reserved before any file admission
          if(job.declaration_id)declarations_.Committed(job.owner,job.declaration_id,records_.back());
          hooks_.send("ATTACH_PUBLISHED "+Identity(job)+" "+StepWire(job.name)+" "+StepWire(job.type)+" "+StepWire(AttachmentPathJSON(job.path))+" "+std::to_string(job.actual_bytes)+(view_enabled?" "+std::to_string(view_ticket):""));
          reserved_-=job.reservation-job.actual_bytes;if(budget_)budget_->reserved-=job.reservation-job.actual_bytes;
        }else if(!job.directory.empty()){
          // The writer's actual waitpid receipt precedes rollback admission.
          // A separate native child owns every potentially blocking cleanup IO.
          job.process=std::make_unique<DedicatedHostProcess>("/proc/self/exe",std::vector<std::string>{"--native-attachment-cleanup"});job.cleaning=true;job.stopped=received;
          SendJob(job,"CLEANUP "+std::to_string(job.id)+" "+attachment_files::Hex(job.directory)+" "+attachment_files::Hex(job.basename)+" "+std::to_string(job.directory_device)+" "+std::to_string(job.directory_inode)+" "+std::to_string(job.device)+" "+std::to_string(job.inode)+" "+std::to_string(received+6000));
          ++it;continue;
        }
      }
      if(!job.error.empty()){reserved_-=job.reservation;if(budget_)budget_->reserved-=job.reservation;}
      if(job.declaration_id){const auto* declaration=declarations_.Find(job.declaration_id);if(!declaration)throw std::runtime_error("Native append body declaration disappeared");if(declaration->receipt==AttachmentDeclarations::Receipt::Pending){if(job.test_skip&&!job.skip_failed)declarations_.Canceled(job.owner,job.declaration_id,job.test_skip->request,received);else if(job.local_skip&&!job.skip_failed)declarations_.Canceled(job.owner,job.declaration_id,job.local_skip->request,received);else{const auto error=MessageJSON(job.error.empty()?"Native append body lacks successful physical receipt":job.error);const bool disposed=!job.callback_retry.Empty()&&job.typed_error&&!job.retry_infrastructure&&!job.step_canceled&&!job.test_skip&&!job.local_skip;if(disposed)RetryFailure(job.owner,job.callback_retry,error,false);declarations_.Failed(job.owner,job.declaration_id,error,received,disposed);}}}
      callback=callbacks_.find(Key(job.owner));
      if(callback!=callbacks_.end()&&!callback->second.exited){
        if(job.test_skip&&!job.skip_failed)hooks_.send(Command(job,"TEST_SKIP_RESULT")+" "+Identity(job)+" "+std::to_string(job.test_skip->request)+" -");
        else if(job.local_skip&&!job.skip_failed)hooks_.send(Command(job,"LOCAL_SKIP_RESULT")+" "+Identity(job)+" "+std::to_string(job.local_skip->request)+" "+std::to_string(job.local_skip->source_step)+" -");
        else hooks_.send(Command(job,"RESULT")+" "+Identity(job)+" "+(job.error.empty()?"1 -":"0 "+Wire(job.error)+(job.callback_retry.Empty()?"":job.retry_infrastructure?" I":" O")));
      }
      if(!job.error.empty()&&!job.step_canceled&&!job.test_skip&&!job.local_skip){StepFailure(job,job.error);const auto* declaration=job.declaration_id?declarations_.Find(job.declaration_id):nullptr;if(!declaration||!declaration->retry_failure_disposed)RetryFailure(job.owner,job.callback_retry,MessageJSON(job.error),job.retry_infrastructure);}
      completed_.emplace(job.id,job.owner);if(budget_)--budget_->physical;it=jobs_.erase(it);
    }
    if(mutation_receipts_)mutation_receipts_->Tick(DedicatedRealMilliseconds());
    DrainSteps();Drain(DedicatedRealMilliseconds());
  }
  void CancelAll(const std::string& error,double now){for(auto&[key,callback]:callbacks_){(void)key;declarations_.Close(callback.owner);MutationClose(callback.owner);callback.closed=true;}for(auto&[id,job]:jobs_){(void)id;Stop(job,error,now);}}
  template<class Add>void ForEachPollDescriptor(Add&& add)const{for(const auto&[key,job]:jobs_){(void)key;if(!job.process)continue;for(const auto& fd:job.process->PollDescriptors())add(fd);}}
  std::vector<pollfd> PollDescriptors()const{std::vector<pollfd> fds;for(const auto& [id,job]:jobs_){(void)id;if(!job.process)continue;const auto actual=job.process->PollDescriptors();fds.insert(fds.end(),actual.begin(),actual.end());}return fds;}
  bool OwnsPendingView(const StepOwner& owner)const{
    const auto callback=callbacks_.find(Key(owner));
    return callback!=callbacks_.end()&&callback->second.owner==owner&&MutationPending(owner);
  }
  size_t Physical()const{return jobs_.size();}
  bool IdleReady()const{return jobs_.empty()&&callbacks_.empty()&&step_fences_.empty()&&(!mutation_receipts_||!mutation_receipts_->PendingCount());}
  size_t ReservedBytes()const{return reserved_;}
  const std::vector<AttachmentRecord>& Records()const{return records_;}
  const std::vector<AttachmentDeclarations::Record>& Declarations()const{return declarations_.Records();}
  const std::filesystem::path& Directory()const{return directory_;}
  template<class Visit>void ForEachAttachmentProjection(Visit&& visit)const{
    if(!IdleReady())throw std::runtime_error("Native attachment projection still owns callback/file/view receipts");
    for(const auto& [key,journal]:mutation_journals_){(void)key;visit(*journal);}
  }
  const std::vector<AttachmentViewReceipts::Record>& PublicationViewReceipts()const{
    if(!IdleReady())throw std::runtime_error("Native attachment view receipt report is not physically idle");
    static const std::vector<AttachmentViewReceipts::Record> empty;
    return mutation_receipts_?mutation_receipts_->Records():empty;
  }
 private:
  struct TestSkip {uint64_t request;std::string reason_json;};
  struct Callback {StepOwner owner;double deadline,opened;bool closed=false,exited=false;std::function<void(const std::optional<std::string>&)> forward{};std::optional<TestSkip> test_skip=std::nullopt;std::optional<std::string> mutation_error=std::nullopt;};
  struct LocalSkip {uint64_t request=0,source_step=0;};
  struct Job {uint64_t step_id=0,id=0,request=0,declaration_id=0,expected=0,sequence=0,actual_bytes=0,device=0,inode=0,directory_device=0,directory_inode=0;StepOwner owner;double started=0,job_cap=0,committed_at=0,stopped=0;size_t reservation=0,chunk_pending=0;bool admitted=false,native_open=false,finish_sent=false,ready=false,committed=false,cleaning=false,cleaned=false,skip_failed=false,step_canceled=false;std::optional<TestSkip> test_skip=std::nullopt;std::optional<LocalSkip> local_skip=std::nullopt;std::string kind,name,type,basename,path,error,directory,raw_error;AttachmentRetryProvenance callback_retry{};bool retry_infrastructure=false,typed_error=false;std::unique_ptr<DedicatedHostProcess> process;};
  struct StepFence {StepOwner owner;std::function<void(const std::optional<std::string>&)> forward;std::optional<std::string> error=std::nullopt;};
  static bool Valid(const StepOwner& o){return o.test_id&&o.attempt_generation&&o.scope&&o.scope_generation&&o.pid>0&&o.pid<=INT32_MAX&&(o.callback_kind=="body"||o.callback_kind=="beforeEach"||o.callback_kind=="afterEach"||o.callback_kind=="beforeAll"||o.callback_kind=="afterAll")&&(o.callback_kind=="body"||o.callback_token);}
  static std::string Key(uint64_t token,uint64_t scope,uint64_t generation){return std::to_string(token)+":"+std::to_string(scope)+":"+std::to_string(generation);}
  static std::string Key(const StepOwner& o){return Key(o.callback_token,o.scope,o.scope_generation);}
  static bool Match(const StepOwner& o,uint64_t token,uint64_t scope,uint64_t generation){return o.callback_token==token&&o.scope==scope&&o.scope_generation==generation;}
  static std::string Wire(std::string_view message){return StepWire(AttachmentPathJSON(attachment_files::Message(message)));}
  static std::string Prefix(const std::string& name){
    // Metadata is checked JSON. Keep ASCII filename characters only; escaped
    // code units cannot acquire filesystem meaning or directory separators.
    std::string out;for(size_t i=1;i+1<name.size()&&out.size()<80;++i){char c=name[i];if(c=='\\'){if(i+1<name.size()&&name[i+1]=='u'){unsigned value=0;for(size_t j=2;j<6;++j)value=(value<<4)|static_cast<unsigned>(HexDigit(name[i+j]));c=value<128?static_cast<char>(value):'-';i+=5;}else{++i;c='-';}}if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))out+=c;else if(out.empty()||out.back()!='-')out+='-';}return out;
  }
  static std::string DefaultType(const std::string& kind,const std::string& path){if(kind=="S")return "\"text/plain\"";if(kind=="P"){const auto ext=std::filesystem::path(path).extension().string();if(ext==".txt")return "\"text/plain\"";if(ext==".json")return "\"application/json\"";if(ext==".png")return "\"image/png\"";if(ext==".jpg"||ext==".jpeg")return "\"image/jpeg\"";if(ext==".html")return "\"text/html\"";}return "\"application/octet-stream\"";}
  static std::string Extension(const std::string& type){if(AttachmentASCII(type)=="text/plain")return ".txt";if(AttachmentASCII(type)=="image/png")return ".png";if(AttachmentASCII(type)=="image/jpeg")return ".jpg";if(AttachmentASCII(type)=="application/json")return ".json";if(AttachmentASCII(type)=="text/html")return ".html";return ".dat";}
  static std::string Identity(const Job& job){return std::to_string(job.request)+" "+std::to_string(job.owner.callback_token)+" "+std::to_string(job.owner.scope)+" "+std::to_string(job.owner.scope_generation)+" "+std::to_string(job.id)+(job.step_id?" "+std::to_string(job.step_id):"");}
  static std::string Command(const Job& job,const std::string& suffix){return std::string(job.step_id?"ATTACH_STEP_":"ATTACH_")+suffix;}
  static std::string MessageJSON(std::string_view error){return AttachmentPathJSON(attachment_files::Message(error));}
  AttachmentRetryProvenance ReadRetry(const std::vector<std::string>& fields,size_t base,const StepOwner& owner,uint64_t public_step,double now,bool continuation)const{
    if(fields.size()==base)return {};
    if(fields.size()!=base+3)throw std::runtime_error("Native attachment callback retry shape invalid");
    AttachmentRetryProvenance retry{attachment_files::UInt(fields[base]),attachment_files::UInt(fields[base+1]),attachment_files::UInt(fields[base+2])};
    if(!retry.Complete()||!hooks_.validate_retry||!hooks_.retry_failure)throw std::runtime_error("Native attachment callback retry has no native controller");
    hooks_.validate_retry(owner,public_step,retry,now,continuation);return retry;
  }
  void RetryFailure(const StepOwner& owner,const AttachmentRetryProvenance& retry,const std::string& error,bool infrastructure){
    if(retry.Empty())return;
    if(!retry.Complete()||!hooks_.retry_failure)throw std::runtime_error("Native attachment callback retry lost native failure controller");
    hooks_.retry_failure(owner,retry,error,infrastructure);
  }
  bool Within(uint64_t id,uint64_t root)const{for(unsigned depth=0;id&&depth<=TestSteps::kDepth;++depth){if(id==root)return true;const auto* step=hooks_.step?hooks_.step(id):nullptr;if(!step)return false;id=step->parent;}return false;}
  bool AuthenticLocalCancellation(const Job& job)const{
    if(!job.local_skip||job.step_canceled||job.skip_failed||!job.raw_error.empty()||!hooks_.step)return false;
    const auto* actual=hooks_.step(SkipStepID(job,job.local_skip->source_step));const auto* source=hooks_.step(job.local_skip->source_step);
    return actual&&source&&(!actual->callback_received||actual->callback_ok)&&(!source->callback_received||source->callback_ok)&&actual->owner==job.owner&&source->owner==job.owner&&actual->status==StepStatus::Skipped&&source->status==StepStatus::Skipped&&actual->receipt==StepReceipt::Pending&&source->receipt==StepReceipt::Pending&&actual->step_skip&&source->step_skip&&*actual->step_skip==*source->step_skip&&actual->step_skip->request==job.local_skip->request&&actual->step_skip->source_step==job.local_skip->source_step;
  }
  bool OwnsStep(const Job& job,uint64_t id)const{return (job.step_id&&Within(job.step_id,id))||(!job.callback_retry.Empty()&&Within(job.callback_retry.iteration_step,id));}
  uint64_t SkipStepID(const Job& job,uint64_t source)const{return job.step_id&&Within(job.step_id,source)?job.step_id:job.callback_retry.iteration_step;}
  bool StepExpired(const Job& job,double now)const{
    const auto expired=[&](uint64_t id){if(!id)return false;const auto* step=hooks_.step?hooks_.step(id):nullptr;return !step||step->owner!=job.owner||now>=step->deadline;};
    return expired(job.step_id)||(!job.callback_retry.Empty()&&expired(job.callback_retry.iteration_step));
  }
  bool FailureAffects(const Job& job,uint64_t fence)const{return job.callback_retry.Empty()||job.retry_infrastructure||fence==job.callback_retry.iteration_step||(job.step_id&&Within(job.step_id,job.callback_retry.iteration_step)&&Within(job.step_id,fence)&&Within(fence,job.callback_retry.iteration_step));}
  void StepFailure(const Job& job,const std::string& error){if(!job.step_id&&job.callback_retry.Empty())return;for(auto&[id,fence]:step_fences_)if(fence.owner==job.owner&&OwnsStep(job,id)&&FailureAffects(job,id)&&!fence.error)fence.error=MessageJSON(error);}
  void DrainSteps(){for(auto it=step_fences_.begin();it!=step_fences_.end();){if(PendingStep(it->second.owner,it->first)){++it;continue;}auto forward=std::move(it->second.forward);const auto error=it->second.error;it=step_fences_.erase(it);forward(error);}}
  void SendJob(Job& job,const std::string& packet){if(!job.process->Send(packet))throw std::runtime_error("Native attachment file control queue rejected");}
  void Stop(Job& job,const std::string& error,double now){if(StepExpired(job,now))job.step_canceled=true;if(job.error.empty())job.error=error;if(!job.stopped)job.stopped=now;if(!job.cleaning&&job.process)job.process->Kill();}
  void Receipt(Job& job,const std::vector<std::string>& f,double now){
    if(f.size()<2||attachment_files::UInt(f[1])!=job.id)throw std::runtime_error("Native attachment file receipt identity mismatch");
    if(f[0]=="ERROR"){
      if(f.size()!=(job.callback_retry.Empty()||job.cleaning?5:6))throw std::runtime_error("Native attachment file error shape invalid");
      if(!job.callback_retry.Empty()&&!job.cleaning){if(job.typed_error||(f[5]!="O"&&f[5]!="I"))throw std::runtime_error("Native attachment typed failure origin invalid");job.typed_error=true;job.retry_infrastructure=f[5]=="I";if(job.retry_infrastructure)hooks_.failure("Native attachment writer reported infrastructure failure");}
      const auto actual=attachment_files::UInt(f[2]);const auto stage=attachment_files::Unhex(f[3],100),message=attachment_files::Unhex(f[4],8192);if(job.step_id)StepFailure(job,message+" ("+stage+", errno="+std::to_string(actual)+")");if(job.step_id&&job.step_canceled&&!job.test_skip&&!job.local_skip&&!job.cleaning){job.raw_error=message+" ("+stage+", errno="+std::to_string(actual)+")";hooks_.failure(job.raw_error);}if((job.test_skip||job.local_skip)&&!job.cleaning)job.raw_error=message+" ("+stage+", errno="+std::to_string(actual)+")";if(job.error.empty())job.error=message+" ("+stage+", errno="+std::to_string(actual)+")";else if(job.cleaning)job.error+="; actual rollback: "+message+" ("+stage+", errno="+std::to_string(actual)+")";
      return;
    }
    if(job.cleaning){
      if(f.size()!=2||f[0]!="CLEANED"||job.cleaned)throw std::runtime_error("Native attachment rollback receipt is invalid");
      job.cleaned=true;return;
    }
    if(f[0]=="DIRECTORY"){
      if(f.size()!=5||!job.directory.empty())throw std::runtime_error("Native attachment duplicate directory receipt");
      const auto directory=attachment_files::Unhex(f[2],8192);const auto actual=std::filesystem::path(directory);
      if(!actual.is_absolute()||actual.parent_path()!=parent_||!actual.filename().string().starts_with("suitecut-attachments-")||!JsonString(AttachmentPathJSON(directory),8192))throw std::runtime_error("Native attachment output directory receipt is invalid");
      job.directory=directory;job.directory_device=attachment_files::UInt(f[3]);job.directory_inode=attachment_files::UInt(f[4]);job.path=(actual/job.basename).string();directory_=actual;
      const auto callback=callbacks_.find(Key(job.owner));
      if(job.error.empty()&&(callback==callbacks_.end()||now>=callback->second.deadline||StepExpired(job,now)||now>=job.job_cap))Stop(job,"Native attachment deadline expired at real directory receipt",now);
      return;
    }
    if(f[0]=="OPEN"){
      if(f.size()!=4||job.native_open||job.directory.empty())throw std::runtime_error("Native attachment duplicate file-open receipt");
      job.device=attachment_files::UInt(f[2]);job.inode=attachment_files::UInt(f[3]);job.native_open=true;
      const auto callback=callbacks_.find(Key(job.owner));
      if(job.error.empty()&&(callback==callbacks_.end()||now>=callback->second.deadline||StepExpired(job,now)||now>=job.job_cap))Stop(job,"Native attachment deadline expired at real file-open receipt",now);
      if(job.error.empty()){job.admitted=true;hooks_.send(Command(job,"ADMITTED")+" "+Identity(job)+" 1 -");}return;
    }
    if(!job.error.empty())return;
    const auto callback=callbacks_.find(Key(job.owner));if(callback==callbacks_.end()||now>=callback->second.deadline||StepExpired(job,now)||now>=job.job_cap){Stop(job,"Native attachment original callback/operation deadline expired",now);return;}
    if(f[0]=="CHUNK_OK"){

      if(f.size()!=4||!job.chunk_pending||attachment_files::UInt(f[2])!=job.sequence||attachment_files::UInt(f[3])!=job.actual_bytes+job.chunk_pending)throw std::runtime_error("Native attachment write acknowledgement mismatch");
      job.actual_bytes+=job.chunk_pending;job.chunk_pending=0;hooks_.send(Command(job,"CHUNK_OK")+" "+Identity(job)+" "+std::to_string(job.sequence));
    }else if(f[0]=="READY"){
      if(f.size()!=5||job.ready||!job.admitted||job.chunk_pending||attachment_files::UInt(f[3])!=job.device||attachment_files::UInt(f[4])!=job.inode)throw std::runtime_error("Native attachment file ready identity mismatch");
      const auto bytes=attachment_files::UInt(f[2]);if(bytes>job.reservation||(job.kind!="P"&&(!job.finish_sent||bytes!=job.expected)))throw std::runtime_error("Native attachment real file bytes exceed reservation");job.actual_bytes=bytes;job.ready=true;SendJob(job,"COMMIT "+std::to_string(job.id));
    }else if(f[0]=="COMMITTED"){
      if(f.size()!=3||!job.ready||job.committed||attachment_files::UInt(f[2])!=job.actual_bytes)throw std::runtime_error("Native attachment actual commit receipt mismatch");
      job.committed=true;job.committed_at=now;
    }else throw std::runtime_error("Unknown native attachment file receipt");
  }
  void Drain(double){for(auto it=callbacks_.begin();it!=callbacks_.end();){auto& callback=it->second;if((callback.forward||callback.exited)&&!Pending(callback.owner.callback_token,callback.owner.scope,callback.owner.scope_generation)){auto forward=std::move(callback.forward);std::optional<std::string> failure=callback.mutation_error;for(const auto& declaration:declarations_.Records())if(declaration.owner==callback.owner&&declaration.callback_retry.Empty()&&(declaration.receipt==AttachmentDeclarations::Receipt::Failed||declaration.receipt==AttachmentDeclarations::Receipt::Rejected)&&!failure)failure=declaration.error_json;declarations_.RetireContext(callback.owner);MutationRetire(callback.owner);if(!callback.exited)hooks_.send("ATTACH_CONTEXT_RETIRED "+std::to_string(callback.owner.callback_token)+" "+std::to_string(callback.owner.scope)+" "+std::to_string(callback.owner.scope_generation));it=callbacks_.erase(it);if(forward)forward(failure);}else ++it;}}
  std::filesystem::path parent_,directory_;Hooks hooks_;Budget* budget_;AttachmentDeclarations declarations_;std::map<std::string,Callback> callbacks_;std::map<uint64_t,Job> jobs_;std::map<uint64_t,StepFence> step_fences_;std::map<uint64_t,StepOwner> completed_;std::vector<AttachmentRecord> records_;uint64_t id_=0,nonce_=0;size_t accepted_=0,reserved_=0;
#include "runner_attachment_mutations.inc"
};
} // namespace suitecut::runner
