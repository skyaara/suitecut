// SPDX-License-Identifier: MIT
#pragma once
#include "retry_plan.h"
#include <climits>

namespace suitecut::runner {
// One already-selected outermost serial group. The native adapter supplies real
// complete-attempt and worker/raw/file/descendant-retirement receipts. This
// policy does not manufacture callbacks, worker exits, deadlines or skipped
// attempts, and has no authority to restart the coordinator's global budget.
class SerialGroup {
 public:
  struct Worker {
    uint64_t index=0;int64_t pid=0;uint64_t generation=0;
    bool operator==(const Worker&) const=default;
  };
  struct Omission {uint64_t test_id=0;unsigned retry=0;uint64_t cause_test=0,cause_generation=0;};
  enum class State {AwaitingWorker,Ready,Active,Retiring,Passed,Failed,Stopped};
  enum class Classification {NotRun,Passed,Skipped,Flaky,Failed,Interrupted};
  explicit SerialGroup(std::vector<TestCase> selected):tests_(std::move(selected)) {
    if(tests_.empty())throw std::invalid_argument("Native serial group must contain selected tests");
    // Keep the existing test/declaration checks and metadata accounting. The
    // group may replay every member up to the largest effective retry count.
    RetryPlan validated(tests_);
    for(const auto& test:tests_)max_retries_=std::max(max_retries_,test.retries);
    size_t bytes=0;
    for(const auto& test:tests_){
      size_t record=test.title.size()+8448;
      for(const auto& fixture:test.fixtures)record+=fixture.size()+32;
      bytes+=record*(static_cast<size_t>(max_retries_)+1);
      if(bytes>67108864)throw std::invalid_argument("Native serial group replay metadata budget64MiB exceeded");
    }
    if(tests_.size()*(static_cast<size_t>(max_retries_)+1)>8192)
      throw std::invalid_argument("Native serial group replay record capacity8192 exceeded");
  }
  bool Begin(Worker worker) {
    if(state_!=State::AwaitingWorker||stop_||worker.pid<=0||worker.pid>INT32_MAX||!worker.generation||
       (last_worker_&&worker.index<=*last_worker_))return false;
    worker_=worker;last_worker_=worker.index;cursor_=0;wave_failed_=false;SkipExcluded();state_=State::Ready;return true;
  }
  std::optional<Attempt> Next(Worker worker,uint64_t global_generation) {
    if(state_!=State::Ready||!Owns(worker)||stop_)return std::nullopt;
    if(!global_generation||global_generation<=last_attempt_generation_)
      throw std::invalid_argument("Native serial group shared attempt generation is stale");
    last_attempt_generation_=global_generation;
    active_=Attempt{tests_.at(cursor_++),retry_,global_generation};runtime_expected_failure_=false;
    state_=State::Active;return active_;
  }
  bool ExpectFailure(Worker worker,uint64_t generation) {
    if(state_!=State::Active||!Owns(worker)||active_->generation!=generation)return false;
    runtime_expected_failure_=true;return true;
  }
  bool Finish(Worker worker,uint64_t generation,Outcome outcome,std::string error={},bool infrastructure_failure=false) {
    if(state_!=State::Active||!Owns(worker)||active_->generation!=generation)return false;
    if(error.size()>8192)error.resize(8192);
    const bool expected=!infrastructure_failure&&(outcome==Outcome::Skipped||
      ((active_->test.expectedFailure||runtime_expected_failure_)?outcome==Outcome::Failed:outcome==Outcome::Passed));
    results_.push_back({*active_,outcome,expected,std::move(error)});
    if(!expected){
      wave_failed_=true;
      // An omitted member has a genuine preceding failure cause but no actual
      // attempt generation, PID, duration or callback. Never add a fake Result.
      for(size_t i=cursor_;i<tests_.size();++i)if(!excluded_.contains(tests_[i].id))
        omissions_.push_back({tests_[i].id,retry_,active_->test.id,active_->generation});
    }
    if(outcome==Outcome::Interrupted)stop_=true;
    active_.reset();runtime_expected_failure_=false;
    SkipExcluded();
    state_=(wave_failed_||stop_||cursor_==tests_.size())?State::Retiring:State::Ready;return true;
  }
  // Only the owning adapter may call this after actual beforeAll/afterAll,
  // fixture teardown, raw callback, file writer/rollback, waitpid and adopted
  // descendant retirement. Logical completion alone never enables replay.
  bool Retired(Worker worker) {
    if(state_!=State::Retiring||!Owns(worker)||active_)return false;
    worker_.reset();
    if(stop_)state_=State::Stopped;
    else if(!wave_failed_)state_=State::Passed;
    else if(retry_<max_retries_){++retry_;state_=State::AwaitingWorker;}
    else state_=State::Failed;
    return true;
  }
  void Stop() {
    stop_=true;
    if(state_==State::AwaitingWorker)state_=State::Stopped;
    else if(state_==State::Ready)state_=State::Retiring;
    // Active callbacks still require their real Finish and physical retirement.
    // Terminal outcomes retain actual results; Stop cannot rewrite history.
  }
  void ExcludeQueued(const std::set<uint64_t>& ids) {
    if(state_==State::Active)throw std::runtime_error("Cannot exclude active native serial-group declarations");
    for(const auto id:ids)if(std::none_of(tests_.begin(),tests_.end(),[&](const auto& test){return test.id==id;}))
      throw std::invalid_argument("Cannot exclude unknown native serial-group declaration");
    excluded_.insert(ids.begin(),ids.end());SkipExcluded();
    if(state_==State::Ready&&cursor_==tests_.size())state_=State::Retiring;
    if(state_==State::AwaitingWorker&&excluded_.size()==tests_.size())state_=State::Passed;
  }
  bool LastSelected(uint64_t id)const {
    for(auto test=tests_.rbegin();test!=tests_.rend();++test)if(!excluded_.contains(test->id))return test->id==id;
    return false;
  }
  State Current()const{return state_;}
  unsigned Retry()const{return retry_;}
  unsigned MaxRetries()const{return max_retries_;}
  bool Done()const{return state_==State::Passed||state_==State::Failed||state_==State::Stopped;}
  const std::vector<Result>& Results()const{return results_;}
  const std::vector<Omission>& Omissions()const{return omissions_;}
  Classification Classify(uint64_t test_id)const {
    if(std::none_of(tests_.begin(),tests_.end(),[&](const auto& test){return test.id==test_id;}))
      throw std::invalid_argument("Unknown native serial group test ID");
    bool expected=false,unexpected=false,non_skip=false,interrupted=false,seen=false;
    for(const auto& result:results_)if(result.attempt.test.id==test_id){
      seen=true;expected|=result.expected;unexpected|=!result.expected;
      non_skip|=result.outcome!=Outcome::Skipped;interrupted|=result.outcome==Outcome::Interrupted;
    }
    if(!seen)return Classification::NotRun;
    if(interrupted)return Classification::Interrupted;
    if(expected&&unexpected)return Classification::Flaky;
    if(expected)return non_skip?Classification::Passed:Classification::Skipped;
    return Classification::Failed;
  }
  // Charge the shared coordinator only after this whole group physically
  // settles. Failed intermediate waves never consume its final failure budget.
  unsigned FinalUnexpectedTests()const {
    if(!Done())return 0;
    unsigned count=0;
    for(const auto& test:tests_)if(Classify(test.id)==Classification::Failed)++count;
    return count;
  }
 private:
  bool Owns(Worker worker)const{return worker_&&*worker_==worker;}
  void SkipExcluded(){while(cursor_<tests_.size()&&excluded_.contains(tests_[cursor_].id))++cursor_;}
  std::vector<TestCase> tests_;std::vector<Result> results_;std::vector<Omission> omissions_;std::set<uint64_t> excluded_;
  std::optional<Worker> worker_;std::optional<uint64_t> last_worker_;std::optional<Attempt> active_;
  State state_=State::AwaitingWorker;size_t cursor_=0;unsigned retry_=0,max_retries_=0;
  uint64_t last_attempt_generation_=0;bool wave_failed_=false,runtime_expected_failure_=false,stop_=false;
};
}
