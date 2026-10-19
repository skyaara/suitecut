// SPDX-License-Identifier: MIT
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "retained_source_leases.h"
#include <cerrno>
#include <charconv>
#include <fstream>
#include <limits>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace suitecut::runner {
// Read-only kernel identity and authenticated receipt decoding. No waitpid,
// kill, subreaper setting, SDK callback, browser dispatch or eligibility switch.
class RetainedSourceProcess {
 public:
  using Policy=RetainedSourceLeases;
  struct Stat {int64_t pid=0,parent=0;uint64_t start=0;char state=0;};
  static Stat ParseStat(std::string_view text){
    const auto open=text.find('('),close=text.rfind(')');
    if(open==text.npos||close==text.npos||open<2||close<=open||close+3>=text.size())throw std::runtime_error("Native Source process stat malformed");
    const auto pid=Number(text.substr(0,open-1));if(!pid||pid>INT32_MAX)throw std::runtime_error("Native Source stat PID invalid");
    auto rest=text.substr(close+1);Stat result;result.pid=static_cast<int64_t>(pid);
    for(unsigned field=3;field<=22;++field){while(!rest.empty()&&rest.front()==' ')rest.remove_prefix(1);const auto end=rest.find(' ');const auto token=rest.substr(0,end);
      if(token.empty())throw std::runtime_error("Native Source process stat truncated");
      if(field==3){if(token.size()!=1)throw std::runtime_error("Native Source process state malformed");result.state=token[0];}
      if(field==4){const auto parent=Number(token);if(parent>INT32_MAX)throw std::runtime_error("Native Source stat parent invalid");result.parent=static_cast<int64_t>(parent);}
      if(field==22)result.start=Number(token);
      if(end==rest.npos&&field<22)throw std::runtime_error("Native Source process stat missing start identity");
      rest=end==rest.npos?std::string_view{}:rest.substr(end+1);
    }
    if(!result.start)throw std::runtime_error("Native Source process start identity missing");
    return result;
  }
  static Stat ReadStat(int64_t pid){
    if(pid<=0||pid>INT32_MAX)throw std::invalid_argument("Native Source observed PID invalid");
    std::ifstream stream("/proc/"+std::to_string(pid)+"/stat");std::string text;
    if(!stream||!std::getline(stream,text)||text.size()>8192)throw std::runtime_error("Native Source process observation unavailable");
    const auto result=ParseStat(text);if(result.pid!=pid)throw std::runtime_error("Native Source process PID changed");return result;
  }
  static Policy::Process Observe(int64_t pid){
    const auto before=ReadStat(pid);struct stat executable{};
    if(before.state=='Z'||before.state=='X'||stat(("/proc/"+std::to_string(pid)+"/exe").c_str(),&executable))throw std::runtime_error("Native Source executable observation unavailable");
    const auto after=ReadStat(pid);
    if(before.pid!=after.pid||before.parent!=after.parent||before.start!=after.start||after.state=='Z'||after.state=='X')throw std::runtime_error("Native Source process incarnation changed");
    return {pid,after.parent,after.start,{static_cast<uint64_t>(executable.st_dev),static_cast<uint64_t>(executable.st_ino)}};
  }
  class PidHandle {
   public:
    explicit PidHandle(int64_t pid):fd_(static_cast<int>(syscall(SYS_pidfd_open,pid,0))){if(fd_<0)throw std::runtime_error("Native Source pidfd_open failed");}
    ~PidHandle(){if(fd_>=0)close(fd_);}PidHandle(const PidHandle&)=delete;PidHandle& operator=(const PidHandle&)=delete;
    int Descriptor()const noexcept{return fd_;}
    bool Exited()const{pollfd item{fd_,POLLIN,0};int result;do{result=poll(&item,1,0);}while(result<0&&errno==EINTR);if(result<0||item.revents&(POLLERR|POLLNVAL))throw std::runtime_error("Native Source pidfd observation failed");return result&&item.revents&POLLIN;}
   private:int fd_;
  };
  static void ReceiveCredentials(int fd){int enabled=1;if(setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&enabled,sizeof(enabled)))throw std::runtime_error("Native Source credential channel setup failed");}
  static std::string StartupPacket(uint64_t ticket,const Policy::Process& process){return "SOURCE_START 1 "+std::to_string(ticket)+" "+std::to_string(process.pid)+" "+std::to_string(process.parent)+" "+std::to_string(process.start)+"\n";}
  struct Receipt {std::array<char,256> bytes{};size_t size=0;int64_t sender=0;bool eof=false;std::string_view Packet()const noexcept{return {bytes.data(),size};}};
  static std::optional<Receipt> ReceiveReceipt(int fd){
    std::array<char,256> bytes{};alignas(cmsghdr) std::array<char,CMSG_SPACE(sizeof(ucred))+CMSG_SPACE(16*sizeof(int))> ancillary{};iovec io{bytes.data(),bytes.size()};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=ancillary.data();message.msg_controllen=ancillary.size();
    ssize_t size;do{size=recvmsg(fd,&message,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);}while(size<0&&errno==EINTR);
    if(size<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return std::nullopt;
    // Reject ancillary descriptors without leaking the kernel-installed copies,
    // including a malformed/truncated packet that will fail below.
    for(auto* header=CMSG_FIRSTHDR(&message);header;header=CMSG_NXTHDR(&message,header))if(header->cmsg_level==SOL_SOCKET&&header->cmsg_type==SCM_RIGHTS&&header->cmsg_len>=CMSG_LEN(0)){
      const auto count=(header->cmsg_len-CMSG_LEN(0))/sizeof(int);
      for(size_t i=0;i<count;++i){int received=-1;__builtin_memcpy(&received,reinterpret_cast<const char*>(CMSG_DATA(header))+i*sizeof(int),sizeof(int));if(received>=0)close(received);}
    }
    if(size==0){if(CMSG_FIRSTHDR(&message))throw std::runtime_error("Native Source empty packet is not channel drain");return Receipt{{},0,0,true};}
    if(size<0||message.msg_flags&(MSG_TRUNC|MSG_CTRUNC))throw std::runtime_error("Native Source startup receipt missing/truncated");
    std::optional<ucred> credentials;
    for(auto* header=CMSG_FIRSTHDR(&message);header;header=CMSG_NXTHDR(&message,header)){
      if(credentials||header->cmsg_level!=SOL_SOCKET||header->cmsg_type!=SCM_CREDENTIALS||header->cmsg_len!=CMSG_LEN(sizeof(ucred)))throw std::runtime_error("Native Source receipt credentials invalid");
      ucred actual{};__builtin_memcpy(&actual,CMSG_DATA(header),sizeof(actual));credentials=actual;
    }
    if(!credentials||credentials->uid!=getuid()||credentials->gid!=getgid())throw std::runtime_error("Native Source receipt sender credentials missing/foreign");
    return Receipt{bytes,static_cast<size_t>(size),credentials->pid,false};
  }
  static Policy::Startup DecodeStartup(const Receipt& receipt,const Policy::Ticket& ticket,Policy::Executable executable){
    if(receipt.eof)throw std::runtime_error("Native Source startup channel closed");
    const auto packet=receipt.Packet();if(!packet.starts_with("SOURCE_START 1 ")||packet.back()!='\n')throw std::runtime_error("Native Source startup packet malformed");
    auto tail=packet.substr(15,packet.size()-16);std::array<uint64_t,4> values{};
    for(size_t i=0;i<values.size();++i){const auto end=tail.find(' ');values[i]=Number(tail.substr(0,end));if((i+1==values.size())!=(end==tail.npos))throw std::runtime_error("Native Source startup packet arity invalid");if(end!=tail.npos)tail.remove_prefix(end+1);}
    if(values[0]!=ticket.id||values[1]>INT32_MAX||values[2]>INT32_MAX||receipt.sender!=static_cast<int64_t>(values[1]))throw std::runtime_error("Native Source startup owner/sender mismatch");
    PidHandle handle(receipt.sender);if(handle.Exited())throw std::runtime_error("Native Source sender exited before startup verification");
    const auto process=Observe(receipt.sender);const auto parent=ReadStat(ticket.owner.host.pid);
    if(process.parent!=ticket.owner.host.pid||process.parent!=static_cast<int64_t>(values[2])||process.start!=values[3]||process.executable!=executable||parent.start!=ticket.owner.host.start||handle.Exited())throw std::runtime_error("Native Source startup kernel parent/start/executable mismatch");
    return Policy::Startup{ticket,1,process,ticket.owner.host,receipt.sender};
  }
  static std::optional<Policy::Startup> ReceiveStartup(int fd,const Policy::Ticket& ticket,Policy::Executable executable){
    const auto receipt=ReceiveReceipt(fd);if(!receipt)return std::nullopt;return DecodeStartup(*receipt,ticket,executable);
  }

 private:
  static uint64_t Number(std::string_view token){uint64_t result=0;if(token.empty()||(token.size()>1&&token[0]=='0'))throw std::runtime_error("Native Source identity number is noncanonical");const auto parsed=std::from_chars(token.data(),token.data()+token.size(),result);if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size())throw std::runtime_error("Native Source identity number malformed");return result;}
};
} // namespace suitecut::runner
