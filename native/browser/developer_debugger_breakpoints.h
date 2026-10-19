// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include "debugger_domains.h"
namespace suitecut {
// One explicit developer lease's real script breakpoints. Native IDs, pending
// set replies and removed-but-unacknowledged IDs remain reserved until an actual
// reply or actual Inspector channel death. UI actor only, except global count.
class DeveloperDebuggerBreakpoints {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  // Return true only when the original native public owner still accepts
  // successful publication. Rejected late publication triggers real removal.
  using Reply = std::function<bool(bool,const std::string&,const std::string&)>;
  struct Hooks {
    std::string session;
    std::function<double()> now;
    std::function<bool(const std::string&)> owns_script;
    std::function<bool()> current;
    std::function<bool(const std::string&,Dict,double,DebuggerDomains::Reply)> send;
    std::function<void(const std::string&)> close_channel;
    std::function<void()> changed;
  };
  explicit DeveloperDebuggerBreakpoints(Hooks hooks);
  ~DeveloperDebuggerBreakpoints();
  DeveloperDebuggerBreakpoints(const DeveloperDebuggerBreakpoints&)=delete;
  DeveloperDebuggerBreakpoints& operator=(const DeveloperDebuggerBreakpoints&)=delete;
  void Set(int owner,Dict location,double deadline,Reply reply);
  void Remove(int owner,const std::string& token,double deadline,Reply reply);
  void Cancel(int owner);
  // Logical close starts real removal; it does not erase pending records.
  void Close();
  void Tick(double now);
  bool NeedsTick()const;
  // Actual Inspector channel closure only, after retained callbacks drain.
  void Closed();
  size_t Retained()const;
  static size_t ProcessRetained();
 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};
}
