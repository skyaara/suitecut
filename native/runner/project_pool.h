// SPDX-License-Identifier: MIT
#pragma once
#include "project_plan.h"
#include <functional>
namespace suitecut::runner {
// Native ready-project pool. Reservation, logical completion, and actual worker
// retirement are distinct receipts. A slot/dependency stays fenced until both.
class ProjectPool {
 public:
  using State=SerialProjectPlan::State;using Outcome=SerialProjectPlan::Outcome;
  using Result=SerialProjectPlan::Result;using Admission=SerialProjectPlan::Admission;
  static constexpr size_t kWorkers=8;
  ProjectPool(ProjectGraph graph,ProjectPlanSelection selection,size_t limit):graph_(std::move(graph)),results_(graph_.Declarations().size()),limit_(limit),no_dependencies_(selection.no_dependencies){
    retired_.resize(results_.size());canceled_.resize(results_.size());
    if(!limit||limit>kWorkers)throw std::invalid_argument("Native parallel project workers require1..8");
    if(selection.selected.size()!=results_.size()||(!no_dependencies_&&graph_.Closure(selection.selected)!=selection.selected))throw std::invalid_argument("Native project pool selection omits dependency/teardown closure");
    for(size_t i=0;i<results_.size();++i)if(selection.selected[i])results_[i].state=State::Pending;
  }
  std::optional<Admission> Next(const std::function<bool(size_t)>& ready={}){
    if(stopped_||active_.size()>=limit_)return std::nullopt;
    bool blocked=false;
    for(size_t i=0;i<results_.size();++i){if(results_[i].state!=State::Pending||IsTeardown(i))continue;
      bool waiting=false;std::optional<size_t> failed;
      if(!no_dependencies_)for(const auto dep:graph_.Dependencies(i)){
        const auto state=results_[dep].state;
        if(state==State::Pending||state==State::Running)waiting=true;
        else if(state!=State::Passed&&!failed)failed=dep;
      }
      if(failed){results_[i]={State::Blocked,failed,"Native project dependency did not pass"};blocked=true;continue;}
      if(!waiting&&(!ready||ready(i)))return Admit(i);
    }
    if(blocked)return Next(ready);
    for(size_t i=0;i<results_.size();++i)if(!IsTeardown(i)&&(results_[i].state==State::Pending||results_[i].state==State::Running))return std::nullopt;
    for(size_t i=0;i<results_.size();++i)if(IsTeardown(i)&&results_[i].state==State::Pending&&(!ready||ready(i)))return Admit(i);
    return std::nullopt;
  }
  bool WorkerStarted(uint64_t generation,unsigned worker,int64_t pid){
    const auto found=active_.find(generation);
    if(found==active_.end()||!worker||pid<=0||found->second.worker||found->second.outcome||worker<=highest_worker_||pids_.contains(pid))return false;
    found->second.worker=Worker{worker,pid};found->second.started=true;highest_worker_=worker;pids_.insert(pid);return true;
  }
  bool WorkerExited(uint64_t generation,unsigned worker,int64_t pid){
    const auto found=active_.find(generation);
    if(found==active_.end()||!found->second.worker||found->second.worker->index!=worker||found->second.worker->pid!=pid)return false;
    found->second.worker.reset();pids_.erase(pid);Retire(found);return true;
  }
  bool Complete(uint64_t generation,Outcome outcome){
    const auto found=active_.find(generation);
    if(found==active_.end()||!found->second.started||found->second.outcome)return false;
    found->second.outcome=outcome;Retire(found);return true;
  }
  void Stop(const std::string& reason){
    if(reason.size()>8192)throw std::invalid_argument("Native project pool stop reason capacity exceeded");
    if(stopped_)return;
    stopped_=true;
    for(auto it=active_.begin();it!=active_.end();)if(!it->second.started){const auto admission=it->second.admission;results_[admission.index]={State::NotRun,std::nullopt,reason};canceled_[admission.index]=admission;it=active_.erase(it);}else ++it;
    for(auto& result:results_)if(result.state==State::Pending)result={State::NotRun,std::nullopt,reason};
  }
  bool Reserved(const Admission& admission)const{
    const auto found=active_.find(admission.generation);return found!=active_.end()&&Same(found->second.admission,admission);
  }
  bool ReservedBeforeStart(const Admission& admission)const{
    const auto found=active_.find(admission.generation);return found!=active_.end()&&Same(found->second.admission,admission)&&!found->second.started&&!found->second.worker&&!found->second.outcome;
  }
  bool Retired(const Admission& admission)const{return admission.index<retired_.size()&&retired_[admission.index]&&Same(*retired_[admission.index],admission);}
  bool CanceledBeforeStart(const Admission& admission)const{return admission.index<canceled_.size()&&canceled_[admission.index]&&Same(*canceled_[admission.index],admission);}
  bool StartedWorkerExited(const Admission& admission)const{
    const auto found=active_.find(admission.generation);
    return found!=active_.end()&&Same(found->second.admission,admission)&&found->second.started&&!found->second.worker&&!found->second.outcome;
  }
  bool Done()const{return active_.empty()&&std::none_of(results_.begin(),results_.end(),[](const auto& result){return result.state==State::Pending||result.state==State::Running;});}
  size_t Active()const{return active_.size();}
  size_t Physical()const{return pids_.size();}
  const std::vector<Result>& Results()const{return results_;}
  const ProjectGraph& Graph()const{return graph_;}
 private:
  static bool Same(const Admission& a,const Admission& b){return a.index==b.index&&a.generation==b.generation&&a.teardown==b.teardown;}
  struct Worker {unsigned index;int64_t pid;};
  struct Owner {Admission admission;std::optional<Worker> worker=std::nullopt;std::optional<Outcome> outcome=std::nullopt;bool started=false;};
  bool IsTeardown(size_t index)const{return !no_dependencies_&&graph_.IsTeardown(index);}
  Admission Admit(size_t index){if(nonce_==UINT64_MAX)throw std::overflow_error("Native project pool generation exhausted");const Admission admission{index,++nonce_,IsTeardown(index)};active_.emplace(admission.generation,Owner{admission});results_[index].state=State::Running;return admission;}
  void Retire(std::map<uint64_t,Owner>::iterator found){if(found->second.worker||!found->second.outcome)return;const auto outcome=*found->second.outcome;retired_[found->second.admission.index]=found->second.admission;results_[found->second.admission.index].state=outcome==Outcome::Passed?State::Passed:outcome==Outcome::Failed?State::Failed:State::Interrupted;active_.erase(found);}
  std::vector<std::optional<Admission>> retired_,canceled_;
  ProjectGraph graph_;std::vector<Result> results_;std::map<uint64_t,Owner> active_;unsigned highest_worker_=0;std::set<int64_t> pids_;size_t limit_;uint64_t nonce_=0;bool no_dependencies_,stopped_=false;
};
}
