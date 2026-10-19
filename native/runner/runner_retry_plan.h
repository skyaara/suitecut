// SPDX-License-Identifier: MIT
#pragma once
#include "serial_group.h"
#include <memory>
#include <map>
namespace suitecut::runner {
// Native composition of independent retry declarations and outermost serial
// groups. No-group mode delegates to the unchanged RetryPlan.
class RunnerRetryPlan {
 public:
  struct Group {uint64_t id=0;std::vector<uint64_t> tests;};
  struct GroupView {uint64_t id=0;const SerialGroup* policy=nullptr;std::vector<uint64_t> tests;};
  RunnerRetryPlan(std::vector<TestCase> tests,unsigned max_failures=0,uint64_t seed=0,std::vector<Group> groups={})
      :generation_(seed),max_failures_(max_failures) {
    if(groups.empty()){legacy_=std::make_unique<RetryPlan>(std::move(tests),max_failures,seed);return;}
    RetryPlan validate(tests,max_failures,seed);
    if(groups.size()>256)throw std::invalid_argument("Native serial group capacity256 exceeded");
    for(const auto& test:tests)known_.emplace(test.id,test);
    std::map<uint64_t,Group> declarations;std::map<uint64_t,uint64_t> membership;
    for(auto& group:groups){
      if(!group.id||group.tests.empty()||!declarations.emplace(group.id,group).second)
        throw std::invalid_argument("Invalid native serial group ID/membership");
      for(const auto id:group.tests)if(!known_.contains(id)||!membership.emplace(id,group.id).second)
        throw std::invalid_argument("Unknown, duplicate or overlapping native serial group test");
    }
    std::set<uint64_t> admitted_groups;size_t projected=0;
    for(size_t index=0;index<tests.size();){
      Unit unit;
      if(const auto member=membership.find(tests[index].id);member!=membership.end()){
        unit.id=member->second;if(!admitted_groups.insert(unit.id).second)throw std::invalid_argument("Noncontiguous native serial group members");
        const auto& group=declarations.at(unit.id);unit.tests=group.tests;
        std::vector<TestCase> members;unsigned retries=0;
        for(const auto id:group.tests){
          if(index>=tests.size()||tests[index].id!=id)throw std::invalid_argument("Native serial group order disagrees with declaration order");
          retries=std::max(retries,tests[index].retries);members.push_back(tests[index++]);
        }
        for(const auto& test:members)Charge(projected,test,retries);
        unit.serial=std::make_unique<SerialGroup>(std::move(members));
      }else{
        std::vector<TestCase> ordinary;
        while(index<tests.size()&&!membership.contains(tests[index].id)){
          unit.tests.push_back(tests[index].id);Charge(projected,tests[index],tests[index].retries);ordinary.push_back(tests[index++]);
        }
        unit.ordinary=std::make_unique<RetryPlan>(std::move(ordinary));
      }
      const auto position=units_.size();for(const auto id:unit.tests)unit_for_test_.emplace(id,position);
      units_.push_back(std::move(unit));
    }
  }
  std::optional<Attempt> Next(uint64_t pid,uint64_t admitted_generation=0,uint64_t worker_index=0){
    if(legacy_)return legacy_->Next(pid,admitted_generation);
    if(active_||Stopped()||NeedsWorkerRetirement()||!pid)return std::nullopt;
    Advance();if(current_==units_.size())return std::nullopt;
    if(generation_==UINT64_MAX)throw std::overflow_error("Native shared retry attempt generation exhausted");
    const auto next_generation=admitted_generation?admitted_generation:generation_+1;
    if(next_generation<=generation_)throw std::invalid_argument("Native shared retry attempt generation is stale");
    auto& unit=units_[current_];std::optional<Attempt> attempt;
    if(unit.serial){
      if(unit.serial->Current()==SerialGroup::State::AwaitingWorker){
        if(wave_sequence_==UINT64_MAX)throw std::overflow_error("Native serial wave generation exhausted");
        SerialGroup::Worker owner{worker_index,static_cast<int64_t>(pid),wave_sequence_+1};
        if(!unit.serial->Begin(owner))return std::nullopt;
        ++wave_sequence_;unit.worker=owner;
      }
      if(!unit.worker||unit.worker->index!=worker_index||unit.worker->pid!=static_cast<int64_t>(pid))return std::nullopt;
      attempt=unit.serial->Next(*unit.worker,next_generation);
      if(attempt)attempt->test.retries=unit.serial->MaxRetries();
    }else attempt=unit.ordinary->Next(pid,next_generation);
    if(attempt){generation_=attempt->generation;active_=*attempt;active_worker_=pid;active_index_=worker_index;}
    return attempt;
  }
  bool ExpectFailure(uint64_t pid,uint64_t generation){
    if(legacy_)return legacy_->ExpectFailure(pid,generation);
    if(!active_||pid!=active_worker_||generation!=active_->generation)return false;
    auto& unit=units_.at(current_);return unit.serial?unit.serial->ExpectFailure(*unit.worker,generation):unit.ordinary->ExpectFailure(pid,generation);
  }
  bool Finish(uint64_t pid,uint64_t generation,Outcome status,std::string error,bool infrastructure=false){
    if(legacy_)return legacy_->Finish(pid,generation,status,std::move(error),infrastructure);
    if(!active_||pid!=active_worker_||generation!=active_->generation)return false;
    auto& unit=units_.at(current_);bool accepted=false;
    if(unit.serial){accepted=unit.serial->Finish(*unit.worker,generation,status,std::move(error),infrastructure);if(accepted)results_.push_back(unit.serial->Results().back());}
    else{accepted=unit.ordinary->Finish(pid,generation,status,std::move(error),infrastructure);if(accepted){results_.push_back(unit.ordinary->Results().back());Account(unit);}}
    if(accepted){
      if(!unit.serial&&(results_.back().expected||active_->retry>=active_->test.retries||status==Outcome::Interrupted))finalized_.insert(active_->test.id);
      active_.reset();
    }
    return accepted;
  }
  bool WorkerExited(uint64_t pid,uint64_t worker_index=0){
    if(legacy_)return legacy_->WorkerExited(pid);
    if(active_||current_>=units_.size())return false;
    auto& unit=units_[current_];
    if(unit.serial){
      if(!unit.worker||unit.worker->pid!=static_cast<int64_t>(pid)||unit.worker->index!=worker_index||!unit.serial->Retired(*unit.worker))return false;
      unit.worker.reset();Account(unit);return true;
    }
    return unit.ordinary->WorkerExited(pid);
  }
  // Called only after the native event pump has actual host/file/descendant
  // retirement receipts. A missing callback result remains interrupted, never
  // an invented successful settlement.
  void ActualWorkerExited(uint64_t pid,uint64_t worker_index){
    if(legacy_)return;
    if(active_&&pid==active_worker_&&worker_index==active_index_){
      stop_=true;
      Finish(pid,active_->generation,Outcome::Interrupted,"Actual worker exited before complete attempt receipt",true);
    }
    if(current_<units_.size()){
      auto& unit=units_[current_];
      if(unit.serial&&unit.worker&&unit.worker->pid==static_cast<int64_t>(pid)&&unit.worker->index==worker_index){
        if(unit.serial->Current()==SerialGroup::State::Ready){stop_=true;unit.serial->Stop();}
        WorkerExited(pid,worker_index);
      }
    }
  }
  void ExcludeQueued(const std::set<uint64_t>& ids){
    if(legacy_){legacy_->ExcludeQueued(ids);return;}
    if(active_)throw std::runtime_error("Cannot exclude native retry declarations during active attempt");
    for(const auto id:ids)if(!known_.contains(id))throw std::invalid_argument("Unknown native retry declaration exclusion");
    for(auto& unit:units_){std::set<uint64_t> own;for(const auto id:unit.tests)if(ids.contains(id))own.insert(id);if(own.empty())continue;
      if(unit.serial)unit.serial->ExcludeQueued(own);else unit.ordinary->ExcludeQueued(own);
      finalized_.insert(own.begin(),own.end());
    }
  }
  bool NeedsWorkerRetirement()const{
    if(legacy_)return legacy_->NeedsWorkerRetirement();
    if(current_>=units_.size())return false;
    const auto& unit=units_[current_];return unit.serial?unit.serial->Current()==SerialGroup::State::Retiring:unit.ordinary->NeedsWorkerRetirement();
  }
  bool Done()const{
    if(legacy_)return legacy_->Done();
    if(active_||NeedsWorkerRetirement())return false;
    if(Stopped())return true;
    for(size_t index=current_;index<units_.size();++index)if(!UnitDone(units_[index]))return false;
    return true;
  }
  bool FailureLimitReached()const{return legacy_?legacy_->FailureLimitReached():max_failures_&&failures_>=max_failures_;}
  unsigned UnexpectedTestFailures()const{return legacy_?legacy_->UnexpectedTestFailures():failures_;}
  const std::vector<Result>& Results()const{return legacy_?legacy_->Results():results_;}
  bool InSerialGroup(uint64_t id)const{return !legacy_&&units_.at(unit_for_test_.at(id)).serial!=nullptr;}
  bool LastInSerialGroup(uint64_t id)const{return InSerialGroup(id)&&units_.at(unit_for_test_.at(id)).serial->LastSelected(id);}
  bool Finalized(uint64_t id)const{
    if(legacy_)return false;
    const auto& unit=units_.at(unit_for_test_.at(id));return unit.serial?unit.serial->Done():finalized_.contains(id);
  }
  bool FinalExpected(uint64_t id)const{
    if(InSerialGroup(id)){const auto c=units_.at(unit_for_test_.at(id)).serial->Classify(id);return c==SerialGroup::Classification::Passed||c==SerialGroup::Classification::Skipped||c==SerialGroup::Classification::Flaky;}
    for(auto result=Results().rbegin();result!=Results().rend();++result)if(result->attempt.test.id==id)return result->expected;
    return false;
  }
  std::vector<GroupView> Groups()const{
    std::vector<GroupView> groups;for(const auto& unit:units_)if(unit.serial)groups.push_back({unit.id,unit.serial.get(),unit.tests});return groups;
  }
  void Stop(){if(legacy_)return;stop_=true;for(auto& unit:units_)if(unit.serial)unit.serial->Stop();}
 private:
  struct Unit {uint64_t id=0;std::vector<uint64_t> tests;std::unique_ptr<SerialGroup> serial;std::unique_ptr<RetryPlan> ordinary;std::optional<SerialGroup::Worker> worker;unsigned accounted=0;};
  static void Charge(size_t& total,const TestCase& test,unsigned retries){
    size_t bytes=test.title.size()+8448;for(const auto& fixture:test.fixtures)bytes+=fixture.size()+32;
    const auto projected=bytes*(static_cast<size_t>(retries)+1);if(projected>67108864-total)throw std::invalid_argument("Native retry/group projected metadata64MiB exceeded");total+=projected;
  }
  static bool UnitDone(const Unit& unit){return unit.serial?unit.serial->Done():unit.ordinary->Done();}
  void Advance(){while(current_<units_.size()&&UnitDone(units_[current_]))++current_;}
  bool Stopped()const{return stop_||FailureLimitReached();}
  void Account(Unit& unit){const auto count=unit.serial?unit.serial->FinalUnexpectedTests():unit.ordinary->UnexpectedTestFailures();failures_+=count-unit.accounted;unit.accounted=count;}
  std::unique_ptr<RetryPlan> legacy_;std::map<uint64_t,TestCase> known_;std::map<uint64_t,size_t> unit_for_test_;
  std::vector<Unit> units_;std::vector<Result> results_;std::set<uint64_t> finalized_;
  std::optional<Attempt> active_;size_t current_=0;uint64_t generation_=0,wave_sequence_=0,active_worker_=0,active_index_=0;
  unsigned max_failures_=0,failures_=0;bool stop_=false;
};
}
