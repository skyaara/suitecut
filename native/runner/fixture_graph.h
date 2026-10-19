// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_attempt_clocks.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace suitecut::runner {
enum class Scope { Worker, Test };
struct Fixture {std::string name;Scope scope;std::vector<std::string> dependencies;bool automatic=false;std::optional<uint64_t> timeout=std::nullopt;std::optional<std::string> title_json=std::nullopt;};
class FixtureGraph {
 public:
  explicit FixtureGraph(std::vector<Fixture> definitions):definitions_(std::move(definitions)){
    if(definitions_.size()>256)throw std::invalid_argument("Fixture definition capacity256 exceeded");
    for(size_t i=0;i<definitions_.size();++i){const auto& d=definitions_[i];
    if(d.name.empty()||d.name.size()>256||(d.timeout&&*d.timeout>120000)||!indices_.emplace(d.name,i).second)throw std::invalid_argument("Invalid or duplicate fixture: "+d.name);}
    size_t edge_count=0;for(const auto& d:definitions_){edge_count+=d.dependencies.size();if(edge_count>4096)throw std::invalid_argument("Fixture dependency edge capacity4096 exceeded");}
    for(const auto& d:definitions_)for(const auto& n:d.dependencies){const auto& dep=Get(n);
    if(d.scope==Scope::Worker&&dep.scope==Scope::Test)throw std::invalid_argument("Worker fixture "+d.name+" cannot depend on test fixture "+n);}
    std::map<std::string,int> seen;std::vector<std::string> result;for(const auto& d:definitions_)Visit(d.name,seen,result);
  }
  const Fixture& Get(const std::string& name)const{auto i=indices_.find(name);
    if(i==indices_.end())throw std::invalid_argument("Unknown fixture: "+name);
    return definitions_[i->second];}
  std::vector<std::string> Plan(const std::vector<std::string>& requested)const{
    if(requested.size()>256)throw std::invalid_argument("Requested fixture capacity256 exceeded");
    std::map<std::string,int> seen;std::vector<std::string> result;
    // Automatic fixtures are admitted before the test's requested fixtures.
    for(const auto scope:{Scope::Worker,Scope::Test})
      for(const auto& d:definitions_)if(d.automatic&&d.scope==scope)Visit(d.name,seen,result);
    for(const auto& n:requested)Visit(n,seen,result);
    return result;
  }
 private:
  void Visit(const std::string& name,std::map<std::string,int>& seen,std::vector<std::string>& result)const{
    auto& status=seen[name];
    if(status==2)return;
    if(status==1)throw std::invalid_argument("Fixture dependency cycle at: "+name);
    status=1;for(const auto& dep:Get(name).dependencies)Visit(dep,seen,result);status=2;result.push_back(name);
  }
  std::vector<Fixture> definitions_;std::map<std::string,size_t> indices_;
};
// One manager belongs to one actual host worker process. A use() readiness
// notification never frees its physical callback lease: only actual promise
// settlement or confirmed process exit can do that.
class FixtureOwners {
 public:
  enum class EventKind { Setup, ResumeTeardown, CancelSetup };
  struct Event {EventKind kind;uint64_t token;std::string fixture;Scope scope;uint64_t test;uint64_t generation;};
  struct Hooks {std::function<void(const Event&)> host;std::function<void(uint64_t,uint64_t)> ready;std::function<void(uint64_t,uint64_t,bool,const std::string&)> finished;std::function<void(const std::string&)> terminateWorker;std::function<std::optional<double>(uint64_t)> fixtureRetirementDeadline={};};
  struct Snapshot {size_t physical=0;size_t workerFixtures=0;size_t testFixtures=0;bool active=false;bool closing=false;};
  FixtureOwners(FixtureGraph graph,Hooks hooks):graph_(std::move(graph)),hooks_(std::move(hooks)){}
  bool Begin(uint64_t test,uint64_t generation,std::vector<std::string> roots,double deadline,double now,std::shared_ptr<FixtureAttemptClocks> timing={}){
    if(!std::isfinite(now)||!std::isfinite(deadline)||active_||closing_||retiring_||phase_!=Phase::Idle||deadline<=now||!test||!generation)return false;
    if(timing&&(timing->Owner().scope!=test||timing->Owner().generation!=generation||!timing->Active(timing->Owner())||timing->Active(timing->Owner())->Expired(now)))return false;
    auto plan=graph_.Plan(roots); // Validation completes before host admission.
    infrastructure_failure_=false;active_=Attempt{test,generation,std::move(plan),0,deadline,std::move(timing)};SyncBudget(now);Advance(now);return true;
  }
  void Ready(uint64_t token,uint64_t test,uint64_t generation,double now){
    if(!std::isfinite(now))return;
    auto i=leases_.find(token);
    if(i==leases_.end()||i->second.state!=LeaseState::Starting||i->second.test!=test||i->second.generation!=generation||closing_)return;
    SyncBudget(now);
    if(active_&&now>=active_->deadline&&(phase_==Phase::Acquire||phase_==Phase::Running)){Fail("Test original deadline expired",now);return;}
    i->second.state=LeaseState::Ready;
    if(active_&&phase_==Phase::Acquire)Advance(now);
  }
  void Settled(uint64_t token,uint64_t test,uint64_t generation,bool ok,std::string error,double now){
    if(!std::isfinite(now))return;
    if(error.size()>8192)error.resize(8192);
    auto i=leases_.find(token);
    if(i==leases_.end()||i->second.test!=test||i->second.generation!=generation)return;
    SyncBudget(now);
    if(active_&&now>=active_->deadline&&(phase_==Phase::Acquire||phase_==Phase::Running))Fail("Test original deadline expired",now);
    i=leases_.find(token);if(i==leases_.end())return;
    const auto lease=i->second;const bool expected=lease.state==LeaseState::Stopping;
    leases_.erase(i);names_.erase(lease.fixture);std::erase(acquired_,token);
    if(!expected){infrastructure_failure_=true;Fail("Fixture "+lease.fixture+(ok?" finished before teardown":" failed: "+error),now);return;}
    if(!ok){infrastructure_failure_=true;failure_=true;stop_after_test_=true;
    if(error_.empty())error_="Fixture "+lease.fixture+" teardown failed: "+error;}
    ContinueTeardown(now);
  }
  void Finish(bool ok,std::string error,double now){
    if(!std::isfinite(now))return;
    if(error.size()>8192)error.resize(8192);
    if(!active_||(phase_!=Phase::Running&&phase_!=Phase::SkipDrain)||closing_)return;
    SyncBudget(now);
    if(phase_==Phase::SkipDrain&&now>=cleanup_deadline_){Tick(now);return;}
    if(now>=active_->deadline){Fail("Test original deadline expired",now);return;}
    if(!ok){failure_=true;stop_after_test_=true;if(error_.empty())error_=std::move(error);}StartTeardown(false,now);
  }
  bool CanExpectFailure(uint64_t test,uint64_t generation,double now)const{
    return std::isfinite(now)&&!closing_&&!failure_&&active_&&active_->test==test&&
        active_->generation==generation&&phase_==Phase::Running&&now<active_->deadline;
  }
  bool InfrastructureFailure()const{return infrastructure_failure_;}
  bool Skip(uint64_t test,uint64_t generation,double now,bool defer_cleanup=false){
    if(!std::isfinite(now))return false;
    SyncBudget(now);
    if(closing_||failure_||!active_||active_->test!=test||
        active_->generation!=generation||phase_!=Phase::Running||now>=active_->deadline)return false;
    if(defer_cleanup){phase_=Phase::SkipDrain;cleanup_deadline_=std::min(active_->deadline,now+1000);}
    else StartTeardown(false,now);
    return true;
  }
  bool AmendDeadline(uint64_t test,uint64_t generation,double deadline,double now){
    if(!std::isfinite(now)||!std::isfinite(deadline)||closing_||failure_||!active_||active_->timing||
        active_->test!=test||active_->generation!=generation||phase_!=Phase::Running||now>=active_->deadline)return false;
    active_->deadline=deadline;return true;
  }
  bool AmendTimeout(uint64_t test,uint64_t generation,uint64_t timeout,double now){
    if(!active_||!active_->timing||active_->test!=test||active_->generation!=generation||phase_!=Phase::Running||closing_||failure_)return false;
    if(!active_->timing->Amend(active_->timing->Owner(),timeout,now))return false;
    SyncBudget(now);return true;
  }
  std::shared_ptr<FixtureChargedClock> ActiveChargedClock()const{return active_&&active_->timing?active_->timing->Active(active_->timing->Owner()):nullptr;}
  std::optional<double> ActiveDeadline(double now){SyncBudget(now);return active_?std::optional<double>(phase_==Phase::Teardown?cleanup_deadline_:active_->deadline):std::nullopt;}
  std::optional<FixtureAttemptClocks::Observation> ClockObservation(uint64_t test,uint64_t generation)const{
    return last_clock_&&last_clock_->test==test&&last_clock_->generation==generation?std::optional<FixtureAttemptClocks::Observation>(last_clock_->observation):std::nullopt;
  }
  void Cancel(std::string error,double now){SyncBudget(now);if(active_&&!closing_)Fail(std::move(error),now);}
  bool FixtureExpired(uint64_t token,uint64_t test,uint64_t generation,const std::string& error,double now){
    const auto found=leases_.find(token);if(found==leases_.end()||found->second.test!=test||found->second.generation!=generation||found->second.state==LeaseState::Ready)return false;
    infrastructure_failure_=true;
    if(active_)Fail(error,now);else{failure_=true;if(error_.empty())error_=error;}
    const auto lease=leases_.find(token);
    if(lease!=leases_.end()&&lease->second.state==LeaseState::Starting){lease->second.state=LeaseState::Stopping;hooks_.host(ToEvent(EventKind::CancelSetup,token,lease->second));}
    return true;
  }
  void StopWorker(double now){
    if(closing_)return;
    retiring_=true;
    if(active_){Fail("Worker stopped while test active",now);stop_after_test_=true;return;}
    StartTeardown(true,now);
  }
  void Tick(double now){
    if(!std::isfinite(now))return;
    if(closing_)return;
    SyncBudget(now);
    if(phase_==Phase::WorkerTeardown&&hooks_.fixtureRetirementDeadline)
      for(const auto&[token,lease]:leases_)if(lease.scope==Scope::Worker&&lease.state==LeaseState::Stopping)
        if(const auto deadline=hooks_.fixtureRetirementDeadline(token))cleanup_deadline_=*deadline;
    if(cleanup_deadline_&&now>=cleanup_deadline_){infrastructure_failure_=true;closing_=true;
      if(phase_==Phase::SkipDrain&&active_&&now>=active_->deadline&&error_.empty())error_="Test original deadline expired";
      hooks_.terminateWorker(phase_==Phase::SkipDrain?"Runtime skip callback/artifact physical cleanup deadline expired":"Fixture teardown physical callback did not settle");return;}
    if(active_&&phase_!=Phase::Teardown&&phase_!=Phase::WorkerTeardown&&now>=active_->deadline)Fail("Test original deadline expired",now);
  }
  void WorkerClosed(){
    // Called only after the OS confirms physical worker exit, never a timeout.
    leases_.clear();names_.clear();acquired_.clear();closing_=true;
    if(active_){infrastructure_failure_=true;const auto a=*active_;active_.reset();hooks_.finished(a.test,a.generation,false,error_.empty()?"Host worker closed":error_);}
  }
  // A ready worker fixture remains a physical callback owner while idle.
  bool IdleReady()const noexcept{
    if(active_||closing_||retiring_||failure_||infrastructure_failure_||phase_!=Phase::Idle||cleanup_deadline_)return false;
    for(const auto& [token,lease]:leases_){(void)token;if(lease.scope!=Scope::Worker||lease.state!=LeaseState::Ready)return false;}
    return true;
  }
  template<class Visitor>void ForEachReadyWorkerLease(Visitor&& visit)const{
    for(const auto& [token,lease]:leases_)if(lease.scope==Scope::Worker&&lease.state==LeaseState::Ready)visit(token,lease.test,lease.generation);
  }
  Snapshot State()const{Snapshot s;s.active=active_.has_value();s.closing=closing_;s.physical=leases_.size();for(const auto&[id,l]:leases_){(void)id;
    if(l.scope==Scope::Worker)++s.workerFixtures;else ++s.testFixtures;}return s;}
 private:
  friend struct FixtureOwnersTestAccess;
  enum class LeaseState {Starting,Ready,Stopping};enum class Phase {Idle,Acquire,Running,SkipDrain,Teardown,WorkerTeardown};
  struct Lease {std::string fixture;Scope scope;LeaseState state;uint64_t test;uint64_t generation;};
  struct Attempt {uint64_t test,generation;std::vector<std::string> plan;size_t index;double deadline;std::shared_ptr<FixtureAttemptClocks> timing;};
  struct ClockResult{uint64_t test,generation;FixtureAttemptClocks::Observation observation;};
  void SyncBudget(double now){
    if(active_&&active_->timing){const auto clock=active_->timing->Active(active_->timing->Owner());if(!clock)throw std::runtime_error("Native flat fixture attempt has no active clock");
      const auto deadline=clock->Deadline(now).value_or(now+3153600000000.0);
      if(phase_==Phase::Teardown)cleanup_deadline_=deadline;else active_->deadline=deadline;
    }
  }
  Event ToEvent(EventKind kind,uint64_t id,const Lease& l)const{return{kind,id,l.fixture,l.scope,l.test,l.generation};}
  void Advance(double now){
    if(!active_||closing_)return;
    phase_=Phase::Acquire;
    while(active_->index<active_->plan.size()){
      const auto name=active_->plan[active_->index];auto found=names_.find(name);
      if(found!=names_.end()){auto& l=leases_.at(found->second);
    if(l.state==LeaseState::Ready){++active_->index;continue;}return;}
      if(leases_.size()>=64){Fail("Physical fixture callback capacity64 exceeded",now);return;}
      if(nonce_==std::numeric_limits<uint64_t>::max()){Fail("Fixture callback token space exhausted",now);return;}
      const auto& d=graph_.Get(name);const uint64_t token=++nonce_;
      auto [i,added]=leases_.emplace(token,Lease{name,d.scope,LeaseState::Starting,active_->test,active_->generation});(void)added;
      names_[name]=token;acquired_.push_back(token);hooks_.host(ToEvent(EventKind::Setup,token,i->second));return;
    }
    phase_=Phase::Running;hooks_.ready(active_->test,active_->generation);
  }
  void Fail(std::string error,double now){
    if(error.size()>8192)error.resize(8192);
    failure_=true;stop_after_test_=true;
    if(error_.empty())error_=std::move(error);
    if(!active_){closing_=true;hooks_.terminateWorker(error_);return;}
    if(phase_!=Phase::Teardown&&phase_!=Phase::WorkerTeardown&&phase_!=Phase::SkipDrain)StartTeardown(false,now);
  }
  void StartTeardown(bool worker,double now){
    if(!worker&&active_&&active_->timing&&!active_->timing->BeginCleanup(active_->timing->Owner(),now))throw std::runtime_error("Native flat fixture cleanup clock transition rejected");
    phase_=worker?Phase::WorkerTeardown:Phase::Teardown;cleanup_deadline_=now+1000;SyncBudget(now);ContinueTeardown(now);
  }
  void ContinueTeardown(double now){
    if(closing_)return;
    const bool worker=phase_==Phase::WorkerTeardown;
    if(phase_!=Phase::WorkerTeardown&&phase_!=Phase::Teardown)return;
    for(auto i=acquired_.rbegin();i!=acquired_.rend();++i){auto l=leases_.find(*i);
    if(l==leases_.end()||(!worker&&l->second.scope==Scope::Worker))continue;
      if(l->second.state==LeaseState::Stopping)return;
    const auto kind=l->second.state==LeaseState::Ready?EventKind::ResumeTeardown:EventKind::CancelSetup;
      l->second.state=LeaseState::Stopping;hooks_.host(ToEvent(kind,*i,l->second));return;
    }
    cleanup_deadline_=0;
    if(worker){phase_=Phase::Idle;retiring_=true;return;}
    if(!active_)return;
    const auto a=*active_;
    if(a.timing){if(!a.timing->Finish(a.timing->Owner(),now))throw std::runtime_error("Native flat fixture clock completion rejected");last_clock_=ClockResult{a.test,a.generation,a.timing->Observe(a.timing->Owner(),now)};}
    active_.reset();phase_=Phase::Idle;
    const bool ok=!failure_;const auto error=error_;failure_=false;error_.clear();hooks_.finished(a.test,a.generation,ok,error);
    if(stop_after_test_){stop_after_test_=false;StartTeardown(true,now);}
  }
  FixtureGraph graph_;Hooks hooks_;std::map<uint64_t,Lease> leases_;std::map<std::string,uint64_t> names_;std::vector<uint64_t> acquired_;
  std::optional<ClockResult> last_clock_;
  std::optional<Attempt> active_;uint64_t nonce_=0;Phase phase_=Phase::Idle;double cleanup_deadline_=0;bool infrastructure_failure_=false,closing_=false,failure_=false,stop_after_test_=false,retiring_=false;std::string error_;
};
}
