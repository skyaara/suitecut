// SPDX-License-Identifier: MIT
#pragma once
#include "test_steps.h"
#include "step_wire.h"
#include "host_process.h"
#include <functional>
#include <sstream>
namespace suitecut::runner {
// Composition boundary: callback invocation/settlement is genuine runner state.
// Retained step work delays forwarding the outer callback receipt to its native
// phase/fixture controller; no host timeout creates a physical receipt.
class RunnerSteps {
 public:
  struct Hooks {std::function<void(const std::string&)> send;std::function<void(const std::string&)> terminate;std::function<void(const StepOwner&,const std::string&)> failure={};std::function<void(const StepOwner&,uint64_t,std::function<void(const std::optional<std::string>&)>,double)> fileFence={};std::function<void(const StepRecord&)> changed={};std::function<bool(const StepOwner&,uint64_t,double)> canSkip={};std::function<void(const StepOwner&,uint64_t,uint64_t,double)> acceptSkip={};std::function<double()> now={};};
  explicit RunnerSteps(Hooks hooks,TestSteps::Budget* budget=nullptr):hooks_(std::move(hooks)),steps_(budget){}
  void WorkerStarted(){if(!owners_.empty()||!managed_.empty()||steps_.Inspect().physical)throw std::runtime_error("Native steps worker ownership remains physical");request_nonce_=0;local_skip_nonce_=0;}
  void BeginCallback(const StepOwner& owner,double deadline,double now){
    const auto key=Key(owner.callback_token,owner.scope,owner.scope_generation);
    if(owners_.contains(key)||!steps_.Open(owner,deadline,now))throw std::runtime_error("Native step callback ownership admission failed");
    owners_.emplace(key,Callback{owner,deadline});
  }
  bool HasCallback(uint64_t token,uint64_t scope,uint64_t generation)const{return owners_.contains(Key(token,scope,generation));}
  bool AdmitsCallback(uint64_t token,uint64_t scope,uint64_t generation)const{const auto found=owners_.find(Key(token,scope,generation));return found!=owners_.end()&&!found->second.closed&&!found->second.outer_observed;}
  const StepOwner* CallbackOwner(uint64_t token,uint64_t scope,uint64_t generation)const{const auto found=owners_.find(Key(token,scope,generation));return found==owners_.end()?nullptr:&found->second.owner;}
  std::optional<double> CallbackDeadline(uint64_t token,uint64_t scope,uint64_t generation)const{const auto found=owners_.find(Key(token,scope,generation));return found==owners_.end()?std::nullopt:std::make_optional(found->second.deadline);}
  std::optional<uint64_t> BeginCallbackIteration(const StepOwner& owner,uint64_t parent,uint64_t assertion,uint64_t invocation,std::optional<double> own_cap,double now){
    const auto found=owners_.find(Key(owner.callback_token,owner.scope,owner.scope_generation));
    if(found==owners_.end()||found->second.owner!=owner||found->second.closed||found->second.outer_observed)return std::nullopt;
    // Allocate the ownership node before native step admission. Inserting this
    // same-allocator node after admission cannot allocate or drop an invocation.
    std::map<uint64_t,Managed> staged;staged.emplace(0,Managed{owner});auto node=staged.extract(staged.begin());
    const auto id=steps_.BeginCallbackIteration(owner,parent,assertion,invocation,own_cap,now);
    if(id){node.key()=*id;if(!managed_.insert(std::move(node)).inserted)throw std::logic_error("Native managed callback identity already exists");}
    return id;
  }
  void SettleCallbackIteration(const StepOwner& owner,uint64_t id,uint64_t invocation,bool ok,const std::string& error,uint64_t source,std::function<void(const std::optional<std::string>&)> forward,double now){
    const auto managed=managed_.find(id);const auto* step=steps_.Find(id);
    if(!forward||managed==managed_.end()||managed->second.owner!=owner||managed->second.forward||!step||!step->callback_iteration||step->callback_iteration->invocation!=invocation||
       !steps_.Receive(owner,id,ok,error,now,0,0,source))throw std::runtime_error("Native managed callback raw receipt is stale or invalid");
    managed->second.forward=std::move(forward);managed_dirty_=true;Emit();DrainManaged();Drain(now);
  }
  void SetCallbackSourceFence(const StepOwner& owner,uint64_t id,const SourceCallbackIterationReceipt& receipt){
    const auto actual=managed_.find(id);
    if(actual==managed_.end()||actual->second.owner!=owner||!actual->second.fencing||!steps_.SetCallbackSourceFence(owner,id,receipt))throw std::runtime_error("Native managed invocation Source receipt is stale or foreign");
  }
  void CancelCallbackIteration(const StepOwner& owner,uint64_t id,const std::string& error,double now){
    const auto actual=managed_.find(id);
    if(actual==managed_.end()||actual->second.owner!=owner||!steps_.CancelCallbackIteration(owner,id,error,now))throw std::runtime_error("Native managed invocation cancellation is stale or foreign");
    Emit();
  }
  bool HasPending(uint64_t token,uint64_t scope,uint64_t generation)const{
    const auto found=owners_.find(Key(token,scope,generation));return found!=owners_.end()&&Pending(found->second.owner)!=0;
  }
  void Amend(uint64_t token,uint64_t scope,uint64_t generation,double deadline,double now){
    const auto found=owners_.find(Key(token,scope,generation));if(found==owners_.end()||found->second.closed||found->second.deadline==deadline)return;
    if(!steps_.UpdateOwnerDeadline(found->second.owner,deadline,now))throw std::runtime_error("Native step deadline amendment rejected");
    found->second.deadline=deadline;Emit();
  }
  void Cancel(uint64_t token,uint64_t scope,uint64_t generation,const std::string& reason,double now){
    const auto found=owners_.find(Key(token,scope,generation));if(found==owners_.end()||found->second.closed)return;
    if(!steps_.Close(found->second.owner,now,reason))throw std::runtime_error("Native step callback cancellation rejected");
    found->second.closed=true;Emit();
  }
  void AcceptTestSkip(uint64_t token,uint64_t scope,uint64_t generation,uint64_t request,const std::string& reason,double now){
    const auto found=owners_.find(Key(token,scope,generation));
    if(found==owners_.end()||found->second.closed||!steps_.SkipTestCallback(found->second.owner,request,reason,now))throw std::runtime_error("Native accepted test-skip step cause rejected");
    found->second.test_skip=request;found->second.closed=true;Emit();
  }
  // This records the already validated actual BODY/CALL result packet. It does
  // not settle the callback or release a phase; a preceding soft operation can
  // still own the raw/file fence when its separate physical packet arrives.
  void ObserveRawResult(uint64_t token,uint64_t scope,uint64_t generation,const std::string& error={},uint64_t source=0){
    const auto found=owners_.find(Key(token,scope,generation));
    if(found==owners_.end()||found->second.outer_observed||found->second.forward)throw std::runtime_error("Native outer raw-result observation is stale or duplicate");
    if(!error.empty()&&!JsonString(error,8192))throw std::runtime_error("Native outer observed error scalar rejected");
    if(source){if(error.empty())throw std::runtime_error("Native originating step has no observed outer error");FailureSource(token,scope,generation,source);}
    found->second.outer_observed=true;found->second.observed_error=error;found->second.observed_source=source;
  }
  void Settled(uint64_t token,uint64_t scope,uint64_t generation,std::function<void()> forward,double now,const std::string& actual_error={}){
    SettledWithFailure(token,scope,generation,[forward=std::move(forward)](const auto&){forward();},now,actual_error);
  }
  void SettledWithFailure(uint64_t token,uint64_t scope,uint64_t generation,std::function<void(const std::optional<std::string>&)> forward,double now,const std::string& actual_error={}){
    SettledWithSource(token,scope,generation,[forward=std::move(forward)](const auto& error,uint64_t){forward(error);},now,actual_error);
  }
  void SettledWithSource(uint64_t token,uint64_t scope,uint64_t generation,std::function<void(const std::optional<std::string>&,uint64_t)> forward,double now,const std::string& actual_error={},uint64_t source=0){

    const auto found=owners_.find(Key(token,scope,generation));if(found==owners_.end()||found->second.forward)throw std::runtime_error("Native step outer callback receipt is stale or duplicate");
    auto& callback=found->second;
    if(callback.outer_observed){if(callback.observed_source!=source||callback.observed_error!=actual_error)throw std::runtime_error("Native outer forward changed its actual observed raw result/source");}else ObserveRawResult(token,scope,generation,actual_error,source);
    if(!callback.closed){if(!steps_.Close(callback.owner,now))throw std::runtime_error("Native step callback close rejected");callback.closed=true;}
    if(!actual_error.empty()){if(!JsonString(actual_error,8192))throw std::runtime_error("Invalid actual native outer callback error");callback.actual_error=actual_error;callback.actual_source=source;}
    callback.forward=std::move(forward);callback.cleanup_deadline=now+1000;Emit();Drain(now);
  }
  bool Packet(const std::vector<std::string>& fields,double now,bool stopped){
    if(fields.empty()||(fields[0]!="STEP_BEGIN"&&fields[0]!="STEP_BEGIN_SKIP"&&fields[0]!="STEP_SETTLED"&&fields[0]!="STEP_SETTLED_FROM_STEP"&&fields[0]!="STEP_CALLBACK_SETTLED"&&fields[0]!="STEP_SETTLED_TEST_SKIP"&&fields[0]!="STEP_LOCAL_SKIP_REQUEST"&&fields[0]!="STEP_SETTLED_LOCAL_SKIP"))return false;
    if(fields[0]=="STEP_LOCAL_SKIP_REQUEST"){
      if(fields.size()!=8)throw std::runtime_error("Invalid native local-step skip request packet");
      const auto request=UInt(fields[1]),id=UInt(fields[2]),token=UInt(fields[3]),scope=UInt(fields[4]),generation=UInt(fields[5]),present=UInt(fields[7]);
      if(!request||local_skip_nonce_==UINT64_MAX||request!=local_skip_nonce_+1||present>1)throw std::runtime_error("Native local-step skip mutation nonce/flags are invalid");
      ++local_skip_nonce_;const auto reason=RuntimeSkipReason(fields[6]);const auto found=owners_.find(Key(token,scope,generation));
      const bool ok=!stopped&&found!=owners_.end()&&!found->second.closed&&!found->second.outer_observed&&(!hooks_.canSkip||hooks_.canSkip(found->second.owner,id,now))&&steps_.SkipStep(found->second.owner,id,request,reason,present!=0,now);
      if(ok&&hooks_.acceptSkip)hooks_.acceptSkip(found->second.owner,id,request,now);
      Emit();hooks_.send("STEP_LOCAL_SKIP_ACK "+fields[1]+" "+fields[2]+" "+fields[3]+" "+fields[4]+" "+fields[5]+" "+(ok?"1 -":"0 "+StepWire("\"Native local-step skip rejected: stale, expired, stopped or unsupported\"")));
    }else if(fields[0]=="STEP_CALLBACK_SETTLED"){
      if(fields.size()!=4)throw std::runtime_error("Invalid native outer callback physical receipt packet");
      const auto found=owners_.find(Key(UInt(fields[1]),UInt(fields[2]),UInt(fields[3])));
      if(found==owners_.end()||!found->second.outer_observed||found->second.raw_settled)throw std::runtime_error("Native outer callback physical receipt is stale or has no actual result");
      found->second.raw_settled=true;Drain(now);
    }else if(fields[0]=="STEP_BEGIN"||fields[0]=="STEP_BEGIN_SKIP"){
      if(fields.size()!=8&&fields.size()!=12&&fields.size()!=21)throw std::runtime_error("Invalid native step admission packet");
      const auto location=fields.size()>=12?PrepareStepLocation(fields[8],UInt(fields[9]),UInt(fields[10]),UInt(fields[11])):std::nullopt;
      StepBox box;
      if(fields.size()==21){
        if(UInt(fields[12])!=1)throw std::runtime_error("Invalid native boxed-step flag");
        box={true,PrepareStepLocation(fields[13],UInt(fields[14]),UInt(fields[15]),UInt(fields[16])),PrepareStepLocation(fields[17],UInt(fields[18]),UInt(fields[19]),UInt(fields[20]))};
        if(!ValidStepBoxLocation(box,location))throw std::runtime_error("Invalid native boxed-step captured location");
      }
      const auto request=UInt(fields[1]),token=UInt(fields[2]),scope=UInt(fields[3]),generation=UInt(fields[4]),parent=UInt(fields[5]),timeout=UInt(fields[7]);
      if(!request||request_nonce_==UINT64_MAX||request!=++request_nonce_)throw std::runtime_error("Native step admission nonce is stale");
      const auto title=DecodeHex(fields[6],4096*6+2);if(!JsonString(title,4096))throw std::runtime_error("Invalid native step title scalar");
      const auto found=owners_.find(Key(token,scope,generation));std::optional<uint64_t> id;
      if(!stopped&&found!=owners_.end()&&!found->second.closed&&!found->second.outer_observed)id=fields[0]=="STEP_BEGIN_SKIP"?steps_.DeclareSkipped(found->second.owner,parent,title,timeout,request,now,location,box):steps_.Begin(found->second.owner,parent,title,timeout,now,location,box);
      Emit();hooks_.send(std::string(fields[0]=="STEP_BEGIN_SKIP"?"STEP_ADMITTED_NOT_INVOKED ":"STEP_ADMITTED ")+fields[1]+" "+fields[2]+" "+fields[3]+" "+fields[4]+" "+std::to_string(id.value_or(0))+" "+(id?"1 -":"0 "+StepWire("\"Native step admission rejected: stale, expired, stopped or capacity exceeded\"")));
    }else{
      const bool from_step=fields[0]=="STEP_SETTLED_FROM_STEP";
      if(fields.size()!=(from_step?8:7))throw std::runtime_error("Invalid native step callback receipt packet");
      const auto id=UInt(fields[1]),token=UInt(fields[2]),scope=UInt(fields[3]),generation=UInt(fields[4]);
      const bool skip=fields[0]=="STEP_SETTLED_TEST_SKIP",local=fields[0]=="STEP_SETTLED_LOCAL_SKIP";const auto value=UInt(fields[5]),ok=(skip||local)?1:value;const auto error=(skip||local)?std::string("\"\""):RuntimeSkipReason(fields[6]);
      if((skip||local)&&(!value||fields[6]!="-"))throw std::runtime_error("Native test-skip control receipt has an error scalar");
      const auto originating=from_step?UInt(fields[7]):0;
      if(from_step&&!originating)throw std::runtime_error("Native step source receipt has no originating step");
      const auto found=owners_.find(Key(token,scope,generation));
      if(managed_.contains(id))throw std::runtime_error("Native managed invocation cannot settle through an ordinary step packet");
      if(found==owners_.end()||ok>1||!steps_.Receive(found->second.owner,id,ok!=0,error,now,skip?value:0,local?value:0,originating))throw std::runtime_error("Native step callback receipt is stale or invalid");
      if(!ok&&(found->second.test_skip||steps_.Find(id)->step_skip)){if(!found->second.actual_error)found->second.actual_error=error;if(hooks_.failure)hooks_.failure(found->second.owner,error);}
      Emit();const auto owner=found->second.owner;
      auto complete=[this,owner,id,now](const std::optional<std::string>& failure){
        // A synchronous fence shares the runner's active native event timestamp.
        // A deferred fence asks the runner for its current real event instead
        // of advancing only this ledger beyond a reentrant phase transition.
        const auto received=hooks_.now?hooks_.now():(hooks_.fileFence?DedicatedRealMilliseconds():now);
        if(!steps_.Complete(owner,id,received,failure))throw std::runtime_error("Native step/file physical receipt is stale or invalid");
        managed_dirty_=true;Emit();hooks_.send("STEP_RECEIVED "+std::to_string(id)+" "+std::to_string(owner.callback_token)+" "+std::to_string(owner.scope)+" "+std::to_string(owner.scope_generation));DrainManaged();Drain(received);
      };
      if(hooks_.fileFence)hooks_.fileFence(owner,id,std::move(complete),now);else complete(std::nullopt);
    }
    DrainManaged();return true;
  }
  void Tick(double now){steps_.Tick(now);Emit();DrainManaged();Drain(now);}
  // Original worker OS exit is required by the runner before this call.
  void WorkerExited(unsigned index,int64_t pid,double now){steps_.ConfirmedWorkerExit(index,pid,now);steps_.TakeEvents();for(const auto&[key,callback]:owners_){(void)key;if(!steps_.Retire(callback.owner))throw std::runtime_error("Native step worker exit did not retire actual callback ownership");}owners_.clear();managed_.clear();}
  std::optional<std::string> PendingError()const{for(const auto&[key,callback]:owners_){(void)key;if(callback.forward&&callback.actual_error)return callback.actual_error;}return std::nullopt;}
  bool HasLocalSkipPhysical()const{for(const auto& step:steps_.Records())if(step.step_skip&&step.receipt==StepReceipt::Pending)return true;return false;}
  size_t Physical()const{return steps_.Inspect().physical;}
  bool IdleReady()const{return owners_.empty()&&managed_.empty()&&!Physical();}
  const std::vector<StepRecord>& Records()const{return steps_.Records();}
  void NoteSoftFailure(const StepOwner& owner,uint64_t id,uint64_t error_id,const std::string& error,double now){if(!steps_.NoteSoftFailure(owner,id,error_id,error,now))throw std::runtime_error("Native soft failure has no actual running step ancestry");}
  const StepRecord* Find(uint64_t id)const{return steps_.Find(id);}
  const StepRecord& FailureSource(uint64_t token,uint64_t scope,uint64_t generation,uint64_t id)const{
    const auto found=owners_.find(Key(token,scope,generation));const auto* source=steps_.Find(id);
    if(found==owners_.end()||found->second.forward||found->second.raw_settled||!source||source->owner!=found->second.owner||source->status!=StepStatus::Failed||!source->callback_received||source->callback_ok)throw std::runtime_error("Native originating-step receipt is forged, foreign or stale");
    return *source;
  }

 private:
  static std::string Key(uint64_t token,uint64_t scope,uint64_t generation){return std::to_string(token)+":"+std::to_string(scope)+":"+std::to_string(generation);}
  static uint64_t UInt(const std::string& text){uint64_t value=0;if(text.empty())throw std::runtime_error("Invalid native step integer");for(const char c:text){if(c<'0'||c>'9'||value>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10)throw std::runtime_error("Invalid native step integer");value=value*10+static_cast<unsigned>(c-'0');}return value;}
  static std::string Hex(std::string_view text){static constexpr char digits[]="0123456789abcdef";std::string out;for(const unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}return out.empty()?"-":out;}
  void Emit(){for(const auto& event:steps_.TakeEvents()){const auto& step=*steps_.Find(event.id);
    if(hooks_.changed)hooks_.changed(step);
    const auto identity=std::to_string(step.id)+" "+std::to_string(step.owner.callback_token)+" "+std::to_string(step.owner.scope)+" "+std::to_string(step.owner.scope_generation);
    if(step.status==StepStatus::Skipped&&step.step_skip){const auto tail=identity+" "+std::to_string(step.step_skip->request)+" "+std::to_string(step.step_skip->source_step);if(event.abort)hooks_.send("STEP_LOCAL_SKIP_ABORT "+tail);hooks_.send("STEP_LOCAL_SKIP_RESULT "+tail);}
    else if(step.status==StepStatus::Failed&&step.soft_error_id&&step.callback_received&&step.callback_ok&&!step.attachment_error_json){hooks_.send("STEP_SOFT_RESULT "+identity+" "+std::to_string(step.soft_error_id)+" "+StepWire(step.error_json));}
    else{const auto tail=identity+" "+StepStatusName(event.status)+" "+StepWire(step.error_json)+(step.status==StepStatus::Skipped?" "+std::to_string(step.test_skip->request):"");if(event.abort)hooks_.send("STEP_ABORT "+tail);hooks_.send("STEP_RESULT "+tail);}
  }}
  size_t Pending(const StepOwner& owner)const{size_t count=0;for(const auto& step:steps_.Records())if(step.owner==owner&&step.receipt==StepReceipt::Pending)++count;return count;}
  bool PendingDescendant(const StepOwner& owner,uint64_t root)const{
    for(const auto& step:steps_.Records())if(step.owner==owner&&step.id!=root&&step.receipt==StepReceipt::Pending){
      for(auto* ancestor=&step;ancestor&&ancestor->parent;ancestor=steps_.Find(ancestor->parent))if(ancestor->parent==root)return true;
    }
    return false;
  }
  void DrainManaged(){
    if(managed_draining_||!managed_dirty_)return;
    managed_draining_=true;
    try{
      for(;;){
        managed_dirty_=false;
        auto found=std::find_if(managed_.begin(),managed_.end(),[this](const auto& item){return item.second.forward&&!item.second.fencing&&!PendingDescendant(item.second.owner,item.first);});
        if(found==managed_.end())break;
        const auto id=found->first;const auto owner=found->second.owner;found->second.fencing=true;
        const auto now=hooks_.now?hooks_.now():steps_.Find(id)->callback_received.value();
        auto complete=[this,owner,id,now](const std::optional<std::string>& failure){
          const auto received=hooks_.now?hooks_.now():(hooks_.fileFence?DedicatedRealMilliseconds():now);
          const auto managed=managed_.find(id);
          if(managed==managed_.end()||managed->second.owner!=owner||!managed->second.fencing||!steps_.Complete(owner,id,received,failure))throw std::runtime_error("Native managed callback joint physical receipt is stale or invalid");
          auto forward=std::move(managed->second.forward);managed_.erase(managed);managed_dirty_=true;Emit();
          hooks_.send("STEP_RECEIVED "+std::to_string(id)+" "+std::to_string(owner.callback_token)+" "+std::to_string(owner.scope)+" "+std::to_string(owner.scope_generation));
          forward(failure);DrainManaged();Drain(received);
        };
        if(hooks_.fileFence)hooks_.fileFence(owner,id,std::move(complete),now);else complete(std::nullopt);
      }
    }catch(...){managed_draining_=false;throw;}
    managed_draining_=false;
  }
  void Drain(double now){
    for(auto it=owners_.begin();it!=owners_.end();){auto& callback=it->second;if(!callback.forward){++it;continue;}
      if(callback.raw_settled&&!Pending(callback.owner)){auto forward=std::move(callback.forward);if(!steps_.Retire(callback.owner))throw std::runtime_error("Native settled step callback did not retire");const auto owner=callback.owner;const auto failure=callback.actual_error;const auto source=callback.actual_source;it=owners_.erase(it);hooks_.send("STEP_CALLBACK_RECEIVED "+std::to_string(owner.callback_token)+" "+std::to_string(owner.scope)+" "+std::to_string(owner.scope_generation));forward(failure,source);}
      else{if(now>=callback.cleanup_deadline&&!callback.termination_requested){callback.termination_requested=true;hooks_.terminate("Native step raw callback physical cleanup deadline expired");}++it;}
    }
  }
  struct Callback {StepOwner owner;double deadline;bool closed=false;std::function<void(const std::optional<std::string>&,uint64_t)> forward{};double cleanup_deadline=0;bool termination_requested=false;bool raw_settled=false,outer_observed=false;std::string observed_error={};uint64_t observed_source=0;std::optional<std::string> actual_error=std::nullopt;uint64_t test_skip=0,actual_source=0;};
  struct Managed {StepOwner owner;std::function<void(const std::optional<std::string>&)> forward={};bool fencing=false;};
  Hooks hooks_;TestSteps steps_;std::map<std::string,Callback> owners_;std::map<uint64_t,Managed> managed_;bool managed_draining_=false,managed_dirty_=false;uint64_t request_nonce_=0,local_skip_nonce_=0;
};
} // namespace suitecut::runner
