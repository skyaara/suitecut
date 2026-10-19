// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <algorithm>
#include <deque>
#include <optional>
#include <limits>
#include <stdexcept>
#include <string>
#include <set>
#include <utility>
#include <vector>
namespace suitecut::runner {
enum class Outcome {Passed,Failed,TimedOut,Interrupted,Skipped};
struct TestCase {uint64_t id;std::string title;std::vector<std::string> fixtures;unsigned retries=0;bool expectedFailure=false;};
struct Attempt {TestCase test;unsigned retry=0;uint64_t generation=0;};
struct Result {Attempt attempt;Outcome outcome;bool expected;std::string error;};
// Serial first slice. A failed worker is a physical fence: a retry cannot be
// admitted until the OS has confirmed that exact worker process exited.
class RetryPlan {
 public:
  explicit RetryPlan(std::vector<TestCase> tests,unsigned maxFailures=0,uint64_t generationSeed=0):tests_(std::move(tests)),generation_(generationSeed),max_failures_(maxFailures){
    if(maxFailures>4096)throw std::invalid_argument("Native maximum failure count must be0..4096");
    if(tests_.size()>4096)throw std::invalid_argument("Native test capacity4096 exceeded");
    size_t result_budget=0;for(const auto& test:tests_){size_t bytes=test.title.size()+8448;for(const auto& fixture:test.fixtures)bytes+=fixture.size()+32;if(test.title.size()>4096||test.fixtures.size()>256||bytes>1048576)throw std::invalid_argument("Native test metadata limit exceeded");result_budget+=bytes*(static_cast<size_t>(test.retries)+1);if(result_budget>67108864)throw std::invalid_argument("Native attempt metadata/error budget64MiB exceeded");}
    for(size_t i=0;i<tests_.size();++i){const auto& t=tests_[i];if(!t.id||t.retries>100)throw std::invalid_argument("Invalid native test ID/retry count");for(size_t j=0;j<i;++j)if(tests_[j].id==t.id)throw std::invalid_argument("Duplicate native test ID");queue_.push_back({t,0,0});}
  }
  std::optional<Attempt> Next(uint64_t worker,uint64_t admittedGeneration=0){
    if(active_||retiring_worker_||queue_.empty()||!worker)return std::nullopt;
    if(generation_==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Native attempt generation space exhausted");
    if(admittedGeneration&&admittedGeneration<=generation_)throw std::invalid_argument("Native shared attempt generation is stale");
    auto a=queue_.front();queue_.pop_front();a.generation=admittedGeneration?generation_=admittedGeneration:++generation_;active_=a;runtime_expected_failure_=false;worker_=worker;return a;
  }
  bool ExpectFailure(uint64_t worker,uint64_t generation){
    if(!active_||worker!=worker_||generation!=active_->generation)return false;
    runtime_expected_failure_=true;return true;
  }
  bool Finish(uint64_t worker,uint64_t generation,Outcome status,std::string error,bool infrastructureFailure=false){
    if(error.size()>8192)error.resize(8192);
    if(!active_||worker!=worker_||generation!=active_->generation)return false;
    const auto a=*active_;const bool expected=!infrastructureFailure&&(status==Outcome::Skipped||((a.test.expectedFailure||runtime_expected_failure_)?status==Outcome::Failed:status==Outcome::Passed));
    results_.push_back({a,status,expected,std::move(error)});active_.reset();runtime_expected_failure_=false;
    if(!expected){
      retiring_worker_=worker;
      if(a.retry<a.test.retries&&status!=Outcome::Interrupted)retry_=Attempt{a.test,a.retry+1,0};
      else if(status!=Outcome::Interrupted){
        ++unexpected_tests_;
        if(max_failures_&&unexpected_tests_>=max_failures_){limit_reached_=true;queue_.clear();}
      }
    }
    return true;
  }
  bool WorkerExited(uint64_t worker){
    if(!worker||worker!=retiring_worker_)return false;
    retiring_worker_=0;
    if(retry_){queue_.push_front(*retry_);retry_.reset();}return true;
  }
  // Removes only queued declarations, preserving every actual attempt and exit fence.
  void ExcludeQueued(const std::set<uint64_t>& ids){
    if(active_)throw std::runtime_error("Cannot exclude native declarations during an active attempt");
    for(const auto id:ids)if(std::none_of(tests_.begin(),tests_.end(),[&](const auto& test){return test.id==id;}))
      throw std::runtime_error("Cannot exclude an unknown native declaration ID");
    for(auto it=queue_.begin();it!=queue_.end();)if(ids.contains(it->test.id))it=queue_.erase(it);else ++it;
    if(retry_&&ids.contains(retry_->test.id))retry_.reset();
  }
  bool NeedsWorkerRetirement()const{return retiring_worker_!=0;}
  bool Done()const{return !active_&&!retiring_worker_&&queue_.empty();}
  bool FailureLimitReached()const{return limit_reached_;}
  unsigned UnexpectedTestFailures()const{return unexpected_tests_;}
  const std::vector<Result>& Results()const{return results_;}
 private:
  friend struct RetryPlanTestAccess;
  std::vector<TestCase> tests_;std::deque<Attempt> queue_;std::optional<Attempt> active_,retry_;std::vector<Result> results_;uint64_t worker_=0,retiring_worker_=0,generation_=0;
  unsigned max_failures_=0,unexpected_tests_=0;bool limit_reached_=false,runtime_expected_failure_=false;
};
}
