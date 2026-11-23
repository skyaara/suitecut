// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "include/cef_values.h"
namespace suitecut {
// Stores genuine Inspector function IDs for exactly pinned default contexts.
// send callbacks must run on actual protocol ACK or actual channel death.
class PristineDispatchFactories {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Context {
    std::string session,frame,loader,identity;
    int context=0;uint64_t source_epoch=0,session_epoch=0;
    bool operator==(const Context&)const=default;
  };
  struct Factory { Context owner;std::string object,group; };
  enum class Barrier { InitialSettingsHold=1, NewDocumentBootstrap=2 };
  using Ticket=uint64_t;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using Ready=std::function<void(bool,std::optional<Factory>,const std::string&)>;
  struct Hooks {
    std::function<double()> now;
    std::function<bool(const Context&)> current;
    // Own a shared Debugger receipt before sending; retire it after this actual
    // callback has enqueued any late-created group cleanup, never on timeout.
    std::function<bool(const Context&,const std::string&,Dict,double,Reply)> send;
    std::function<void(const std::string&,const std::string&)> close_channel;
    std::function<void()> wake;
  };
  explicit PristineDispatchFactories(Hooks hooks);
  ~PristineDispatchFactories();
  // Source may authorize only a genuine native idle hold or creation observed
  // under the already-armed bootstrap barrier. Matching a later script alone
  // never authorizes a warmed realm. Tickets are one-shot per actual context.
  Ticket Expect(Context owner,Barrier actual_barrier,double original_deadline);
  void Capture(Ticket ticket,Ready ready);
  std::optional<Factory> Find(const Context& owner)const;
  // Logical cancellation retains the consumed-ticket tombstone.
  void Invalidate(const Context& owner,const std::string& reason);
  void ContextDestroyed(const Context& owner,const std::string& reason);
  void ContextsCleared(const std::string& session,const std::string& reason);
  // Actual renderer-process death destroys its V8 groups but is NOT Inspector
  // session closure: accepted command callbacks remain owned until actual ACK.
  void RealmDestroyed(const std::string& session,uint64_t source_epoch,const std::string& reason);
  // Only after the actual session died; logical context loss is not closure.
  void SessionClosed(const std::string& session);
  void Closed();
  void Tick(double now);
  bool NeedsTick()const;
  size_t PhysicalCount()const;
  void Diagnostics(Dict output)const;
 private:class Impl;std::shared_ptr<Impl> impl_;
};
} // namespace suitecut
