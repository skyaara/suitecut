// SPDX-License-Identifier: MIT
#pragma once
#include "retained_source_process.h"
#include "retained_source_work_wire.h"
#include <fcntl.h>
#include <string_view>
#include <type_traits>
namespace suitecut::runner {
// One endpoint per native browser helper PROCESS, not per Source page. This
// staged transport has no idle, accepted-work-zero or physical-retire API.
class RetainedSourceEndpoint {
 public:
  struct Config {int fd=-1;uint64_t ticket=0,cookie=0;bool enabled=false;};
  static uint64_t SocketCookie(int fd){
    int type=0;socklen_t size=sizeof(type);sockaddr_storage address{};socklen_t address_size=sizeof(address);uint64_t cookie=0;socklen_t cookie_size=sizeof(cookie);
    if(fd<3||getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size)||size!=sizeof(type)||type!=SOCK_SEQPACKET||getsockname(fd,reinterpret_cast<sockaddr*>(&address),&address_size)||address.ss_family!=AF_UNIX||getsockopt(fd,SOL_SOCKET,SO_COOKIE,&cookie,&cookie_size)||cookie_size!=sizeof(cookie)||!cookie)throw std::runtime_error("Native process registration endpoint identity invalid");
    return cookie;
  }
  // This helper returns a trivial value before CefExecuteProcess. No owning
  // object or buffer need span the zygote dispatch wrapper. Subprocess flags
  // cannot justify closing an unrelated descriptor that reused the same number.
  static Config Prepare(int argc,char*const argv[]){
    bool subprocess=false,bad=false;std::optional<uint64_t> fd,ticket,cookie;size_t flags=0;
    for(int i=1;i<argc;++i){const std::string_view arg=argv[i];if(arg=="--type"||arg.starts_with("--type="))subprocess=true;
      const auto read=[&](std::string_view prefix,std::optional<uint64_t>& destination){if(arg==prefix.substr(0,prefix.size()-1)){bad=true;++flags;return;}if(!arg.starts_with(prefix))return;const auto number=Number(arg.substr(prefix.size()));if(destination||!number)bad=true;destination=number;++flags;};
      read("--suitecut-runner-source-fd=",fd);read("--suitecut-runner-source-ticket=",ticket);read("--suitecut-runner-source-cookie=",cookie);
    }
    if(!flags)return {};
    const bool valid=!bad&&flags==3&&fd&&*fd>=3&&*fd<=INT32_MAX&&ticket&&*ticket&&cookie&&*cookie;
    if(subprocess){
      if(valid){const int descriptor=static_cast<int>(*fd);int type=0;uint64_t actual=0;socklen_t n=sizeof(type),m=sizeof(actual);sockaddr_storage address{};socklen_t address_size=sizeof(address);
        if(!getsockopt(descriptor,SOL_SOCKET,SO_TYPE,&type,&n)&&type==SOCK_SEQPACKET&&!getsockname(descriptor,reinterpret_cast<sockaddr*>(&address),&address_size)&&address.ss_family==AF_UNIX&&!getsockopt(descriptor,SOL_SOCKET,SO_COOKIE,&actual,&m)&&m==sizeof(actual)&&actual==*cookie)close(descriptor);
      }
      return {};
    }
    if(!valid)throw std::runtime_error("Native process registration configuration invalid");
    const int descriptor=static_cast<int>(*fd);if(SocketCookie(descriptor)!=*cookie)throw std::runtime_error("Native process registration socket incarnation mismatch");
    const int old=fcntl(descriptor,F_GETFD);if(old<0||fcntl(descriptor,F_SETFD,old|FD_CLOEXEC))throw std::runtime_error("Native process registration CLOEXEC setup failed");
    return {descriptor,*ticket,*cookie,true};
  }
  explicit RetainedSourceEndpoint(Config config):config_(config){
    if(!config_.enabled)return;
    try{
      const int flags=fcntl(config_.fd,F_GETFD);
      if(SocketCookie(config_.fd)!=config_.cookie||flags<0||!(flags&FD_CLOEXEC))throw std::runtime_error("Native process registration was not prepared before CEF launch");
      RetainedSourceProcess::ReceiveCredentials(config_.fd);
      process_=RetainedSourceProcess::Observe(getpid());
    }catch(...){CloseOwned();throw;}
  }
  ~RetainedSourceEndpoint(){CloseOwned();}
  RetainedSourceEndpoint(const RetainedSourceEndpoint&)=delete;RetainedSourceEndpoint& operator=(const RetainedSourceEndpoint&)=delete;
  uint64_t Ticket()const noexcept{return Enabled()?config_.ticket:0;}
  // Install only with the actual native Engine work authority. Existing startup
  // registration remains a distinct protocol and cannot imply this capability.
  int WorkDescriptor()const noexcept{return work_enabled_?config_.fd:-1;}
  bool PrepareAcceptedWork(){
    if(!Enabled()||!startup_||shutdown_||work_enabled_||!process_)return false;
    if(SocketCookie(config_.fd)!=config_.cookie)throw std::runtime_error("Native Source work endpoint incarnation changed");
    struct ucred peer{};socklen_t size=sizeof(peer);
    if(getsockopt(config_.fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)||size!=sizeof(peer)||peer.pid<=0||peer.uid!=geteuid())throw std::runtime_error("Native Source work peer credentials unavailable");
    const auto host=RetainedSourceProcess::ReadStat(process_->parent),owner=RetainedSourceProcess::ReadStat(peer.pid);
    if(host.parent!=peer.pid||host.state=='Z'||host.state=='X'||owner.state=='Z'||owner.state=='X')throw std::runtime_error("Native Source work peer is not its actual SDK-host owner");
    RetainedSourceProcess::ReceiveCredentials(config_.fd);work_owner_=owner;work_enabled_=true;
    return true;
  }
  bool WriteAcceptedCapability(){return work_enabled_&&!shutdown_&&Send(RetainedSourceWorkWire::Encode(RetainedSourceWorkWire::Capability{config_.ticket,process_->pid,process_->start}));}
  std::optional<RetainedSourceWorkWire::Message> ReceiveAcceptedWork(){
    if(!work_enabled_||shutdown_)return std::nullopt;
    if(SocketCookie(config_.fd)!=config_.cookie)throw std::runtime_error("Native Source work endpoint incarnation changed");
    const auto receipt=RetainedSourceProcess::ReceiveReceipt(config_.fd);if(!receipt)return std::nullopt;
    if(receipt->eof){work_enabled_=false;throw std::runtime_error("Native Source accepted-work owner channel closed");}
    const auto actual=RetainedSourceProcess::ReadStat(work_owner_->pid);
    if(receipt->sender!=work_owner_->pid||actual.pid!=work_owner_->pid||actual.start!=work_owner_->start||actual.state=='Z'||actual.state=='X')throw std::runtime_error("Native Source work sender incarnation changed");
    const auto message=RetainedSourceWorkWire::Decode(receipt->Packet());
    const auto kind=message.kind;
    const auto ticket=kind==RetainedSourceWorkWire::Kind::Install?message.ticket:kind==RetainedSourceWorkWire::Kind::Begin?message.challenge.ticket:kind==RetainedSourceWorkWire::Kind::Observed?message.cuts.ticket:kind==RetainedSourceWorkWire::Kind::Resume?message.resume.ticket:0;
    if(ticket!=config_.ticket)throw std::runtime_error("Native Source work command has foreign process ticket/direction");
    return message;
  }
  bool WriteAcceptedCuts(RetainedSourceWorkWire::Kind kind,const RetainedSourceWorkWire::Cuts& cuts){
    if(!work_enabled_||shutdown_||cuts.ticket!=config_.ticket||(kind!=RetainedSourceWorkWire::Kind::Cut&&kind!=RetainedSourceWorkWire::Kind::Acknowledged))return false;
    return Send(RetainedSourceWorkWire::Encode(kind,cuts));
  }
  bool WriteAcceptedResume(const RetainedSourceWorkWire::Resume& value){return work_enabled_&&!shutdown_&&value.ticket==config_.ticket&&Send(RetainedSourceWorkWire::Resumed(value));}
  bool WriteAcceptedUnsupported(uint64_t nonce,uint64_t reason){return work_enabled_&&!shutdown_&&Send(RetainedSourceWorkWire::Unsupported(config_.ticket,nonce,reason));}
  bool Enabled()const noexcept{return config_.enabled;}
  bool StartupWritten()const noexcept{return startup_;}
  bool ShutdownBoundaryWritten()const noexcept{return shutdown_;}
  bool WriteStartup(){
    if(!Enabled()||startup_||shutdown_||getpid()!=process_->pid)return false;
    if(!Send(RetainedSourceProcess::StartupPacket(config_.ticket,*process_)))return false;
    startup_=true;return true;
  }
  // Called only at the actual process-level hook after Engine destruction,
  // CefShutdown and Writer.Stop. This receipt is NOT a core CloseReceipt, an
  // idle grant, a successful command receipt or proof of physical process exit.
  bool WriteShutdownReached(int exit_code){
    if(!Enabled()||!startup_||shutdown_||exit_code<0||exit_code>255)return false;
    const auto actual=RetainedSourceProcess::Observe(getpid());if(actual.pid!=process_->pid||actual.start!=process_->start||actual.executable!=process_->executable)throw std::runtime_error("Native process shutdown identity changed");
    const auto packet="SOURCE_SHUTDOWN_REACHED 1 "+std::to_string(config_.ticket)+" "+std::to_string(actual.pid)+" "+std::to_string(process_->parent)+" "+std::to_string(actual.start)+" "+std::to_string(actual.parent)+" "+std::to_string(exit_code)+"\n";
    if(!Send(packet))return false;
    shutdown_=true;return true;
  }
 private:
  static std::optional<uint64_t> Number(std::string_view token)noexcept{uint64_t result=0;if(token.empty()||(token.size()>1&&token.front()=='0'))return std::nullopt;const auto parsed=std::from_chars(token.data(),token.data()+token.size(),result);return parsed.ec==std::errc{}&&parsed.ptr==token.data()+token.size()?std::optional<uint64_t>{result}:std::nullopt;}
  void CloseOwned()noexcept{if(config_.enabled){try{if(SocketCookie(config_.fd)==config_.cookie)close(config_.fd);}catch(...){}config_.enabled=false;}}
  bool Send(const std::string& packet){if(SocketCookie(config_.fd)!=config_.cookie)throw std::runtime_error("Native process registration socket incarnation changed");ssize_t result;do{result=send(config_.fd,packet.data(),packet.size(),MSG_DONTWAIT|MSG_NOSIGNAL);}while(result<0&&errno==EINTR);return result==static_cast<ssize_t>(packet.size());}
  Config config_;std::optional<RetainedSourceLeases::Process> process_;std::optional<RetainedSourceProcess::Stat> work_owner_;bool startup_=false,shutdown_=false,work_enabled_=false;
};
static_assert(std::is_trivially_copyable_v<RetainedSourceEndpoint::Config>);
} // namespace suitecut::runner
