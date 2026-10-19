// SPDX-License-Identifier: MIT
#pragma once
#include "accepted_work_origin.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <exception>
#include <optional>
#include <string_view>
namespace suitecut {
// Process-owned CEF UI-thread ledger. It observes real queue/send/reply/channel
// boundaries independently of logical operation registries. Capacity failure
// poisons idle authority while existing backend behavior can continue normally.
// It is one required Engine lane, never an Engine idle predicate by itself.
class AcceptedInspectorWork {
 public:
  static constexpr size_t kChannels=256,kPending=4096,kSessionBytes=128,kCallbacks=64;
  enum class Kind {Page,Broker,Utility};
  struct Channel {uint64_t nonce=0;int browser=0;Kind kind=Kind::Page;bool operator==(const Channel&)const=default;};
  struct Ticket {uint64_t nonce=0;Channel channel;int command=0;bool operator==(const Ticket&)const=default;};
  enum class Reply {Observed,Unknown,Foreign,NotDispatched};
  struct Snapshot {size_t channels,queued,submitted,callbacks;uint64_t dispatches,accepted,replied,refused,unwound,not_dispatched,channel_closed;bool poisoned;};
  std::optional<Channel> Open(int actual_browser,Kind kind)noexcept{
    if(actual_browser<=0||next_channel_==UINT64_MAX)return PoisonChannel();
    for(const auto& value:channels_)if(value&&value->browser==actual_browser)return PoisonChannel();
    for(auto& value:channels_)if(!value){value=Channel{++next_channel_,actual_browser,kind};++channel_count_;return value;}
    return PoisonChannel();
  }
  std::optional<Ticket> Reserve(Channel channel,int command,std::string_view session,NativeWorkOrigin origin=CurrentNativeWorkOrigin())noexcept{
    if(!Current(channel)||command<=0||session.size()>kSessionBytes||next_ticket_==UINT64_MAX)return PoisonTicket();
    for(const auto& row:pending_)if(row.active&&row.ticket.channel==channel&&row.ticket.command==command)return PoisonTicket();
    for(auto& row:pending_)if(!row.active){row={};row.active=true;row.ticket={++next_ticket_,channel,command};row.origin=origin;row.session_size=session.size();for(size_t i=0;i<session.size();++i)row.session[i]=session[i];++queued_;return row.ticket;}
    return PoisonTicket();
  }
  // Called immediately before the real CEF send, including deferred sends.
  // A synchronous/reentrant actual reply can safely observe Submitted already.
  bool Dispatch(const Ticket& ticket,std::string_view session)noexcept{
    auto* row=Find(ticket);if(!row||row->submitted||dispatches_==UINT64_MAX){poisoned_=true;return false;}
    if(session.size()!=row->session_size){poisoned_=true;return false;}
    for(size_t i=0;i<session.size();++i)if(session[i]!=row->session[i]){poisoned_=true;return false;}
    row->submitted=true;--queued_;++submitted_;++dispatches_;return true;
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
      if(!row.submitted||row.replied||row.session_size!=session.size())return {};
      for(size_t i=0;i<session.size();++i)if(session[i]!=row.session[i])return {};
      return row.origin;
    }
    return {};
  }
  Reply ActualReply(Channel channel,int command,std::string_view session)noexcept{
    if(!Current(channel))return Reply::Foreign;
    for(auto& row:pending_)if(row.active&&row.ticket.channel==channel&&row.ticket.command==command){
      if(session.size()!=row.session_size)return Reply::Foreign;
      for(size_t i=0;i<session.size();++i)if(session[i]!=row.session[i])return Reply::Foreign;
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
      value.reset();--channel_count_;return true;
    }
    return false;
  }
  // Genuine CEF callback entry/stack return is a separate physical lane;
  // observing a reply cannot make its executing native callback idle.
  uint64_t BeginCallback(Channel channel)noexcept{
    if(!Current(channel)||next_callback_==UINT64_MAX){poisoned_=true;return 0;}
    for(auto& callback:callbacks_)if(!callback){callback=++next_callback_;++callback_count_;return callback;}
    poisoned_=true;return 0;
  }
  bool CallbackReturned(uint64_t receipt)noexcept{
    if(!receipt)return false;
    for(auto& callback:callbacks_)if(callback==receipt){callback=0;--callback_count_;return true;}
    return false;
  }
  void Unsupported()noexcept{poisoned_=true;}
  Snapshot Inspect()const noexcept{return {channel_count_,queued_,submitted_,callback_count_,dispatches_,accepted_,replied_,refused_,unwound_,not_dispatched_,channel_closed_,poisoned_};}
  bool ReceiptLaneReady()const noexcept{return !poisoned_&&!queued_&&!submitted_&&!callback_count_;}
  // Scoped observation only; it neither retires rows nor relaxes global idle.
  // Callback frames lack exact per-frame origins, so an executing real native
  // Inspector callback conservatively blocks every handler observation.
  bool LocatorHandlerReady(uint64_t invocation)const noexcept{
    if(!invocation||poisoned_||callback_count_)return false;
    for(const auto& row:pending_)if(row.active&&row.origin.locator_handler_invocation==invocation)return false;
    return true;
  }
 private:
  struct Record {Ticket ticket;NativeWorkOrigin origin;std::array<char,kSessionBytes> session{};size_t session_size=0;bool active=false,submitted=false,returned=false,replied=false,closed=false;};
  bool Current(Channel channel)const noexcept{for(const auto& value:channels_)if(value&&*value==channel)return true;return false;}
  Record* Find(const Ticket& ticket)noexcept{for(auto& row:pending_)if(row.active&&row.ticket==ticket)return &row;return nullptr;}
  std::optional<Channel> PoisonChannel()noexcept{poisoned_=true;return std::nullopt;}
  std::optional<Ticket> PoisonTicket()noexcept{poisoned_=true;return std::nullopt;}
  std::array<std::optional<Channel>,kChannels> channels_{};std::array<Record,kPending> pending_{};
  std::array<uint64_t,kCallbacks> callbacks_{};uint64_t next_callback_=0;size_t callback_count_=0;
  uint64_t next_channel_=0,next_ticket_=0,dispatches_=0,accepted_=0,replied_=0,refused_=0,unwound_=0,not_dispatched_=0,channel_closed_=0;size_t channel_count_=0,queued_=0,submitted_=0;bool poisoned_=false;
};
} // namespace suitecut
