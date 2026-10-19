// SPDX-License-Identifier: MIT
#include "developer_debugger_evaluations.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include "developer_debugger_evaluation_projection.h"
namespace suitecut {
namespace {std::atomic<size_t>process_evaluations{0};constexpr size_t kSession=8,kProcess=64;constexpr double kCleanup=30000;}
class DeveloperDebuggerEvaluations::Impl:public std::enable_shared_from_this<Impl>{
 public:
  struct Record{
    uint64_t serial=0;int owner=0;double deadline=0,cleanup=0;
    std::string pause,frame,group,json,error;Reply reply;
    bool evaluating=true,releasing=false,failed=false,canceled=false,result_ok=false,realm_dead=false;
    Record()=default;Record(const Record&)=delete;Record&operator=(const Record&)=delete;
    ~Record(){--process_evaluations;}
  };
  explicit Impl(Hooks hooks):hooks_(std::move(hooks)){}
  Hooks hooks_;std::map<uint64_t,std::shared_ptr<Record>>records_;std::shared_ptr<Impl>physical_self_;uint64_t serial_=0;
  bool closed_=false,closing_=false,close_requested_=false;
  bool Expired(double deadline)const{return deadline>0&&hooks_.now()>=deadline;}
  bool Current(const std::shared_ptr<Record>&record)const{const auto found=records_.find(record->serial);return !closed_&&found!=records_.end()&&found->second==record;}
  void Changed(){if(hooks_.changed)hooks_.changed();}
  void CleanupClock(const std::shared_ptr<Record>&record){if(!record->cleanup)record->cleanup=(Expired(record->deadline)?record->deadline:hooks_.now())+kCleanup;}
  void Finish(const std::shared_ptr<Record>&record,bool ok,const std::string&error){
    auto reply=std::move(record->reply);record->owner=0;if(!reply)return;
    if(Expired(record->deadline)){reply(false,"","Native paused evaluation original deadline expired");return;}
    if(ok&&(closing_||record->canceled||!hooks_.current(record->pause,record->frame))){reply(false,"","Native paused evaluation exact pause or frame changed before publication");return;}
    reply(ok,ok?record->json:std::string(),error);
  }
  void RequestClose(const std::string&error){if(close_requested_||closed_)return;close_requested_=true;if(hooks_.close_channel)hooks_.close_channel(error);}
  void Erase(const std::shared_ptr<Record>&record){records_.erase(record->serial);if(records_.empty())physical_self_.reset();}
  void Release(const std::shared_ptr<Record>&record){
    if(!Current(record)||record->evaluating||record->releasing)return;
    if(record->realm_dead){Erase(record);Finish(record,false,"Actual paused evaluation V8 realm was destroyed");Changed();return;}
    if(record->failed)return;
    CleanupClock(record);record->releasing=true;
    auto params=CefDictionaryValue::Create();params->SetString("objectGroup",record->group);
    auto self=shared_from_this();
    const auto actual=[self,record](bool ok,Dict,const std::string&error){
      if(!self->Current(record))return;
      record->releasing=false;
      if(!ok&&!record->realm_dead){record->failed=true;self->Finish(record,false,"Actual paused evaluation object-group release failed: "+error);self->RequestClose("Actual paused evaluation object-group release failed; physical ownership retained");self->Changed();return;}
      self->Erase(record);self->Finish(record,record->result_ok&&!record->realm_dead,record->realm_dead?"Actual paused evaluation V8 realm was destroyed":record->error);self->Changed();
    };
    if(!hooks_.send("Runtime.releaseObjectGroup",params,record->cleanup,actual))actual(false,nullptr,"Native release dispatch was not admitted");
  }
  void Evaluate(int owner,const std::string&pause,const std::string&frame,const std::string&expression_json,double deadline,Reply reply){
    if(!reply)return;
    if(std::isfinite(deadline)&&Expired(deadline)){reply(false,"","Native paused evaluation original deadline expired before admission");return;}
    std::u16string expression;
    if(closed_||closing_||close_requested_||owner<1||!std::isfinite(deadline)||deadline<0||pause.empty()||pause.size()>512||frame.empty()||frame.size()>512||
       expression_json.size()>49154||!DevToolsStringUnits(expression_json,expression)||expression.empty()||expression.size()>8192||DevToolsHasIsolated(expression)||
       !hooks_.current(pause,frame)||records_.size()>=kSession||serial_==9007199254740991ULL){
      reply(false,"","Native paused evaluation requires bounded source and an exact current pause/frame; isolated source units require JavaScript Unicode escapes");return;
    }
    if(Expired(deadline)){reply(false,"","Native paused evaluation original deadline expired during admission");return;}
    size_t count=process_evaluations.load();while(count<kProcess&&!process_evaluations.compare_exchange_weak(count,count+1)){}
    if(count>=kProcess){reply(false,"","Native paused evaluation process group capacity exceeded");return;}
    auto record=std::make_shared<Record>();record->serial=++serial_;record->owner=owner;record->deadline=deadline;record->pause=pause;record->frame=frame;
    record->group=hooks_.session+":evaluation:"+std::to_string(record->serial);record->reply=std::move(reply);records_.emplace(record->serial,record);physical_self_=shared_from_this();
    auto params=CefDictionaryValue::Create();params->SetString("callFrameId",frame);params->SetString("expression",CefString(expression));params->SetString("objectGroup",record->group);
    params->SetBool("returnByValue",false);params->SetBool("generatePreview",false);params->SetBool("includeCommandLineAPI",false);params->SetBool("silent",true);
    if(deadline>0)params->SetDouble("timeout",std::max(0.0,deadline-hooks_.now()));
    struct Acceptance{struct Result{bool ok,projected;std::string json,error;};bool returned=false,admitted=false;std::optional<Result>pending;};
    auto accepted=std::make_shared<Acceptance>();auto self=shared_from_this();
    const auto consume=[self,record](Acceptance::Result result,bool admitted){
      if(!self->Current(record))return;
      record->evaluating=false;
      if(!admitted&&!result.ok){self->Erase(record);self->Finish(record,false,result.error);self->Changed();return;}
      record->result_ok=result.ok&&result.projected;record->json=std::move(result.json);record->error=std::move(result.error);
      self->Release(record);
    };
    const auto actual=[self,record,accepted,consume](bool ok,Dict data,const std::string&error){
      if(!self->Current(record))return;
      Acceptance::Result result{ok,false,"",error};
      if(ok)result.projected=DeveloperDebuggerEvaluationProjection::Build(data,result.json,result.error);
      if(!accepted->returned){accepted->pending=std::move(result);return;}
      consume(std::move(result),accepted->admitted);
    };
    accepted->admitted=hooks_.send("Debugger.evaluateOnCallFrame",params,deadline,actual);accepted->returned=true;
    if(accepted->pending){auto pending=std::move(*accepted->pending);accepted->pending.reset();consume(std::move(pending),accepted->admitted);}
    else if(!accepted->admitted)consume(Acceptance::Result{false,false,"","Native paused evaluation dispatch was not admitted"},false);
  }
  void Cancel(int owner){for(auto&[id,record]:records_)if(record->owner==owner){CleanupClock(record);record->owner=0;record->reply={};record->canceled=true;}}
  void Close(){if(closed_||closing_)return;auto self=shared_from_this();closing_=true;std::vector<std::shared_ptr<Record>>records;for(const auto&[id,record]:records_)records.push_back(record);for(const auto&record:records){CleanupClock(record);record->canceled=true;record->owner=0;record->reply={};if(Current(record))Release(record);}Changed();}
  void RealmDestroyed(){
    if(closed_)return;
    const auto self=shared_from_this();std::vector<std::shared_ptr<Record>>records;
    for(const auto&[id,record]:records_)records.push_back(record);
    for(const auto&record:records){record->realm_dead=true;if(Current(record))Release(record);}
  }
  void Tick(double now){if(closed_||close_requested_)return;const auto self=shared_from_this();for(const auto&[id,record]:records_)if(record->cleanup&&now>=record->cleanup){RequestClose("Native paused evaluation physical cleanup grace expired; actual channel closure required");return;}}
  void Closed(){auto self=shared_from_this();closed_=closing_=true;records_.clear();physical_self_.reset();}
};
DeveloperDebuggerEvaluations::DeveloperDebuggerEvaluations(Hooks hooks):impl_(std::make_shared<Impl>(std::move(hooks))){}
DeveloperDebuggerEvaluations::~DeveloperDebuggerEvaluations(){impl_->Close();}
void DeveloperDebuggerEvaluations::Evaluate(int owner,const std::string&pause,const std::string&frame,const std::string&expression_json,double deadline,Reply reply){impl_->Evaluate(owner,pause,frame,expression_json,deadline,std::move(reply));}
void DeveloperDebuggerEvaluations::Cancel(int owner){impl_->Cancel(owner);}
void DeveloperDebuggerEvaluations::Close(){impl_->Close();}
void DeveloperDebuggerEvaluations::RealmDestroyed(){impl_->RealmDestroyed();}
void DeveloperDebuggerEvaluations::Tick(double now){impl_->Tick(now);}
bool DeveloperDebuggerEvaluations::NeedsTick()const{if(impl_->closed_||impl_->close_requested_)return false;for(const auto&[id,record]:impl_->records_)if(record->cleanup)return true;return false;}
void DeveloperDebuggerEvaluations::Closed(){impl_->Closed();}
size_t DeveloperDebuggerEvaluations::Retained()const{return impl_->records_.size();}
size_t DeveloperDebuggerEvaluations::ProcessRetained(){return process_evaluations.load();}
}
