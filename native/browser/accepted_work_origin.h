// SPDX-License-Identifier: MIT
#pragma once
#include "accepted_stream_work.h"
#include <cstdint>
namespace suitecut {
// Native attribution only. A fixture binding supplies its actual registered
// process-lifetime source ticket. The independent handler tag is issued by the
// Engine; it grants no fixture lease or reader ticket. No SDK counter/alias.
struct NativeWorkOrigin {
  enum class Kind {Unknown,Command,ResidentWorker};
  Kind kind=Kind::Unknown;
  uint64_t source_lease=0;
  NativeStreamTicket command{};
  // A genuine native handler invocation can own standalone browser work even
  // when source_lease==0 and kind remains Unknown. Zero preserves old behavior.
  uint64_t locator_handler_invocation=0;
  bool operator==(const NativeWorkOrigin&)const=default;
  static NativeWorkOrigin Command(uint64_t actual_source_lease,NativeStreamTicket actual_reader_ticket)noexcept{
    if(!actual_source_lease||!actual_reader_ticket.instance||!actual_reader_ticket.sequence)return {};
    return {Kind::Command,actual_source_lease,actual_reader_ticket};
  }
  // Call only for a genuinely identified worker-owned native producer. An
  // unclassified CEF event may finish a prior navigation/command: it is NOT
  // automatically ResidentWorker merely because it is not a CDP reply.
  static NativeWorkOrigin ResidentWorker(uint64_t actual_source_lease)noexcept{
    if(!actual_source_lease)return {};
    return {Kind::ResidentWorker,actual_source_lease,{}};
  }
  bool PriorTo(uint64_t actual_source_lease,const AcceptedReaderWork& actual_reader,NativeStreamCut issued_reader_cut)const noexcept{
    return actual_reader.KnownCut(issued_reader_cut)&&kind==Kind::Command&&source_lease==actual_source_lease&&command.instance==issued_reader_cut.instance&&command.sequence<=issued_reader_cut.sequence;
  }
};
inline NativeWorkOrigin& CurrentNativeWorkOrigin()noexcept{static thread_local NativeWorkOrigin origin;return origin;}
class NativeWorkOriginFrame {
 public:
  explicit NativeWorkOriginFrame(NativeWorkOrigin actual)noexcept:previous_(CurrentNativeWorkOrigin()){CurrentNativeWorkOrigin()=actual;}
  NativeWorkOriginFrame(const NativeWorkOriginFrame&)=delete;NativeWorkOriginFrame& operator=(const NativeWorkOriginFrame&)=delete;
  ~NativeWorkOriginFrame(){CurrentNativeWorkOrigin()=previous_;}
 private:
  NativeWorkOrigin previous_;
};
// This value can be stored on a real callback/operation record before dispatch.
// It supplies no completion/discharge; the actual owner retains its old lease.
class NativeWorkContinuation {
 public:
  NativeWorkContinuation()noexcept:origin_(CurrentNativeWorkOrigin()){}
  explicit NativeWorkContinuation(NativeWorkOrigin actual)noexcept:origin_(actual){}
  NativeWorkOrigin Origin()const noexcept{return origin_;}
 private:
  NativeWorkOrigin origin_;
};
} // namespace suitecut
