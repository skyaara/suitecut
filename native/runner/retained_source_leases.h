// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace suitecut::runner {
// Bounded receipt policy only. The native adapter supplies verified kernel and
// Source-channel receipts. This controller never kills/reaps, guesses process
// completion, resets a global deadline, or makes a living PID physically zero.
class RetainedSourceLeases {
 public:
  static constexpr size_t kLive=8,kHistory=256,kAccepted=64;
  struct Host {uint64_t lease=0;unsigned worker=0;int64_t pid=0;uint64_t start=0;bool operator==(const Host&)const=default;};
  enum class SetupScope {Test,BeforeAll,AfterAll};
  struct Fixture {uint64_t catalog=0,declaration=0,token=0,test=0,generation=0;SetupScope setup_scope=SetupScope::Test;uint64_t callback_owner=0;bool operator==(const Fixture&)const=default;};
  struct Owner {Host host;Fixture fixture;bool operator==(const Owner&)const=default;};
  struct Executable {uint64_t device=0,inode=0;bool operator==(const Executable&)const=default;};
  struct Process {int64_t pid=0,parent=0;uint64_t start=0;Executable executable;bool operator==(const Process&)const=default;};
  struct Ticket {uint64_t id=0;Owner owner;bool operator==(const Ticket&)const=default;};
  // Host comparison includes the kernel start identity; executable equality is
  // an additional constraint, never a replacement for parent/fixture ownership.
  struct Startup {Ticket ticket;uint64_t emission=0;Process process;Host observed_parent;int64_t credential_pid=0;};
  struct Request {Ticket ticket;uint64_t nonce=0;double started=0,deadline=0;};
  struct IdleReceipt {Ticket ticket;uint64_t emission=0,nonce=0,accepted=0;};
  struct CloseReceipt {Ticket ticket;uint64_t emission=0,nonce=0,accepted=0;};
  // Supplied only from the one existing actual host waitpid/descendant domain.
  // channel_drained includes already-queued authenticated startup receipts.
  struct HostRetirement {Host host;uint64_t batch=0;bool actual_waitpid=false,channel_drained=false,descendants_retired=false;};
  enum class State {Reserved,Active,AwaitingIdle,Idle,Closing,Exited,Retired};
  struct Record {
    Ticket ticket;Executable executable;State state=State::Reserved;
    std::optional<Process> process;double startup_deadline=0,last_observed=0;
    uint64_t emission=0,accepted=0;std::array<uint64_t,kAccepted> work{};size_t pending=0;
    std::optional<Request> idle,close;bool idle_ack=false,canceled_idle_ack=false,close_ack=false,exit_observed=false;
    uint64_t retired_batch=0;
  };
  explicit RetainedSourceLeases(std::optional<double> original_global_deadline=std::nullopt):global_(original_global_deadline){
    if(global_&&(!std::isfinite(*global_)||*global_<=0))throw std::invalid_argument("Invalid retained Source original global deadline");
  }
  std::optional<Ticket> Reserve(const Owner& owner,Executable executable,double deadline,double now)noexcept{
    if(stopped_||count_==kHistory||outstanding_==kLive||!Valid(owner)||!executable.inode||!Time(now)||!Time(deadline)||deadline<=now||(global_&&deadline>*global_))return std::nullopt;
    for(size_t i=0;i<count_;++i){const auto& row=records_[i];if((row.retired_batch&&row.ticket.owner.host==owner.host)||(row.state!=State::Retired&&row.ticket.owner.host.pid==owner.host.pid&&row.ticket.owner.host!=owner.host))return std::nullopt;}
    const Ticket ticket{count_+1,owner};auto& row=records_[count_++];row.ticket=ticket;row.executable=executable;row.startup_deadline=deadline;row.last_observed=now;++outstanding_;return ticket;
  }
  bool Started(const Startup& receipt,double now)noexcept{
    auto* row=Find(receipt.ticket);if(!row||row->process||row->state==State::Retired||!At(*row,now)||receipt.emission!=1||
      receipt.observed_parent!=row->ticket.owner.host||receipt.credential_pid!=receipt.process.pid||!Valid(receipt.process)||
      receipt.process.parent!=row->ticket.owner.host.pid||receipt.process.start<row->ticket.owner.host.start||receipt.process.executable!=row->executable)return false;
    for(size_t i=0;i<count_;++i)if(records_[i].state!=State::Retired&&records_[i].process&&records_[i].process->pid==receipt.process.pid)return false;
    row->process=receipt.process;row->emission=1;row->last_observed=now;++live_;
    row->state=stopped_||row->state==State::Closing||now>=row->startup_deadline?State::Closing:State::Active;return true;
  }
  // Every actual Source accepted operation must enter this bounded ledger
  // before its backend admission. A timeout/request cancellation never retires it.
  bool Accepted(const Ticket& ticket,uint64_t emission,uint64_t operation,double now)noexcept{
    auto* row=Next(ticket,emission,now);if(!row||row->accepted==UINT64_MAX||operation!=row->accepted+1||row->pending==kAccepted)return false;
    const auto slot=std::find(row->work.begin(),row->work.end(),0);if(slot==row->work.end())return false;
    *slot=operation;++row->pending;row->accepted=operation;Commit(*row,emission,now);if(row->state==State::Idle)row->state=State::Active;return true;
  }
  bool OperationRetired(const Ticket& ticket,uint64_t emission,uint64_t operation,double now)noexcept{
    auto* row=Next(ticket,emission,now);if(!row||!operation)return false;const auto found=std::find(row->work.begin(),row->work.end(),operation);
    if(found==row->work.end())return false;
    *found=0;--row->pending;Commit(*row,emission,now);return true;
  }
  std::optional<Request> RequestIdle(const Ticket& ticket,double deadline,double now)noexcept{
    auto* row=Find(ticket);if(stopped_||!row||row->state!=State::Active||!At(*row,now)||row->pending||!Bounded(deadline,now)||nonce_==UINT64_MAX)return std::nullopt;
    row->idle=Request{ticket,++nonce_,now,deadline};row->idle_ack=row->canceled_idle_ack=false;row->last_observed=now;row->state=State::AwaitingIdle;return row->idle;
  }
  bool ObserveIdle(const IdleReceipt& receipt,double now)noexcept{
    auto* row=Next(receipt.ticket,receipt.emission,now);if(!row||!row->idle||row->idle->nonce!=receipt.nonce||row->idle_ack||row->canceled_idle_ack||receipt.accepted!=row->accepted)return false;
    if(row->state==State::Closing){row->canceled_idle_ack=true;Commit(*row,receipt.emission,now);return true;}
    if(row->state!=State::AwaitingIdle||now>=row->idle->deadline||row->pending)return false;
    row->idle_ack=true;row->state=State::Idle;Commit(*row,receipt.emission,now);return true;
  }
  std::optional<Request> RequestClose(const Ticket& ticket,double deadline,double now)noexcept{
    auto* row=Find(ticket);if(!row||!row->process||row->state==State::Exited||row->state==State::Retired||row->close||!At(*row,now)||!CleanupBounded(deadline,now)||nonce_==UINT64_MAX)return std::nullopt;
    row->close=Request{ticket,++nonce_,now,deadline};row->state=State::Closing;row->last_observed=now;return row->close;
  }
  bool ObserveClose(const CloseReceipt& receipt,double now)noexcept{
    auto* row=Next(receipt.ticket,receipt.emission,now);if(!row||row->state!=State::Closing||!row->close||row->close->nonce!=receipt.nonce||row->close_ack||row->pending||receipt.accepted!=row->accepted)return false;
    row->close_ack=true;Commit(*row,receipt.emission,now);return true;
  }
  // A native pidfd exit observation identifies this same process incarnation.
  // It is an exit receipt, not a successful close/work receipt or waitpid owner.
  bool ObservedExit(const Ticket& ticket,const Process& process,double now)noexcept{
    auto* row=Find(ticket);if(!row||!row->process||*row->process!=process||row->exit_observed||row->state==State::Retired||!At(*row,now))return false;
    row->exit_observed=true;row->state=State::Exited;row->last_observed=now;return true;
  }
  bool Retire(const Ticket& ticket,const HostRetirement& receipt,double now)noexcept{
    auto* row=Find(ticket);if(!row||row->state==State::Retired||receipt.host!=ticket.owner.host||!receipt.batch||!receipt.actual_waitpid||!receipt.channel_drained||!receipt.descendants_retired||!At(*row,now)||(row->process&&!row->exit_observed))return false;
    // Physical death invalidates pending work; it never fabricates settled ACKs.
    if(row->process)--live_;
    row->state=State::Retired;row->retired_batch=receipt.batch;row->last_observed=now;--outstanding_;return true;
  }
  // Cancellation admission closes author startup authority, never physical
  // ownership. Late authentic startup remains observable for cleanup only.
  bool CancelStartup(const Ticket& ticket,double now)noexcept{
    auto* row=Find(ticket);if(!row||row->state==State::Retired||row->state==State::Exited||!At(*row,now))return false;
    row->state=State::Closing;row->last_observed=now;return true;
  }
  void Stop(double now)noexcept{if(!Time(now))return;stopped_=true;for(size_t i=0;i<count_;++i)if(records_[i].state!=State::Retired&&records_[i].state!=State::Exited&&now>=records_[i].last_observed){records_[i].state=State::Closing;records_[i].last_observed=now;}}
  void Expire(double now)noexcept{
    if(!Time(now))return;
    for(size_t i=0;i<count_;++i){auto& row=records_[i];if(now<row.last_observed)continue;
      if((row.state==State::Reserved&&now>=row.startup_deadline)||(row.state==State::AwaitingIdle&&now>=row.idle->deadline)||(global_&&now>=*global_&&row.state!=State::Retired&&row.state!=State::Exited)){row.state=State::Closing;row.last_observed=now;}}
  }
  bool IdleReady(const Ticket& ticket)const noexcept{const auto* row=Find(ticket);return !stopped_&&row&&row->state==State::Idle&&row->idle_ack&&!row->pending&&row->process&&!row->exit_observed;}
  size_t Outstanding()const noexcept{return outstanding_;}size_t LiveChildren()const noexcept{return live_;}
  std::span<const Record> Records()const noexcept{return {records_.data(),count_};}
 private:
  static bool Time(double now)noexcept{return std::isfinite(now)&&now>=0;}
  static bool Valid(const Owner& o)noexcept{return o.host.lease&&o.host.worker&&o.host.pid>0&&o.host.pid<=INT32_MAX&&o.host.start&&o.fixture.catalog&&o.fixture.declaration&&o.fixture.token&&o.fixture.generation&&
      (o.fixture.setup_scope==SetupScope::Test?o.fixture.test!=0:!o.fixture.test&&o.fixture.callback_owner&&(o.fixture.setup_scope==SetupScope::BeforeAll||o.fixture.setup_scope==SetupScope::AfterAll));}
  static bool Valid(const Process& p)noexcept{return p.pid>0&&p.pid<=INT32_MAX&&p.parent>0&&p.parent<=INT32_MAX&&p.pid!=p.parent&&p.start&&p.executable.inode;}
  static bool At(const Record& row,double now)noexcept{return Time(now)&&now>=row.last_observed;}
  static bool CleanupBounded(double end,double now)noexcept{return Time(end)&&end>now&&end<=now+1000;}
  bool Bounded(double end,double now)const noexcept{return CleanupBounded(end,now)&&(!global_||end<=*global_);}
  Record* Find(const Ticket& ticket)noexcept{return ticket.id&&ticket.id<=count_&&records_[ticket.id-1].ticket==ticket?&records_[ticket.id-1]:nullptr;}
  const Record* Find(const Ticket& ticket)const noexcept{return ticket.id&&ticket.id<=count_&&records_[ticket.id-1].ticket==ticket?&records_[ticket.id-1]:nullptr;}
  Record* Next(const Ticket& ticket,uint64_t emission,double now)noexcept{auto* row=Find(ticket);return row&&row->process&&!row->exit_observed&&row->state!=State::Retired&&At(*row,now)&&row->emission!=UINT64_MAX&&emission==row->emission+1?row:nullptr;}
  static void Commit(Record& row,uint64_t emission,double now)noexcept{row.emission=emission;row.last_observed=now;}
  std::array<Record,kHistory> records_{};size_t count_=0,outstanding_=0,live_=0;uint64_t nonce_=0;std::optional<double> global_;bool stopped_=false;
};
} // namespace suitecut::runner
