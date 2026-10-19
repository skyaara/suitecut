// SPDX-License-Identifier: MIT
#pragma once
#include "retained_source_endpoint.h"
namespace suitecut::runner {
// Owns one native registration channel and one retained pidfd per actual SDK
// host. This does not create a waitpid owner or grant browser-child reuse.
class RetainedSourceReceiver {
 public:
  using Policy=RetainedSourceLeases;using Native=RetainedSourceProcess;
  struct WorkAuthority {
    RetainedSourceWorkWire::Challenge challenge;
    std::optional<RetainedSourceWorkWire::Cuts> cuts;
    bool observed=false,acknowledged=false,canceled=false,late_acknowledged=false;
    uint64_t refusal=0,resume_generation=0;bool resumed=false;
    double observed_at=0,acknowledged_at=0,resumed_at=0;
  };
  struct ShutdownBoundary {Policy::Ticket ticket;Policy::Process process;int64_t current_parent=0;unsigned planned_code=0;};
  explicit RetainedSourceReceiver(Policy& policy):policy_(policy){
    int pair[2]{-1,-1};
    if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair))throw std::runtime_error("Native Source receiver channel creation failed");
    try{
      Native::ReceiveCredentials(pair[0]);
      receiver_=Duplicate(pair[0]);peer_=Duplicate(pair[1]);cookie_=RetainedSourceEndpoint::SocketCookie(peer_);
    }catch(...){close(pair[0]);close(pair[1]);Close();throw;}
    close(pair[0]);close(pair[1]);
  }
  ~RetainedSourceReceiver(){Close();} // Descriptor release is NOT policy retirement.
  RetainedSourceReceiver(const RetainedSourceReceiver&)=delete;RetainedSourceReceiver& operator=(const RetainedSourceReceiver&)=delete;
  int PeerDescriptor()const noexcept{return peer_;}uint64_t PeerCookie()const noexcept{return cookie_;}
  // The caller maps PeerDescriptor into the real SDK child before this method.
  // Its existing host controller remains the only direct-host waitpid owner.
  void HostLaunched(const Policy::Host& host){
    if(host_||peer_<0||!host.lease||!host.worker)throw std::runtime_error("Native Source receiver host already bound/invalid");
    const auto actual=Native::ReadStat(host.pid);
    if(actual.parent!=getpid()||actual.start!=host.start)throw std::runtime_error("Native Source receiver host is not this owned child incarnation");
    host_=host;close(peer_);peer_=-1;
  }
  std::optional<Policy::Ticket> Reserve(const Policy::Fixture& fixture,Policy::Executable executable,double deadline,double now){
    if(!host_||ticket_||eof_)return std::nullopt; // First slice: one helper per host endpoint.
    const auto reserved=policy_.Reserve({*host_,fixture},executable,deadline,now);
    if(reserved){ticket_=reserved;executable_=executable;}
    return reserved;
  }
  // Bounded, nonblocking and allocation-free storage. Observe all queued
  // shutdown/startup evidence before accepting channel drain at real host exit.
  void Tick(double now){
    policy_.Expire(now);
    if(!eof_)for(unsigned n=0;n<4;++n){
      const auto receipt=Native::ReceiveReceipt(receiver_);if(!receipt)break;
      if(receipt->eof){eof_=true;close(receiver_);receiver_=-1;break;}
      if(!ticket_)throw std::runtime_error("Native Source receipt without native fixture admission");
      if(!process_){
        const auto startup=Native::DecodeStartup(*receipt,*ticket_,executable_);
        // The temporary verification handle in DecodeStartup is not our owned
        // lease. Re-observe the same incarnation around the retained pidfd.
        process_handle_.emplace(startup.process.pid);
        if(process_handle_->Exited()||Native::Observe(startup.process.pid)!=startup.process||!policy_.Started(startup,now))throw std::runtime_error("Native Source receiver startup ownership rejected");
        process_=startup.process;
      }else{
        if(receipt->Packet().starts_with("SOURCE_WORK_")){ObserveWork(*receipt,now);continue;}
        const auto boundary=DecodeBoundary(*receipt);
        if(shutdown_)throw std::runtime_error("Native Source shutdown boundary replayed");
        shutdown_=boundary; // Observational only. Never ObserveClose/Idle here.
      }
    }
    if(process_&&!exit_observed_&&process_handle_->Exited()){
      if(!policy_.ObservedExit(*ticket_,*process_,now))throw std::runtime_error("Native Source exit incarnation rejected");
      exit_observed_=true;
    }
  }
  // Native bridge negotiation is explicit. An older SDK/Runner pair never
  // sends INSTALL, so the registered Source emits no new protocol packets.
  bool InstallAcceptedWork(){
    if(work_install_||!AdmissionActive()||eof_||!process_handle_||process_handle_->Exited())return false;
    SendWork(RetainedSourceWorkWire::Install(ticket_->id));work_install_=true;return true;
  }
  bool AcceptedWorkInstalled()const noexcept{return work_capability_;}
  bool AcceptedWorkRequested()const noexcept{return work_install_;}
  bool BeginAcceptedWork(const RetainedSourceWorkWire::Borrow& borrow,const RetainedSourceWorkWire::Challenge& challenge,double now){
    if(!work_capability_||work_||!AdmissionActive()||eof_||!host_||challenge.ticket!=ticket_->id||
       borrow.pid!=host_->pid||borrow.start!=host_->start||borrow.worker!=host_->worker||
       challenge.borrow_lease!=borrow.lease||challenge.borrow_generation!=borrow.generation||challenge.nonce<=work_nonce_||now>=challenge.deadline)return false;
    auto encoded=RetainedSourceWorkWire::Encode(challenge);
    // Store the exact owned challenge before real send. A send exception cannot
    // drop live Source ownership or transform uncertain delivery into success.
    work_.emplace();work_->challenge=challenge;work_nonce_=challenge.nonce;
    SendWork(encoded);return true;
  }
  bool ObserveOriginalDecoder(const RetainedSourceWorkWire::Cuts& actual,double now){
    if(!work_||!work_->cuts||*work_->cuts!=actual||work_->observed||work_->refusal)return false;
    // Late exact original-decoder observation is still an actual receipt, but
    // cancellation/expiry/death cannot admit a new successful Source handshake.
    work_->observed=true;work_->observed_at=now;
    if(!work_->canceled&&now<work_->challenge.deadline&&AdmissionActive())SendWork(RetainedSourceWorkWire::Encode(RetainedSourceWorkWire::Kind::Observed,actual));
    return true;
  }
  bool ResumeAcceptedWork(uint64_t generation,double now){
    if(!AcceptedWorkReady(now)||generation<=work_->challenge.borrow_generation)return false;
    const auto resume=RetainedSourceWorkWire::Resume{ticket_->id,work_->challenge.nonce,generation};
    if(work_->resume_generation)return false;
    work_->resume_generation=generation;SendWork(RetainedSourceWorkWire::Encode(resume));return true;
  }
  void CancelAcceptedWork()noexcept{if(work_)work_->canceled=true;}
  bool AcceptedWorkReady(double now)const noexcept{
    return work_capability_&&work_&&work_->cuts&&work_->observed&&work_->acknowledged&&
      !work_->canceled&&!work_->refusal&&std::isfinite(now)&&AdmissionActive();
  }
  bool FinishAcceptedResume(uint64_t generation){
    if(!work_||!work_->resumed||work_->resume_generation!=generation||work_->canceled||!AdmissionActive())return false;
    work_.reset();return true;
  }
  const std::optional<WorkAuthority>& Work()const noexcept{return work_;}
  template<class Visitor>void ForEachPollDescriptor(Visitor&& visitor)const{
    if(receiver_>=0)visitor(pollfd{receiver_,POLLIN,0});
    if(process_handle_&&!exit_observed_)visitor(pollfd{process_handle_->Descriptor(),POLLIN,0});
  }
  // Call only with the existing actual host waitpid + shared adopted-descendant
  // batch receipt. A readable pidfd and shutdown label are never substitutes.
  bool Retire(const Policy::HostRetirement& receipt,double now){
    if(!ticket_||retired_||!host_||receipt.host!=*host_||!eof_||!receipt.channel_drained)return false;
    if(!policy_.Retire(*ticket_,receipt,now))return false;
    retired_=true;return true;
  }
  const std::optional<Policy::Ticket>& Registration()const noexcept{return ticket_;}
  const std::optional<Policy::Process>& ProcessIdentity()const noexcept{return process_;}
  bool ChannelDrained()const noexcept{return eof_;}
  bool StartupObserved()const noexcept{return process_.has_value();}
  bool AdmissionActive()const noexcept{if(!ticket_||!process_||exit_observed_)return false;const auto rows=policy_.Records();return ticket_->id<=rows.size()&&rows[ticket_->id-1].ticket==*ticket_&&rows[ticket_->id-1].state==Policy::State::Active;}
  bool ExitObserved()const noexcept{return exit_observed_;}
  bool Retired()const noexcept{return retired_;}
  const std::optional<ShutdownBoundary>& Boundary()const noexcept{return shutdown_;}
  // Work receipt support is not a CEF cohort/kernel membership grant. Unknown
  // children still fail closed until the separate real cohort attestation binds.
  bool BrowserReuseSupported()const noexcept{return false;}
 private:
  void SendWork(const std::string& packet){
    if(receiver_<0||eof_||!process_||!process_handle_||process_handle_->Exited())throw std::runtime_error("Native Source work has no live actual channel owner");
    ssize_t result;do{result=send(receiver_,packet.data(),packet.size(),MSG_DONTWAIT|MSG_NOSIGNAL);}while(result<0&&errno==EINTR);
    if(result!=static_cast<ssize_t>(packet.size()))throw std::runtime_error("Native Source work packet delivery failed");
  }
  void ObserveWork(const Native::Receipt& receipt,double now){
    using Wire=RetainedSourceWorkWire;
    if(!work_install_||!ticket_||!process_||receipt.sender!=process_->pid)throw std::runtime_error("Native Source work receipt lacks native install/actual process credentials");
    const auto message=Wire::Decode(receipt.Packet());
    if(message.kind==Wire::Kind::Capability){
      if(work_capability_||message.capability.ticket!=ticket_->id||message.capability.pid!=process_->pid||message.capability.start!=process_->start||!process_handle_||process_handle_->Exited())throw std::runtime_error("Native Source work capability incarnation invalid/replayed");
      work_capability_=true;return;
    }
    if(!work_capability_||!work_)throw std::runtime_error("Native Source work receipt has no actual issued challenge");
    if(message.kind==Wire::Kind::Resumed){
      if(message.resume.ticket!=ticket_->id||message.resume.nonce!=work_->challenge.nonce||
         !work_->resume_generation||message.resume.generation!=work_->resume_generation||work_->resumed||!work_->acknowledged)throw std::runtime_error("Native Source resumed borrow receipt foreign/replayed");
      work_->resumed=true;work_->resumed_at=now;return;
    }
    if(message.kind==Wire::Kind::Unsupported){
      if(message.ticket!=ticket_->id||message.nonce!=work_->challenge.nonce||work_->refusal||work_->acknowledged)throw std::runtime_error("Native Source work refusal foreign/replayed");
      work_->refusal=message.reason;return;
    }
    if(message.kind!=Wire::Kind::Cut&&message.kind!=Wire::Kind::Acknowledged)throw std::runtime_error("Native Source work receipt direction invalid");
    if(message.cuts.ticket!=ticket_->id||message.cuts.nonce!=work_->challenge.nonce||work_->refusal)throw std::runtime_error("Native Source work cut has a foreign challenge");
    if(message.kind==Wire::Kind::Cut){
      if(work_->cuts)throw std::runtime_error("Native Source work cutoff replayed");
      work_->cuts=message.cuts;return;
    }
    if(!work_->cuts||*work_->cuts!=message.cuts||!work_->observed||work_->acknowledged||work_->late_acknowledged)throw std::runtime_error("Native Source work acknowledgement lacks exact actual decoder observation");
    // A late genuine ACK remains observable during Stop/physical close without
    // authorizing idle, transfer, or a manufactured process-retirement receipt.
    if(work_->canceled||now>=work_->challenge.deadline||!AdmissionActive()||process_handle_->Exited())work_->late_acknowledged=true;
    else {work_->acknowledged=true;work_->acknowledged_at=now;}
  }
  static int Duplicate(int fd){const int result=fcntl(fd,F_DUPFD_CLOEXEC,16);if(result<0)throw std::runtime_error("Native Source receiver descriptor reservation failed");return result;}
  static uint64_t Number(std::string_view token){uint64_t result=0;if(token.empty()||(token.size()>1&&token[0]=='0'))throw std::runtime_error("Native Source shutdown number noncanonical");const auto parsed=std::from_chars(token.data(),token.data()+token.size(),result);if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size())throw std::runtime_error("Native Source shutdown number malformed");return result;}
  ShutdownBoundary DecodeBoundary(const Native::Receipt& receipt)const{
    constexpr std::string_view prefix="SOURCE_SHUTDOWN_REACHED 1 ";const auto packet=receipt.Packet();
    if(!packet.starts_with(prefix)||packet.back()!='\n'||receipt.sender!=process_->pid)throw std::runtime_error("Native Source shutdown sender/packet invalid");
    auto tail=packet.substr(prefix.size(),packet.size()-prefix.size()-1);std::array<uint64_t,6> fields{};
    for(size_t i=0;i<fields.size();++i){const auto end=tail.find(' ');fields[i]=Number(tail.substr(0,end));if((i+1==fields.size())!=(end==tail.npos))throw std::runtime_error("Native Source shutdown arity invalid");if(end!=tail.npos)tail.remove_prefix(end+1);}
    if(fields[0]!=ticket_->id||fields[1]!=static_cast<uint64_t>(process_->pid)||fields[2]!=static_cast<uint64_t>(process_->parent)||fields[3]!=process_->start||!fields[4]||fields[4]>INT32_MAX||fields[5]>255)throw std::runtime_error("Native Source shutdown identity invalid");
    return {*ticket_,*process_,static_cast<int64_t>(fields[4]),static_cast<unsigned>(fields[5])};
  }
  void Close()noexcept{if(receiver_>=0){close(receiver_);receiver_=-1;}if(peer_>=0){close(peer_);peer_=-1;}}
  Policy& policy_;int receiver_=-1,peer_=-1;uint64_t cookie_=0;std::optional<Policy::Host> host_;std::optional<Policy::Ticket> ticket_;Policy::Executable executable_{};
  bool work_install_=false,work_capability_=false;uint64_t work_nonce_=0;std::optional<WorkAuthority> work_;
  std::optional<Native::PidHandle> process_handle_;std::optional<Policy::Process> process_;std::optional<ShutdownBoundary> shutdown_;bool eof_=false,exit_observed_=false,retired_=false;
};
} // namespace suitecut::runner
