// SPDX-License-Identifier: MIT
#include "coverage_targets.h"
#include "coverage_dispatch.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>
namespace suitecut {
class CoverageTargets::Impl : public std::enable_shared_from_this<Impl> {
 public:
  using Kind=bool; // true is actual JS coverage, false actual CSS coverage.
  enum class Phase{Starting,Active,Stopping,Cleaning,Failed};
  enum class State{Starting,Active,Stopping,Finalized,Absent};
  static constexpr size_t kTargets=32,kObservedTargets=2049,kPayload=48*1048576,kResult=2*1048576;
  struct Payload{size_t bytes=0;};
  struct Record{Kind kind=true;Phase phase=Phase::Starting;Dict options;int owner=0;double deadline=0;Reply reply;std::map<std::string,State> states;std::map<std::string,std::string> results;size_t result_bytes=0;std::string error,loss;bool scheduling=false,retain_error=false,include_workers=false,reset=true;};
  struct Target{std::string id;std::shared_ptr<CoverageControls> controls;bool alive=true,current_document=true,worker=false;double deadline=0;std::vector<Ready> ready;std::set<Kind> startup,cleanup;};
  explicit Impl(Hooks hooks):hooks_(std::move(hooks)),payload_(std::make_shared<Payload>()){
    CoverageDispatch::Hooks dispatch;dispatch.now=hooks_.now;dispatch.send=hooks_.send;dispatch.wake=hooks_.wake;dispatch_=std::make_shared<CoverageDispatch>(std::move(dispatch));
  }
  void Init(){targets_.emplace("",NewTarget("",""));}
  static std::string Start(Kind kind){return kind?"startJS":"startCSS";}static std::string Stop(Kind kind){return kind?"stopJS":"stopCSS";}
  bool Current(const std::shared_ptr<Record>& record)const{const auto found=records_.find(record->kind);return !closed_&&found!=records_.end()&&found->second==record;}
  void Wake(){if(hooks_.wake)hooks_.wake();}
  std::shared_ptr<Target> NewTarget(const std::string& session,const std::string& id){
    auto target=std::make_shared<Target>();target->id=id;CoverageControls::Hooks hooks;hooks.target_session=session;hooks.target_id=id;hooks.now=hooks_.now;hooks.wake=hooks_.wake;hooks.root_target_only=[](){return true;};hooks.author_context=[owner=hooks_.author_context,session](int context){return owner(session,context);};hooks.script_url=hooks_.script_url;
    if(hooks_.script_inventory)hooks.script_inventory=[inventory=hooks_.script_inventory,session](double deadline,std::vector<Dict>& output,std::string& error){return inventory(session,deadline,output,error);};
    hooks.send=[dispatch=dispatch_,session](const std::string& method,Dict params,double deadline,CoverageControls::ProtocolReply reply){return dispatch->Send(session,method,params,deadline,std::move(reply));};
    if(hooks_.debugger_acquire)hooks.debugger_acquire=[acquire=hooks_.debugger_acquire,session](double deadline,CoverageControls::ProtocolReply reply){return acquire(session,deadline,std::move(reply));};
    if(hooks_.debugger_release)hooks.debugger_release=[release=hooks_.debugger_release,session](double deadline,CoverageControls::ProtocolReply reply){return release(session,deadline,std::move(reply));};
    hooks.close_channel=[close=hooks_.close_channel,session](const std::string& error){close(session,error);};
    hooks.reserve_payload=[budget=payload_](size_t bytes){if(bytes>kPayload-budget->bytes)return false;budget->bytes+=bytes;return true;};hooks.release_payload=[budget=payload_](size_t bytes){budget->bytes-=bytes;};
    target->controls=std::make_shared<CoverageControls>(std::move(hooks));return target;
  }
  void ReplyOwner(const std::shared_ptr<Record>& record,bool ok,const std::string& error,const std::string& json=""){
    auto reply=std::move(record->reply);record->owner=0;record->deadline=0;if(reply)reply(ok,error,json);
  }
  void Fail(const std::shared_ptr<Record>& record,const std::string& error){
    if(!Current(record)||record->phase==Phase::Cleaning)return;
    record->retain_error=!record->owner&&record->phase==Phase::Active;record->phase=Phase::Cleaning;if(record->error.empty())record->error=error.substr(0,8192);
    // Stop dispatch before abort callbacks may synchronously complete a domain.
    dispatch_->CancelQueued(Start(record->kind),record->error);
    std::vector<std::shared_ptr<Target>> targets;for(const auto& [session,state]:record->states){const auto found=targets_.find(session);if(found!=targets_.end())targets.push_back(found->second);}for(const auto& target:targets)target->controls->Abort(Start(record->kind),record->error);
    ReplyOwner(record,false,record->error);Wake();
  }
  static bool Append(const std::string& json,std::string& output,size_t& entries,std::string& error){
    if(json.size()<2||json.front()!='['||json.back()!=']'){error="Actual native coverage target returned an invalid array";return false;}
    bool quoted=false,escape=false,expect=true;size_t depth=0,count=0;
    for(size_t i=1;i+1<json.size();++i){const char c=json[i];if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;continue;}if(!depth){if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;if(c=='{'&&expect){++depth;++count;expect=false;continue;}if(c==','&&!expect){expect=true;continue;}error="Actual native coverage entry is not an object";return false;}if(c=='"'){quoted=true;continue;}if(c=='{'||c=='[')++depth;else if(c=='}'||c==']')--depth;}
    if(quoted||depth||(count&&expect)||count>2048-entries){error="Native coverage target result entry capacity exceeded";return false;}if(json.size()-2+(entries&&count?1:0)>kResult-output.size()-1){error="Native combined coverage result exceeds 2 MiB";return false;}
    if(count){if(entries)output+=',';output.append(json,1,json.size()-2);entries+=count;}return true;
  }
  void Complete(const std::shared_ptr<Record>& record){
    if(!Current(record)||record->scheduling||record->phase==Phase::Cleaning)return;
    if(record->owner&&record->deadline>0&&hooks_.now()>=record->deadline){Fail(record,"Native coverage operation exceeded its original deadline");return;}
    if(record->phase==Phase::Starting){for(const auto& [session,state]:record->states)if(state!=State::Active)return;record->phase=Phase::Active;ReplyOwner(record,true,"");return;}
    if(record->phase!=Phase::Stopping)return;
    for(const auto& [session,state]:record->states)if(state!=State::Absent)return;
    std::string output="[",error;size_t entries=0;for(const auto& [session,json]:record->results)if(!Append(json,output,entries,error)){Fail(record,error);return;}output+=']';records_.erase(record->kind);ReplyOwner(record,true,"",output);
  }
  int Owner(){return next_owner_==std::numeric_limits<int>::max()?0:++next_owner_;}
  bool Eligible(const std::shared_ptr<Record>& record,const std::shared_ptr<Target>& target)const{return target->alive&&target->current_document&&(!target->worker||(record->kind&&record->include_workers));}
  void StartTarget(const std::shared_ptr<Record>& record,const std::string& session,double deadline){
    const auto found=targets_.find(session);if(!Current(record)||found==targets_.end()||!Eligible(record,found->second))return;const auto target=found->second;const auto owner=Owner();if(!owner){Fail(record,"Native coverage internal operation identity exceeded");return;}
    if(!record->reset&&record->states.size()>=2048){Fail(record,"Native retained coverage lifetime target capacity exceeded");return;}
    target->startup.insert(record->kind);record->states[session]=State::Starting;auto params=record->options->Copy(false);params->SetString("action",Start(record->kind));const auto weak=weak_from_this();
    target->controls->Handle(owner,params,deadline,[weak,record,target,session](bool ok,const std::string& error,const std::string&){if(const auto self=weak.lock()){
      target->startup.erase(record->kind);if(!ok)target->cleanup.insert(record->kind);
      if(self->Current(record)&&target->alive){if(ok){record->states[session]=State::Active;if(record->phase==Phase::Stopping)self->StopTarget(record,session);}else self->Fail(record,error.empty()?"Actual native coverage child startup failed":error);self->Complete(record);}self->ReadyTarget(target);self->Wake();
    }});
  }
  bool SaveResult(const std::shared_ptr<Record>& record,const std::string& session,const std::shared_ptr<Target>&,const std::string& result){
    if(record->results.contains(session)||record->results.size()>=2048){Fail(record,"Native retained coverage target result identity/capacity exceeded");return false;}
    const auto& json=result;
    if(json.size()>kResult+2*kTargets-record->result_bytes){Fail(record,"Native combined coverage response exceeds 2 MiB");return false;}
    record->result_bytes+=json.size();record->results[session]=json;return true;
  }
  void StopTarget(const std::shared_ptr<Record>& record,const std::string& session){
    const auto found=targets_.find(session);if(!Current(record)||found==targets_.end()||!found->second->alive)return;const auto target=found->second;const auto owner=Owner();if(!owner){Fail(record,"Native coverage internal operation identity exceeded");return;}
    record->states[session]=State::Stopping;auto params=CefDictionaryValue::Create();params->SetString("action",Stop(record->kind));const auto weak=weak_from_this();
    target->controls->Handle(owner,params,record->deadline,[weak,record,target,session](bool ok,const std::string& error,const std::string& json){if(const auto self=weak.lock()){if(!self->Current(record)||!target->alive)return;if(!ok){self->Fail(record,error.empty()?"Actual native coverage child collection failed":error);return;}if(record->states[session]==State::Finalized)return;if(!self->SaveResult(record,session,target,json))return;record->states[session]=State::Absent;self->Complete(record);self->Wake();}});
  }
  void Prune(){
    // Actual domain teardown may finish between deadline ticks. Reconcile those
    // completed owners before admitting the next public operation; a timer is
    // never a physical completion or a renewed cleanup budget.
    std::vector<std::shared_ptr<Record>> records;for(const auto& [kind,record]:records_)if(record->phase==Phase::Cleaning)records.push_back(record);
    for(const auto& record:records)if(Current(record)&&record->phase==Phase::Cleaning){bool ready=true;for(const auto& [session,state]:record->states){const auto found=targets_.find(session);if(found!=targets_.end()&&!found->second->controls->Ready(Start(record->kind)))ready=false;}if(ready){if(record->retain_error)record->phase=Phase::Failed;else records_.erase(record->kind);}}
    // Clear only genuinely completed prior cleanup markers. Pending bootstrap
    // callbacks remain gated by any new startup added immediately afterwards.
    for(const auto& [session,target]:targets_)for(auto it=target->cleanup.begin();it!=target->cleanup.end();)if(target->controls->Ready(Start(*it)))it=target->cleanup.erase(it);else ++it;
  }
  void Handle(int owner,Dict params,double deadline,Reply reply){
    if(!reply){return;}Prune();
    if(closed_||owner<1||!params||!std::isfinite(deadline)||deadline<0||(deadline>0&&hooks_.now()>=deadline)){reply(false,"Invalid, closed or expired native target coverage command","");return;}
    const auto action=params->GetString("action").ToString();const bool starting=action=="startJS"||action=="startCSS",stopping=action=="stopJS"||action=="stopCSS";if(!starting&&!stopping){reply(false,"Unsupported native target coverage action","");return;}const Kind kind=action=="startJS"||action=="stopJS";const auto found=records_.find(kind);
    if(starting){
      if(found!=records_.end()){reply(false,"Native coverage is enabled or its actual target cleanup is pending","");return;}
      if(params->HasKey("includeWorkers")&&(!kind||params->GetType("includeWorkers")!=VTYPE_BOOL)){reply(false,"includeWorkers is a boolean JavaScript-only native coverage option; worker CSS is unsupported","");return;}
      auto record=std::make_shared<Record>();record->kind=kind;record->include_workers=kind&&params->GetBool("includeWorkers");record->reset=!params->HasKey("resetOnNavigation")||params->GetBool("resetOnNavigation");
      size_t target_count=0;for(const auto& [session,target]:targets_)if(Eligible(record,target))++target_count;if(target_count>kTargets){reply(false,"Native coverage target capacity exceeded","");return;}
      record->options=params->Copy(false);record->options->Remove("includeWorkers");record->owner=owner;record->deadline=deadline;record->reply=std::move(reply);record->scheduling=true;records_[kind]=record;std::vector<std::string> sessions;for(const auto& [session,target]:targets_)if(Eligible(record,target))sessions.push_back(session);for(const auto& session:sessions){if(record->phase==Phase::Cleaning)break;StartTarget(record,session,deadline);}record->scheduling=false;Complete(record);return;
    }
    if(found==records_.end()){reply(false,"Native coverage is not enabled","");return;}const auto record=found->second;if(record->phase==Phase::Failed){const auto error=record->error;records_.erase(found);reply(false,error,"");return;}if(record->phase!=Phase::Active){reply(false,"Native coverage is starting, stopping or physically cleaning up","");return;}record->owner=owner;record->deadline=deadline;record->reply=std::move(reply);record->phase=Phase::Stopping;
    if(!record->loss.empty()){Fail(record,record->loss);return;}record->scheduling=true;std::vector<std::string> active;for(const auto& [session,state]:record->states)if(state==State::Active)active.push_back(session);for(const auto& session:active){if(record->phase==Phase::Cleaning)break;StopTarget(record,session);}record->scheduling=false;Complete(record);
  }
  void ReadyTarget(const std::shared_ptr<Target>& target){
    if(closed_||!target->alive)return;
    for(auto it=target->cleanup.begin();it!=target->cleanup.end();)if(target->controls->Ready(Start(*it)))it=target->cleanup.erase(it);else ++it;
    if(!target->startup.empty()||!target->cleanup.empty()||target->ready.empty())return;
    const bool expired=target->deadline>0&&hooks_.now()>=target->deadline;auto replies=std::move(target->ready);for(auto& reply:replies)if(reply)reply(!expired,expired?"Native coverage target bootstrap exceeded its original deadline":"");
  }
  void Attached(const std::string& session,const std::string& id,double deadline,Ready ready,bool worker=false){
    if(closed_||session.empty()||session.size()>128||id.empty()||id.size()>256||!std::isfinite(deadline)||deadline<0||(deadline&&hooks_.now()>=deadline)){if(ready)ready(false,"Invalid or expired actual native coverage target identity");return;}
    auto found=targets_.find(session);if(found!=targets_.end()){if(found->second->id!=id||found->second->worker!=worker||found->second->ready.size()>=4){if(ready)ready(false,"Actual native coverage target identity/callback collision");return;}if(ready){found->second->deadline=found->second->deadline&&deadline?std::min(found->second->deadline,deadline):deadline?deadline:found->second->deadline;found->second->ready.push_back(std::move(ready));}ReadyTarget(found->second);return;}
    if(targets_.size()>=kObservedTargets){if(ready)ready(false,"Native coverage observed target registry capacity exceeded");return;}const auto target=NewTarget(session,id);target->worker=worker;target->deadline=deadline;if(ready)target->ready.push_back(std::move(ready));targets_[session]=target;
    // Stop freezes admission at its native invocation. A later target is still
    // paused before author execution, so it belongs to the next recording.
    std::vector<std::shared_ptr<Record>> active;for(const auto& [kind,record]:records_)if((record->phase==Phase::Active||record->phase==Phase::Starting)&&Eligible(record,target))active.push_back(record);
    // Each recording counts only its actual eligible targets: idle workers
    // never constrain ordinary CSS or the default document-only JS capture.
    for(auto it=active.begin();it!=active.end();){size_t count=0;for(const auto& [actual,entry]:targets_)if(Eligible(*it,entry))++count;if(count>kTargets){Fail(*it,"Native coverage target capacity exceeded");it=active.erase(it);}else ++it;}
    for(const auto& record:active)target->startup.insert(record->kind);
    for(const auto& record:active){const auto budget=record->owner&&record->deadline?deadline?std::min(deadline,record->deadline):record->deadline:deadline;StartTarget(record,session,budget);}ReadyTarget(target);Wake();
  }
  void Lost(const std::string& session,const std::string& error){
    if(session.empty()){return;}const auto found=targets_.find(session);if(found==targets_.end()||!found->second->current_document)return;
    found->second->current_document=false;std::vector<std::shared_ptr<Record>> affected;for(const auto& [kind,record]:records_)if(record->states.contains(session))affected.push_back(record);
    for(const auto& record:affected){if(!record->reset)continue;if(record->loss.empty())record->loss=error;if(record->phase!=Phase::Active)Fail(record,record->loss);}Wake();
  }
  void Detached(const std::string& session){
    if(session.empty()){return;}
    const auto found=targets_.find(session);if(found==targets_.end())return;auto target=found->second;target->alive=false;
    std::vector<std::shared_ptr<Record>> records;for(const auto& [kind,record]:records_)records.push_back(record);
    for(const auto& record:records)if(record->states.contains(session)){
      if(!record->reset){
        if(record->states[session]==State::Finalized&&record->results.contains(session))record->states[session]=State::Absent;
        else Fail(record,"CEF retained coverage target detached without its final agent measurement receipt");
      }else{if(target->current_document&&record->loss.empty())record->loss="Actual native coverage target detached before its engine counters could be collected";record->states.erase(session);if(target->current_document&&record->phase!=Phase::Active&&record->phase!=Phase::Cleaning)Fail(record,record->loss);}
    }
    target->controls->Closed();dispatch_->Detached(session);targets_.erase(session);for(const auto& record:records)Complete(record);auto replies=std::move(target->ready);for(auto& reply:replies)if(reply)reply(false,"Actual native coverage target detached");Wake();
  }
  bool Event(const std::string& method,Dict params,const std::string& session){
    const auto found=targets_.find(session);if(found==targets_.end()||!found->second->alive)return false;const auto target=found->second;
    const bool terminal=method=="Profiler.navigationCoverageFinalized"||method=="CSS.navigationCoverageFinalized";
    if(terminal){
      const auto capture=records_.find(method.starts_with("Profiler.")?true:false);if(capture==records_.end()||capture->second->reset||!capture->second->states.contains(session))return true;const auto record=capture->second;
      if(record->phase==Phase::Cleaning||record->phase==Phase::Failed)return true;
      if(session.empty()){Fail(record,"CEF root coverage agent retired; a paused replacement-agent admission barrier is required before author execution");return true;}
      if(record->states[session]==State::Finalized||record->states[session]==State::Absent){Fail(record,"Duplicate native retained coverage terminal agent receipt");return true;}
      target->controls->Event(method,params);std::string output,error;
      if(!target->controls->FinalReceipt(Start(record->kind),output,error)||!error.empty()){Fail(record,error.empty()?"CEF retained coverage terminal payload lacks its required final receipt":error);return true;}
      if(!SaveResult(record,session,target,output)){return true;}record->states[session]=State::Finalized;Wake();return true;
    }
    if(method!="Debugger.scriptParsed"&&method!="Debugger.paused"&&method!="Debugger.resumed"&&method!="CSS.styleSheetAdded")return false;
    bool retained=false;for(const auto& [kind,record]:records_)if(!record->reset&&record->states.contains(session))retained=true;
    if(target->current_document||retained||method=="Debugger.paused"||method=="Debugger.resumed"){target->controls->Event(method,params);}return true;
  }
  void RendererTerminated(){std::vector<std::shared_ptr<Record>> records;for(const auto& [kind,record]:records_)records.push_back(record);for(const auto& record:records)Fail(record,"Actual CEF renderer terminated before a verified coverage agent handoff; retained results are incomplete");}

  void Navigated(){
    // A genuine main-document commit retires the old child document scope. Its
    // eventual target-detach event must not turn that configured reset into
    // claimed loss from the new document, nor admit late old-document sources.
    for(const auto& [session,target]:targets_){if(!session.empty())target->current_document=false;target->controls->Navigated();}
    std::vector<std::shared_ptr<Record>> records;for(const auto& [kind,record]:records_)records.push_back(record);
    for(const auto& record:records){if(!record->reset)continue;if(record->phase==Phase::Stopping)Fail(record,"Actual main document navigated while native coverage was being collected");else if(record->phase==Phase::Active){record->loss.clear();record->results.clear();record->result_bytes=0;}}
  }
  void Cancel(int owner){std::vector<std::shared_ptr<Record>> records;for(const auto& [kind,record]:records_)if(record->owner==owner)records.push_back(record);for(const auto& record:records)Fail(record,"Native coverage operation canceled");}
  void Tick(double now){dispatch_->Tick(now);std::vector<std::shared_ptr<Record>> records;for(const auto& [kind,record]:records_)records.push_back(record);for(const auto& record:records)if(Current(record)&&record->owner&&record->deadline&&now>=record->deadline)Fail(record,"Native coverage operation exceeded its original deadline");std::vector<std::shared_ptr<Target>> targets;for(const auto& [session,target]:targets_)targets.push_back(target);for(const auto& target:targets)if(target->alive){target->controls->Tick(now);ReadyTarget(target);}Prune();}
  void Closed(){if(closed_)return;closed_=true;auto targets=std::move(targets_);auto records=std::move(records_);for(const auto& [session,target]:targets)target->controls->Closed();dispatch_->Closed();for(const auto& [kind,record]:records)ReplyOwner(record,false,"Actual native coverage page channel closed");for(const auto& [session,target]:targets)for(auto& reply:target->ready)if(reply)reply(false,"Actual native coverage page channel closed");}
  size_t PhysicalCount()const{size_t physical=0;for(const auto& [session,target]:targets_)physical+=target->controls->PhysicalCount();return physical;}
  bool NeedsTick()const{if(dispatch_->Pending())return true;for(const auto& [session,target]:targets_)if(target->controls->NeedsTick()||!target->ready.empty())return true;for(const auto& [kind,record]:records_)if(record->owner||record->phase==Phase::Cleaning)return true;return false;}
  void Diagnostics(Dict output)const{size_t recordings=0,physical=0,queued=0,bytes=0;for(const auto& [session,target]:targets_){auto values=CefDictionaryValue::Create();target->controls->Diagnostics(values);recordings+=values->GetInt("nativeCoverageRecordings");physical+=values->GetInt("nativeCoveragePhysical");queued+=values->GetInt("nativeCoverageQueuedSources");bytes+=size_t(values->GetDouble("nativeCoverageSourceBytes"));}output->SetDouble("nativeCoverageSharedPayloadBytes",double(payload_->bytes));output->SetInt("nativeCoverageBackendPhysical",int(dispatch_->Physical()));output->SetInt("nativeCoverageRecordings",int(recordings));output->SetInt("nativeCoveragePhysical",int(physical));output->SetInt("nativeCoverageQueuedSources",int(queued));output->SetDouble("nativeCoverageSourceBytes",double(bytes));}
  Hooks hooks_;std::shared_ptr<Payload> payload_;std::shared_ptr<CoverageDispatch> dispatch_;std::map<std::string,std::shared_ptr<Target>> targets_;std::map<Kind,std::shared_ptr<Record>> records_;int next_owner_=0;bool closed_=false;
};
void CoverageTargets::NativeScript(const std::string& session,int context,const std::string& script){const auto target=impl_->targets_.find(session);if(target!=impl_->targets_.end()&&target->second->alive)target->second->controls->NativeScript(context,script);}
CoverageTargets::CoverageTargets(Hooks hooks):impl_(std::make_shared<Impl>(std::move(hooks))){impl_->Init();}CoverageTargets::~CoverageTargets(){impl_->Closed();}
void CoverageTargets::Handle(int owner,Dict params,double deadline,Reply reply){impl_->Handle(owner,params,deadline,std::move(reply));}void CoverageTargets::Attached(const std::string& session,const std::string& target,double deadline,Ready ready){impl_->Attached(session,target,deadline,std::move(ready));}void CoverageTargets::WorkerAttached(const std::string& session,const std::string& target,double deadline,Ready ready){impl_->Attached(session,target,deadline,std::move(ready),true);}
void CoverageTargets::Lost(const std::string& session,const std::string& error){impl_->Lost(session,error);}
void CoverageTargets::Detached(const std::string& session){impl_->Detached(session);}bool CoverageTargets::Event(const std::string& method,Dict params,const std::string& session){return impl_->Event(method,params,session);}void CoverageTargets::Navigated(){impl_->Navigated();}void CoverageTargets::RendererTerminated(){impl_->RendererTerminated();}void CoverageTargets::Cancel(int owner){impl_->Cancel(owner);}void CoverageTargets::Tick(double now){impl_->Tick(now);}void CoverageTargets::Closed(){impl_->Closed();}size_t CoverageTargets::PhysicalCount()const{return impl_->PhysicalCount();}bool CoverageTargets::NeedsTick()const{return impl_->NeedsTick();}void CoverageTargets::Diagnostics(Dict output)const{impl_->Diagnostics(output);}
} // namespace suitecut
