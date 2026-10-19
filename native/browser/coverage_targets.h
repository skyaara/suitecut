// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include "coverage_controls.h"
namespace suitecut {
// Coordinates genuine document Inspector targets and optional dedicated workers.
class CoverageTargets {
 public:
  using Dict=CoverageControls::Dict;using Reply=CoverageControls::Reply;using Ready=std::function<void(bool,const std::string&)>;
  struct Hooks{
    std::function<double()> now;
    std::function<bool(const std::string&,const std::string&,Dict,double,CoverageControls::ProtocolReply)> send;
    std::function<bool(const std::string&,double,CoverageControls::ProtocolReply)> debugger_acquire;
    std::function<bool(const std::string&,double,CoverageControls::ProtocolReply)> debugger_release;
    std::function<bool(const std::string&,int)> author_context;
    std::function<bool(Dict,std::string&)> script_url;
    std::function<bool(const std::string&,double,std::vector<Dict>&,std::string&)> script_inventory;
    std::function<void(const std::string&,const std::string&)> close_channel;
    std::function<void()> wake;
  };
  explicit CoverageTargets(Hooks hooks);~CoverageTargets();
  void Handle(int owner,Dict params,double deadline,Reply reply);
  void Attached(const std::string& session,const std::string& target,double deadline,Ready ready);
  void WorkerAttached(const std::string& session,const std::string& target,double deadline,Ready ready);
  // Context loss is logical data loss, never an actual channel-retirement ACK.
  void Lost(const std::string& session,const std::string& error);
  void Detached(const std::string& session);
  bool Event(const std::string& method,Dict params,const std::string& session);
  void NativeScript(const std::string& session,int context,const std::string& script);
  void RendererTerminated();
  void Navigated();void Cancel(int owner);void Tick(double now);void Closed();
  size_t PhysicalCount()const;bool NeedsTick()const;void Diagnostics(Dict output)const;
 private:class Impl;std::shared_ptr<Impl> impl_;
};
} // namespace suitecut
