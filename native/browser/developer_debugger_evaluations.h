// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include "debugger_domains.h"
namespace suitecut {
// Each direct paused-frame evaluation owns its actual native object group until
// release ACK/channel death, including exceptions, unsupported objects and abort.
class DeveloperDebuggerEvaluations {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,const std::string&,const std::string&)>;
  struct Hooks {
    std::string session;
    std::function<double()>now;
    std::function<bool(const std::string&,const std::string&)>current;
    std::function<bool(const std::string&,Dict,double,DebuggerDomains::Reply)>send;
    std::function<void(const std::string&)>close_channel;
    std::function<void()>changed;
  };
  explicit DeveloperDebuggerEvaluations(Hooks hooks);
  ~DeveloperDebuggerEvaluations();
  DeveloperDebuggerEvaluations(const DeveloperDebuggerEvaluations&)=delete;
  DeveloperDebuggerEvaluations&operator=(const DeveloperDebuggerEvaluations&)=delete;
  void Evaluate(int owner,const std::string&pause,const std::string&frame,const std::string&expression_json,double deadline,Reply reply);
  void Cancel(int owner);
  void Close();
  void Tick(double now);
  void RealmDestroyed(); // Genuine old V8 realm death; accepted calls remain.
  bool NeedsTick()const;
  void Closed(); // Actual Inspector boundary only.
  size_t Retained()const;
  static size_t ProcessRetained();
 private:
  class Impl;std::shared_ptr<Impl>impl_;
};
}
