// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace suitecut {
// These cuts describe accepted native work, not renderer/JavaScript quiet.
// Future worker-owned publications may be admitted after a cut. An Engine
// barrier must also authenticate its own callback/physical-lane ownership.
struct NativeStreamTicket {
  uint64_t instance=0,sequence=0;
  bool operator==(const NativeStreamTicket&)const=default;
};
struct NativeStreamCut {
  uint64_t instance=0,sequence=0,nonce=0;
  bool operator==(const NativeStreamCut&)const=default;
};
inline uint64_t NextNativeStreamInstance()noexcept{
  static std::atomic<uint64_t> sequence{0};
  auto value=sequence.load(std::memory_order_relaxed);
  while(value!=UINT64_MAX){if(sequence.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value+1;}
  return 0;
}
class AcceptedReaderWork {
 public:
  static constexpr size_t kPending=128;
  struct Snapshot {size_t pending,waiting_post,executing;uint64_t admitted,posted,returned,not_posted,unwound;bool poisoned;};
  AcceptedReaderWork():instance_(NextNativeStreamInstance()){if(!instance_)poisoned_=true;}
  std::optional<NativeStreamTicket> Admit()noexcept{
    std::lock_guard lock(mutex_);
    if(next_==UINT64_MAX){poisoned_=true;return std::nullopt;}
    for(auto& row:rows_)if(!row.active){row={};row.active=true;row.ticket={instance_,++next_};return row.ticket;}
    poisoned_=true;return std::nullopt;
  }
  // The UI task may genuinely enter/return before CefPostTask's caller resumes.
  bool PostingReturned(NativeStreamTicket ticket,bool accepted)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||row->post_returned)return false;
    row->post_returned=true;row->post_accepted=accepted;
    if(accepted)++posted_;
    else{++not_posted_;if(row->entered)poisoned_=true;}
    Retire(*row);return true;
  }
  // Only before a backend call was attempted, e.g. task allocation failure or
  // actual reply-credit reservation refusal. Never use for timeout/cancellation.
  bool NeverPosted(NativeStreamTicket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||row->post_returned||row->entered)return false;
    row->post_returned=true;row->post_accepted=false;++not_posted_;Retire(*row);return true;
  }
  bool PostingUnwound(NativeStreamTicket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||row->post_returned)return false;
    row->post_returned=true;row->post_uncertain=true;poisoned_=true;++unwound_;Retire(*row);return true;
  }
  bool Enter(NativeStreamTicket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||row->entered||(row->post_returned&&!row->post_accepted&&!row->post_uncertain)){poisoned_=true;return false;}
    row->entered=true;return true;
  }
  bool Returned(NativeStreamTicket ticket,bool unwound=false)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||!row->entered||row->returned)return false;
    row->returned=true;++returned_;if(unwound)poisoned_=true;Retire(*row);return true;
  }
  NativeStreamCut Cut()noexcept{
    std::lock_guard lock(mutex_);if(cut_sequence_==UINT64_MAX){poisoned_=true;return {};}
    for(auto& cut:cuts_)if(!cut.nonce){cut={instance_,next_,++cut_sequence_};return cut;}
    poisoned_=true;return {};
  }
  bool KnownCut(NativeStreamCut cut)const noexcept{std::lock_guard lock(mutex_);return ValidCut(cut);}
  bool Through(NativeStreamCut cut)const noexcept{
    std::lock_guard lock(mutex_);if(poisoned_||!ValidCut(cut))return false;
    for(const auto& row:rows_)if(row.active&&row.ticket.sequence<=cut.sequence)return false;
    return true;
  }
  bool ReleaseCut(NativeStreamCut cut)noexcept{
    std::lock_guard lock(mutex_);for(auto& value:cuts_)if(value==cut&&cut.nonce){value={};return true;}return false;
  }
  void Unsupported()noexcept{std::lock_guard lock(mutex_);poisoned_=true;}
  Snapshot Inspect()const noexcept{
    std::lock_guard lock(mutex_);Snapshot out{0,0,0,next_,posted_,returned_,not_posted_,unwound_,poisoned_};
    for(const auto& row:rows_)if(row.active){++out.pending;if(!row.entered)++out.waiting_post;else if(!row.returned)++out.executing;}
    return out;
  }
 private:
  bool ValidCut(NativeStreamCut cut)const noexcept{for(const auto& value:cuts_)if(value==cut&&cut.nonce)return true;return false;}
  struct Row {NativeStreamTicket ticket;bool active=false,post_returned=false,post_accepted=false,post_uncertain=false,entered=false,returned=false;};
  Row* Find(NativeStreamTicket ticket)noexcept{for(auto& row:rows_)if(row.active&&row.ticket==ticket)return &row;return nullptr;}
  static void Retire(Row& row)noexcept{
    if(row.post_returned&&((!row.post_accepted&&!row.post_uncertain&&!row.entered)||row.returned))row.active=false;
  }
  mutable std::mutex mutex_;const uint64_t instance_;uint64_t next_=0,cut_sequence_=0,posted_=0,returned_=0,not_posted_=0,unwound_=0;bool poisoned_=false;std::array<Row,kPending> rows_{};std::array<NativeStreamCut,64> cuts_{};
};
class AcceptedWriterWork {
 public:
  static constexpr size_t kPending=8192;
  enum class Kind {Control,Event,Frame,Audio,Watermark};
  enum class Discard {FrameReplaced,FrameBudget,PageDropped,AudioBudget};
  struct Snapshot {size_t queued,writing;uint64_t admitted,full_written,discarded,actual_bytes;bool poisoned;};
  AcceptedWriterWork():instance_(NextNativeStreamInstance()){if(!instance_)poisoned_=true;}
  std::optional<NativeStreamTicket> Admit(Kind kind,size_t exact_packet_bytes)noexcept{
    std::lock_guard lock(mutex_);
    if(exact_packet_bytes<4||next_==UINT64_MAX){poisoned_=true;return std::nullopt;}
    for(auto& row:rows_)if(!row.active){row={};row.active=true;row.ticket={instance_,++next_};row.kind=kind;row.bytes=exact_packet_bytes;return row.ticket;}
    poisoned_=true;return std::nullopt;
  }
  bool Dequeue(NativeStreamTicket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||row->writing)return false;
    row->writing=true;return true;
  }
  // Called for each real positive write() result, never requested byte count.
  bool Wrote(NativeStreamTicket ticket,size_t actual_bytes)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||!row->writing||!actual_bytes||actual_bytes>row->bytes-row->written||actual_bytes>UINT64_MAX-actual_bytes_){poisoned_=true;return false;}
    row->written+=actual_bytes;actual_bytes_+=actual_bytes;return true;
  }
  bool FullWrite(NativeStreamTicket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||!row->writing||row->written!=row->bytes)return false;
    row->active=false;++full_written_;return true;
  }
  bool Dropped(NativeStreamTicket ticket,Discard reason)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);
    if(!row||row->writing||!DropAllowed(row->kind,reason))return false;
    row->active=false;++discarded_;return true;
  }
  // A queue insertion exception before native publication is not a full write.
  // It invalidates barrier authority, while the original exception is preserved.
  bool AdmissionUnwound(NativeStreamTicket ticket)noexcept{
    std::lock_guard lock(mutex_);auto* row=Find(ticket);poisoned_=true;
    if(!row||row->writing)return false;
    row->active=false;return true;
  }
  void WriteFailed()noexcept{std::lock_guard lock(mutex_);poisoned_=true;}
  bool ReleaseCut(NativeStreamCut cut)noexcept{
    std::lock_guard lock(mutex_);for(auto& value:cuts_)if(value==cut&&cut.nonce){value={};return true;}return false;
  }
  void Unsupported()noexcept{std::lock_guard lock(mutex_);poisoned_=true;}
  NativeStreamCut Cut()noexcept{
    std::lock_guard lock(mutex_);if(cut_sequence_==UINT64_MAX){poisoned_=true;return {};}
    for(auto& cut:cuts_)if(!cut.nonce){cut={instance_,next_,++cut_sequence_};return cut;}
    poisoned_=true;return {};
  }
  bool KnownCut(NativeStreamCut cut)const noexcept{std::lock_guard lock(mutex_);return ValidCut(cut);}
  bool Through(NativeStreamCut cut)const noexcept{
    std::lock_guard lock(mutex_);if(poisoned_||!ValidCut(cut))return false;
    for(const auto& row:rows_)if(row.active&&row.ticket.sequence<=cut.sequence)return false;
    return true;
  }
  Snapshot Inspect()const noexcept{
    std::lock_guard lock(mutex_);Snapshot out{0,0,next_,full_written_,discarded_,actual_bytes_,poisoned_};
    for(const auto& row:rows_)if(row.active){if(row.writing)++out.writing;else++out.queued;}
    return out;
  }
 private:
  bool ValidCut(NativeStreamCut cut)const noexcept{for(const auto& value:cuts_)if(value==cut&&cut.nonce)return true;return false;}
  static bool DropAllowed(Kind kind,Discard reason)noexcept{
    return kind==Kind::Frame?(reason==Discard::FrameReplaced||reason==Discard::FrameBudget||reason==Discard::PageDropped):kind==Kind::Audio&&(reason==Discard::AudioBudget||reason==Discard::PageDropped);
  }
  struct Row {NativeStreamTicket ticket;Kind kind=Kind::Control;size_t bytes=0,written=0;bool active=false,writing=false;};
  Row* Find(NativeStreamTicket ticket)noexcept{for(auto& row:rows_)if(row.active&&row.ticket==ticket)return &row;return nullptr;}
  mutable std::mutex mutex_;const uint64_t instance_;uint64_t next_=0,cut_sequence_=0,full_written_=0,discarded_=0,actual_bytes_=0;bool poisoned_=false;std::array<Row,kPending> rows_{};std::array<NativeStreamCut,64> cuts_{};
};
} // namespace suitecut
