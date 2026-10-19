// SPDX-License-Identifier: MIT
#pragma once
#include "soft_assertions.h"
#include "soft_cleanup_policy.h"
#include "step_wire.h"
namespace suitecut::runner {
// Actual callback ownership and raw matcher leases; no matcher implementation.
class RunnerSoftAssertions {
 public:
  struct Hooks {
    std::function<void(const std::string&)> send;
    std::function<void(const SoftAssertionRecord&,double)> accepted_failure;
    std::function<void(const StepOwner&,const std::string&,bool)> hard;
    std::function<void(const std::string&)> terminate;
    std::function<const StepRecord*(uint64_t)> step;
  };
  explicit RunnerSoftAssertions(Hooks hooks,SoftAssertions::Budget* budget=nullptr):hooks_(std::move(hooks)),journal_(hooks_.step,budget){}
  void WorkerStarted(){if(!IdleReady())throw std::runtime_error("Native soft worker still owns callback work");nonce_=0;}
  void Open(const StepOwner& owner,double deadline,double now,std::optional<double> suite_deadline={},std::optional<double> cleanup_cap={}){
    if(owner.callback_kind!="body"&&owner.callback_kind!="beforeEach"&&owner.callback_kind!="afterEach")return;
    journal_.Open(owner,SoftCallbackDeadline(owner.callback_kind,deadline,suite_deadline,cleanup_cap),now);contexts_.emplace(Key(owner.callback_token,owner.scope,owner.scope_generation),Context{owner});
    hooks_.send("SOFT_CONTEXT "+Identity(owner)+" "+std::to_string(owner.test_id)+" "+std::to_string(owner.attempt_generation)+" "+std::to_string(owner.worker_index)+" "+std::to_string(owner.pid)+" "+owner.callback_kind);
  }
  void Amend(uint64_t token,uint64_t scope,uint64_t generation,double deadline,double now,std::optional<double> suite_deadline={},std::optional<double> cleanup_cap={}){if(auto* c=Find(token,scope,generation);c&&!c->closed)journal_.Amend(c->owner,SoftCallbackDeadline(c->owner.callback_kind,deadline,suite_deadline,cleanup_cap),now);}
  void Cancel(uint64_t token,uint64_t scope,uint64_t generation,const std::string& reason,double now){if(auto* c=Find(token,scope,generation))Cancel(*c,reason,now);}
  void CancelAll(const std::string& reason,double now){for(auto&[key,c]:contexts_){(void)key;Cancel(c,reason,now);}}
  void Fence(uint64_t token,uint64_t scope,uint64_t generation,std::function<void()> forward,double now,const std::string& raw_error={}){
    auto* c=Find(token,scope,generation);if(!c){forward();return;}
    if(c->forward)throw std::runtime_error("Native soft callback raw receipt is duplicate");
    journal_.CloseAdmission(c->owner);c->closed=true;c->forward=std::move(forward);c->cleanup_deadline=now+1000;
    if(!raw_error.empty())Cancel(*c,raw_error,now);
    Drain(now);
  }
  void FenceStep(const StepOwner& owner,uint64_t step,std::function<void()> forward,double now){
    if(!journal_.PendingStep(owner,step)){forward();return;}
    if(step_fences_.size()>=64||step_fences_.contains(step))throw std::runtime_error("Native soft step fence capacity or duplicate");
    step_fences_.emplace(step,StepFence{owner,std::move(forward),now+1000});
  }
  bool Packet(const std::vector<std::string>& f,double now,bool stopped,bool cleanup_admitted=false){
    if(f.empty()||(f[0]!="SOFT_BEGIN"&&f[0]!="SOFT_SETTLED"))return false;
    if(f[0]=="SOFT_BEGIN"){
      if(f.size()!=10)throw std::runtime_error("Invalid native soft admission packet");
      const auto request=UInt(f[1]),token=UInt(f[2]),scope=UInt(f[3]),generation=UInt(f[4]),step=UInt(f[5]);
      if(!request||nonce_==UINT64_MAX||request!=nonce_+1)throw std::runtime_error("Native soft request nonce rejected");
      ++nonce_;
      const auto location=PrepareStepLocation(f[6],UInt(f[7]),UInt(f[8]),UInt(f[9]));
      auto* c=Find(token,scope,generation);std::optional<SoftAssertions::Admission> admitted;std::string error="\"Native soft admission rejected: unsupported, stopped or stale callback\"";
      try{if(!c||c->closed||!SoftAdmissionAllowed(c->owner.callback_kind,stopped,cleanup_admitted))throw std::runtime_error("Native soft admission rejected: unsupported, stopped or stale callback");admitted=journal_.Begin(c->owner,request,step,location,now);}
      catch(const std::exception& actual){error=ErrorJSON(actual.what());if(c)hooks_.hard(c->owner,error,true);else throw;}
      const auto id=admitted?admitted->id:0;const auto remaining=admitted?(admitted->remaining>=3150000000000.0?0:static_cast<uint64_t>(std::max(1.0,std::min(120000.0,std::ceil(admitted->remaining))))):0;
      if(admitted)requests_.emplace(request,Request{c->owner,id});
      hooks_.send("SOFT_ADMITTED "+f[1]+" "+f[2]+" "+f[3]+" "+f[4]+" "+std::to_string(id)+" "+(admitted?"1":"0")+" "+std::to_string(remaining)+" "+StepWire(admitted?"\"\"":error));
    }else{
      if(f.size()!=8)throw std::runtime_error("Invalid native soft raw settlement packet");
      const auto request=UInt(f[1]),token=UInt(f[2]),scope=UInt(f[3]),generation=UInt(f[4]),id=UInt(f[5]);
      const auto found=requests_.find(request);auto* c=Find(token,scope,generation);
      if(!c||found==requests_.end()||found->second.owner!=c->owner||found->second.id!=id)throw std::runtime_error("Native soft settlement has no exact actual admission");
      const auto raw=f[6]=="P"?SoftRawOutcome::Passed:f[6]=="S"?SoftRawOutcome::RecognizedMismatch:f[6]=="D"?SoftRawOutcome::RecognizedDeadline:f[6]=="E"?SoftRawOutcome::OtherFailure:throw std::runtime_error("Invalid native soft raw outcome");
      const auto error=RuntimeSkipReason(f[7]);const auto verdict=journal_.Settled(c->owner,id,raw,error,now);
      if(SoftAssertions::IsAcceptedFailure(verdict)){const auto& record=journal_.Records().back();hooks_.accepted_failure(record,now);}
      else if(verdict==SoftAssertions::Verdict::OtherFailure)hooks_.hard(c->owner,error,false);
      const bool ok=verdict==SoftAssertions::Verdict::Passed||SoftAssertions::IsAcceptedFailure(verdict)||verdict==SoftAssertions::Verdict::OtherFailure;
      hooks_.send("SOFT_SETTLED_ACK "+f[1]+" "+f[2]+" "+f[3]+" "+f[4]+" "+f[5]+" "+(ok?"1 -":"0 "+StepWire("\"Native soft raw receipt arrived after cancellation or original deadline\"")));
      requests_.erase(found);Drain(now);
    }
    return true;
  }
  void Tick(double now){journal_.Tick(now,[this](auto id,const auto& owner,auto,const auto& reason){Abort(id,owner,reason);});Drain(now);}
  void WorkerExited(unsigned worker,int64_t pid,double now){
    journal_.ConfirmedWorkerExit(worker,pid,now);
    for(auto it=contexts_.begin();it!=contexts_.end();){if(it->second.owner.worker_index==worker&&it->second.owner.pid==pid){journal_.RetireContext(it->second.owner);it=contexts_.erase(it);}else ++it;}
    for(auto it=requests_.begin();it!=requests_.end();)if(it->second.owner.worker_index==worker&&it->second.owner.pid==pid)it=requests_.erase(it);else ++it;
    for(auto it=step_fences_.begin();it!=step_fences_.end();)if(it->second.owner.worker_index==worker&&it->second.owner.pid==pid)it=step_fences_.erase(it);else ++it;
  }
  bool Pending(uint64_t token,uint64_t scope,uint64_t generation)const{const auto it=contexts_.find(Key(token,scope,generation));return it!=contexts_.end()&&journal_.Pending(it->second.owner);}
  bool CanSkip(uint64_t token,uint64_t scope,uint64_t generation)const{const auto it=contexts_.find(Key(token,scope,generation));return it==contexts_.end()||(!journal_.Pending(it->second.owner)&&!journal_.FailedAttempt(it->second.owner.attempt_generation));}
  bool FailedAttempt(uint64_t generation)const{return journal_.FailedAttempt(generation);}
  const SoftAssertionRecord* FirstFailure(uint64_t generation)const{for(const auto& r:journal_.Records())if(r.owner.attempt_generation==generation&&IsAcceptedSoftFailure(r.receipt))return &r;return nullptr;}
  size_t Physical()const{return journal_.Physical();}
  bool IdleReady()const{return journal_.IdleReady()&&step_fences_.empty()&&requests_.empty();}
  const std::vector<SoftAssertionRecord>& Records()const{return journal_.Records();}
 private:
  using KeyType=std::tuple<uint64_t,uint64_t,uint64_t>;
  static KeyType Key(uint64_t token,uint64_t scope,uint64_t generation){return {token,scope,generation};}
  static uint64_t UInt(const std::string& text){uint64_t v=0;if(text.empty())throw std::runtime_error("Invalid native soft integer");for(const char c:text){if(c<'0'||c>'9'||v>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10)throw std::runtime_error("Invalid native soft integer");v=v*10+static_cast<unsigned>(c-'0');}return v;}
  static std::string Identity(const StepOwner& o){return std::to_string(o.callback_token)+" "+std::to_string(o.scope)+" "+std::to_string(o.scope_generation);}
  static std::string ErrorJSON(const std::string& text){std::string out="\"";for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32)out+=' ';else out+=static_cast<char>(c);}return out+'"';}
  struct Context {StepOwner owner;bool closed=false;std::function<void()> forward={};double cleanup_deadline=0;bool termination_requested=false;};
  struct Request {StepOwner owner;uint64_t id;};
  struct StepFence {StepOwner owner;std::function<void()> forward;double deadline;};
  Context* Find(uint64_t token,uint64_t scope,uint64_t generation){const auto it=contexts_.find(Key(token,scope,generation));return it==contexts_.end()?nullptr:&it->second;}
  void Abort(uint64_t id,const StepOwner& owner,const std::string& reason){for(const auto&[request,r]:requests_)if(r.id==id&&r.owner==owner){hooks_.send("SOFT_ABORT "+std::to_string(request)+" "+Identity(owner)+" "+std::to_string(id)+" "+StepWire(reason));return;}throw std::runtime_error("Native soft abort lost raw request owner");}
  void Cancel(Context& c,const std::string& reason,double now){journal_.Cancel(c.owner,reason,now,[this](auto id,const auto& owner,auto,const auto& actual){Abort(id,owner,actual);});c.closed=true;}
  void Drain(double now){
    for(auto it=step_fences_.begin();it!=step_fences_.end();){if(!journal_.PendingStep(it->second.owner,it->first)){auto forward=std::move(it->second.forward);it=step_fences_.erase(it);forward();}else{if(now>=it->second.deadline){hooks_.terminate("Native soft step raw operation retirement deadline expired");it->second.deadline=std::numeric_limits<double>::max();}++it;}}
    for(auto it=contexts_.begin();it!=contexts_.end();){auto& c=it->second;if(c.forward&&!journal_.Pending(c.owner)){auto forward=std::move(c.forward);const auto owner=c.owner;journal_.RetireContext(owner);it=contexts_.erase(it);hooks_.send("SOFT_CONTEXT_RETIRED "+Identity(owner));forward();}else{if(c.forward&&now>=c.cleanup_deadline&&!c.termination_requested){c.termination_requested=true;hooks_.terminate("Native soft raw matcher retirement deadline expired");}++it;}}
  }
  Hooks hooks_;SoftAssertions journal_;std::map<KeyType,Context> contexts_;std::map<uint64_t,Request> requests_;std::map<uint64_t,StepFence> step_fences_;uint64_t nonce_=0;
};
}
