// SPDX-License-Identifier: MIT
#pragma once
#include "growth_guard.h"
#include "accepted_work_origin.h"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace suitecut {
// Process-owned CEF UI-thread ledger. It observes real queue/send/reply/channel
// boundaries independently of logical operation registries. Every table grows
// with the actual work; a broken receipt poisons idle authority while existing
// backend behavior can continue normally.
// It is one required Engine lane, never an Engine idle predicate by itself.
class AcceptedInspectorWork {
 public:
  enum class Kind {Page,Broker,Utility};
  struct Channel {uint64_t nonce=0;int browser=0;Kind kind=Kind::Page;bool operator==(const Channel&)const=default;};
  struct Ticket {uint64_t nonce=0;Channel channel;int command=0;bool operator==(const Ticket&)const=default;};
  enum class Reply {Observed,Unknown,Foreign,NotDispatched};
  struct Snapshot {size_t channels,queued,submitted,callbacks;uint64_t dispatches,accepted,replied,refused,unwound,not_dispatched,channel_closed;bool poisoned;};
  std::optional<Channel> Open(int actual_browser,Kind kind)noexcept{
    if(actual_browser<=0||next_channel_==UINT64_MAX)return PoisonChannel();
    for(const auto& value:channels_)if(value&&value->browser==actual_browser)return PoisonChannel();
    const Channel created{++next_channel_,actual_browser,kind};
    for(auto& value:channels_)if(!value){value=created;++channel_count_;return created;}
    // Growth failure poisons idle authority instead of terminating the process.
    SUITECUT_GROWTH_TRY{channels_.emplace_back(created);}SUITECUT_GROWTH_CATCH{return PoisonChannel();}
    ++channel_count_;return created;
  }
  std::optional<Ticket> Reserve(Channel channel,int command,std::string_view session,NativeWorkOrigin origin=CurrentNativeWorkOrigin())noexcept{
    if(!Current(channel)||command<=0||next_ticket_==UINT64_MAX)return PoisonTicket();
    for(const auto& row:pending_)if(row.active&&row.ticket.channel==channel&&row.ticket.command==command)return PoisonTicket();
    SUITECUT_GROWTH_TRY{
      Record created{};created.active=true;created.ticket={++next_ticket_,channel,command};created.origin=origin;created.session.assign(session);
      for(auto& row:pending_)if(!row.active){row=std::move(created);++queued_;return row.ticket;}
      pending_.push_back(std::move(created));++queued_;return pending_.back().ticket;
    }SUITECUT_GROWTH_CATCH{return PoisonTicket();}
  }
  // Called immediately before the real CEF send, including deferred sends.
  // A synchronous/reentrant actual reply can safely observe Submitted already.
  bool Dispatch(const Ticket& ticket,std::string_view session)noexcept{
    auto* row=Find(ticket);if(!row||row->submitted||dispatches_==UINT64_MAX){poisoned_=true;return false;}
    if(session!=row->session){poisoned_=true;return false;}
    row->submitted=true;--queued_;++submitted_;++dispatches_;
    if(Detached(row->ticket.channel,session)){row->closed=true;++session_detached_;}
    return true;
  }
  // Exact native queue removal/refusal before any backend call. Timeout alone
  // cannot retire Submitted work: it has no corresponding API here.
  bool NeverDispatched(const Ticket& ticket)noexcept{auto* row=Find(ticket);if(!row||row->submitted)return false;row->active=false;--queued_;++not_dispatched_;return true;}
  // The immediate real CEF bool/positive-int return authenticates acceptance.
  // Keep a row through a reentrant reply/OnBeforeClose until this exact stack
  // frame returns too. No callback can make the executing send frame idle.
  bool SendReturned(const Ticket& ticket,bool accepted)noexcept{
    auto* row=Find(ticket);if(!row||!row->submitted||row->returned)return false;
    row->returned=true;if(accepted)++accepted_;else ++refused_;
    if(!accepted&&row->replied)poisoned_=true;
    if(!accepted||row->replied||row->closed){row->active=false;--submitted_;}
    return true;
  }
  // A native exception unwinds the send stack, but supplies no CEF accepted
  // or refused receipt. Keep physical ownership until actual reply/close and
  // poison idle authority rather than inventing a false send return.
  bool SendUnwound(const Ticket& ticket)noexcept{
    auto* row=Find(ticket);if(!row||!row->submitted||row->returned)return false;
    row->returned=true;poisoned_=true;++unwound_;
    if(row->replied||row->closed){row->active=false;--submitted_;}
    return true;
  }
  std::optional<Ticket> Pending(Channel channel,int command)const noexcept{for(const auto& row:pending_)if(row.active&&row.ticket.channel==channel&&row.ticket.command==command)return row.ticket;return std::nullopt;}
  NativeWorkOrigin ReplyOrigin(Channel channel,int command,std::string_view session)const noexcept{
    if(!Current(channel))return {};
    for(const auto& row:pending_)if(row.active&&row.ticket.channel==channel&&row.ticket.command==command){
      if(!row.submitted||row.replied||row.session!=session)return {};
      return row.origin;
    }
    return {};
  }
  Reply ActualReply(Channel channel,int command,std::string_view session)noexcept{
    if(!Current(channel))return Reply::Foreign;
    for(auto& row:pending_)if(row.active&&row.ticket.channel==channel&&row.ticket.command==command){
      if(row.session!=session)return Reply::Foreign;
      if(!row.submitted)return Reply::NotDispatched;
      if(row.replied)return Reply::Unknown;
      row.replied=true;++replied_;
      if(row.returned){row.active=false;--submitted_;}
      return Reply::Observed;
    }
    return Reply::Unknown;
  }
  // Only actual OnBeforeClose for this exact browser incarnation. A renderer
  // termination, context clear, logical timeout or session detach is no receipt.
  bool ActualChannelClosed(Channel channel)noexcept{
    for(auto& value:channels_)if(value&&*value==channel){
      for(auto& row:pending_)if(row.active&&row.ticket.channel==channel){
        row.closed=true;++channel_closed_;
        if(!row.submitted){--queued_;row.active=false;}
        else if(row.returned){--submitted_;row.active=false;}
      }
      std::erase_if(detached_,[&](const DetachedSession& slot){return slot.channel==channel;});
      value.reset();--channel_count_;return true;
    }
    return false;
  }
  // Actual Target.detachedFromTarget for one flattened child session. Chrome
  // drops whatever that session was sent and never answers it, so this
  // detach is the only boundary those submitted commands will see. Commands
  // still queued are kept: sending them gets a session-not-found reply.
  size_t ActualSessionDetached(Channel channel,std::string_view session)noexcept{
    if(!Current(channel)||session.empty())return 0;
    size_t retired=0;
    for(auto& row:pending_)if(row.active&&row.submitted&&row.ticket.channel==channel&&row.session==session){
      row.closed=true;++session_detached_;
      if(row.returned){row.active=false;--submitted_;++retired;}
    }
    if(!Detached(channel,session)){SUITECUT_GROWTH_TRY{detached_.push_back({channel,std::string(session)});}SUITECUT_GROWTH_CATCH{poisoned_=true;}}
    return retired;
  }
  // Genuine CEF callback entry/stack return is a separate physical lane;
  // observing a reply cannot make its executing native callback idle.
  uint64_t BeginCallback(Channel channel)noexcept{
    if(!Current(channel)||next_callback_==UINT64_MAX){poisoned_=true;return 0;}
    const uint64_t created=++next_callback_;
    for(auto& callback:callbacks_)if(!callback){callback=created;++callback_count_;return created;}
    SUITECUT_GROWTH_TRY{callbacks_.push_back(created);}SUITECUT_GROWTH_CATCH{poisoned_=true;return 0;}
    ++callback_count_;return created;
  }
  bool CallbackReturned(uint64_t receipt)noexcept{
    if(!receipt)return false;
    for(auto& callback:callbacks_)if(callback==receipt){callback=0;--callback_count_;return true;}
    return false;
  }
  void Unsupported()noexcept{poisoned_=true;}
  Snapshot Inspect()const noexcept{return {channel_count_,queued_,submitted_,callback_count_,dispatches_,accepted_,replied_,refused_,unwound_,not_dispatched_,channel_closed_,poisoned_};}
  bool ReceiptLaneReady()const noexcept{return !poisoned_&&!queued_&&!submitted_&&!callback_count_;}
  // A viewer handover may pass commands already sent to a crashed renderer:
  // they can act on nothing, and recovery joins them before the page runs.
  // Queued commands still block, since they could reach the recovered page.
  // A worker's own startup commands are resident page behaviour too: a shared
  // worker that never starts never answers them, and their late reply only
  // resumes or publishes that worker.
  template<class Crashed>bool ViewerReceiptLaneReady(Crashed&& crashed)const{
    if(poisoned_||callback_count_)return false;
    for(const auto& row:pending_)if(ViewerWaits(row,crashed))return false;
    return true;
  }
  void Resident(const Ticket& ticket)noexcept{if(auto* row=Find(ticket))row->resident=true;}
  // The first row a handover waits on, for diagnostics only.
  template<class Crashed>std::string Blocker(Crashed&& crashed)const{
    if(poisoned_)return "inspector ledger poisoned";
    if(callback_count_)return "inspector callback running";
    size_t waiting=0;const Record* first=nullptr;
    for(const auto& row:pending_)if(ViewerWaits(row,crashed)){++waiting;if(!first)first=&row;}
    if(!first)return {};
    const std::string_view session(first->session);
    const auto kind=first->ticket.channel.kind;
    return first->method+(first->submitted?" awaiting its reply":" queued")+
           (session.empty()?"":Detached(first->ticket.channel,session)?" in a detached child session":" in a child session")+
           (kind==Kind::Broker?" of the context's worker channel":kind==Kind::Utility?" of the utility channel":"")+
           (waiting>1?" and "+std::to_string(waiting-1)+" more":"");
  }
  void Label(const Ticket& ticket,std::string_view method)noexcept{
    auto* row=Find(ticket);if(!row)return;
    // Diagnostic text only: a failed copy leaves the label empty, the ledger exact.
    SUITECUT_GROWTH_TRY{row->method.assign(method);}SUITECUT_GROWTH_CATCH{row->method.clear();}
  }
  // Scoped observation only; it neither retires rows nor relaxes global idle.
  // Callback frames lack exact per-frame origins, so an executing real native
  // Inspector callback conservatively blocks every handler observation.
  bool LocatorHandlerReady(uint64_t invocation)const noexcept{
    if(!invocation||poisoned_||callback_count_)return false;
    for(const auto& row:pending_)if(row.active&&row.origin.locator_handler_invocation==invocation)return false;
    return true;
  }
 private:
  struct Record {Ticket ticket;NativeWorkOrigin origin;std::string session,method;bool active=false,submitted=false,returned=false,replied=false,closed=false,resident=false;};
  template<class Crashed>static bool ViewerWaits(const Record& row,Crashed&& crashed){
    return row.active&&!row.resident&&(!row.submitted||!crashed(row.ticket.channel));
  }
  struct DetachedSession {Channel channel;std::string session;};
  // Every genuine child session detach, kept until its channel closes.
  bool Detached(Channel channel,std::string_view session)const noexcept{
    if(session.empty())return false;
    for(const auto& slot:detached_)if(slot.channel==channel&&slot.session==session)return true;
    return false;
  }
  std::vector<DetachedSession> detached_;
  bool Current(Channel channel)const noexcept{for(const auto& value:channels_)if(value&&*value==channel)return true;return false;}
  Record* Find(const Ticket& ticket)noexcept{for(auto& row:pending_)if(row.active&&row.ticket==ticket)return &row;return nullptr;}
  std::optional<Channel> PoisonChannel()noexcept{poisoned_=true;return std::nullopt;}
  std::optional<Ticket> PoisonTicket()noexcept{poisoned_=true;return std::nullopt;}
  std::vector<std::optional<Channel>> channels_;std::vector<Record> pending_;
  std::vector<uint64_t> callbacks_;uint64_t next_callback_=0;size_t callback_count_=0;
  uint64_t session_detached_=0,next_channel_=0,next_ticket_=0,dispatches_=0,accepted_=0,replied_=0,refused_=0,unwound_=0,not_dispatched_=0,channel_closed_=0;size_t channel_count_=0,queued_=0,submitted_=0;bool poisoned_=false;
};
} // namespace suitecut
