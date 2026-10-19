// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "debugger_domains.h"
#include "developer_debugger_scripts.h"
#include "developer_debugger_frame_scopes.h"

namespace suitecut {
// Explicit developer control of one actual document. This is separate
// from raw Inspector ownership and from native bootstrap breakpoint capture.
class DeveloperDebuggerControls {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Reply = std::function<void(bool, const std::string&, const std::string&)>;
  struct Document {
    uint64_t epoch = 0;
    int context = 0;
    std::string unique_context;
    bool operator==(const Document&) const = default;
  };
  struct Hooks {
    std::function<double()> now;
    std::shared_ptr<DebuggerDomains> domains;
    // Source-wide monotonically unique lease identities shared with coverage
    // and factories. Never derive these from author protocol identifiers.
    std::function<uint64_t()> lease;
    std::string owner_namespace;
    // Immutable actual Inspector channel captured by the Source adapter. An
    // empty session is the real root channel, including same-process children.
    std::string inspector_session;
    // Agent-wide pause/step requests cannot target a child realm faithfully.
    // Frame adapters disable them before any pause is consumed or command sent.
    bool agent_controls = true;
    // A non-frame adapter captures its actual target/Runtime unique-context.
    std::function<DebuggerDomains::DeveloperPauseDisposition(Dict)> target_pause_eligibility;
    std::function<Document()> current;
    // Optional native channel fence. Both hooks must be supplied together.
    // Missing actual default metadata waits under the original open deadline;
    // no lease or scope token is invented for that interval. A successful pin
    // must identify the same actual root document/channel as current().
    std::shared_ptr<FrameDebuggerScopes> scopes;
    std::function<bool(const Document&,FrameDebuggerScopes::Pin&)> scope_pin;
    // Called only by the captured channel arbiter, with the exact immutable
    // native scope token. Foreign pauses are resumed by the arbiter; no IDs or
    // continuations from another context may be borrowed by this controller.
    std::function<DebuggerDomains::DeveloperPauseDisposition(FrameDebuggerScopes::Token,Dict)> pause_eligibility;
    // Validate actual observed script/context ownership; no ID spelling/URL
    // inference. Return false for child/worker or an obsolete document.
    std::function<bool(Dict, const Document&)> owns_pause;
    std::function<bool(const std::string&, const Document&)> owns_script;
    // Also excludes only attested native script identities; matching author
    // sourceURL/embedder/hash tuples never attest native ownership.
    std::function<bool(const std::string&, const Document&)> owns_breakpoint_script;
    // Selected actual metadata only; no Inspector command or source snapshot.
    std::function<bool(const Document&,const std::string&,const uint64_t*,double,
                       DeveloperDebuggerScripts::Page&,std::string&)> script_page;
    // Reply only at actual command ACK/real transport closure. A false return
    // proves that dispatch was not admitted and cannot subsequently reply.
    std::function<bool(const std::string&, Dict, double, DebuggerDomains::Reply)> send;
    std::function<void()> wake;
    // Must post genuine channel teardown. Calling this never retires receipts.
    std::function<void(const std::string&)> close_channel;
  };
  explicit DeveloperDebuggerControls(Hooks hooks);
  ~DeveloperDebuggerControls();
  // Actions: open, pause, waitForPause, resume, stepOver, stepInto, stepOut,
  // scriptSource, scripts, setBreakpoint, removeBreakpoint, evaluateOnCallFrame, close. Session and pause strings are native opaque tokens.
  void Handle(int owner, Dict params, double original_deadline, Reply reply);
  void Cancel(int owner);
  // Logical document/renderer invalidation only. Does not clear physical work.
  void Changed();
  void RealmDestroyed(uint64_t actual_source_epoch);
  void Tick(double now);
  // Actual root Inspector channel/Source closure, after native callback drain.
  void Closed();
  bool NeedsTick() const;
  size_t PhysicalCount() const;
  // Source manager retention only. Logical closure cannot make a record free
  // while its domain lease, accepted cleanup or exact scope fence still lives.
  bool Retained() const;
  bool OwnsSession(const std::string& native_token) const;
  void Diagnostics(Dict output) const;
 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace suitecut
