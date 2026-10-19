// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstddef>
#include <utility>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace suitecut {
// Genuine renderer virtual-time budget control. This is deliberately narrower
// than a fake-JavaScript clock: no Date/timer implementation is replaced.
class NativeVirtualTime {
 public:
  enum class Action { InstallPaused, Pause, Advance, RunFor };
  enum class Kind { EnableMetrics, Pause, Advance, ObservePerformance };
  struct Dispatch { uint64_t token; int owner; uint64_t generation; Kind kind; double budget=0;std::optional<double> epoch_ms; };
  struct Request {int id;uint64_t generation;Action action;double value;double deadline;int context=0;};
  struct ExceptionReceipt {uint64_t sequence;int context;bool synchronous;std::string error;};
  struct Hooks {
    std::function<void(const Dispatch&)> send;
    std::function<void(int,uint64_t,bool,const std::string&)> complete;
    std::function<void(const std::string&)> close;
  };
  struct Snapshot {bool installed=false;bool paused=false;size_t queued=0;bool physical=false;bool closing=false;bool active=false;};
  explicit NativeVirtualTime(Hooks hooks):hooks_(std::move(hooks)){}
  // Public duration strings use SS, MM:SS or HH:MM:SS (two digits per field).
  // Parsing and
  // bounded budget validation belong to the native scheduler, not the SDK.
  static std::optional<double> ParseDuration(std::string_view duration);
  static bool IsSynchronousException(std::string_view text);
  bool Begin(Request request,double now);
  void Acknowledge(uint64_t token,bool ok,double now,std::optional<double> performance=std::nullopt,uint64_t receipt_sequence=0);
  // Source-assigned native receipt order, not timer-exclusive provenance.
  bool Exception(const ExceptionReceipt& receipt);
  void BudgetExpired(double now);
  void Cancel(int id,double now);
  void Tick(double now);
  void Closed();
  void TargetChanged(){FailClosed("Native virtual time current-document scope changed");}
  Snapshot State()const;
  // Ownership begins before install ACK and lasts through actual target close.
  bool OwnsTarget()const;
 private:
  enum class Phase {None,Enable,Install,Pause,StartObservation,Advance,AwaitBudget,EndObservation,Fence};
  struct Work {Request request;Phase phase=Phase::None;bool logical=true;bool physical=false;bool hint=false;uint64_t token=0;double cleanup_deadline=0;double target=0;uint64_t receipt_begin=0;std::string exception{};};
  void Start(double now);
  void Send(Kind kind,Phase phase,double now,double budget=0,std::optional<double> epoch=std::nullopt);
  void Fence(double now);
  void Retire(double now,bool success,const std::string& error={});
  void FailClosed(const std::string& error);
  Hooks hooks_;
  std::deque<Request> queue_;
  std::optional<Work> active_;
  uint64_t nonce_=0,exception_sequence_=0;
  bool installed_=false,paused_=false,closing_=false;
};
}
