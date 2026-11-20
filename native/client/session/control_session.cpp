// SPDX-License-Identifier: MIT
#include "control_session.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
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
volatile std::sig_atomic_t stopping=0;
void stopped(int){stopping=1;}
struct Fd {
  int value=-1;
  explicit Fd(int fd=-1):value(fd){}
  ~Fd(){if(value>=0)::close(value);}
  Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;
};
[[noreturn]] void fail(const std::string& why){throw std::runtime_error(why+": "+std::strerror(errno));}
// True when the peer on a connected Unix socket runs as this user. Linux has
// SO_PEERCRED; macOS and the BSDs have getpeereid, which glibc lacks.
bool peer_is_same_user(int fd) {
#if defined(__linux__)
  ucred peer{};socklen_t bytes=sizeof(peer);
  return ::getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&bytes)==0&&peer.uid==::getuid();
#else
  uid_t uid=0;gid_t gid=0;
  return ::getpeereid(fd,&uid,&gid)==0&&uid==::getuid();
#endif
}
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
void send_json(int fd,const Json& value,Deadline deadline) {
  auto data=value.dump();
  // The 4-byte length prefix is the only size limit of this framing.
  if(data.size()>std::numeric_limits<std::uint32_t>::max())throw std::invalid_argument("Session message exceeds the 4-byte length prefix");
  const auto size=static_cast<std::uint32_t>(data.size());
  char prefix[]{static_cast<char>(size),static_cast<char>(size>>8),static_cast<char>(size>>16),static_cast<char>(size>>24)};
  transfer(fd,prefix,4,true,deadline);transfer(fd,data.data(),data.size(),true,deadline);
}
Json receive_json(int fd,Deadline deadline) {
  unsigned char prefix[4];transfer(fd,reinterpret_cast<char*>(prefix),4,false,deadline);
  const std::size_t size=prefix[0]|(std::size_t(prefix[1])<<8)|(std::size_t(prefix[2])<<16)|(std::size_t(prefix[3])<<24);
  if(!size)throw std::invalid_argument("Session message is empty");
  std::string data(size,'\0');transfer(fd,data.data(),size,false,deadline);
  return Json::parse(data);
}
std::string directory(const SessionOptions& options) {
  const auto& name=options.name;
  if(name.empty()||name=="."||name=="..")throw std::invalid_argument("Session name must contain letters, digits, '.', '-' or '_'");
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
        const auto ready=receive_json(parent.value,Clock::now()+std::chrono::seconds(35));
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
  send_json(fd.value,request,deadline);
  return receive_json(fd.value,deadline);
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
      if(!peer_is_same_user(client.value))continue;
      Json result;
      try {
        const auto request=receive_json(client.value,Clock::now()+std::chrono::seconds(2));
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
void serve_shared_session(const SessionOptions& options,int listener,int ready,int connections,
                          const std::function<Json(const Json&)>& call,const std::function<void()>& close,
                          const std::function<bool()>& alive) {
  serve_shared_session(options,listener,ready,connections,[call](Peer&,const Json& request){return call(request);},
                       [](Peer&){},close,alive);
}
void serve_shared_session(const SessionOptions& options,int listener,int ready,int connections,
                          const PeerCall& call,const PeerClosed& closed,const std::function<void()>& close,
                          const std::function<bool()>& alive) {
  // 0 means any number of connections; a positive value is the caller's own bound.
  if(connections<0)throw std::invalid_argument("Shared session connections must not be negative");
  // Connection threads are detached, so what they share outlives this frame
  // until the last of them lets go of it.
  struct Shared {
    std::mutex mutex;std::condition_variable idle;
    int active=0;bool closed=false,close_requested=false;Clock::time_point last=Clock::now();
    std::uint64_t next_peer=0;
    PeerCall call;PeerClosed closed_peer;Json status;
  };
  auto shared=std::make_shared<Shared>();
  shared->call=call;shared->closed_peer=closed;
  shared->status=Json{{"running",true},{"pid",::getpid()},{"session",options.name},{"idleTimeoutMs",options.idle_ms}};
  send_json(ready,Json{{"ok",true}},Clock::now()+std::chrono::seconds(1));
  auto serve=[shared](int raw,std::uint64_t id) {
    Peer peer;peer.id=id;
    {
      Fd client(raw);
      auto touch=[&]{std::lock_guard lock(shared->mutex);shared->last=Clock::now();};
      try {
        for(;;) {
          // A client that keeps its connection idles between requests; only
          // the whole session's idle clock retires the worker.
          pollfd p{client.value,POLLIN,0};
          const auto rc=::poll(&p,1,1000);
          if(rc<0&&errno==EINTR)continue;
          if(rc<0||stopping)break;
          {std::lock_guard lock(shared->mutex);if(shared->closed)break;}
          if(!rc)continue;
          if(p.revents&(POLLHUP|POLLERR|POLLNVAL)&&!(p.revents&POLLIN))break;
          const auto request=receive_json(client.value,Clock::now()+std::chrono::seconds(2));
          touch();
          Json result;
          const auto tool=request.contains("request")&&request.at("request").contains("tool")?request.at("request").at("tool").as_string():std::string{};
          if(tool=="_session_status")result=Json{{"ok",true},{"result",shared->status}};
          else if(tool=="_session_close") {
            {std::lock_guard lock(shared->mutex);shared->close_requested=true;}
            result=Json{{"ok",true},{"result",Json{{"closed",true},{"session",shared->status.at("session")}}}};
          } else {
            try{result=shared->call(peer,request);}
            catch(const std::exception& error){result=Json{{"ok",false},{"error",Json{{"message",error.what()}}}};}
          }
          // A reply can be large (a screenshot); allow it time to drain.
          send_json(client.value,result,Clock::now()+std::chrono::seconds(10));
          touch();
          if(peer.close_session){std::lock_guard lock(shared->mutex);shared->close_requested=true;}
        }
      } catch(...) {}
    }
    try{shared->closed_peer(peer);}catch(...){}
    std::lock_guard lock(shared->mutex);--shared->active;shared->last=Clock::now();shared->idle.notify_all();
  };
  auto finish=[&] {
    // Withdraw discovery first, so a new start launches a fresh worker, then
    // let connection threads see the close and finish before the browser goes.
    ::unlink((directory(options)+"/"+options.name+".sock").c_str());
    std::unique_lock lock(shared->mutex);shared->closed=true;
    shared->idle.wait(lock,[&]{return shared->active==0;});
  };
  try {
    for(;;) {
      {
        std::lock_guard lock(shared->mutex);
        // A held connection (a viewer) keeps the worker alive.
        if(stopping||shared->close_requested||(alive&&!alive())||(shared->active==0&&Clock::now()-shared->last>=std::chrono::milliseconds(options.idle_ms)))break;
      }
      pollfd p{listener,POLLIN,0};const auto rc=::poll(&p,1,1000);
      if(rc<0&&errno==EINTR)continue;
      if(rc<0)fail("Session accept poll failed");
      if(!rc)continue;
      Fd client(::accept(listener,nullptr,nullptr));if(client.value<0){if(errno==EINTR)continue;fail("Session accept failed");}
      ::fcntl(client.value,F_SETFD,FD_CLOEXEC);::fcntl(client.value,F_SETFL,::fcntl(client.value,F_GETFL)|O_NONBLOCK);
      if(!peer_is_same_user(client.value))continue;
      std::uint64_t id=0;
      {
        std::lock_guard lock(shared->mutex);
        if(connections&&shared->active>=connections) {
          try{send_json(client.value,Json{{"ok",false},{"error",Json{{"message","Shared session is at its connection limit"},{"code","busy"}}}},Clock::now()+std::chrono::milliseconds(200));}catch(...){}
          continue;
        }
        ++shared->active;id=++shared->next_peer;
      }
      try {std::thread(serve,client.value,id).detach();client.value=-1;}
      catch(...) {std::lock_guard lock(shared->mutex);--shared->active;}
    }
  } catch(...) {
    finish();
    try{close();}catch(...){}
    throw;
  }
  finish();
  close();
}
#else
Json session_request(const SessionOptions&,const Json&,bool,const Worker&){throw std::runtime_error("Named CLI sessions require a POSIX platform");}
void serve_session(const SessionOptions&,int,int,const std::function<Json(const Json&)>&,const std::function<void()>&){}
void serve_shared_session(const SessionOptions&,int,int,int,const std::function<Json(const Json&)>&,const std::function<void()>&,const std::function<bool()>&){}
void serve_shared_session(const SessionOptions&,int,int,int,const PeerCall&,const PeerClosed&,const std::function<void()>&,const std::function<bool()>&){}
#endif
} // namespace suitecut::control
