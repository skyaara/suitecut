// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_graph.h"
#include "fixture_timeout_clocks.h"
#include <deque>

namespace suitecut::runner {
enum class FixtureScopeKind { Test, BeforeAll, AfterAll };

// A real host process owns this pool. Scope IDs identify actual fixture value
// namespaces; worker fixtures use namespace zero. Timeout never erases a lease.
class ScopedFixtures {
 public:
  enum class CommandKind { Setup, Release, Cancel };
  struct Command {
    CommandKind kind;
    uint64_t token, scope, generation;
    std::string name;
    bool worker;
  };
  struct Notice {
    uint64_t scope, generation;
    bool ok;
    std::string error;
  };
  struct Callbacks {
    std::function<void(const Command&)> host;
    std::function<void(const Notice&)> ready;
    std::function<void(const Notice&)> closed;
    std::function<void(const std::string&)> terminate;
    std::function<std::optional<double>(uint64_t)> fixtureRetirementDeadline = {};
  };
  struct State { size_t scopes, physical, workers; bool retiring, terminating; };

  ScopedFixtures(std::vector<Fixture> definitions, Callbacks callbacks)
      : definitions_(std::move(definitions)), graph_(Explicit(definitions_)),
        callbacks_(std::move(callbacks)) {}

  bool Open(uint64_t id, uint64_t generation, FixtureScopeKind kind,
            const std::vector<std::string>& roots, double deadline,
            double now, std::shared_ptr<FixtureChargedClock> charged = {}) {
    if (!Time(deadline, now) || deadline <= now || !id || !generation ||
        retiring_ || terminating_ || scopes_.contains(id) || scopes_.size() >= 16)
      return false;
    if (charged && (!charged->CanObserve(now) || charged->Expired(now))) return false;
    auto requested = Automatic(kind);
    MergeRoots(requested, roots);
    auto plan = graph_.Plan(requested); // Validate before changing ownership.
    scopes_.emplace(id, Owner{generation, deadline, std::move(plan), std::move(charged)});
    Advance(id, now);
    return true;
  }

  bool AddRoots(uint64_t id, uint64_t generation,
                const std::vector<std::string>& roots, double now) {
    auto owner = Find(id, generation);
    if (!owner || !std::isfinite(now) || owner->closing || (owner->failed && !owner->cleanup_phase))
      return false;
    SyncBudget(*owner, now);
    if (now >= owner->deadline) { Fail(id, "Fixture scope original deadline expired", now); return false; }
    auto requested = owner->plan;
    MergeRoots(requested, roots);
    auto plan = graph_.Plan(requested);
    owner->plan = std::move(plan);
    owner->ready = false;
    Advance(id, now);
    return true;
  }

  // An explicit native phase transition creates the one teardown budget.
  // AddRoots/Ready never reset this deadline, and this transition is one-shot.
  bool BeginCleanup(uint64_t id, uint64_t generation, double deadline, double now, std::shared_ptr<FixtureChargedClock> charged = {}) {
    auto owner = Find(id, generation);
    if (!owner || !Time(deadline, now) || deadline <= now || owner->closing ||
        owner->cleanup_phase || terminating_) return false;
    if (charged && !charged->CanObserve(now)) return false;
    if (owner->charged ? owner->charged->Expired(now) : now >= owner->deadline) Record(*owner, "Fixture scope original deadline expired");
    owner->cleanup_phase = true;
    owner->charged = std::move(charged);
    owner->deadline = deadline;
    SyncBudget(*owner, now);
    return true;
  }

  bool CanAmendDeadline(uint64_t id, uint64_t generation, double now) const {
    const auto found = scopes_.find(id);
    return std::isfinite(now) && !terminating_ && found != scopes_.end() &&
        found->second.generation == generation && !found->second.closing &&
        (!found->second.failed || found->second.cleanup_phase) && now < found->second.deadline;
  }
  bool AmendDeadline(uint64_t id, uint64_t generation, double deadline, double now) {
    if (!std::isfinite(deadline) || !CanAmendDeadline(id, generation, now)) return false;
    scopes_.at(id).deadline = deadline;
    return true;
  }

  void Ready(uint64_t token, uint64_t scope, uint64_t generation, double now) {
    if (!std::isfinite(now) || terminating_) return;
    Tick(now);
    if (terminating_) return;
    auto found = leases_.find(token);
    if (found == leases_.end() || found->second.scope != scope ||
        found->second.generation != generation || found->second.phase != Phase::Starting)
      return;
    found->second.phase = Phase::Ready;
    // Worker readiness may legitimately serve a later cleanup hook after the
    // creating scope expired. Every dependent scope still checks its deadline.
    std::vector<uint64_t> ids;
    for (const auto& [id, owner] : scopes_) if (!owner.closing) ids.push_back(id);
    for (const auto id : ids) Advance(id, now);
  }

  bool OwnsFixture(uint64_t token,uint64_t scope,uint64_t generation)const{const auto found=leases_.find(token);return found!=leases_.end()&&found->second.scope==scope&&found->second.generation==generation;}
  void Settled(uint64_t token, uint64_t scope, uint64_t generation,
               bool ok, std::string error, double now) {
    if (!std::isfinite(now)) return;
    Tick(now);
    auto found = leases_.find(token);
    if (found == leases_.end() || found->second.scope != scope ||
        found->second.generation != generation) return;
    const auto lease = found->second;
    const bool expected = lease.phase == Phase::Stopping;
    leases_.erase(found);
    names_.erase(Key(lease.worker ? 0 : lease.scope, lease.name));
    if (!expected || !ok) {
      const std::string cause = "Fixture " + lease.name +
          (!expected ? " settled before native release" : " teardown failed: " + Bounded(error));
      if (lease.worker) {
        for (auto& [id, owner] : scopes_) { (void)id; Record(owner, cause); }
        Terminate(cause);
      } else if (scopes_.contains(scope)) {
        Record(scopes_.at(scope), cause);
        Close(scope, generation, now + 1000, now);
      }
    }
    if (lease.worker) ReleaseWorkers();
    else ReleaseNext(scope);
  }

  void Close(uint64_t id, uint64_t generation, double cleanupDeadline, double now) {
    auto owner = Find(id, generation);
    if (!owner || !Time(cleanupDeadline, now) || owner->closing || terminating_) return;
    if (cleanupDeadline <= now) { Terminate("Fixture cleanup deadline already expired"); return; }
    if (now >= owner->deadline) Record(*owner, "Fixture scope original deadline expired");
    owner->closing = true;
    owner->cleanup = cleanupDeadline;
    if(owner->charged){
      const auto original=owner->charged->Deadline(now);
      if(original&&*original<=now){owner->cleanup_charge=owner->charged->Charged(now);owner->cleanup_budget=cleanupDeadline-now;}
      SyncBudget(*owner,now);
    }
    ReleaseNext(id);
  }

  void Cancel(uint64_t id, uint64_t generation, std::string error,
              double cleanupDeadline, double now) {
    auto owner = Find(id, generation);
    if (!owner || !Time(cleanupDeadline, now)) return;
    Record(*owner, Bounded(error));
    Close(id, generation, cleanupDeadline, now);
  }
  bool FixtureExpired(uint64_t token,uint64_t scope,uint64_t generation,const std::string& error,double now) {
    const auto found=leases_.find(token);
    if(found==leases_.end()||found->second.scope!=scope||found->second.generation!=generation||found->second.phase==Phase::Ready)return false;
    if(auto* owner=Find(scope,generation)){Record(*owner,error);Close(scope,generation,now+1000,now);}
    const auto remaining=leases_.find(token);
    if(remaining!=leases_.end()&&remaining->second.phase==Phase::Starting){remaining->second.phase=Phase::Stopping;callbacks_.host({CommandKind::Cancel,token,scope,generation,remaining->second.name,remaining->second.worker});}
    return true;
  }

  void Retire(double cleanupDeadline, double now) {
    if (!Time(cleanupDeadline, now) || retiring_ || terminating_) return;
    retiring_ = true;
    worker_cleanup_ = cleanupDeadline;
    std::vector<std::pair<uint64_t, uint64_t>> ids;
    for (const auto& [id, owner] : scopes_) ids.emplace_back(id, owner.generation);
    for (const auto& [id, generation] : ids) Close(id, generation, cleanupDeadline, now);
    ReleaseWorkers();
  }

  void Tick(double now) {
    if (!std::isfinite(now) || terminating_) return;
    std::vector<uint64_t> expired;
    for (auto& [id, owner] : scopes_) {
      SyncBudget(owner,now);
      if (owner.closing && now >= owner.cleanup) {
        Terminate("Fixture scope physical cleanup deadline expired"); return;
      }
      if (!owner.closing && now >= owner.deadline) expired.push_back(id);
    }
    for (const auto id : expired) Fail(id, "Fixture scope original deadline expired", now);
    double worker_deadline=worker_cleanup_;
    if(retiring_&&callbacks_.fixtureRetirementDeadline)
      for(const auto&[token,lease]:leases_)if(lease.worker&&lease.phase==Phase::Stopping)
        if(const auto deadline=callbacks_.fixtureRetirementDeadline(token))worker_deadline=*deadline;
    if (retiring_ && !leases_.empty() && now >= worker_deadline)
      Terminate("Worker fixture physical cleanup deadline expired");
  }

  void WorkerClosed() {
    // Only actual waitpid-confirmed exit permits forgetting physical callbacks.
    terminating_ = true;
    leases_.clear(); names_.clear();
    auto scopes = std::move(scopes_);
    scopes_.clear();
    for (auto& [id, owner] : scopes) {
      Record(owner, "Host worker physically exited");
      callbacks_.closed({id, owner.generation, false, owner.error});
    }
  }
  State Inspect() const {
    size_t workers = 0;
    for (const auto& [id, lease] : leases_) { (void)id; workers += lease.worker; }
    return {scopes_.size(), leases_.size(), workers, retiring_, terminating_};
  }

 private:
  enum class Phase { Starting, Ready, Stopping };
  struct Owner {
    uint64_t generation;
    double deadline;
    std::vector<std::string> plan;
    std::vector<uint64_t> acquired;
    bool ready = false, closing = false, failed = false, cleanup_phase = false;
    double cleanup = 0, cleanup_budget = 0;
    std::optional<double> cleanup_charge;
    std::shared_ptr<FixtureChargedClock> charged;
    std::string error;
    Owner(uint64_t g, double d, std::vector<std::string> p, std::shared_ptr<FixtureChargedClock> clock)
        : generation(g), deadline(d), plan(std::move(p)), charged(std::move(clock)) {}
  };
  static void SyncBudget(Owner& owner,double now){
    if(!owner.charged)return;
    const auto deadline=owner.charged->Deadline(now).value_or(now+3153600000000.0);
    if(owner.closing)owner.cleanup=owner.cleanup_charge?now+owner.cleanup_budget-(owner.charged->Charged(now)-*owner.cleanup_charge):deadline;
    else owner.deadline=deadline;
  }
  struct Lease {
    uint64_t scope, generation;
    std::string name;
    bool worker;
    Phase phase = Phase::Starting;
  };
  using FixtureKey = std::pair<uint64_t, std::string>;
  static FixtureKey Key(uint64_t scope, const std::string& name) { return {scope, name}; }
  static bool Time(double deadline, double now) { return std::isfinite(deadline) && std::isfinite(now); }
  static std::string Bounded(const std::string& error) { return error.substr(0, 8192); }
  static std::vector<Fixture> Explicit(std::vector<Fixture> defs) {
    for (auto& definition : defs) definition.automatic = false;
    return defs;
  }
  static void MergeRoots(std::vector<std::string>& into, const std::vector<std::string>& roots) {
    if (roots.size() > 256) throw std::invalid_argument("Requested fixture capacity256 exceeded");
    for (const auto& root : roots)
      if (std::find(into.begin(), into.end(), root) == into.end()) into.push_back(root);
  }
  std::vector<std::string> Automatic(FixtureScopeKind kind) const {
    std::vector<std::string> roots;
    for (const auto target : {Scope::Worker, Scope::Test})
      for (const auto& definition : definitions_)
        if (definition.automatic && definition.scope == target &&
            (target == Scope::Worker || kind == FixtureScopeKind::Test)) roots.push_back(definition.name);
    return roots;
  }
  Owner* Find(uint64_t id, uint64_t generation) {
    auto found = scopes_.find(id);
    return found != scopes_.end() && found->second.generation == generation ? &found->second : nullptr;
  }
  static void Record(Owner& owner, const std::string& error) {
    owner.failed = true;
    if (owner.error.empty()) owner.error = Bounded(error);
  }
  void Terminate(const std::string& cause) {
    if (terminating_) return;
    terminating_ = true;
    callbacks_.terminate(cause); // Retains all leases until actual process exit.
  }
  void Fail(uint64_t id, const std::string& cause, double now) {
    auto found = scopes_.find(id);
    if (found == scopes_.end()) return;
    Record(found->second, cause);
    Close(id, found->second.generation, now + 1000, now);
  }
  void Advance(uint64_t id, double now) {
    auto found = scopes_.find(id);
    if (found == scopes_.end() || found->second.closing || terminating_) return;
    SyncBudget(found->second,now);
    if (now >= found->second.deadline) { Fail(id, "Fixture scope original deadline expired", now); return; }
    for (const auto& name : found->second.plan) {
      const bool worker = graph_.Get(name).scope == Scope::Worker;
      auto existing = names_.find(Key(worker ? 0 : id, name));
      if (existing != names_.end()) {
        if (leases_.at(existing->second).phase == Phase::Ready) continue;
        return;
      }
      if (leases_.size() >= 64 || nonce_ == UINT64_MAX) {
        Record(found->second, "Physical scoped fixture callback capacity exhausted");
        Terminate(found->second.error); return;
      }
      const uint64_t token = ++nonce_;
      const uint64_t generation = found->second.generation;
      leases_.emplace(token, Lease{id, generation, name, worker});
      names_.emplace(Key(worker ? 0 : id, name), token);
      if (worker) worker_acquired_.push_back(token);
      else found->second.acquired.push_back(token);
      callbacks_.host({CommandKind::Setup, token, id, generation, name, worker});
      return;
    }
    if (!found->second.ready) {
      found->second.ready = true;
      callbacks_.ready({id, found->second.generation, true, {}});
    }
  }
  void ReleaseNext(uint64_t id) {
    auto found = scopes_.find(id);
    if (found == scopes_.end() || !found->second.closing || terminating_) return;
    for (auto token = found->second.acquired.rbegin(); token != found->second.acquired.rend(); ++token) {
      auto lease = leases_.find(*token);
      if (lease == leases_.end()) continue;
      Release(*token, lease->second); return;
    }
    const Notice notice{id, found->second.generation, !found->second.failed, found->second.error};
    scopes_.erase(found);
    callbacks_.closed(notice);
    ReleaseWorkers();
  }
  void Release(uint64_t token, Lease& lease) {
    if (lease.phase == Phase::Stopping) return;
    const auto kind = lease.phase == Phase::Ready ? CommandKind::Release : CommandKind::Cancel;
    lease.phase = Phase::Stopping;
    callbacks_.host({kind, token, lease.scope, lease.generation, lease.name, lease.worker});
  }
  void ReleaseWorkers() {
    if (!retiring_ || !scopes_.empty() || terminating_) return;
    for (auto token = worker_acquired_.rbegin(); token != worker_acquired_.rend(); ++token) {
      auto lease = leases_.find(*token);
      if (lease != leases_.end()) { Release(*token, lease->second); return; }
    }
  }
  std::vector<Fixture> definitions_;
  FixtureGraph graph_;
  Callbacks callbacks_;
  std::map<uint64_t, Owner> scopes_;
  std::map<uint64_t, Lease> leases_;
  std::map<FixtureKey, uint64_t> names_;
  std::vector<uint64_t> worker_acquired_;
  uint64_t nonce_ = 0;
  double worker_cleanup_ = 0;
  bool retiring_ = false, terminating_ = false;
};
} // namespace suitecut::runner
