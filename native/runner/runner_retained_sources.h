// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_options.h"
#include "host_process.h"
#include "project_descendants.h"
#include "retained_source_receiver.h"
#include "test_steps.h"
#include "source_callback_iteration_receipt.h"
#include <exception>
#include <functional>
#include <string>
namespace suitecut::runner {
// Native adapter for one actual SDK host. Authentic accepted-work receipts
// never relax the separate unknown-child/cohort inventory requirement.
class RunnerRetainedSources {
 public:
  using Policy=RetainedSourceLeases;
  struct Selection {
    uint64_t catalog=0,declaration=0,token=0,owner=0,test=0,generation=0;
    std::string key;Scope scope=Scope::Test;
    FixtureOptionCatalog::Origin origin=FixtureOptionCatalog::Origin::DeclaredLiteral;
    double deadline=0;
    Policy::SetupScope callback_scope=Policy::SetupScope::Test;
  };
  // Issued by the actual native callback/step controller. The validator must
  // look up these same records; an SDK tuple never establishes this owner.
  struct CallbackIterationOwner {
    StepOwner callback;uint64_t request=0,outer_step=0,iteration_step=0,assertion=0,invocation=0;
    bool operator==(const CallbackIterationOwner&)const=default;
  };
  struct Hooks {std::function<void(const std::string&)> send;std::function<bool(const CallbackIterationOwner&)> iterationOwner={};};
  RunnerRetainedSources(Policy& policy,Hooks hooks):policy_(policy),receiver_(policy),hooks_(std::move(hooks)){}
  int PeerDescriptor()const noexcept{return receiver_.PeerDescriptor();}
  void HostLaunched(const Policy::Host& host,DedicatedHostProcess& actual){if(actual.Pid()!=host.pid)throw std::runtime_error("Native Source binding differs from actual owned host");receiver_.HostLaunched(host);host_=host;actual_host_=&actual;}
  void Enable(unsigned bridge_version=1){if((bridge_version!=1&&bridge_version!=2)||enabled_||!host_.pid)throw std::runtime_error("Native Source capability duplicate/unowned");enabled_=true;bridge_version_=bridge_version;}
  bool Enabled()const noexcept{return enabled_;}
  // Called by the native fixture dispatcher before its genuine SETUP. The
  // selected option origin is authoritative, including configured callbacks
  // replaced by constants. No authored key is parsed into a declaration ID.
  std::optional<std::string> Setup(const Selection& selection,double now){
    if(!enabled_||selection.origin!=FixtureOptionCatalog::Origin::Callback||selection.scope!=Scope::Worker)return std::nullopt;
    if(selection.callback_scope!=Policy::SetupScope::Test&&bridge_version_<2)return std::nullopt;
    if((selection.callback_scope==Policy::SetupScope::Test)!=static_cast<bool>(selection.test))return std::nullopt;
    if(!selection.catalog||!selection.declaration||!selection.token||!selection.owner||!selection.generation||selection.key.empty()||selection.key.size()>256||!std::isfinite(selection.deadline)||selection.deadline<=now)throw std::runtime_error("Native Source selected callback identity/deadline invalid");
    for(size_t i=0;i<setups_;++i)if(contexts_[i].selection.token==selection.token)throw std::runtime_error("Native Source callback token reused");
    // Optional registration instrumentation cannot reject otherwise valid
    // fixture admission. Retain prior ticket contexts; choose fresh-only when
    // the fixed history is full, rather than replacing a live continuation.
    if(setups_==contexts_.size())return std::nullopt;
    contexts_[setups_++]={selection,true};
    return "SOURCE_SETUP "+Number(selection.catalog)+" "+Number(selection.declaration)+" "+Number(selection.token)+" "+Number(selection.owner)+" "+Number(selection.test)+" "+Number(selection.generation)+" "+selection.key+(bridge_version_>=2?" "+std::string(CallbackScopeName(selection.callback_scope)):"");
  }
  void SetupClosed(uint64_t token,uint64_t owner,uint64_t generation)noexcept{for(size_t i=0;i<setups_;++i){auto& context=contexts_[i];if(context.selection.token==token&&context.selection.owner==owner&&context.selection.generation==generation)context.open=false;}}
  bool Packet(std::span<const std::string> fields,double now,bool stopped){
    if(!fields.empty()&&(fields[0]=="SOURCE_WORK_READY"||fields[0]=="SOURCE_WORK_HOST_RESUMED")){
      if(bridge_version_<2||!callback_fence_||fields.size()!=(fields[0]=="SOURCE_WORK_READY"?3:4))throw std::runtime_error("Native callback Source receipt has no exact pending fence");
      auto& fence=*callback_fence_;const auto& work=receiver_.Work();
      if(!work||UInt(fields[1])!=work->challenge.ticket||UInt(fields[2])!=work->challenge.nonce)throw std::runtime_error("Native callback Source receipt has a foreign challenge");
      if(stopped)CancelCallbackIterationFence(fence.owner);
      if(fields[0]=="SOURCE_WORK_READY"){
        if(fence.host_ready||!work->acknowledged)throw std::runtime_error("Native callback Source ready lacks genuine native acknowledgement");
        fence.host_ready=true;fence.host_ready_at=now;
      }else{
        if(fence.host_resumed||!fence.host_resume_sent||!work->resumed||UInt(fields[3])!=fence.resume_generation)throw std::runtime_error("Native callback Source resumed receipt is foreign or premature");
        fence.host_resumed=true;fence.host_resumed_at=now;
      }
      Tick(now);return true;
    }
    if(!fields.empty()&&fields[0]=="SOURCE_WORK_OBSERVED"){
      if(bridge_version_<2||fields.size()!=9)throw std::runtime_error("Native Source decoder observation shape/bridge invalid");
      const RetainedSourceWorkWire::Cuts cuts{UInt(fields[1]),UInt(fields[2]),{UInt(fields[3]),UInt(fields[4]),UInt(fields[5])},{UInt(fields[6]),UInt(fields[7]),UInt(fields[8])}};
      if(stopped)receiver_.CancelAcceptedWork();
      if(!receiver_.ObserveOriginalDecoder(cuts,now))throw std::runtime_error("Native Source decoder observation has no exact native-issued cut");
      Tick(now);return true;
    }
    if(fields.empty()||(fields[0]!="SOURCE_REGISTER"&&fields[0]!="SOURCE_WAIT"&&fields[0]!="SOURCE_CANCEL"))return false;
    if(!enabled_||fields.size()!=(fields[0]=="SOURCE_REGISTER"?10:6))throw std::runtime_error("Native Source packet shape/capability invalid");
    const auto nonce=UInt(fields[1]);if(!nonce||nonce<=nonce_)throw std::runtime_error("Native Source request nonce stale/replayed");
    if(fields[0]=="SOURCE_REGISTER"){
      const auto& context=Match(fields[2],fields[3],fields[4],fields[5],fields[6],fields[7],fields[8]);
      const auto executable=ExecutablePath(fields[9]);nonce_=nonce;
      if(!context.open||stopped||now>=context.selection.deadline){hooks_.send("SOURCE_REGISTERED "+Number(nonce)+" "+Number(context.selection.token)+" "+Number(context.selection.owner)+" "+Number(context.selection.generation)+" 2 0 0 0");return true;}
      std::optional<Policy::Ticket> ticket;
      {struct stat actual{};if(stat(executable.c_str(),&actual)==0&&S_ISREG(actual.st_mode))ticket=receiver_.Reserve({context.selection.catalog,context.selection.declaration,context.selection.token,context.selection.test,context.selection.generation,context.selection.callback_scope,context.selection.owner},{static_cast<uint64_t>(actual.st_dev),static_cast<uint64_t>(actual.st_ino)},context.selection.deadline,now);else throw std::runtime_error("Native Source executable unavailable at registration");}
      hooks_.send("SOURCE_REGISTERED "+Number(nonce)+" "+Number(context.selection.token)+" "+Number(context.selection.owner)+" "+Number(context.selection.generation)+(ticket?" 1 "+Number(ticket->id)+" 4 "+Number(receiver_.PeerCookie()):" 0 0 0 0"));
    }else{
      const auto id=UInt(fields[2]),token=UInt(fields[3]),owner=UInt(fields[4]),generation=UInt(fields[5]);const auto& ticket=receiver_.Registration();
      if(!ticket||ticket->id!=id||ticket->owner.fixture.token!=token||ticket->owner.fixture.generation!=generation||!HasOwner(token,owner,generation))throw std::runtime_error("Native Source continuation owner differs from actual reservation");
      nonce_=nonce;
      if(fields[0]=="SOURCE_CANCEL"){
        if(!policy_.CancelStartup(*ticket,now))throw std::runtime_error("Native Source cancellation has no retained reservation");
        hooks_.send("SOURCE_CANCELLED "+Number(nonce)+" "+Number(id)+" "+Number(token)+" "+Number(owner)+" "+Number(generation));
      }else{
        if(wait_)throw std::runtime_error("Native Source startup wait already pending");
        wait_=Wait{nonce,id,token,owner,generation};
      }
    }
    Tick(now);return true;
  }
  bool BeginAcceptedIdle(const RetainedSourceWorkWire::Borrow& borrow,double end,double now){
    // Source worker reuse remains disabled after iteration fences too. Do not
    // reinterpret an old reuse generation as a newer callback authority epoch.
    if(bridge_version_<2||!receiver_.AdmissionActive()||idle_borrow_||callback_fence_||callback_generation_||now>=end)return false;
    idle_borrow_=borrow;idle_end_=end;
    if(!receiver_.InstallAcceptedWork())return false;
    Tick(now);return true;
  }
  // end is the caller's fixed original physical-cleanup grant, not a new
  // assertion/author budget. The controller separately vetoes RUN/pass after
  // its logical cap and validates this owner while genuinely Draining.
  bool BeginCallbackIterationFence(const CallbackIterationOwner& owner,double end,double now){
    if(bridge_version_<2||!receiver_.AdmissionActive()||idle_borrow_||callback_fence_||receiver_.Work()||
       !ValidCallbackIteration(owner)||!std::isfinite(now)||!std::isfinite(end)||now>=end||
       completed_callback_fences_>=64||callback_generation_>UINT64_MAX-2)return false;
    // Store ownership before INSTALL/BEGIN/host writes: uncertain delivery may
    // only select real cleanup, never erase the live iteration or Source lease.
    callback_fence_.emplace(CallbackFence{owner,end,callback_generation_+1,callback_generation_+2});
    callback_generation_+=2;
    if(!receiver_.AcceptedWorkRequested()&&!receiver_.InstallAcceptedWork())return false;
    Tick(now);return true;
  }
  bool CallbackIterationFenceReady(const CallbackIterationOwner& owner,double now)const{
    return callback_fence_&&callback_fence_->owner==owner&&!callback_fence_->canceled&&
      std::isfinite(now)&&now<callback_fence_->deadline&&ValidCallbackIteration(owner)&&
      callback_fence_->host_ready&&callback_fence_->host_resumed&&receiver_.AcceptedWorkReady(now)&&
      receiver_.Work()->resumed;
  }
  bool CallbackIterationFenceRefused(const CallbackIterationOwner& owner)const{
    return callback_fence_&&callback_fence_->owner==owner&&
      (callback_fence_->canceled||(receiver_.Work()&&receiver_.Work()->refusal));
  }
  void CancelCallbackIterationFence(const CallbackIterationOwner& owner){
    if(!callback_fence_||callback_fence_->owner!=owner)throw std::runtime_error("Native callback Source cancellation has foreign ownership");
    callback_fence_->canceled=true;receiver_.CancelAcceptedWork();
  }
  std::optional<SourceCallbackIterationReceipt> CallbackIterationFenceReceipt(const CallbackIterationOwner& owner,double now)const{
    if(!CallbackIterationFenceReady(owner,now))return std::nullopt;
    const auto& work=*receiver_.Work();const auto& fence=*callback_fence_;
    return SourceCallbackIterationReceipt{work.challenge,*work.cuts,fence.resume_generation,work.observed,work.acknowledged,work.resumed,
      fence.host_ready,fence.host_resumed,work.observed_at,work.acknowledged_at,work.resumed_at,fence.host_ready_at,fence.host_resumed_at};
  }
  const CallbackIterationOwner* PendingCallbackIterationOwner()const noexcept{return callback_fence_?&callback_fence_->owner:nullptr;}
  bool FinishCallbackIterationFence(const CallbackIterationOwner& owner,double now){
    if(!CallbackIterationFenceReady(owner,now))return false;
    if(!receiver_.FinishAcceptedResume(callback_fence_->resume_generation))throw std::runtime_error("Native callback Source resume lost its actual receipts");
    callback_fence_.reset();work_expect_sent_=work_ack_sent_=work_refusal_sent_=false;++completed_callback_fences_;return true;
  }
  bool AcceptedIdleReady(double now)const noexcept{return idle_borrow_&&receiver_.AcceptedWorkReady(now);}
  bool AcceptedIdleRefused()const noexcept{return receiver_.Work()&&receiver_.Work()->refusal;}
  void Tick(double now){
    if(actual_host_&&!host_exited_&&actual_host_->Exited()){host_exited_=true;wait_.reset();Cancel(now);}
    receiver_.Tick(now);
    if((idle_borrow_||callback_fence_)&&receiver_.AdmissionActive()){
      const auto end=callback_fence_?callback_fence_->deadline:idle_end_;
      if(callback_fence_&&(now>=end||!ValidCallbackIteration(callback_fence_->owner)))callback_fence_->canceled=true;
      if(now>=end||(callback_fence_&&callback_fence_->canceled))receiver_.CancelAcceptedWork();
      else if(receiver_.AcceptedWorkInstalled()&&!receiver_.Work()){
        if(work_sequence_==UINT64_MAX)throw std::runtime_error("Native Source work nonce exhausted");
        const auto borrow=callback_fence_?RetainedSourceWorkWire::Borrow{host_.lease,callback_fence_->generation,host_.worker,host_.pid,host_.start}:*idle_borrow_;
        const auto challenge=RetainedSourceWorkWire::Issue(receiver_.Registration()->id,++work_sequence_,borrow.lease,borrow.generation,end);
        if(!receiver_.BeginAcceptedWork(borrow,challenge,now))throw std::runtime_error("Native Source work challenge lost its actual parent borrow");
        hooks_.send("SOURCE_WORK_BEGIN "+Number(challenge.ticket)+" "+Number(challenge.nonce)+" "+Number(challenge.borrow_lease)+" "+Number(challenge.borrow_generation)+" "+challenge.deadline_text+(callback_fence_?" callbackIteration":""));
      }
      if(const auto& work=receiver_.Work();work){
        if(work->cuts&&!work_expect_sent_){hooks_.send(WorkSidecar("SOURCE_WORK_EXPECT",*work->cuts));work_expect_sent_=true;}
        if(work->acknowledged&&!work_ack_sent_&&!work->canceled){hooks_.send(WorkSidecar("SOURCE_WORK_ACK",*work->cuts));work_ack_sent_=true;}
        if(work->refusal&&!work_refusal_sent_){hooks_.send("SOURCE_WORK_REFUSE "+Number(work->challenge.ticket)+" "+Number(work->challenge.nonce)+" "+Number(work->refusal));work_refusal_sent_=true;}
        if(callback_fence_&&!callback_fence_->canceled&&now<end&&!work->refusal){
          auto& fence=*callback_fence_;
          if(fence.host_ready&&work->acknowledged&&!fence.resume_requested){
            fence.resume_requested=true;
            if(!receiver_.ResumeAcceptedWork(fence.resume_generation,now))throw std::runtime_error("Native callback Source resume lacks exact completed barrier");
          }
          if(work->resumed&&!fence.host_resume_sent){
            fence.host_resume_sent=true;
            hooks_.send("SOURCE_WORK_RESUME "+Number(work->challenge.ticket)+" "+Number(work->challenge.nonce)+" "+Number(fence.resume_generation));
          }
        }
      }
    }
    if(wait_){
      const auto& ticket=*receiver_.Registration();const auto& row=policy_.Records()[ticket.id-1];
      if(receiver_.StartupObserved()||row.state==Policy::State::Closing||row.state==Policy::State::Exited||row.state==Policy::State::Retired||now>=row.startup_deadline){
        const auto wait=*wait_;wait_.reset();const bool ready=receiver_.AdmissionActive();const auto& process=receiver_.ProcessIdentity();
        hooks_.send("SOURCE_STARTED "+Number(wait.nonce)+" "+Number(wait.ticket)+" "+Number(wait.token)+" "+Number(wait.owner)+" "+Number(wait.generation)+(ready?" 1 "+Number(static_cast<uint64_t>(process->pid))+" "+Number(process->start):" 0 0 0"));
      }
    }
  }
  void Cancel(double now){if(callback_fence_)callback_fence_->canceled=true;receiver_.CancelAcceptedWork();for(size_t i=0;i<setups_;++i)contexts_[i].open=false;if(receiver_.Registration())policy_.CancelStartup(*receiver_.Registration(),now);}
  // An actor that already failed still owns its real channel and child. Drain
  // one bounded receiver tick without allowing a malformed queued receipt to
  // bypass the original host/domain cleanup. Retain the first error for native
  // infrastructure reporting; it is never an idle or successful-work receipt.
  // No further startup response may admit author work during this drain.
  void Drain(double now)noexcept{wait_.reset();try{Cancel(now);receiver_.Tick(now);}catch(...){if(!drain_error_)drain_error_=std::current_exception();}}
  const std::exception_ptr& DrainFailure()const noexcept{return drain_error_;}
  void DomainArrived(ProjectDescendants& domain,uint64_t generation){
    if(!actual_host_||!domain.Arrived(generation,host_.worker,*actual_host_)||(domain_&&(domain_!=&domain||generation_!=generation)))throw std::runtime_error("Native Source descendant arrival is not the exact actual batch owner");
    domain_=&domain;generation_=generation;
  }
  bool Retire(const Policy::HostRetirement& receipt,double now){if(!actual_host_||!actual_host_->Exited()||!domain_||receipt.batch!=generation_||domain_->Pending(generation_))return false;Tick(now);return RetireDrained(receipt,now);}
  // The emergency actor uses this after Drain, then reports DrainFailure (or
  // rethrows its original exception) only after the real physical fence. A
  // cleanup protocol error cannot be turned into a successful report.
  bool RetireDrained(const Policy::HostRetirement& receipt,double now){if(!actual_host_||!actual_host_->Exited()||!domain_||receipt.batch!=generation_||domain_->Pending(generation_))return false;if(!receiver_.Registration())return receipt.host==host_&&receipt.actual_waitpid&&receipt.descendants_retired&&receiver_.ChannelDrained();return receiver_.Retire(receipt,now);}
  size_t Physical()const noexcept{return receiver_.Registration()&&!receiver_.Retired()?1:0;}
  bool ChannelDrained()const noexcept{return receiver_.ChannelDrained();}
  template<class Visitor>void ForEachPollDescriptor(Visitor&& visitor)const{receiver_.ForEachPollDescriptor(std::forward<Visitor>(visitor));}
  const RetainedSourceReceiver& Receiver()const noexcept{return receiver_;}
 private:
  struct CallbackFence {
    CallbackIterationOwner owner;double deadline=0;uint64_t generation=0,resume_generation=0;
    bool canceled=false,host_ready=false,resume_requested=false,host_resume_sent=false,host_resumed=false;
    double host_ready_at=0,host_resumed_at=0;
  };
  bool ValidCallbackIteration(const CallbackIterationOwner& owner)const{
    return owner.request&&owner.assertion&&owner.invocation&&owner.iteration_step&&owner.outer_step!=owner.iteration_step&&
      owner.callback.test_id&&owner.callback.attempt_generation&&owner.callback.scope&&owner.callback.scope_generation&&
      owner.callback.worker_index==host_.worker&&owner.callback.pid==host_.pid&&
      hooks_.iterationOwner&&hooks_.iterationOwner(owner);
  }
  struct Context {Selection selection;bool open=false;};struct Wait {uint64_t nonce,ticket,token,owner,generation;};
  static std::string WorkSidecar(const char* name,const RetainedSourceWorkWire::Cuts& value){return std::string(name)+" "+Number(value.ticket)+" "+Number(value.nonce)+" "+Number(value.reader.instance)+" "+Number(value.reader.sequence)+" "+Number(value.reader.nonce)+" "+Number(value.writer.instance)+" "+Number(value.writer.sequence)+" "+Number(value.writer.nonce);}
  static const char* CallbackScopeName(Policy::SetupScope scope){
    switch(scope){case Policy::SetupScope::Test:return "test";case Policy::SetupScope::BeforeAll:return "beforeAll";case Policy::SetupScope::AfterAll:return "afterAll";}
    throw std::invalid_argument("Native Source callback scope invalid");
  }
  static std::string Number(uint64_t value){return std::to_string(value);}
  static uint64_t UInt(std::string_view text){uint64_t value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);if(text.empty()||(text.size()>1&&text.front()=='0')||result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw std::runtime_error("Native Source request integer invalid");return value;}
  static std::string ExecutablePath(const std::string& encoded){const auto path=DecodeHex(encoded,4096);if(path.empty()||path.front()!='/'||path.find('\0')!=path.npos)throw std::runtime_error("Native Source executable path must be absolute");return path;}
  const Context& Match(const std::string& catalog,const std::string& declaration,const std::string& token,const std::string& owner,const std::string& test,const std::string& generation,const std::string& key)const{
    const std::array<uint64_t,6> values{UInt(catalog),UInt(declaration),UInt(token),UInt(owner),UInt(test),UInt(generation)};
    for(size_t i=0;i<setups_;++i){const auto& selection=contexts_[i].selection;if(std::array<uint64_t,6>{selection.catalog,selection.declaration,selection.token,selection.owner,selection.test,selection.generation}==values&&selection.key==key)return contexts_[i];}
    throw std::runtime_error("Native Source registration has no actual selected callback");
  }
  bool HasOwner(uint64_t token,uint64_t owner,uint64_t generation)const noexcept{for(size_t i=0;i<setups_;++i){const auto& selection=contexts_[i].selection;if(selection.token==token&&selection.owner==owner&&selection.generation==generation)return true;}return false;}
  std::optional<CallbackFence> callback_fence_;uint64_t callback_generation_=0;size_t completed_callback_fences_=0;
  std::optional<RetainedSourceWorkWire::Borrow> idle_borrow_;double idle_end_=0;uint64_t work_sequence_=0;
  bool work_expect_sent_=false,work_ack_sent_=false,work_refusal_sent_=false;
  Policy& policy_;RetainedSourceReceiver receiver_;Hooks hooks_;Policy::Host host_{};DedicatedHostProcess* actual_host_=nullptr;ProjectDescendants* domain_=nullptr;uint64_t generation_=0;bool host_exited_=false,enabled_=false;unsigned bridge_version_=1;uint64_t nonce_=0;std::array<Context,64> contexts_{};size_t setups_=0;std::optional<Wait> wait_;std::exception_ptr drain_error_;
};
} // namespace suitecut::runner
