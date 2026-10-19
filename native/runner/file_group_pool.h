// SPDX-License-Identifier: MIT
#pragma once
#include "file_work_groups.h"
#include <functional>
#include <optional>
namespace suitecut::runner {
// Pure bounded policy. WorkerExited is supplied only after actual host/raw,
// file/output rollback and shared descendant receipts; logical completion does
// not release that physical lease. Storage is allocated before any admission.
class FileGroupPool {
 public:
  enum class State {Pending,Running,Passed,Failed,Interrupted,NotRun};
  enum class Outcome {Passed,Failed,Interrupted};
  struct Admission {
    SuiteFileToken file;size_t index;uint64_t generation;
    bool operator==(const Admission&)const=default;
  };
  static constexpr size_t kWorkers=8;
  FileGroupPool(FileWorkGroups groups,size_t limit)
      :plan_(std::move(groups)),rows_(plan_.Groups().size()),limit_(limit){
    if(!limit||limit>kWorkers||rows_.empty()||rows_.size()>NativeTestGroupTree::kGroups)
      throw std::invalid_argument("Native file work-group pool capacity rejected");
  }
  std::optional<Admission> Next(const std::function<bool(size_t)>& ready={}){
    if(stopped_||active_>=limit_)return std::nullopt;
    for(size_t i=0;i<rows_.size();++i)if(rows_[i].state==State::Pending&&(!ready||ready(i))){
      if(nonce_==UINT64_MAX)throw std::overflow_error("Native file work-group generation exhausted");
      const Admission owner{plan_.File(),i,nonce_+1};
      auto& row=rows_[i];row.owner=owner;row.state=State::Running;++nonce_;++active_;return owner;
    }
    return std::nullopt;
  }
  bool WorkerStarted(const Admission& owner,unsigned index,int64_t pid){
    auto* row=Find(owner);
    if(!row||row->worker||row->outcome||!index||index<=highest_worker_||pid<=0)return false;
    for(const auto& existing:rows_)if(existing.worker&&existing.worker->pid==pid)return false;
    row->worker=Worker{index,pid};row->started=true;highest_worker_=index;++physical_;return true;
  }
  bool WorkerExited(const Admission& owner,unsigned index,int64_t pid){
    auto* row=Find(owner);
    if(!row||!row->worker||row->worker->index!=index||row->worker->pid!=pid)return false;
    row->worker.reset();--physical_;Retire(*row);return true;
  }
  bool Complete(const Admission& owner,Outcome outcome){
    auto* row=Find(owner);
    if(!row||!row->started||row->outcome||(outcome!=Outcome::Passed&&outcome!=Outcome::Failed&&outcome!=Outcome::Interrupted))return false;
    row->outcome=outcome;Retire(*row);return true;
  }
  void Stop()noexcept{
    if(stopped_)return;
    stopped_=true;
    for(auto& row:rows_){
      if(row.state==State::Pending)row.state=State::NotRun;
      else if(row.state==State::Running&&!row.started){row.state=State::NotRun;row.canceled=true;--active_;}
    }
  }
  bool Reserved(const Admission& owner)const{return Find(owner)!=nullptr;}
  bool ReservedBeforeStart(const Admission& owner)const{const auto* row=Find(owner);return row&&!row->started&&!row->worker&&!row->outcome;}
  bool StartedWorkerExited(const Admission& owner)const{const auto* row=Find(owner);return row&&row->started&&!row->worker&&!row->outcome;}
  bool Retired(const Admission& owner)const{const auto* row=Exact(owner);return row&&row->retired;}
  bool CanceledBeforeStart(const Admission& owner)const{const auto* row=Exact(owner);return row&&row->canceled;}
  bool Done()const{
    return !active_&&std::none_of(rows_.begin(),rows_.end(),[](const auto& row){return row.state==State::Pending;});
  }
  size_t Active()const{return active_;}
  size_t Physical()const{return physical_;}
  State Group(size_t index)const{return rows_.at(index).state;}
  const FileWorkGroups& Plan()const{return plan_;}
 private:
  struct Worker {unsigned index;int64_t pid;};
  struct Row {
    State state=State::Pending;std::optional<Admission> owner;
    std::optional<Worker> worker;std::optional<Outcome> outcome;
    bool started=false,retired=false,canceled=false;
  };
  const Row* Exact(const Admission& owner)const{
    if(owner.index>=rows_.size())return nullptr;
    const auto& row=rows_[owner.index];return row.owner&&*row.owner==owner?&row:nullptr;
  }
  const Row* Find(const Admission& owner)const{const auto* row=Exact(owner);return row&&row->state==State::Running?row:nullptr;}
  Row* Find(const Admission& owner){return const_cast<Row*>(std::as_const(*this).Find(owner));}
  void Retire(Row& row)noexcept{
    if(row.worker||!row.outcome)return;
    row.state=*row.outcome==Outcome::Passed?State::Passed:*row.outcome==Outcome::Failed?State::Failed:State::Interrupted;
    row.retired=true;--active_;
  }
  FileWorkGroups plan_;std::vector<Row> rows_;size_t limit_,active_=0,physical_=0;
  uint64_t nonce_=0;unsigned highest_worker_=0;bool stopped_=false;
};
}
