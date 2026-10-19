// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "include/cef_values.h"
namespace suitecut {
// One real Inspector domain per actual Source/session. Logical timeouts never
// retire accepted commands, factory capture receipts or paused execution.
class DebuggerDomains {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Ready=std::function<void(bool,const std::string&)>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using Resume=std::function<void(Ready)>;
  using Paused=std::function<void(Dict,double,Resume)>;
  // Only internal factory consumers use the bounded cache of collected-script
  // sources. Explicit debugger/coverage consumers retain historical sources.
  static constexpr int kFactoryScriptCacheBytes = 8 * 1024 * 1024;
  enum class Kind { Coverage, Factory, Developer };
  enum class Continuation { Resume, StepOver, StepInto, StepOut };
  using Continue=std::function<void(Continuation,double,Ready)>;
  using DeveloperPaused=std::function<void(uint64_t,Dict,double,Continue)>;
  using DeveloperDeadline=std::function<double(Dict)>;
  enum class DeveloperPauseDisposition {Owned,Foreign,Unsupported};
  using DeveloperEligibility=std::function<DeveloperPauseDisposition(Dict)>;
  using DeveloperExpired=std::function<void(uint64_t,Dict,const std::string&)>;
  struct Hooks {
    std::function<double()> now;
    // Actual ACK/real channel closure only; false means dispatch not accepted.
    std::function<bool(const std::string&,const std::string&,Dict,double,Reply)> send;
    // Actual current document bootstrap budget, not the original arm budget.
    std::function<double(const std::string&,Dict)> pause_deadline;
    std::function<void(uint64_t,Dict,const std::string&)> pause_expired;
    std::function<void(const std::string&,const std::string&)> close_channel;
    std::function<void()> wake;
  };
  explicit DebuggerDomains(Hooks hooks);
  ~DebuggerDomains();
  // IDs are caller-owned monotonically unique identities, never context IDs.
  void Acquire(const std::string& session,uint64_t lease,Kind kind,double deadline,Ready ready);
  // Atomic callback admission before domain enable; exact factory hits keep
  // priority over this explicit developer consumer. Returned pause IDs are
  // native capture identities, never author call-frame or context IDs.
  void AcquireDeveloper(const std::string& session,uint64_t lease,double deadline,
                        DeveloperDeadline pause_deadline,DeveloperPaused paused,
                        DeveloperExpired expired,Ready ready);
  // Opt-in exact frame scope. Foreign pauses follow the real unowned resume
  // path, never receiving a developer token; factory hits retain priority.
  void AcquireScopedDeveloper(const std::string& session,uint64_t lease,double deadline,
                        DeveloperDeadline pause_deadline,DeveloperPaused paused,
                        DeveloperExpired rejected,DeveloperEligibility eligible,Ready ready);
  void Arm(uint64_t lease,Dict exact_breakpoint_params,Paused paused,Ready ready);
  void Release(uint64_t lease,double cleanup_deadline,Ready ready);
  // Track an actual Source-owned capture/source/group command. The returned
  // reservation survives logical cancel. Retire only at actual ACK/closure or
  // a transport refusal proving that command was never physically admitted.
  uint64_t Hold(uint64_t lease);
  void Retire(uint64_t receipt);
  // Native registry facts, never timer-based retirement. A failed logical
  // release may leave HasLease true until actual cleanup ACK/session closure.
  bool HasLease(uint64_t lease)const;
  bool IsPaused(uint64_t lease,uint64_t pause)const;
  bool Event(const std::string& method,Dict params,const std::string& session);
  // Raw public commands need a retained receipt even after caller cancellation.
  // Conservative ownership remains after successful raw commands until an
  // actual uncontended Debugger.disable ACK or genuine session death.
  uint64_t BeginRaw(const std::string& session,const std::string& method,double deadline);
  void EndRaw(uint64_t receipt,bool actual_ok,bool actually_admitted=true);
  bool Conflicts(const std::string& session,const std::string& raw_method)const;
  void Tick(double now);
  void Detached(const std::string& session);
  void Closed();
  bool NeedsTick()const;
  size_t PhysicalCount()const;
  void Diagnostics(Dict output)const;
 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace suitecut
