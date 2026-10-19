// SPDX-License-Identifier: MIT
#include "control_session.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#ifdef SUITECUT_CLI_SANITIZERS
#include <sanitizer/lsan_interface.h>
#endif
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#endif

namespace suitecut::control {
#ifndef _WIN32
namespace {
using Clock=std::chrono::steady_clock;
using Deadline=Clock::time_point;
constexpr std::size_t max_request=4*1024*1024,max_response=8*1024*1024;
volatile std::sig_atomic_t stopping=0;
void stopped(int){stopping=1;}
struct Fd {
  int value=-1;
  explicit Fd(int fd=-1):value(fd){}
  ~Fd(){if(value>=0)::close(value);}
  Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;
};
[[noreturn]] void fail(const std::string& why){throw std::runtime_error(why+": "+std::strerror(errno));}
void ready_io(int fd,short events,Deadline deadline) {
  for(;;) {
    const auto left=std::chrono::ceil<std::chrono::milliseconds>(deadline-Clock::now()).count();
    if(left<=0)throw std::runtime_error("Session deadline expired; a dispatched mutation has unknown outcome and was not retried");
    pollfd p{fd,events,0};const auto rc=::poll(&p,1,static_cast<int>(left));
    if(rc<0&&errno==EINTR)continue;
    if(rc<0)fail("Session poll failed");
    if(rc&&p.revents&events)return;
    if(rc&&p.revents&(POLLHUP|POLLERR|POLLNVAL))throw std::runtime_error("Session connection closed; dispatched mutations are never replayed");
  }
}
void transfer(int fd,char* data,std::size_t bytes,bool write,Deadline deadline) {
  std::size_t offset=0;
  while(offset<bytes) {
    ready_io(fd,write?POLLOUT:POLLIN,deadline);
    const auto n=write ? ::write(fd,data+offset,bytes-offset) : ::read(fd,data+offset,bytes-offset);
    if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
    if(n<=0)throw std::runtime_error("Session connection ended; do not automatically repeat dispatched input");
    offset+=static_cast<std::size_t>(n);
  }
}
void send_json(int fd,const Json& value,Deadline deadline,std::size_t maximum=max_response) {
  auto data=value.dump({maximum,64,65536,maximum});
  const auto size=static_cast<std::uint32_t>(data.size());
  char prefix[]{static_cast<char>(size),static_cast<char>(size>>8),static_cast<char>(size>>16),static_cast<char>(size>>24)};
  transfer(fd,prefix,4,true,deadline);transfer(fd,data.data(),data.size(),true,deadline);
}
Json receive_json(int fd,Deadline deadline,std::size_t maximum) {
  unsigned char prefix[4];transfer(fd,reinterpret_cast<char*>(prefix),4,false,deadline);
  const std::size_t size=prefix[0]|(std::size_t(prefix[1])<<8)|(std::size_t(prefix[2])<<16)|(std::size_t(prefix[3])<<24);
  if(!size||size>maximum)throw std::invalid_argument("Session message exceeds its byte budget");
  std::string data(size,'\0');transfer(fd,data.data(),size,false,deadline);
  return Json::parse(data,{maximum,64,65536,maximum});
}
std::string directory(const SessionOptions& options) {
  const auto& name=options.name;
  if(name.empty()||name.size()>48||name=="."||name=="..")throw std::invalid_argument("Session name must contain1..48 letters, digits, '.', '-' or '_'");
  for(const unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))
    throw std::invalid_argument("Session name contains an invalid character");
  auto path=options.directory;
  if(path.empty()) {
    const char* runtime=std::getenv("XDG_RUNTIME_DIR");
    path=runtime&&*runtime?std::string(runtime)+"/suitecut-control":"/tmp/suitecut-control-"+std::to_string(::getuid());
  }
  if(path.empty()||path[0]!='/')throw std::invalid_argument("Session directory must be absolute");
  if(::mkdir(path.c_str(),0700)<0&&errno!=EEXIST)fail("Could not create private session directory");
  struct stat st{};
  if(::lstat(path.c_str(),&st)<0||!S_ISDIR(st.st_mode)||st.st_uid!=::getuid()||(st.st_mode&0077))
    throw std::runtime_error("Session directory must be a real private directory owned by this user (mode0700)");
  return path;
}
sockaddr_un address(const std::string& path) {
  sockaddr_un a{};a.sun_family=AF_UNIX;
  if(path.size()>=sizeof(a.sun_path))throw std::invalid_argument("Session socket path is too long; choose a shorter --session-dir");
  std::memcpy(a.sun_path,path.c_str(),path.size()+1);return a;
}
int connect_to(const std::string& path) {
  struct stat st{};
  if(::lstat(path.c_str(),&st)<0){if(errno==ENOENT)return -1;fail("Cannot inspect session socket");}
  if(!S_ISSOCK(st.st_mode)||st.st_uid!=::getuid()||(st.st_mode&0077))throw std::runtime_error("Unsafe session socket ownership or permissions");
  Fd fd(::socket(AF_UNIX,SOCK_STREAM,0));if(fd.value<0)fail("Cannot open session socket");
  ::fcntl(fd.value,F_SETFL,::fcntl(fd.value,F_GETFL)|O_NONBLOCK);
  const auto a=address(path);
  if(::connect(fd.value,reinterpret_cast<const sockaddr*>(&a),sizeof(a))<0) {
    if(errno==ECONNREFUSED||errno==ENOENT)return -1;
    if(errno==EINPROGRESS) {
      ready_io(fd.value,POLLOUT,Clock::now()+std::chrono::seconds(1));
      int error=0;socklen_t bytes=sizeof(error);
      if(::getsockopt(fd.value,SOL_SOCKET,SO_ERROR,&error,&bytes)<0||error)throw std::runtime_error("Session connection failed before dispatch");
    } else fail("Cannot connect to session before dispatch");
  }
  const int out=fd.value;fd.value=-1;
  ::fcntl(out,F_SETFD,FD_CLOEXEC);::fcntl(out,F_SETFL,::fcntl(out,F_GETFL)|O_NONBLOCK);return out;
}
void retire_children() {
#ifdef __linux__
  // This standalone CLI is a subreaper and launches no unrelated subprocesses.
  // Enumerate only direct children; never signal a cached PID without checking
  // current parentage. No hot-loop /proc scan is used during browser work.
  for(int phase=0;phase<3;++phase) {
    for(;;){const auto p=::waitpid(-1,nullptr,WNOHANG);if(p>0)continue;if(p<0&&errno==EINTR)continue;if(p<0&&errno==ECHILD)return;break;}
    if(phase)for(const auto& entry:std::filesystem::directory_iterator("/proc")) {
      const auto name=entry.path().filename().string();if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;
      auto owned=[&] {
        std::ifstream in(entry.path()/"stat");std::string line;std::getline(in,line);const auto end=line.rfind(')');
        if(end==std::string::npos)return false;
        std::istringstream fields(line.substr(end+2));char state;pid_t parent;return bool(fields>>state>>parent)&&parent==::getpid();
      };
      const auto pid=static_cast<pid_t>(std::stol(name));
      Fd identity(static_cast<int>(::syscall(SYS_pidfd_open,pid,0)));
      if(identity.value>=0&&owned())::syscall(SYS_pidfd_send_signal,identity.value,phase==1?SIGTERM:SIGKILL,nullptr,0);
    }
    const auto until=Clock::now()+std::chrono::milliseconds(300);
    while(Clock::now()<until) {
      const auto p=::waitpid(-1,nullptr,WNOHANG);if(p>0)continue;if(p<0&&errno==ECHILD)return;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  for(;;) {
    const auto p=::waitpid(-1,nullptr,WNOHANG);if(p>0||(p<0&&errno==EINTR))continue;
    if(p<0&&errno==ECHILD)return;
    throw std::runtime_error("Owned session children did not retire within cleanup budget");
  }
#endif
}
}
Json session_request(const SessionOptions& options,const Json& request,bool start,const Worker& worker) {
  if(options.timeout_ms<1||options.timeout_ms>300000||options.idle_ms<1000||options.idle_ms>86400000)
    throw std::invalid_argument("Session timeout must be1..300000ms and idle timeout1000..86400000ms");
  ::signal(SIGPIPE,SIG_IGN);
  const auto base=directory(options),path=base+"/"+options.name+".sock";
  int connected=connect_to(path);
  if(connected<0&&start) {
    const auto lock_path=base+"/"+options.name+".lock";
    Fd lock(::open(lock_path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600));
    if(lock.value<0)fail("Cannot open session startup lock");
    struct stat st{};
    if(::fstat(lock.value,&st)<0||!S_ISREG(st.st_mode)||st.st_uid!=::getuid()||st.st_nlink!=1||(st.st_mode&0077))
      throw std::runtime_error("Unsafe session startup lock");
    bool locked=false;
    const auto startup_deadline=Clock::now()+std::chrono::seconds(35);
    while(connected<0&&!locked) {
      locked=::flock(lock.value,LOCK_EX|LOCK_NB)==0;
      if(locked)break;
      if(errno!=EWOULDBLOCK&&errno!=EAGAIN)fail("Cannot lock session startup");
      if(Clock::now()>=startup_deadline)throw std::runtime_error("Session startup lock remained busy; request was not dispatched");
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      connected=connect_to(path);
    }
    if(locked) {
      int pipes[2];if(::pipe(pipes)<0)fail("Cannot create session readiness pipe");
      ::fcntl(pipes[0],F_SETFD,FD_CLOEXEC);::fcntl(pipes[1],F_SETFD,FD_CLOEXEC);
      Fd parent(pipes[0]),child(pipes[1]);
      const auto pid=::fork();if(pid<0)fail("Cannot start native session worker");
      if(pid==0) {
        ::close(parent.value);parent.value=-1;if(::setsid()<0)::_exit(1);::umask(0077);
        ::signal(SIGTERM,stopped);::signal(SIGINT,stopped);
#ifdef __linux__
        if(::prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0)<0)::_exit(1);
#endif
        Fd null(::open("/dev/null",O_RDWR));
        if(null.value>=0)for(int target=0;target<3;++target)::dup2(null.value,target);
        int code=0;
        try {
          if(::unlink(path.c_str())<0&&errno!=ENOENT)fail("Cannot retire stale session socket");
          Fd listener(::socket(AF_UNIX,SOCK_STREAM,0));if(listener.value<0)fail("Cannot create session listener");
          ::fcntl(listener.value,F_SETFD,FD_CLOEXEC);const auto a=address(path);
          if(::bind(listener.value,reinterpret_cast<const sockaddr*>(&a),sizeof(a))<0||::chmod(path.c_str(),0600)<0||::listen(listener.value,16)<0)fail("Cannot bind session listener");
          worker(listener.value,child.value);
        } catch(const std::exception& error) {
          code=1;try{send_json(child.value,Json{{"ok",false},{"error",error.what()}},Clock::now()+std::chrono::seconds(1));}catch(...){}
        }
        try{retire_children();}catch(...){code=1;}
#ifdef SUITECUT_CLI_SANITIZERS
        // _exit avoids running inherited host destructors after fork. Request
        // leak checking explicitly so sanitizer builds still cover the worker.
        if(__lsan_do_recoverable_leak_check())code=1;
#endif
        ::unlink(path.c_str());::_exit(code);
      }
      ::close(child.value);child.value=-1;
      try {
        const auto ready=receive_json(parent.value,Clock::now()+std::chrono::seconds(35),65536);
        if(!ready.at("ok").as_bool())throw std::runtime_error("Session startup failed: "+ready.at("error").as_string());
      } catch(...) {
        ::kill(pid,SIGTERM);
        const auto until=Clock::now()+std::chrono::seconds(3);
        while(Clock::now()<until){const auto rc=::waitpid(pid,nullptr,WNOHANG);if(rc==pid||rc<0)throw;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        ::kill(pid,SIGKILL);while(::waitpid(pid,nullptr,0)<0&&errno==EINTR){}throw;
      }
      connected=connect_to(path);
    }
  }
  if(connected<0) {
    const auto tool=request.contains("request")?request.at("request").at("tool").as_string():std::string{};
    if(tool=="_session_status")return Json{{"ok",true},{"result",Json{{"running",false},{"session",options.name}}}};
    if(tool=="_session_close")return Json{{"ok",true},{"result",Json{{"closed",true},{"alreadyClosed",true}}}};
    throw std::runtime_error("Session is not running; start it with --session NAME open --browser /absolute/browser");
  }
  Fd fd(connected);const auto deadline=Clock::now()+std::chrono::milliseconds(options.timeout_ms);
  send_json(fd.value,request,deadline,max_request);
  return receive_json(fd.value,deadline,max_response);
}
void serve_session(const SessionOptions& options,int listener,int ready,
                   const std::function<Json(const Json&)>& call,const std::function<void()>& close) {
  send_json(ready,Json{{"ok",true}},Clock::now()+std::chrono::seconds(1));
  bool closed=false;
  auto last=Clock::now();
  try {
    while(!stopping&&Clock::now()-last<std::chrono::milliseconds(options.idle_ms)) {
      const auto remaining=options.idle_ms-std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-last).count();
      pollfd p{listener,POLLIN,0};const auto rc=::poll(&p,1,static_cast<int>(std::max<std::int64_t>(1,std::min<std::int64_t>(1000,remaining))));
      if(rc<0&&errno==EINTR)continue;
      if(rc<0)fail("Session accept poll failed");
      if(!rc)continue;
      Fd client(::accept(listener,nullptr,nullptr));if(client.value<0){if(errno==EINTR)continue;fail("Session accept failed");}
      ::fcntl(client.value,F_SETFD,FD_CLOEXEC);::fcntl(client.value,F_SETFL,::fcntl(client.value,F_GETFL)|O_NONBLOCK);
#ifdef __linux__
      ucred peer{};socklen_t bytes=sizeof(peer);
      if(::getsockopt(client.value,SOL_SOCKET,SO_PEERCRED,&peer,&bytes)<0||peer.uid!=::getuid())continue;
#endif
      Json result;
      try {
        const auto request=receive_json(client.value,Clock::now()+std::chrono::seconds(2),max_request);
        last=Clock::now();const auto tool=request.at("request").at("tool").as_string();
        if(tool=="_session_close") {
          // Withdraw discovery before acknowledging shutdown. A new explicit
          // open waits for this worker's startup lock and starts a fresh worker.
          ::unlink((directory(options)+"/"+options.name+".sock").c_str());
          ::shutdown(listener,SHUT_RDWR);
          close();closed=true;retire_children();
          result=Json{{"ok",true},{"result",Json{{"closed",true},{"session",options.name}}}};
        } else if(tool=="_session_status")result=Json{{"ok",true},{"result",Json{{"running",true},{"pid",::getpid()},{"session",options.name},{"idleTimeoutMs",options.idle_ms}}}};
        else result=call(request);
      } catch(const std::exception& error) {result=Json{{"ok",false},{"error",Json{{"message",error.what()}}}};}
      try{send_json(client.value,result,Clock::now()+std::chrono::seconds(2));}catch(...){}
      last=Clock::now();if(closed)break;
    }
  }catch(...){if(!closed)try{close();}catch(...){}throw;}
  if(!closed)close();
}
#else
Json session_request(const SessionOptions&,const Json&,bool,const Worker&){throw std::runtime_error("Named CLI sessions require a POSIX platform");}
void serve_session(const SessionOptions&,int,int,const std::function<Json(const Json&)>&,const std::function<void()>&){}
#endif
} // namespace suitecut::control
