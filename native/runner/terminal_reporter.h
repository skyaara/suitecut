// SPDX-License-Identifier: MIT
#pragma once
#include "report_publication.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
namespace suitecut::runner {
enum class NativeTerminalReporter {Console,JSON,None};
inline NativeTerminalReporter ParseNativeTerminalReporter(std::string_view value){
  if(value=="json")return NativeTerminalReporter::JSON;
  if(value=="none")return NativeTerminalReporter::None;
  throw std::invalid_argument("Native reporter must be json or none");
}
namespace terminal_reporter_detail {
class Descriptor {
 public:
  explicit Descriptor(int fd=-1):fd_(fd){}
  ~Descriptor(){if(fd_>=0)::close(fd_);}
  Descriptor(const Descriptor&)=delete;Descriptor&operator=(const Descriptor&)=delete;
  int Get()const{return fd_;}
 private:int fd_;
};
// Block SIGPIPE only in this emitting thread. Preserve any preexisting pending
// SIGPIPE and restore the original mask; a newly generated EPIPE becomes a real
// reporting error rather than killing the already-drained native coordinator.
class PipeSignal {
 public:
  PipeSignal(){
    ::sigemptyset(&pipe_);::sigaddset(&pipe_,SIGPIPE);sigset_t pending{};
    if(::sigpending(&pending)<0||::sigprocmask(SIG_BLOCK,&pipe_,&previous_)<0)
      throw std::system_error(errno,std::generic_category(),"Native reporter SIGPIPE mask");
    had_pending_=::sigismember(&pending,SIGPIPE)==1;
  }
  ~PipeSignal(){
    if(epipe_&&!had_pending_){const timespec zero{};(void)::sigtimedwait(&pipe_,nullptr,&zero);}
    (void)::sigprocmask(SIG_SETMASK,&previous_,nullptr);
  }
  void BrokenPipe(){epipe_=true;}
 private:sigset_t pipe_{},previous_{};bool had_pending_=false,epipe_=false;
};
}
// Stream only after actual execution/artifact retirement. No path reopen,
// report reserialization, callback replay, background writer or full-document
// allocation. Existing regular-file offsets and parent descriptor flags remain
// unchanged except for the genuine bytes written to regular stdout.
inline void EmitNativeJSONReporter(const NativeReportPublication& report,
    const std::function<bool()>& stopped={},int output=STDOUT_FILENO){
  using namespace terminal_reporter_detail;
  report.Validate();struct stat target{};
  if(::fstat(output,&target)<0)throw std::system_error(errno,std::generic_category(),"Native reporter stdout stat");
  struct stat source{};
  if(::fstat(report.Descriptor(),&source)<0)throw std::system_error(errno,std::generic_category(),"Native reporter source identity stat");
  if(source.st_dev==target.st_dev&&source.st_ino==target.st_ino)
    throw std::runtime_error("Native reporter stdout aliases its committed report inode");
  const bool socket=S_ISSOCK(target.st_mode),regular=S_ISREG(target.st_mode);
  if(!socket&&!regular&&!S_ISFIFO(target.st_mode)&&!S_ISCHR(target.st_mode))
    throw std::runtime_error("Native reporter stdout descriptor type is unsupported");
  // Reopening proc's descriptor link gives a separate open-file description for
  // pipe/device nonblocking flags. dup()+F_SETFL would mutate the parent's flags.
  const std::string proc="/proc/self/fd/"+std::to_string(output);
  Descriptor separate(!socket&&!regular?::open(proc.c_str(),O_WRONLY|O_NONBLOCK|O_CLOEXEC|O_NOCTTY):-1);
  const int destination=socket||regular?output:separate.Get();
  if(destination<0)throw std::system_error(errno,std::generic_category(),"Native reporter independent stdout open");
  struct stat actual{};
  if(::fstat(destination,&actual)<0)throw std::system_error(errno,std::generic_category(),"Native reporter stdout identity stat");
  if(target.st_dev!=actual.st_dev||target.st_ino!=actual.st_ino||(target.st_mode&S_IFMT)!=(actual.st_mode&S_IFMT))
    throw std::runtime_error("Native reporter stdout identity changed");
  PipeSignal pipe_signal;
  using Clock=std::chrono::steady_clock;
  const auto end=Clock::now()+std::chrono::seconds(5);
  const bool already_stopped=stopped&&stopped();
  const auto checkpoint=[&]{
    if(Clock::now()>=end)throw std::runtime_error("Native JSON reporter final publication exceeded5000ms");
    if(!already_stopped&&stopped&&stopped())throw std::runtime_error("Native JSON reporter publication interrupted after execution retirement");
  };
  std::array<char,16384> buffer{};size_t offset=0;
  while(offset<report.Bytes()){
    checkpoint();const auto requested=std::min(buffer.size(),report.Bytes()-offset);
    ssize_t received;
    do{checkpoint();received=::pread(report.Descriptor(),buffer.data(),requested,static_cast<off_t>(offset));}while(received<0&&errno==EINTR);
    if(received<0)throw std::system_error(errno,std::generic_category(),"Native reporter committed inode read");
    if(!received)throw std::runtime_error("Native reporter committed report ended early");
    size_t sent=0;
    while(sent<static_cast<size_t>(received)){
      checkpoint();const auto count=socket?::send(destination,buffer.data()+sent,static_cast<size_t>(received)-sent,MSG_DONTWAIT|MSG_NOSIGNAL)
          : ::write(destination,buffer.data()+sent,static_cast<size_t>(received)-sent);
      if(count>0){sent+=static_cast<size_t>(count);continue;}
      if(count==0)throw std::runtime_error("Native reporter stdout write made no progress");
      const int error=errno;
      if(error==EINTR)continue;
      if(error!=EAGAIN&&error!=EWOULDBLOCK){if(error==EPIPE&&!socket)pipe_signal.BrokenPipe();throw std::system_error(error,std::generic_category(),"Native reporter stdout write");}
      // A stopped/timed-out run can still report immediately writable results,
      // but never waits for downstream capacity after its original stop.
      if(stopped&&stopped())throw std::runtime_error("Native JSON reporter backpressure after original execution stop");
      const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count();
      const int timeout=static_cast<int>(std::clamp<int64_t>(remaining,0,10));pollfd descriptor{destination,POLLOUT,0};
      const auto ready=::poll(&descriptor,1,timeout);
      if(ready<0&&errno!=EINTR)throw std::system_error(errno,std::generic_category(),"Native reporter stdout backpressure poll");
    }
    offset+=static_cast<size_t>(received);
  }
  report.Validate();
  checkpoint();
}
} // namespace suitecut::runner
