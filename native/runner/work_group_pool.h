// SPDX-License-Identifier: MIT
#pragma once
#include "project_plan.h"
#include "test_work_groups.h"
namespace suitecut::runner {
// Native policy only. The adapter supplies physical receipts after host, raw,
// fixture, file, output and shared descendant retirement. No policy method
// calls waitpid, extends a deadline or assigns a callback/attempt identity.
class WorkGroupPool {
 public:
  using State=SerialProjectPlan::State;using Outcome=SerialProjectPlan::Outcome;using Result=SerialProjectPlan::Result;
  struct Admission {size_t index,project;uint64_t generation;bool teardown;};
  static constexpr size_t kWorkers=8;
  WorkGroupPool(ProjectGraph graph,ProjectPlanSelection selection,TestWorkGroups groups,size_t limit)
      :graph_(std::move(graph)),groups_(std::move(groups)),projects_(graph_.Declarations().size()),
       states_(groups_.Groups().size(),State::Pending),started_(projects_.size(),false),
       retired_(states_.size()),canceled_(states_.size()),limit_(limit),no_dependencies_(selection.no_dependencies){
    if(!limit||limit>kWorkers||selection.selected.size()!=projects_.size()||groups_.ByProject().size()!=projects_.size()||
       (!no_dependencies_&&graph_.Closure(selection.selected)!=selection.selected))
      throw std::invalid_argument("Native work-group pool capacity or project selection rejected");
    for(size_t project=0;project<projects_.size();++project){
      if(selection.selected[project]!=!groups_.ByProject()[project].empty())throw std::invalid_argument("Native work-group plan disagrees with selected projects");
      if(selection.selected[project])projects_[project].state=State::Pending;
    }
  }
  std::optional<Admission> Next(const std::function<bool(size_t)>& ready={}){
    if(stopped_||active_.size()>=limit_)return std::nullopt;
    bool blocked;
    do{
      blocked=false;
      for(size_t project=0;project<projects_.size();++project){
        const auto state=projects_[project].state;
        if(IsTeardown(project)||(state!=State::Pending&&state!=State::Running))continue;
        bool waiting=false;std::optional<size_t> failed;
        if(!no_dependencies_)for(const auto dependency:graph_.Dependencies(project)){
          const auto result=projects_[dependency].state;
          if(result==State::Pending||result==State::Running)waiting=true;
          else if(result!=State::Passed&&!failed)failed=dependency;
        }
        if(failed){
          for(const auto group:groups_.ByProject()[project]){
            if(states_[group]!=State::Pending)throw std::runtime_error("Native dependent group was admitted before dependency retirement");
            states_[group]=State::Blocked;
          }
          projects_[project]={State::Blocked,failed,"Native project dependency did not pass"};blocked=true;continue;
        }
        if(!waiting)for(const auto group:groups_.ByProject()[project])
          if(states_[group]==State::Pending&&(!ready||ready(group)))return Admit(group);
      }
    }while(blocked);
    for(size_t project=0;project<projects_.size();++project)
      if(!IsTeardown(project)&&(projects_[project].state==State::Pending||projects_[project].state==State::Running))return std::nullopt;
    for(size_t project=0;project<projects_.size();++project)if(IsTeardown(project))
      for(const auto group:groups_.ByProject()[project])if(states_[group]==State::Pending&&(!ready||ready(group)))return Admit(group);
    return std::nullopt;
  }
  bool WorkerStarted(const Admission& admission,unsigned worker,int64_t pid){
    const auto found=Find(admission);
    if(found==active_.end()||!worker||pid<=0||found->second.worker||found->second.outcome||worker<=highest_worker_||pids_.contains(pid))return false;
    found->second.worker=Worker{worker,pid};found->second.started=true;started_[admission.project]=true;highest_worker_=worker;pids_.insert(pid);return true;
  }
  bool WorkerExited(const Admission& admission,unsigned worker,int64_t pid){
    const auto found=Find(admission);
    if(found==active_.end()||!found->second.worker||found->second.worker->index!=worker||found->second.worker->pid!=pid)return false;
    found->second.worker.reset();pids_.erase(pid);Retire(found);return true;
  }
  bool Complete(const Admission& admission,Outcome outcome){
    const auto found=Find(admission);
    if(found==active_.end()||!found->second.started||found->second.outcome)return false;
    found->second.outcome=outcome;Retire(found);return true;
  }
  void Stop(const std::string& reason){
    if(reason.size()>8192)throw std::invalid_argument("Native work-group stop reason capacity exceeded");
    if(stopped_)return;
    stopped_=true;
    for(auto it=active_.begin();it!=active_.end();)if(!it->second.started){
      const auto owner=it->second.admission;states_[owner.index]=State::NotRun;canceled_[owner.index]=owner;it=active_.erase(it);
    }else ++it;
    for(auto& state:states_)if(state==State::Pending)state=State::NotRun;
    for(size_t project=0;project<projects_.size();++project){
      if(projects_[project].state==State::Pending||projects_[project].state==State::Running){UpdateProject(project);projects_[project].reason=reason;}
    }
  }
  bool Reserved(const Admission& owner)const{return Find(owner)!=active_.end();}
  bool ReservedBeforeStart(const Admission& owner)const{
    const auto found=Find(owner);return found!=active_.end()&&!found->second.started&&!found->second.worker&&!found->second.outcome;
  }
  bool StartedWorkerExited(const Admission& owner)const{
    const auto found=Find(owner);return found!=active_.end()&&found->second.started&&!found->second.worker&&!found->second.outcome;
  }
  bool Retired(const Admission& owner)const{return owner.index<retired_.size()&&retired_[owner.index]&&Same(*retired_[owner.index],owner);}
  bool CanceledBeforeStart(const Admission& owner)const{return owner.index<canceled_.size()&&canceled_[owner.index]&&Same(*canceled_[owner.index],owner);}
  bool Done()const{return active_.empty()&&std::none_of(states_.begin(),states_.end(),[](const auto state){return state==State::Pending||state==State::Running;});}
  size_t Active()const{return active_.size();}
  size_t Physical()const{return pids_.size();}
  const std::vector<Result>& Projects()const{return projects_;}
  const std::vector<State>& Groups()const{return states_;}
  const TestWorkGroups& Plan()const{return groups_;}
  const ProjectGraph& Graph()const{return graph_;}
 private:
  struct Worker {unsigned index;int64_t pid;};
  struct Owner {Admission admission;std::optional<Worker> worker=std::nullopt;std::optional<Outcome> outcome=std::nullopt;bool started=false;};
  using Owners=std::map<uint64_t,Owner>;
  static bool Same(const Admission& a,const Admission& b){return a.index==b.index&&a.project==b.project&&a.generation==b.generation&&a.teardown==b.teardown;}
  Owners::iterator Find(const Admission& owner){const auto found=active_.find(owner.generation);return found!=active_.end()&&Same(found->second.admission,owner)?found:active_.end();}
  Owners::const_iterator Find(const Admission& owner)const{const auto found=active_.find(owner.generation);return found!=active_.end()&&Same(found->second.admission,owner)?found:active_.end();}
  bool IsTeardown(size_t project)const{return !no_dependencies_&&graph_.IsTeardown(project);}
  Admission Admit(size_t index){
    if(nonce_==UINT64_MAX)throw std::overflow_error("Native work-group generation exhausted");
    const auto project=groups_.Groups().at(index).project;const Admission owner{index,project,nonce_+1,IsTeardown(project)};
    active_.emplace(owner.generation,Owner{owner});++nonce_;states_[index]=State::Running;projects_[project].state=State::Running;return owner;
  }
  void Retire(Owners::iterator found){
    if(found->second.worker||!found->second.outcome)return;
    const auto owner=found->second.admission;const auto outcome=*found->second.outcome;
    retired_[owner.index]=owner;states_[owner.index]=outcome==Outcome::Passed?State::Passed:outcome==Outcome::Failed?State::Failed:State::Interrupted;
    active_.erase(found);UpdateProject(owner.project);
  }
  void UpdateProject(size_t project){
    bool pending=false,running=false,failed=false,interrupted=false,not_run=false;
    for(const auto group:groups_.ByProject()[project]){
      const auto state=states_[group];pending|=state==State::Pending;running|=state==State::Running;failed|=state==State::Failed;
      interrupted|=state==State::Interrupted;not_run|=state==State::NotRun;
    }
    projects_[project].state=running?State::Running:pending?(started_[project]?State::Running:State::Pending):failed?State::Failed:
      interrupted?State::Interrupted:not_run?(started_[project]?State::Interrupted:State::NotRun):State::Passed;
  }
  ProjectGraph graph_;TestWorkGroups groups_;std::vector<Result> projects_;std::vector<State> states_;std::vector<bool> started_;
  std::vector<std::optional<Admission>> retired_,canceled_;Owners active_;std::set<int64_t> pids_;unsigned highest_worker_=0;
  size_t limit_;uint64_t nonce_=0;bool no_dependencies_,stopped_=false;
};
}
