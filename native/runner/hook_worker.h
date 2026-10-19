// SPDX-License-Identifier: MIT
#pragma once
#include "hook_phases.h"
#include "scoped_fixtures.h"
#include <memory>
#include <set>

namespace suitecut::runner {
// Native binding between the pure phase controller and real fixture callback
// ownership. It emits dedicated-fd host commands; it never executes JS itself.
class HookWorker {
 public:
  struct Context {
    uint64_t scope, generation, test_id;
    unsigned retry;
    uint64_t timeout;
    FixtureScopeKind kind;
    uint64_t associated_test_id = 0;
    PhaseObservation observation{};
    double budget_elapsed = 0;
    std::shared_ptr<FixtureAttemptClocks> timing = {};
  };
  struct Callbacks {
    std::function<double()> now;
    std::function<void(const std::string&)> send;
    std::function<void(const HookPhases::Result&)> completed;
    std::function<void(const std::string&)> terminate;
    std::function<std::optional<double>(uint64_t)> fixtureRetirementDeadline = {};
    std::function<bool(uint64_t, uint64_t)> cleanup_ready = {};
  };
  struct State { size_t fixtures, callbacks, scopes; bool terminating; };
  HookWorker(std::vector<Fixture> definitions, Callbacks callbacks) : callbacks_(std::move(callbacks)) {
    fixtures_ = std::make_unique<ScopedFixtures>(std::move(definitions), ScopedFixtures::Callbacks{
      [this](const auto& command) { FixtureCommand(command); },
      [this](const auto& notice) {
        const auto pending = acquire_.find(notice.scope);
        if (pending == acquire_.end()) { Terminate("Native fixture readiness has no actual hook owner"); return; }
        phases_->Acquired(pending->second, notice.scope, notice.generation,
            notice.ok, notice.error, callbacks_.now());
      },
      [this](const auto& notice) {
        opened_.erase(notice.scope); cleanup_.erase(notice.scope);
        const auto pending = releases_.find(notice.scope);
        if (pending == releases_.end()) {
          phases_->ResourceFailed(notice.scope, notice.generation,
              notice.error.empty() ? "Actual fixture scope closed before hook completion" : notice.error,
              callbacks_.now());
        } else phases_->Released(pending->second, notice.scope, notice.generation,
            notice.ok, notice.error, callbacks_.now());
      }, [this](const auto& error) { Terminate(error); },callbacks_.fixtureRetirementDeadline});
    phases_ = std::make_unique<HookPhases>(HookPhases::Callbacks{
      [this](const auto& command) { Acquire(command); },
      [this](const auto& command) { Invoke(command); },
      [this](const auto& command) { callbacks_.send("ABORT_CALL " + Number(command.token) + " " +
          Number(command.scope) + " " + Number(command.generation) + " " + Hex(command.cancel_reason.empty() ? "Native hook phase canceled or original deadline expired" : command.cancel_reason)); },
      [this](const auto& command) {
        releases_[command.scope] = command.request;
        if (!opened_.contains(command.scope)) phases_->Released(command.request, command.scope,
            command.generation, false, "Actual fixture scope already closed", callbacks_.now());
        else fixtures_->Close(command.scope, command.generation, command.deadline, callbacks_.now());
      },
      [this](const auto& result) {
        contexts_.erase(result.scope); acquire_.erase(result.scope); releases_.erase(result.scope);
        callbacks_.completed(result);
      }, [this](const auto& error) { Terminate(error); },
      [this](uint64_t scope,uint64_t generation){return CleanupReady(scope,generation);}});
  }
  bool Begin(Context context, std::vector<HookCall> calls, double now) {
    if (terminating_ || contexts_.contains(context.scope) || contexts_.size() >= 16 ||
        context.timeout > 120000 || context.retry > 100 || calls.empty() ||
        (context.kind != FixtureScopeKind::Test && context.budget_elapsed != 0) ||
        (context.kind == FixtureScopeKind::Test && context.associated_test_id && context.associated_test_id != context.test_id) ||
        (context.kind != FixtureScopeKind::Test && context.kind != FixtureScopeKind::BeforeAll &&
          context.kind != FixtureScopeKind::AfterAll) ||
        (context.kind == FixtureScopeKind::Test ? !context.test_id : context.test_id != 0) ||
        (context.kind == FixtureScopeKind::BeforeAll && (calls.size() != 1 || calls[0].kind != HookKind::BeforeAll)) ||
        (context.kind == FixtureScopeKind::AfterAll && (calls.size() != 1 || calls[0].kind != HookKind::AfterAll))) return false;
    contexts_.emplace(context.scope, context);
    bool accepted = false;
    try { accepted = phases_->Begin(context.scope, context.generation, std::move(calls), context.timeout, now, context.observation, context.budget_elapsed, context.timing); }
    catch (...) { if(!terminating_)contexts_.erase(context.scope); throw; }
    if (!accepted) contexts_.erase(context.scope);
    return accepted;
  }
  void FixtureReady(uint64_t token, uint64_t scope, uint64_t generation, double now) {
    if(terminating_)return;
    phases_->Tick(now); if(terminating_)return; fixtures_->Ready(token, scope, generation, now);
  }
  bool FixtureExpired(uint64_t token,uint64_t scope,uint64_t generation,const std::string& error,double now){
    // The original timeout ledger may still query its exact physical lease
    // during kill/waitpid drain. Confirm ownership without new phase work.
    return terminating_?fixtures_->OwnsFixture(token,scope,generation):fixtures_->FixtureExpired(token,scope,generation,error,now);
  }
  std::shared_ptr<FixtureChargedClock> FixtureChargedClockFor(uint64_t scope,uint64_t generation)const{
    return phases_->ActiveChargedClock(scope,generation);
  }
  bool HasPhaseErrors(uint64_t scope,uint64_t generation)const{return phases_->HasErrors(scope,generation);}
  bool OwnsFixture(uint64_t token,uint64_t scope,uint64_t generation)const{return fixtures_->OwnsFixture(token,scope,generation);}
  void FixtureSettled(uint64_t token, uint64_t scope, uint64_t generation,
                      bool ok, const std::string& error, double now) {
    if(terminating_)return;
    phases_->Tick(now); if(terminating_)return; fixtures_->Settled(token, scope, generation, ok, error, now);
  }
  void CallbackSettled(uint64_t token, uint64_t scope, uint64_t generation,
                       bool ok, const std::string& error, double now, bool skipped = false,uint64_t originating_step=0) {
    if(!terminating_)phases_->Settled(token, scope, generation, ok, error, now, skipped,originating_step);
  }
  std::optional<double> AmendTimeout(uint64_t token, uint64_t scope, uint64_t generation,
                                     uint64_t timeout, double now) {
    if(terminating_)return std::nullopt;
    if (!fixtures_->CanAmendDeadline(scope, generation, now)) return std::nullopt;
    const auto deadline = phases_->AmendTimeout(token, scope, generation, timeout, now);
    if (!deadline) return std::nullopt;
    if (!fixtures_->AmendDeadline(scope, generation, *deadline, now))
      throw std::runtime_error("Native live timeout fixture-owner synchronization failed");
    contexts_.at(scope).timeout = timeout;
    phases_->Tick(now);
    return phases_->ActiveDeadline().value_or(*deadline);
  }

  bool NoteSoftFailure(uint64_t token,uint64_t scope,uint64_t generation,uint64_t test,double now){
    if(terminating_)return false;
    const auto context=contexts_.find(scope);
    return context!=contexts_.end()&&context->second.kind==FixtureScopeKind::Test&&context->second.test_id==test&&
      context->second.generation==generation&&phases_->NoteSoftFailure(token,scope,generation,now);
  }

  bool CanExpectFailure(uint64_t token,uint64_t scope,uint64_t generation,uint64_t test,double now){
    if(terminating_)return false;
    const auto context=contexts_.find(scope);
    return context!=contexts_.end()&&context->second.kind==FixtureScopeKind::Test&&
        context->second.test_id==test&&context->second.generation==generation&&
        phases_->CanExpectFailure(token,scope,generation,now);
  }

  bool RequestSkip(uint64_t token, uint64_t scope, uint64_t generation,
                   const std::string& reason_json, double now, std::function<void()> accepted = {}, bool defer_cleanup = false) {
    if(terminating_)return false;
    const auto context = contexts_.find(scope);
    if (context == contexts_.end() || (context->second.kind != FixtureScopeKind::Test && context->second.kind != FixtureScopeKind::BeforeAll)) return false;
    return phases_->RequestSkip(token, scope, generation, reason_json, now,std::move(accepted),defer_cleanup);
  }

  void RefreshFixtureBudget(double now){if(!terminating_)phases_->RefreshFixtureBudget(now);}
  std::optional<double> ActiveDeadline() const { return phases_->ActiveDeadline(); }
  std::optional<double> TimeoutAt() const { return phases_->TimeoutAt(); }
  void Cancel(const std::string& error, double now, bool interrupted = true) { if(!terminating_)phases_->Cancel(error, now, interrupted); }
  void Tick(double now) { if(terminating_)return; phases_->Tick(now); if(!terminating_)fixtures_->Tick(now); }
  bool Retire(double deadline, double now) {
    if(terminating_)return false;
    if (!phases_->Retire(deadline, now)) return false;
    fixtures_->Retire(deadline, now); return true;
  }
  void WorkerClosed() {
    terminating_ = true;
    // Avoid resource notices attempting to resume a phase after confirmed exit.
    phases_->WorkerClosed(callbacks_.now());
    fixtures_->WorkerClosed();
    opened_.clear(); cleanup_.clear(); acquire_.clear(); releases_.clear(); contexts_.clear();
  }
  State Inspect() const {
    return {fixtures_->Inspect().physical, phases_->Inspect().physical,
        fixtures_->Inspect().scopes, terminating_};
  }
 private:
  static std::string Number(uint64_t value) { return std::to_string(value); }
  static std::string Hex(const std::string& text) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    for (const auto c : text) { const auto byte = static_cast<unsigned char>(c); result += alphabet[byte >> 4]; result += alphabet[byte & 15]; }
    return result.empty() ? "-" : result;
  }
  static const char* Kind(HookKind kind) {
    switch (kind) {
      case HookKind::BeforeAll: return "beforeAll";
      case HookKind::BeforeEach: return "beforeEach";
      case HookKind::Body: return "body";
      case HookKind::AfterEach: return "afterEach";
      case HookKind::AfterAll: return "afterAll";
    }
    throw std::invalid_argument("Unknown native hook kind");
  }
  void Terminate(const std::string& error) {
    if (terminating_) return;
    terminating_ = true;
    // The native cause must survive actual WorkerClosed even when the Runner's
    // termination callback only kills the host. The phase callback reenters
    // this method, but the terminal latch prevents a second dispatch/kill.
    phases_->InfrastructureFailure(error);
    callbacks_.terminate(error);
  }
  void FixtureCommand(const ScopedFixtures::Command& command) {
    if (terminating_) return;
    const auto base = Number(command.token) + " " + Number(command.scope) + " " + Number(command.generation) + " " + command.name;
    if (command.kind != ScopedFixtures::CommandKind::Setup) {
      callbacks_.send(std::string(command.kind == ScopedFixtures::CommandKind::Release ? "TEARDOWN " : "CANCEL ") + base);
      return;
    }
    const auto& context = contexts_.at(command.scope);
    const std::string kind = context.kind == FixtureScopeKind::BeforeAll ? "beforeAll" :
        context.kind == FixtureScopeKind::AfterAll ? "afterAll" : "test";
    callbacks_.send("SETUP " + base + " " + kind + " " + Number(context.test_id));
  }
  bool CleanupReady(uint64_t scope,uint64_t generation) {
    if(!callbacks_.cleanup_ready||callbacks_.cleanup_ready(scope,generation))return true;
    // Cleanup's phase clock already began before admission was deferred. Keep
    // the existing fixture scope on that same clock while its parent drains.
    if(opened_.contains(scope)&&!cleanup_.contains(scope)){
      const auto deadline=phases_->ActiveDeadline();
      if(!deadline||!fixtures_->BeginCleanup(scope,generation,*deadline,callbacks_.now(),
          phases_->ActiveChargedClock(scope,generation))){
        Terminate("Native deferred fixture cleanup budget transition rejected");return false;
      }
      cleanup_.insert(scope);
    }
    return false;
  }
  void Acquire(const HookPhases::Acquire& command) {
    if (terminating_) return;
    acquire_[command.scope] = command.request;
    const auto now = callbacks_.now();
    bool accepted = false;
    try {
      if (!opened_.contains(command.scope)) {
        opened_.insert(command.scope);
        accepted = fixtures_->Open(command.scope, command.generation, contexts_.at(command.scope).kind,
            command.fixtures, command.deadline, now, phases_->ActiveChargedClock(command.scope,command.generation));
        if (!accepted) opened_.erase(command.scope);
      } else {
        if (command.cleanup && !cleanup_.contains(command.scope)) {
          if (!fixtures_->BeginCleanup(command.scope, command.generation, command.deadline, now, phases_->ActiveChargedClock(command.scope,command.generation))) {
            phases_->Acquired(command.request, command.scope, command.generation, false,
                "Native fixture cleanup budget transition rejected", now); return;
          }
          cleanup_.insert(command.scope);
        }
        accepted = fixtures_->AddRoots(command.scope, command.generation, command.fixtures, now);
      }
    } catch (const std::exception& error) {
      if(terminating_)throw; // The actual host termination callback failed.
      // Open/AddRoots can synchronously reach Invoke and partially send CALL.
      // Ownership may already be physical: only actual worker exit can retire
      // it. Reporting Acquired(false) here would be ignored in Phase::Invoke.
      Terminate(error.what()); return;
    } catch (...) {
      if(terminating_)throw;
      Terminate("Native hook acquisition or callback dispatch threw an unknown exception"); return;
    }
    if (!accepted) phases_->Acquired(command.request, command.scope, command.generation,
        false, "Native hook fixture scope admission rejected", now);
  }
  void Invoke(const HookPhases::Invoke& command) {
    if(terminating_)return;
    try {
    const auto& context = contexts_.at(command.scope);
    callbacks_.send("CALL " + Number(command.token) + " " + Number(command.scope) + " " +
        Number(command.generation) + " " + Kind(command.kind) + " " + Number(command.callback) + " " +
        Number(context.retry) + " " + Number(context.associated_test_id ? context.associated_test_id : context.test_id) + " " + Number(context.timeout) + " " + PhaseStatusName(command.observation.status) + " " + PhaseMilliseconds(command.observation.duration));
    } catch (const std::exception& error) {
      // Also covers a genuine deferred fixture READY, outside Acquire's stack.
      Terminate(error.what());
    } catch (...) {
      Terminate("Native hook callback dispatch threw an unknown exception");
    }
  }
  Callbacks callbacks_;
  std::unique_ptr<ScopedFixtures> fixtures_;
  std::unique_ptr<HookPhases> phases_;
  std::map<uint64_t, Context> contexts_;
  std::map<uint64_t, uint64_t> acquire_, releases_;
  std::set<uint64_t> opened_, cleanup_;
  bool terminating_ = false;
};
} // namespace suitecut::runner
