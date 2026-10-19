// SPDX-License-Identifier: MIT
#pragma once
#include "suite_tree.h"
#include "host_process.h"
#include <optional>

namespace suitecut::runner {
// Native logical routing only. HookWorker retains actual fixture/callback leases;
// the runner's descendant fence remains responsible for physical helper exits.
class NativeSuiteWorkerPlan {
 public:
  enum class Kind { BeforeAll, Test, AfterAll, SkippedTest };
  struct Operation {
    Kind kind;
    uint64_t epoch, token, suite, test;
    std::vector<uint64_t> callbacks, before_each, after_each;
    uint64_t skip_cause = 0;
    std::vector<uint64_t> skipped_tests{};
  };
  NativeSuiteWorkerPlan(NativeSuiteTree tree,const std::set<uint64_t>& selected,
                        uint64_t epoch,pid_t worker)
      :tree_(std::move(tree)),routes_(tree_.Plan(selected)),epoch_(epoch),worker_(worker) {
    if(!epoch || worker<=0)throw std::runtime_error("Invalid native suite worker identity");
  }
  std::optional<Operation> Next() {
    if(pending_)throw std::runtime_error("Native suite operation still awaiting its actual result");
    if(closed_)return std::nullopt;
    if(closing_) {
      if(active_.empty())throw std::runtime_error("Native suite close route has no owner");
      return Admit(Kind::AfterAll,active_.back(),routes_.at(index_).test,
                   tree_.Hooks(active_.back(),SuiteHookKind::AfterAll),{},{});
    }
    if(retire_ || index_>=routes_.size())return std::nullopt;
    const auto& route=routes_.at(index_);
    if(active_.size()>route.ancestors.size() ||
       !std::equal(active_.begin(),active_.end(),route.ancestors.begin()))
      throw std::runtime_error("Native suite active branch is inconsistent");
    if(skip_cause_){
      (void)Admit(Kind::SkippedTest,active_.back(),route.test,{},{},{});
      pending_->skip_cause=skip_cause_;pending_->skipped_tests=skipped_tests_;
      return *pending_;
    }
    if(active_.size()<route.ancestors.size()) {
      if(nonce_==UINT64_MAX)throw std::runtime_error("Native suite operation identity exhausted");
      const auto suite=route.ancestors[active_.size()];
      active_.push_back(suite); // entered before actual beforeAll: cleanup still runs on failure
      return Admit(Kind::BeforeAll,suite,route.test,tree_.Hooks(suite,SuiteHookKind::BeforeAll),{},{});
    }
    return Admit(Kind::Test,route.ancestors.back(),route.test,{},route.before_each,route.after_each);
  }
  bool Complete(const Operation& operation,bool ok) {
    if(!pending_ || operation.epoch!=epoch_ || operation.token!=pending_->token ||
       operation.kind!=pending_->kind || operation.suite!=pending_->suite || operation.test!=pending_->test ||
       operation.skip_cause!=pending_->skip_cause || operation.skipped_tests!=pending_->skipped_tests)
      return false;
    if(!ok) {
      if(failures_.size()>=8192)throw std::runtime_error("Native suite failure capacity exceeded");
      failures_.push_back(*pending_);
      retire_=true;
    }
    const auto kind=pending_->kind;
    pending_.reset();
    if(kind==Kind::BeforeAll) {
      if(!ok){closing_=true;exit_remaining_=active_.size();}
    } else if(kind==Kind::Test || kind==Kind::SkippedTest) {
      if(kind==Kind::SkippedTest){skip_cause_=0;skipped_tests_.clear();}
      exit_remaining_=retire_?active_.size():routes_.at(index_).finish_suites.size();
      closing_=exit_remaining_!=0;
      if(!closing_)++index_;
    } else {
      if(active_.empty() || active_.back()!=operation.suite || !exit_remaining_)
        throw std::runtime_error("Native suite close acknowledgment has no owner");
      active_.pop_back();
      --exit_remaining_;
      if(retire_)exit_remaining_=active_.size();
      if(!exit_remaining_){closing_=false;if(!retire_)++index_;}
    }
    return true;
  }
  bool SkipBeforeAll(const Operation& operation,uint64_t cause){
    if(!cause||!pending_||pending_->kind!=Kind::BeforeAll||operation.kind!=Kind::BeforeAll||
       operation.epoch!=epoch_||operation.token!=pending_->token||operation.suite!=pending_->suite||
       operation.test!=pending_->test||operation.skip_cause||!operation.skipped_tests.empty()||
       active_.empty()||active_.back()!=operation.suite||retire_||closed_||closing_||skip_cause_)
      return false;
    // Build the bounded replacement before acknowledging the actual hook result.
    auto routes=routes_;
    std::vector<uint64_t> omitted;
    omitted.reserve(routes.size()-index_-1);
    const auto retained=std::remove_if(routes.begin()+static_cast<std::ptrdiff_t>(index_+1),routes.end(),[&](const auto& route){
      if(std::find(route.ancestors.begin(),route.ancestors.end(),operation.suite)==route.ancestors.end())return false;
      omitted.push_back(route.test);return true;
    });
    routes.erase(retained,routes.end());
    auto& current=routes.at(index_);current.ancestors=active_;current.before_each.clear();current.after_each.clear();current.finish_suites.clear();
    size_t shared=0;
    if(index_+1<routes.size()){
      const auto& next=routes[index_+1].ancestors;
      while(shared<active_.size()&&shared<next.size()&&active_[shared]==next[shared])++shared;
    }
    for(size_t count=active_.size();count>shared;--count)current.finish_suites.push_back(active_[count-1]);
    if(!Complete(operation,true))return false;
    routes_=std::move(routes);skip_cause_=cause;skipped_tests_=std::move(omitted);return true;
  }
  bool WorkerClosed(DedicatedHostProcess& worker) {
    if(worker.Pid()!=worker_ || !worker.Exited())return false;
    pending_.reset();active_.clear();closed_=true;closing_=false;
    return true;
  }
  void RetireAfterCurrentTest() {
    if(pending_)throw std::runtime_error("Cannot retire a native suite with an outstanding operation");
    retire_=true;exit_remaining_=active_.size();closing_=exit_remaining_!=0;
  }
  bool HasClosures() const {return closing_;}
  bool NeedsRetirement() const {return retire_;}
  bool AwaitingResult() const {return pending_.has_value();}
  bool Closed() const {return closed_;}
  const std::vector<Operation>& Failures() const {return failures_;}
 private:
  Operation Admit(Kind kind,uint64_t suite,uint64_t test,std::vector<uint64_t> callbacks,
                  std::vector<uint64_t> before_each,std::vector<uint64_t> after_each) {
    if(nonce_==UINT64_MAX)throw std::runtime_error("Native suite operation identity exhausted");
    pending_=Operation{kind,epoch_,++nonce_,suite,test,std::move(callbacks),std::move(before_each),std::move(after_each)};
    return *pending_;
  }
  NativeSuiteTree tree_;
  std::vector<NativeSuiteTree::Route> routes_;
  uint64_t epoch_,nonce_=0,skip_cause_=0;
  std::vector<uint64_t> skipped_tests_;
  pid_t worker_;
  size_t index_=0,exit_remaining_=0;
  std::vector<uint64_t> active_;
  std::optional<Operation> pending_;
  std::vector<Operation> failures_;
  bool closing_=false,retire_=false,closed_=false;
};
}
