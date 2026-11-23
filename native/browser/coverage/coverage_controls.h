// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_values.h"
namespace suitecut {
// Native author-realm coverage for one actual Inspector target. CEF target/session
// ownership and transactions across targets are embedder responsibilities. Retention
// probes the actual versioned backend final-lifecycle contract before enabling.
// This controller exclusively enables DOM for CSS, then disables CSS before
// DOM; other engine consumers currently use DOM methods without enabling it.
// Any future domain-enabling consumer must share an embedder-owned lease.
// A direct root-only owner rejects child targets; CoverageTargets instead owns
// one controller per actual iframe session. Workers remain a distinct API.
class CoverageControls {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using ProtocolReply=std::function<void(bool,Dict,const std::string&)>;
  using Reply=std::function<void(bool,const std::string&,const std::string&)>;
  struct Hooks {
    // Actual coordinator-owned Inspector identities, never inferred from URLs.
    std::string target_session,target_id;
    std::function<double()> now;
    // Must reply only on actual Inspector ACK or genuine transport closure.
    // The deadline is the original logical call/read deadline, not a new budget.
    std::function<bool(const std::string&,Dict,double,ProtocolReply)> send;
    // Optional paired shared domain leases. Positive readiness requires actual
    // domain-state ACKs. A logical failure may settle this readiness waiter
    // earlier; the shared owner must independently retain its accepted physical
    // callbacks until actual ACK/closure. A factory lease can keep pauses on.
    std::function<bool(double,ProtocolReply)> debugger_acquire;
    std::function<bool(double,ProtocolReply)> debugger_release;
    std::function<bool(int)> author_context;
    // Classify only actual context-registry script identities; false denotes a
    // genuine native binding bridge, empty URL denotes unnamed author init code.
    std::function<bool(Dict,std::string&)> script_url;
    // Snapshot only genuine prior scriptParsed metadata after shared Debugger readiness.
    std::function<bool(double,std::vector<Dict>&,std::string&)> script_inventory;
    std::function<bool()> root_target_only;
    // Optional shared native payload admission across actual target controllers.
    // Reservations follow real stored source/measurement ownership, including
    // canceled records retained by actual callbacks.
    std::function<bool(size_t)> reserve_payload;
    std::function<void(size_t)> release_payload;
    std::function<void()> wake;
    // A domain teardown failure or >30s late physical ACK requires actual channel
    // closure. No timeout is treated as a physical command retirement.
    std::function<void(const std::string&)> close_channel;
  };
  static std::string SourceHash(const std::string& source);
  explicit CoverageControls(Hooks hooks);
  ~CoverageControls();
  void Handle(int owner,Dict params,double deadline,Reply reply);
  void Event(const std::string& method,Dict params);
  void NativeScript(int context,const std::string& script);
  void Navigated();
  // A final counter payload is separate from actual session/command retirement.
  bool FinalReceipt(const std::string& action,std::string& output,std::string& error)const;
  void OutOfProcessFrameAttached();
  void Cancel(int owner);
  void Abort(const std::string& action,const std::string& error);
  bool Ready(const std::string& action) const;
  void Tick(double now);
  // Call only after the real CEF channel closed and its callbacks were retired.
  void Closed();
  size_t PhysicalCount()const;
  bool NeedsTick()const;
  void Diagnostics(Dict output)const;
 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace suitecut
