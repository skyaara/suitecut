// SPDX-License-Identifier: MIT
#pragma once
#include "accepted_stream_work.h"
#include "accepted_work_origin.h"
#include <memory>
#include <exception>
#include <utility>
namespace suitecut {
// Move-only native payload ownership: an omitted success/drop boundary poisons
// future barrier authority rather than silently releasing an unwritten packet.
class AcceptedPacketReceipt {
 public:
  AcceptedPacketReceipt()=default;
  AcceptedPacketReceipt(std::shared_ptr<AcceptedWriterWork> work,AcceptedWriterWork::Kind kind,size_t bytes):work_(std::move(work)){
    if(work_)ticket_=work_->Admit(kind,bytes);
  }
  AcceptedPacketReceipt(const AcceptedPacketReceipt&)=delete;
  AcceptedPacketReceipt& operator=(const AcceptedPacketReceipt&)=delete;
  AcceptedPacketReceipt(AcceptedPacketReceipt&& other)noexcept:work_(std::move(other.work_)),ticket_(std::exchange(other.ticket_,std::nullopt)){}
  AcceptedPacketReceipt& operator=(AcceptedPacketReceipt&& other)noexcept{
    if(this!=&other){Abandoned();work_=std::move(other.work_);ticket_=std::exchange(other.ticket_,std::nullopt);}return *this;
  }
  ~AcceptedPacketReceipt(){Abandoned();}
  void Dequeued()noexcept{if(work_&&ticket_&&!work_->Dequeue(*ticket_))work_->Unsupported();}
  void Wrote(size_t actual_bytes)noexcept{if(work_&&ticket_&&!work_->Wrote(*ticket_,actual_bytes))work_->Unsupported();}
  bool FullWrite()noexcept{
    if(!work_||!ticket_)return false;
    if(!work_->FullWrite(*ticket_)){work_->Unsupported();return false;}
    ticket_.reset();return true;
  }
  void Dropped(AcceptedWriterWork::Discard reason)noexcept{
    if(work_&&ticket_){if(!work_->Dropped(*ticket_,reason))work_->Unsupported();else ticket_.reset();}
  }
  void Failed()noexcept{if(work_)work_->WriteFailed();}
  std::optional<NativeStreamTicket> Ticket()const noexcept{return ticket_;}
 private:
  void Abandoned()noexcept{
    if(work_&&ticket_){work_->AdmissionUnwound(*ticket_);ticket_.reset();}
  }
  std::shared_ptr<AcceptedWriterWork> work_;std::optional<NativeStreamTicket> ticket_;
};
// Retain the real UI execution stack even if Engine::Route throws. The task's
// caller owns PostingReturned independently; reentrant entry is supported.
class AcceptedRouteFrame {
 public:
  AcceptedRouteFrame(std::shared_ptr<AcceptedReaderWork> work,std::optional<NativeStreamTicket> ticket,uint64_t actual_source_lease=0)noexcept:origin_(ticket?NativeWorkOrigin::Command(actual_source_lease,*ticket):NativeWorkOrigin{}),work_(std::move(work)),ticket_(ticket),unwinding_(std::uncaught_exceptions()){
    if(work_&&ticket_&&!work_->Enter(*ticket_))work_->Unsupported();
  }
  AcceptedRouteFrame(const AcceptedRouteFrame&)=delete;AcceptedRouteFrame& operator=(const AcceptedRouteFrame&)=delete;
  ~AcceptedRouteFrame(){if(work_&&ticket_&&!work_->Returned(*ticket_,std::uncaught_exceptions()>unwinding_))work_->Unsupported();}
 private:
  NativeWorkOriginFrame origin_;
  std::shared_ptr<AcceptedReaderWork> work_;std::optional<NativeStreamTicket> ticket_;int unwinding_;
};
} // namespace suitecut
namespace suitecut {
class AcceptedPostReceipt {
 public:
  explicit AcceptedPostReceipt(std::shared_ptr<AcceptedReaderWork> work):work_(std::move(work)){
    if(work_)ticket_=work_->Admit();
  }
  AcceptedPostReceipt(const AcceptedPostReceipt&)=delete;AcceptedPostReceipt& operator=(const AcceptedPostReceipt&)=delete;
  ~AcceptedPostReceipt(){if(work_&&ticket_&&!returned_){if(attempted_)work_->PostingUnwound(*ticket_);else work_->NeverPosted(*ticket_);}}
  std::optional<NativeStreamTicket> Ticket()const noexcept{return ticket_;}
  void AboutToPost()noexcept{attempted_=true;}
  void Returned(bool actual)noexcept{
    if(work_&&ticket_&&!work_->PostingReturned(*ticket_,actual))work_->Unsupported();
    returned_=true;
  }
 private:
  std::shared_ptr<AcceptedReaderWork> work_;std::optional<NativeStreamTicket> ticket_;bool attempted_=false,returned_=false;
};
} // namespace suitecut
