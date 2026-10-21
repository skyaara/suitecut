// SPDX-License-Identifier: MIT
#include "connection.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cctype>
#include <climits>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#ifndef _WIN32
#include <dirent.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
extern char** environ;
#endif

namespace suitecut::detail {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t command_limit = 4 * 1024 * 1024;
constexpr std::size_t outgoing_limit = 8 * 1024 * 1024;
constexpr std::size_t event_limit = 4096;
constexpr std::size_t stderr_limit = 16384;
std::runtime_error failure(const std::string& message) { return std::runtime_error(message); }
bool nul(const std::string& value) { return value.find('\0') != std::string::npos; }
void validate(const LaunchOptions& o) {
  if (o.cdp_endpoint.empty() && (o.executable.empty() || nul(o.executable) || !std::filesystem::path(o.executable).is_absolute()))
    throw failure("Chrome executable must be an absolute path without null bytes");
  if (!o.cdp_endpoint.empty() && (o.cdp_endpoint.size() > 16384 || nul(o.cdp_endpoint) ||
      !o.profile.empty() || !o.arguments.empty()))
    throw failure("Chrome attachment requires a bounded endpoint and no launch profile or arguments");
  if (o.connect_headers.size() > 64) throw failure("Too many WebSocket authentication headers");
  std::size_t header_bytes=0;
  std::map<std::string,bool> header_names;
  for(const auto& [name,value]:o.connect_headers){
    if(name.empty()||name.size()>256||value.size()>8192)throw failure("WebSocket request header exceeds byte budget");
    for(unsigned char c:name)if(c>=127||(!std::isalnum(c)&&std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c))==std::string_view::npos))
      throw failure("Invalid WebSocket request header name");
    for(unsigned char c:value)if((c<32&&c!='\t')||c==127)throw failure("Invalid WebSocket request header value");
    auto lower=name;for(auto& c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(!header_names.emplace(lower,true).second)throw failure("Duplicate WebSocket request header name");
    if(lower=="host"||lower=="connection"||lower=="upgrade"||lower=="content-length"||lower=="transfer-encoding"||lower.starts_with("sec-websocket-"))
      throw failure("Request headers cannot override managed WebSocket handshake fields");
    if(name.size()+value.size()+4>32768-header_bytes)throw failure("WebSocket request headers exceed 32 KiB");
    header_bytes+=name.size()+value.size()+4;
  }
  for(const auto* path:{&o.tls_ca_file,&o.tls_client_certificate,&o.tls_client_key}){
    if(path->size()>16384||nul(*path))throw failure("Invalid TLS certificate/key path");
    if(!path->empty()&&(!std::filesystem::is_regular_file(*path)||std::filesystem::file_size(*path)>16*1024*1024))
      throw failure("TLS certificate/key must be a regular file within 16 MiB");
  }
  if(o.tls_client_certificate.empty()!=o.tls_client_key.empty())throw failure("TLS client certificate and private key must be supplied together");
  if(o.cdp_endpoint.empty()&&(!o.connect_headers.empty()||!o.tls_ca_file.empty()||!o.tls_client_certificate.empty()))
    throw failure("Connection authentication/TLS options require an attachment endpoint");
  if (!o.profile.empty() && (nul(o.profile) || !std::filesystem::path(o.profile).is_absolute()))
    throw failure("Chrome profile must be an absolute path without null bytes");
  if (o.width < 2 || o.width > 3840 || o.height < 2 || o.height > 2160)
    throw failure("Viewport must be within 2..3840 by 2..2160");
  for (auto timeout : {o.startup_timeout, o.shutdown_timeout})
    if (timeout.count() <= 0 || timeout > std::chrono::hours(24)) throw failure("Invalid Chrome lifecycle timeout");
  if (!o.max_pending || o.max_pending > 4096 || !o.max_event_bytes ||
      o.max_event_bytes > 256 * 1024 * 1024 || !o.max_payload_bytes || o.max_payload_bytes > 192 * 1024 * 1024)
    throw failure("Invalid CDP transport storage limits");
  if (o.arguments.size() > 1024) throw failure("Too many Chrome arguments");
  std::size_t bytes = 0;
  for (const auto& arg : o.arguments) {
    if (nul(arg) || !arg.starts_with("--") || arg == "--" ||
        arg.starts_with("--remote-debugging") || arg.starts_with("--user-data-dir") ||
        arg.starts_with("--profile-directory") || arg.starts_with("--window-size") ||
        arg.starts_with("--force-device-scale-factor") || arg.starts_with("--headless") ||
        arg.starts_with("--suitecut-") || arg == "--app" || arg.starts_with("--app="))
      throw failure("Chrome arguments cannot override managed debug, profile or viewport switches");
    if (arg.size() > 65536 || bytes > 512 * 1024 - arg.size()) throw failure("Chrome argument budget exceeded");
    bytes += arg.size();
  }
}
#ifndef _WIN32
void close_fd(int& fd) noexcept { if (fd >= 0) { ::close(fd); fd = -1; } }
void nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) throw failure("Could not configure CDP pipe");
}
std::array<int,2> pipe_pair() {
  std::array<int,2> pair{-1,-1};
#if defined(__linux__)
  if (pipe2(pair.data(), O_CLOEXEC)) throw failure("Could not create CDP pipe");
#else
  if (pipe(pair.data())) throw failure("Could not create CDP pipe");
#endif
  try {
    for (auto& fd : pair) {
      // Chrome specifically consumes FD3 and FD4. Sources must not alias them.
      if (fd < 10) {
        int replacement = fcntl(fd, F_DUPFD_CLOEXEC, 10);
        if (replacement < 0) throw failure("Could not relocate CDP pipe descriptor");
        ::close(fd); fd = replacement;
      }
      if (fcntl(fd, F_SETFD, FD_CLOEXEC)) throw failure("Could not protect CDP descriptor");
#ifdef F_SETNOSIGPIPE
      if (fcntl(fd, F_SETNOSIGPIPE, 1)) throw failure("Could not protect CDP pipe writes");
#endif
    }
  } catch (...) { for (auto& fd : pair) close_fd(fd); throw; }
  return pair;
}
struct Identity { pid_t group = 0, session = 0; unsigned long long start = 0; char state = 0; };
std::optional<Identity> identity(pid_t pid) {
#if defined(__linux__)
  std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  if (!std::getline(file,line)) return std::nullopt;
  auto end = line.rfind(") ");
  if (end == std::string::npos) return std::nullopt;
  std::istringstream fields(line.substr(end+2));
  Identity result; long long parent; std::string ignored;
  if (!(fields >> result.state >> parent >> result.group >> result.session)) return std::nullopt;
  for (int i = 7; i <= 21; ++i) if (!(fields >> ignored)) return std::nullopt;
  if (!(fields >> result.start)) return std::nullopt;
  return result;
#else
  (void)pid; return std::nullopt;
#endif
}

std::string base64(std::string_view bytes) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    auto a = static_cast<unsigned char>(bytes[i]);
    auto b = i+1 < bytes.size() ? static_cast<unsigned char>(bytes[i+1]) : 0;
    auto c = i+2 < bytes.size() ? static_cast<unsigned char>(bytes[i+2]) : 0;
    result.push_back(alphabet[a>>2]); result.push_back(alphabet[((a&3)<<4)|(b>>4)]);
    result.push_back(i+1 < bytes.size() ? alphabet[((b&15)<<2)|(c>>6)] : '=');
    result.push_back(i+2 < bytes.size() ? alphabet[c&63] : '=');
  }
  return result;
}
std::string websocket_accept(const std::string& key) {
  std::string bytes = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  const auto bits = static_cast<std::uint64_t>(bytes.size()) * 8;
  bytes.push_back(static_cast<char>(0x80));
  while (bytes.size()%64 != 56) bytes.push_back(0);
  for (int i = 7; i >= 0; --i) bytes.push_back(static_cast<char>(bits>>(i*8)));
  std::array<std::uint32_t,5> hash{0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
  for (std::size_t block = 0; block < bytes.size(); block += 64) {
    std::array<std::uint32_t,80> words{};
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 4; ++j)
      words[i] = (words[i]<<8)|static_cast<unsigned char>(bytes[block+i*4+j]);
    for (int i = 16; i < 80; ++i) words[i] = std::rotl(words[i-3]^words[i-8]^words[i-14]^words[i-16],1);
    auto a=hash[0],b=hash[1],c=hash[2],d=hash[3],e=hash[4];
    for (int i = 0; i < 80; ++i) {
      std::uint32_t f,k;
      if (i < 20) { f=(b&c)|(~b&d); k=0x5a827999; }
      else if (i < 40) { f=b^c^d; k=0x6ed9eba1; }
      else if (i < 60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdc; }
      else { f=b^c^d; k=0xca62c1d6; }
      auto next=std::rotl(a,5)+f+e+k+words[i]; e=d;d=c;c=std::rotl(b,30);b=a;a=next;
    }
    hash[0]+=a;hash[1]+=b;hash[2]+=c;hash[3]+=d;hash[4]+=e;
  }
  std::string digest;
  for (auto word : hash) for (int i = 3; i >= 0; --i) digest.push_back(static_cast<char>(word>>(i*8)));
  return base64(digest);
}
std::string random_bytes(std::size_t count) {
  int fd = open("/dev/urandom",O_RDONLY|O_CLOEXEC);
  if (fd < 0) throw failure("Could not acquire WebSocket masking entropy");
  std::string result(count,0); std::size_t offset = 0;
  while (offset < count) {
    auto n = ::read(fd,result.data()+offset,count-offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { close_fd(fd); throw failure("Could not read WebSocket masking entropy"); }
    offset += static_cast<std::size_t>(n);
  }
  close_fd(fd); return result;
}
std::string websocket_frame(std::string_view text,unsigned char opcode = 1) {
  std::string result; result.reserve(text.size()+14);
  result.push_back(static_cast<char>(0x80|opcode));
  if (text.size() < 126) result.push_back(static_cast<char>(0x80|text.size()));
  else if (text.size() <= 65535) {
    result.push_back(static_cast<char>(0x80|126));
    result.push_back(static_cast<char>(text.size()>>8)); result.push_back(static_cast<char>(text.size()));
  } else {
    result.push_back(static_cast<char>(0x80|127));
    for (int i = 7; i >= 0; --i) result.push_back(static_cast<char>(static_cast<std::uint64_t>(text.size())>>(i*8)));
  }
  const auto mask = random_bytes(4); result += mask;
  for (std::size_t i = 0; i < text.size(); ++i) result.push_back(static_cast<char>(text[i]^mask[i%4]));
  return result;
}
struct Endpoint { std::string host="127.0.0.1",path; int port=0; bool websocket=false,tls=false; };
int port_number(std::string_view text) {
  if (text.empty() || text.size() > 5) throw failure("Invalid local Chrome debugging port");
  int port=0;
  for (char c : text) { if(c<'0'||c>'9') throw failure("Invalid local Chrome debugging port"); port=port*10+c-'0'; }
  if (!port || port > 65535) throw failure("Invalid local Chrome debugging port");
  return port;
}
Endpoint endpoint(const std::string& text,bool browser=true) {
  for (unsigned char c : text) if (c<=32 || c==127) throw failure("Invalid control or whitespace in CDP endpoint");
  Endpoint result;
  if (std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';})) {
    result.port=port_number(text); return result;
  }
  std::size_t begin;
  if(text.starts_with("ws://")){begin=5;result.websocket=true;}
  else if(text.starts_with("wss://")){begin=6;result.websocket=true;result.tls=true;}
  else if(text.starts_with("http://")) begin=7;
  else if(text.starts_with("https://")){begin=8;result.tls=true;}
  else throw failure("Attachment endpoint must be a debugging port or an HTTP(S)/WS(S) URL");
  auto slash=text.find('/',begin);
  auto authority=text.substr(begin,slash==std::string::npos?std::string::npos:slash-begin);
  if(authority.empty()||authority.find('@')!=std::string::npos||authority.find('#')!=std::string::npos||authority.find('?')!=std::string::npos)
    throw failure("Invalid CDP endpoint authority");
  std::string port;bool explicit_port=false;
  if(authority.front()=='[') {
    auto end=authority.find(']');
    if(end==std::string::npos)throw failure("Invalid IPv6 CDP endpoint");
    result.host=authority.substr(1,end-1);
    if(end+1<authority.size()){if(authority[end+1]!=':')throw failure("Invalid CDP endpoint port");port=authority.substr(end+2);explicit_port=true;}
  } else {
    auto colon=authority.find(':');
    result.host=authority.substr(0,colon);
    if(colon!=std::string::npos){port=authority.substr(colon+1);explicit_port=true;}
  }
  result.port=explicit_port?port_number(port):(result.tls?443:80);
  if(result.host.empty()||result.host.size()>253)throw failure("Invalid attachment hostname");
  for(unsigned char c:result.host)if(!std::isalnum(c)&&c!='.'&&c!='-'&&c!=':')throw failure("Invalid attachment hostname");
  result.path=slash==std::string::npos?"/":text.substr(slash);
  if(result.path.find('#')!=std::string::npos) throw failure("CDP endpoint fragments are unsupported");
  if(browser && result.websocket && !result.path.starts_with("/devtools/browser/")) throw failure("Expected a browser WebSocket endpoint");
  return result;
}
void socket_wait(int fd,short events,Deadline deadline) {
  for (;;) {
    const auto left=std::chrono::ceil<std::chrono::milliseconds>(deadline-Clock::now()).count();
    if(left<=0)throw failure("Chrome attachment startup timed out");
    pollfd item{fd,events,0};
    auto result=::poll(&item,1,static_cast<int>(std::min<long long>(left,INT_MAX)));
    if(result>0){if(item.revents&POLLNVAL)throw failure("Invalid Chrome attachment socket");return;}
    if(result<0&&errno==EINTR)continue;
    if(result<0)throw failure("Chrome attachment socket poll failed");
  }
}
struct Address { sockaddr_storage value{};socklen_t size=0; };
std::vector<Address> resolve(const Endpoint& target,Deadline deadline) {
  std::vector<Address> result;
  const auto numeric=[&](const std::string& host){
    Address address;auto* v4=reinterpret_cast<sockaddr_in*>(&address.value);auto* v6=reinterpret_cast<sockaddr_in6*>(&address.value);
    if(inet_pton(AF_INET,host.c_str(),&v4->sin_addr)==1){v4->sin_family=AF_INET;v4->sin_port=htons(target.port);address.size=sizeof(*v4);}
    else if(inet_pton(AF_INET6,host.c_str(),&v6->sin6_addr)==1){v6->sin6_family=AF_INET6;v6->sin6_port=htons(target.port);address.size=sizeof(*v6);}
    else return false;
    result.push_back(address);return true;
  };
  if(target.host=="localhost"){numeric("127.0.0.1");numeric("::1");return result;}
  if(numeric(target.host))return result;
#if defined(__linux__)
  struct Lookup {
    std::string host,service;addrinfo hints{};gaicb request{};
    ~Lookup(){if(request.ar_result)freeaddrinfo(request.ar_result);}
  };
  struct Registry { std::mutex mutex;std::vector<std::shared_ptr<Lookup>> jobs; };
  // libc cancellation may report EAI_NOTCANCELED. Keep at most eight such
  // requests alive until completion, rather than freeing storage still owned by
  // the resolver, blocking close, or spawning unbounded per-lookup threads.
  static auto* registry=new Registry;
  auto lookup=std::make_shared<Lookup>();lookup->host=target.host;lookup->service=std::to_string(target.port);
  lookup->hints.ai_family=AF_UNSPEC;lookup->hints.ai_socktype=SOCK_STREAM;lookup->hints.ai_protocol=IPPROTO_TCP;
  lookup->request.ar_name=lookup->host.c_str();lookup->request.ar_service=lookup->service.c_str();lookup->request.ar_request=&lookup->hints;
  {
    std::lock_guard lock(registry->mutex);
    std::erase_if(registry->jobs,[](const auto& job){return job.use_count()==1&&gai_error(&job->request)!=EAI_INPROGRESS;});
    if(registry->jobs.size()>=8)throw failure("Native hostname resolver is at its bounded in-flight capacity");
    registry->jobs.push_back(lookup);
    gaicb* requests[]={&lookup->request};sigevent notification{};notification.sigev_notify=SIGEV_NONE;
    if(getaddrinfo_a(GAI_NOWAIT,requests,1,&notification)!=0){registry->jobs.pop_back();throw failure("Could not start hostname resolution");}
  }
  for(;;){
    const auto state=gai_error(&lookup->request);
    if(state!=EAI_INPROGRESS){
      {std::lock_guard lock(registry->mutex);std::erase(registry->jobs,lookup);}
      if(state!=0)throw failure("Attachment hostname resolution failed");
      break;
    }
    if(Clock::now()>=deadline){(void)gai_cancel(&lookup->request);throw failure("Attachment hostname resolution timed out");}
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  for(auto* entry=lookup->request.ar_result;entry&&result.size()<32;entry=entry->ai_next)
    if((entry->ai_family==AF_INET||entry->ai_family==AF_INET6)&&entry->ai_addrlen<=sizeof(sockaddr_storage)){
      Address address;address.size=entry->ai_addrlen;std::memcpy(&address.value,entry->ai_addr,entry->ai_addrlen);result.push_back(address);
    }
  if(result.empty())throw failure("Hostname resolution returned no usable TCP addresses");
  return result;
#else
  (void)deadline;throw failure("Bounded asynchronous hostname resolution currently requires Linux; use a numeric IP address");
#endif
}
int connect_socket(const Endpoint& target,Deadline deadline) {
  const auto addresses=resolve(target,deadline);
  for(const auto& address:addresses) {
    int fd=::socket(address.value.ss_family,SOCK_STREAM,0);
    if(fd<0)continue;
    try {
      if(fcntl(fd,F_SETFD,FD_CLOEXEC)<0)throw failure("Could not protect Chrome socket");
      nonblocking(fd);
#ifdef SO_NOSIGPIPE
      const int yes=1; if(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes)))throw failure("Could not protect Chrome socket writes");
#endif
      int result=::connect(fd,reinterpret_cast<const sockaddr*>(&address.value),address.size);
      if(result<0&&errno!=EINPROGRESS&&errno!=EINTR){close_fd(fd);continue;}
      if(result<0)socket_wait(fd,POLLOUT,deadline);
      int error=0;socklen_t length=sizeof(error);
      if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)||error){close_fd(fd);continue;}
      return fd;
    }catch(...){close_fd(fd);throw;}
  }
  throw failure("Could not connect to remote browser endpoint");
}
struct ScopedSigpipe {
  sigset_t previous{};bool pending=false,active=false;
  ScopedSigpipe(){
    sigset_t existing,blocked;sigpending(&existing);pending=sigismember(&existing,SIGPIPE)==1;
    sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);
    active=pthread_sigmask(SIG_BLOCK,&blocked,&previous)==0;
    if(!active)throw failure("Could not isolate TLS socket signals");
  }
  ~ScopedSigpipe(){
    if(!active)return;
    if(!pending&&sigismember(&previous,SIGPIPE)!=1){
      sigset_t only;sigemptyset(&only);sigaddset(&only,SIGPIPE);
#if defined(__APPLE__)
      sigset_t current;
      if(sigpending(&current)==0&&sigismember(&current,SIGPIPE)==1){
        int received=0;
        (void)sigwait(&only,&received);
      }
#else
      timespec zero{};
      while(sigtimedwait(&only,nullptr,&zero)<0&&errno==EINTR){}
#endif
    }
    (void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);
  }
};
struct SocketStream {
  int fd=-1;SSL_CTX* context=nullptr;SSL* ssl=nullptr;
  short read_want=POLLIN,write_want=POLLOUT;
  SocketStream(const Endpoint& target,const LaunchOptions& options,Deadline deadline){
    fd=connect_socket(target,deadline);
    try{
      if(!target.tls)return;
      ScopedSigpipe protection;
      context=SSL_CTX_new(TLS_client_method());
      if(!context||SSL_CTX_set_min_proto_version(context,TLS1_2_VERSION)!=1)throw failure("Could not initialize verified TLS");
      SSL_CTX_set_verify(context,SSL_VERIFY_PEER,nullptr);
      SSL_CTX_set_verify_depth(context,16);
      SSL_CTX_set_default_passwd_cb(context,[](char*,int,int,void*){return 0;});
      if(options.tls_ca_file.empty()){
        if(SSL_CTX_set_default_verify_paths(context)!=1)throw failure("Could not load system TLS trust roots");
      }else if(SSL_CTX_load_verify_locations(context,options.tls_ca_file.c_str(),nullptr)!=1)
        throw failure("Could not load custom TLS trust roots");
      if(!options.tls_client_certificate.empty()){
        if(SSL_CTX_use_certificate_chain_file(context,options.tls_client_certificate.c_str())!=1||
            SSL_CTX_use_PrivateKey_file(context,options.tls_client_key.c_str(),SSL_FILETYPE_PEM)!=1||
            SSL_CTX_check_private_key(context)!=1)throw failure("Could not load matching TLS client certificate and private key");
      }
      ssl=SSL_new(context);
      if(!ssl||SSL_set_fd(ssl,fd)!=1)throw failure("Could not initialize TLS socket");
      SSL_set_mode(ssl,SSL_MODE_ENABLE_PARTIAL_WRITE|SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
      auto* parameters=SSL_get0_param(ssl);
      X509_VERIFY_PARAM_set_hostflags(parameters,X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
      in_addr v4{};in6_addr v6{};
      const bool ip=inet_pton(AF_INET,target.host.c_str(),&v4)==1||inet_pton(AF_INET6,target.host.c_str(),&v6)==1;
      if(ip){if(X509_VERIFY_PARAM_set1_ip_asc(parameters,target.host.c_str())!=1)throw failure("Could not configure TLS IP verification");}
      else if(SSL_set1_host(ssl,target.host.c_str())!=1||SSL_set_tlsext_host_name(ssl,target.host.c_str())!=1)
        throw failure("Could not configure TLS hostname verification");
      for(;;){
        if(Clock::now()>=deadline)throw failure("Verified TLS handshake timed out");
        ERR_clear_error();const auto connected=SSL_connect(ssl);
        if(connected==1)break;
        const auto error=SSL_get_error(ssl,connected);
        if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE){socket_wait(fd,error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT,deadline);continue;}
        if(SSL_get_verify_result(ssl)!=X509_V_OK)throw failure("TLS peer certificate or hostname verification failed");
        throw failure("Verified TLS handshake failed");
      }
      if(SSL_get_verify_result(ssl)!=X509_V_OK)throw failure("TLS peer certificate verification failed");
    }catch(...){close();throw;}
  }
  ~SocketStream(){close();}
  void close() noexcept {
    if(ssl){SSL_free(ssl);ssl=nullptr;}
    if(context){SSL_CTX_free(context);context=nullptr;}
    close_fd(fd);
  }
  bool buffered() const {return ssl&&SSL_pending(ssl)>0;}
  ssize_t read(void* data,std::size_t size){
    if(!ssl)return ::read(fd,data,size);
    std::size_t count=0;ERR_clear_error();const auto ok=SSL_read_ex(ssl,data,size,&count);
    if(ok==1){read_want=POLLIN;return static_cast<ssize_t>(count);}
    const auto error=SSL_get_error(ssl,ok);
    if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE){read_want=error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;errno=EAGAIN;return -1;}
    if(error==SSL_ERROR_ZERO_RETURN)return 0;
    throw failure("TLS browser response stream failed");
  }
  ssize_t write(const void* data,std::size_t size){
    if(!ssl){
#ifdef MSG_NOSIGNAL
      return ::send(fd,data,size,MSG_NOSIGNAL);
#else
      return ::send(fd,data,size,0);
#endif
    }
    std::size_t count=0;ERR_clear_error();const auto ok=SSL_write_ex(ssl,data,size,&count);
    if(ok==1){write_want=POLLOUT;return static_cast<ssize_t>(count);}
    const auto error=SSL_get_error(ssl,ok);
    if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE){write_want=error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;errno=EAGAIN;return -1;}
    throw failure("TLS browser command stream failed");
  }
};
void socket_write(SocketStream& socket,std::string_view text,Deadline deadline) {
  ScopedSigpipe protection;
  std::size_t offset=0;
  while(offset<text.size()){
    const auto count=socket.write(text.data()+offset,text.size()-offset);
    if(count>0){offset+=static_cast<std::size_t>(count);continue;}
    if(count<0&&errno==EINTR)continue;
    if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){socket_wait(socket.fd,socket.write_want,deadline);continue;}
    throw failure("Chrome attachment handshake write failed");
  }
}
void socket_read(SocketStream& socket,std::string& text,std::size_t limit,Deadline deadline){
  ScopedSigpipe protection;
  std::array<char,4096> bytes;
  for(;;){
    const auto count=socket.read(bytes.data(),bytes.size());
    if(count>0){if(static_cast<std::size_t>(count)>limit-text.size())throw failure("Chrome HTTP response exceeds byte budget");text.append(bytes.data(),static_cast<std::size_t>(count));return;}
    if(count<0&&errno==EINTR)continue;
    if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){socket_wait(socket.fd,socket.read_want,deadline);continue;}
    throw failure("Chrome closed its attachment handshake");
  }
}
struct HttpReply { int status=0; std::map<std::string,std::string> headers; std::string body; };
HttpReply http_headers(SocketStream& socket,Deadline deadline){
  std::string raw;
  while(raw.find("\r\n\r\n")==std::string::npos)socket_read(socket,raw,16384,deadline);
  const auto end=raw.find("\r\n\r\n");
  std::istringstream stream(raw.substr(0,end));std::string line,version;HttpReply result;
  if(!std::getline(stream,line))throw failure("Invalid Chrome HTTP response");
  std::istringstream status(line);if(!(status>>version>>result.status)||(version!="HTTP/1.1"&&version!="HTTP/1.0"))throw failure("Invalid Chrome HTTP status");
  while(std::getline(stream,line)){
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    const auto colon=line.find(':');if(colon==std::string::npos)throw failure("Invalid Chrome HTTP header");
    auto name=line.substr(0,colon),value=line.substr(colon+1);
    for(auto& c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto first=value.find_first_not_of(" \t"),last=value.find_last_not_of(" \t");
    value=first==std::string::npos?"":value.substr(first,last-first+1);
    if(!result.headers.emplace(std::move(name),std::move(value)).second)throw failure("Duplicate Chrome HTTP header");
  }
  result.body=raw.substr(end+4);return result;
}
std::string host_header(const Endpoint& e){return (e.host.find(':')==std::string::npos?e.host:"["+e.host+"]")+":"+std::to_string(e.port);}
std::string request_headers(const LaunchOptions& options){
  std::string result;
  for(const auto& [name,value]:options.connect_headers)result+=name+": "+value+"\r\n";
  return result;
}
std::string discovery_body(SocketStream& socket,HttpReply response,Deadline deadline){
  constexpr std::size_t limit=1024*1024;
  if(response.headers.contains("transfer-encoding")){
    auto encoding=response.headers.at("transfer-encoding");
    for(auto& c:encoding)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(encoding!="chunked"||response.headers.contains("content-length"))throw failure("Unsupported or ambiguous discovery transfer encoding");
    std::string pending=std::move(response.body),result;
    const auto line=[&]() {
      while(pending.find("\r\n")==std::string::npos){
        if(pending.size()>16384)throw failure("Discovery chunk metadata exceeds byte budget");
        socket_read(socket,pending,limit+16384,deadline);
      }
      auto end=pending.find("\r\n");auto text=pending.substr(0,end);pending.erase(0,end+2);return text;
    };
    for(std::size_t chunks=0;chunks<65536;++chunks){
      auto header=line();
      if(header.size()>256)throw failure("Invalid discovery chunk size");
      const auto semicolon=header.find(';');if(semicolon!=std::string::npos)header.resize(semicolon);
      if(header.empty()||header.size()>16)throw failure("Invalid discovery chunk size");
      std::uint64_t count=0;
      for(unsigned char c:header){
        const unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;
        if(digit==16||count>(limit-digit)/16)throw failure("Discovery chunk exceeds byte budget");
        count=count*16+digit;
      }
      if(!count){
        std::size_t trailers=0;
        for(;;){auto trailer=line();if(trailer.empty())break;trailers+=trailer.size()+2;if(trailers>16384)throw failure("Discovery trailers exceed byte budget");}
        return result;
      }
      if(count>limit-result.size())throw failure("Discovery body exceeds byte budget");
      while(pending.size()<count+2)socket_read(socket,pending,limit+16384,deadline);
      if(pending.substr(static_cast<std::size_t>(count),2)!="\r\n")throw failure("Invalid discovery chunk terminator");
      result.append(pending.data(),static_cast<std::size_t>(count));pending.erase(0,static_cast<std::size_t>(count)+2);
    }
    throw failure("Discovery chunk count exceeds budget");
  }
  if(response.headers.contains("content-length")){
    const auto& length=response.headers.at("content-length");std::size_t count=0;
    if(length.empty()||length.size()>7)throw failure("Invalid Chrome discovery length");
    for(char c:length){if(c<'0'||c>'9')throw failure("Invalid Chrome discovery length");count=count*10+c-'0';}
    if(count>limit||response.body.size()>count)throw failure("Chrome discovery exceeds byte budget");
    while(response.body.size()<count)socket_read(socket,response.body,count,deadline);
    return std::move(response.body);
  }
  ScopedSigpipe protection;
  std::array<char,4096> bytes;
  for(;;){
    const auto count=socket.read(bytes.data(),bytes.size());
    if(!count)return std::move(response.body);
    if(count>0){if(static_cast<std::size_t>(count)>limit-response.body.size())throw failure("Discovery body exceeds byte budget");response.body.append(bytes.data(),static_cast<std::size_t>(count));}
    else if(errno==EINTR)continue;
    else if(errno==EAGAIN||errno==EWOULDBLOCK)socket_wait(socket.fd,socket.read_want,deadline);
    else throw failure("Could not finish discovery response");
  }
}
Endpoint discover(Endpoint target,const LaunchOptions& options,Deadline deadline){
  if(target.websocket)return target;
  SocketStream socket(target,options,deadline);
    socket_write(socket,"GET "+(target.path=="/"?std::string("/json/version"):target.path)+" HTTP/1.1\r\nHost: "+host_header(target)+"\r\nConnection: close\r\n"+request_headers(options)+"\r\n",deadline);
    auto response=http_headers(socket,deadline);
    if(response.status!=200)throw failure("Remote browser HTTP discovery was not successful");
    auto document=Json::parse(discovery_body(socket,std::move(response),deadline),{1024*1024,16,4096,1024*1024});
    auto found=endpoint(document.at("webSocketDebuggerUrl").as_string());
    if(!found.websocket)throw failure("Chrome discovery omitted browser WebSocket URL");
    // Never forward credentials to another origin or downgrade authenticated
    // HTTPS discovery to plaintext WebSocket. Remote providers can expose their
    // WebSocket URL directly when it deliberately uses another origin.
    auto lower=[](std::string host){for(auto& c:host)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return host;};
    if(found.port!=target.port||lower(found.host)!=lower(target.host)||found.tls!=target.tls)
      throw failure("Chrome discovery changed endpoint origin or TLS protection");
    return found;
}
#endif
} // namespace

struct CdpConnection::Impl {
  LaunchOptions options;
  mutable std::mutex mutex;
  std::mutex close_mutex;
  std::condition_variable changed;
  struct Pending { std::promise<Json> result; Deadline deadline; };
  struct Write { int id; std::string bytes; std::size_t offset = 0; };
  struct Event { Json value; std::size_t bytes; bool disposable = false; };
  std::map<int,Pending> pending;
  std::deque<Write> outgoing;
  std::deque<Event> events;
  std::size_t outgoing_bytes = 0, event_bytes = 0;
  std::uint64_t commands = 0, responses = 0, dropped_events = 0;
  int next_id = 0;
  bool closing = false, finished = false;
  const bool attached;
  std::function<bool(const Json&)> event_handler;
  std::exception_ptr terminal;
  std::string profile, stderr_tail;
  bool owned_profile = false;
  Deadline close_started{};
  std::thread io;
#ifndef _WIN32
  pid_t pid = -1;
  std::optional<Identity> original;
  int input = -1, output = -1, error_output = -1, wake_read = -1, wake_write = -1;
  bool child_exited = false, sent_term = false, sent_kill = false;
  Deadline exited_at{};
  std::string ws_buffer, ws_message;
  std::unique_ptr<SocketStream> network;
  bool ws_fragment = false, ws_peer_closed = false;
#endif
  explicit Impl(LaunchOptions o) : options(std::move(o)), attached(!options.cdp_endpoint.empty()) {}
  ~Impl() {
#ifndef _WIN32
    close_output();
    for (int* fd : {&input,&error_output,&wake_read,&wake_write}) close_fd(*fd);
#endif
    if (owned_profile) { std::error_code ignored; std::filesystem::remove_all(profile,ignored); }
  }
  int allocate() {
    if (next_id == INT_MAX) throw failure("CDP request IDs exhausted; relaunch Chrome");
    return ++next_id;
  }
  void wake() noexcept {
#ifndef _WIN32
    const char byte = 1;
    if (wake_write >= 0) while (::write(wake_write,&byte,1) < 0 && errno == EINTR) {}
#endif
  }
  void reject_locked(std::exception_ptr reason) {
    for (auto& [id,p] : pending) { (void)id; p.result.set_exception(reason); }
    pending.clear();
  }
  void cancel_locked(int id) {
    auto it = pending.find(id);
    if (it == pending.end()) return;
    it->second.result.set_exception(std::make_exception_ptr(failure("CDP request timed out")));
    pending.erase(it);
    for (auto out = outgoing.begin(); out != outgoing.end(); ++out)
      if (out->id == id && out->offset == 0) { outgoing_bytes -= out->bytes.size(); outgoing.erase(out); break; }
    // A partially written request must finish its NUL frame. Its late result
    // is ignored; IDs are never reused and remote side effects cannot cancel.
  }
  std::string encode(std::string text) const {
#ifndef _WIN32
    if (attached) return websocket_frame(text);
#endif
    text.push_back('\0'); return text;
  }
  void begin_close_locked(bool graceful) {
    if (closing) return;
    closing = true; close_started = Clock::now();
    reject_locked(terminal ? terminal : std::make_exception_ptr(failure("CDP connection closed")));
    // Preserve an in-progress frame before the final Browser.close command.
    if (!outgoing.empty() && outgoing.front().offset) {
      auto first = std::move(outgoing.front()); outgoing.clear();
      outgoing_bytes = first.bytes.size() - first.offset; outgoing.push_back(std::move(first));
    } else { outgoing.clear(); outgoing_bytes = 0; }
    if (graceful && attached) {
#ifndef _WIN32
      auto bytes = websocket_frame({},8);
      outgoing_bytes += bytes.size(); outgoing.push_back({0,std::move(bytes),0});
#endif
    } else if (graceful && next_id < INT_MAX) {
      const int id = allocate();
      auto bytes = encode(Json{{"id",id},{"method","Browser.close"},{"params",Json::object{}}}.dump());
      outgoing_bytes += bytes.size(); outgoing.push_back({id,std::move(bytes),0});
      ++commands;
    }
    changed.notify_all(); wake();
  }
  void fail(std::exception_ptr reason) {
    std::unique_lock lock(mutex);
    if (!terminal) terminal = reason;
    begin_close_locked(false); changed.notify_all(); wake();
  }
  void receive(std::string_view bytes) {
    auto value = Json::parse(bytes,{options.max_payload_bytes,64,1024*1024,options.max_payload_bytes});
    if (!value.is_object()) throw failure("Malformed CDP response");
    std::unique_lock lock(mutex);
    if (value.contains("id")) {
      const auto id = value.at("id").as_int();
      if (id < 1 || id > next_id) throw failure("Invalid CDP response ID");
      ++responses;
      auto it = pending.find(static_cast<int>(id));
      if (it == pending.end()) return;
      if (Clock::now() >= it->second.deadline) { cancel_locked(static_cast<int>(id)); return; }
      if (value.contains("error")) {
        const auto& error=value.at("error");
        const auto message=error.is_string()?error.as_string()+(value.contains("message")?": "+value.at("message").as_string():std::string{}):error.dump();
        it->second.result.set_exception(std::make_exception_ptr(failure("Browser protocol error: " + message)));
      }
      else if (value.contains("result")) it->second.result.set_value(std::move(value.at("result")));
      else throw failure("CDP response has neither result nor error");
      pending.erase(it);
    } else {
      if (!value.contains("method") || !value.at("method").is_string()) throw failure("Malformed CDP event");
      if (closing) return;
      auto observer = event_handler;
      // Backend state/control observes every event before public telemetry can
      // be discarded. Observers never run under the wire lock and may only
      // enqueue follow-up work, not synchronously call this I/O thread.
      lock.unlock();
      const bool consumed=observer&&observer(value);
      lock.lock();
      if (closing) return;
      const auto& method = value.at("method").as_string();
      const bool bidi_telemetry=(method=="network.beforeRequestSent"||method=="network.responseStarted"||
          method=="network.responseCompleted"||method=="network.fetchError"||method=="log.entryAdded");
      // BiDi network events can also represent paused interception. An event
      // carrying isBlocked=true is control state and must never be evicted.
      const bool blocked=value.contains("params")&&value.at("params").is_object()&&value.at("params").contains("isBlocked")&&
          value.at("params").at("isBlocked").is_bool()&&value.at("params").at("isBlocked").as_bool();
      const bool consumed_frame=consumed&&method=="Page.screencastFrame";
      const bool disposable = observer && !blocked && (method.starts_with("Network.") || method.starts_with("Runtime.console") || bidi_telemetry || consumed_frame);
      if (bytes.size() > options.max_event_bytes) {
        if (disposable) { ++dropped_events; return; }
        throw failure("Critical CDP event exceeds queue byte budget");
      }
      // Preserve unobserved low-level semantics: without a backend observer,
      // event loss is always an explicit transport failure. Observed telemetry
      // instead forms a bounded public ring; lifecycle/control is never evicted.
      if (observer) while (events.size() >= event_limit || bytes.size() > options.max_event_bytes - event_bytes) {
        const auto oldest = std::find_if(events.begin(),events.end(),[](const Event& event){return event.disposable;});
        if (oldest == events.end()) break;
        event_bytes -= oldest->bytes; events.erase(oldest); ++dropped_events;
      }
      if (events.size() >= event_limit || bytes.size() > options.max_event_bytes - event_bytes) {
        if (disposable) { ++dropped_events; return; }
        throw failure("Critical CDP event queue overflow; poll events before continuing");
      }
      event_bytes += bytes.size(); events.push_back({std::move(value),bytes.size(),disposable}); changed.notify_all();
    }
  }
#ifndef _WIN32
  void close_output() noexcept {
    if(network){network->close();output=-1;}
    else close_fd(output);
  }
  void attach_socket(Deadline deadline,bool browser=true) {
    const auto parsed=endpoint(options.cdp_endpoint,browser);
    if(!browser&&!parsed.websocket)throw failure("Generic browser connection requires a ws:// or wss:// endpoint");
    const auto target=browser?discover(parsed,options,deadline):parsed;
    auto socket=std::make_unique<SocketStream>(target,options,deadline);
    std::array<int,2> wake_pipe{-1,-1};
    try {
      const auto key=base64(random_bytes(16));
      socket_write(*socket,"GET "+target.path+" HTTP/1.1\r\nHost: "+host_header(target)+
          "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "+key+
          "\r\nSec-WebSocket-Version: 13\r\n"+request_headers(options)+"\r\n",deadline);
      auto response=http_headers(*socket,deadline);
      const auto token=[](std::string value,const std::string& expected) {
        for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::istringstream tokens(value);std::string item;
        while(std::getline(tokens,item,',')) {
          const auto first=item.find_first_not_of(" \t"),last=item.find_last_not_of(" \t");
          if(first!=std::string::npos&&item.substr(first,last-first+1)==expected)return true;
        }
        return false;
      };
      if(response.status!=101||!response.headers.contains("upgrade")||!token(response.headers.at("upgrade"),"websocket")||
          !response.headers.contains("connection")||!token(response.headers.at("connection"),"upgrade")||
          !response.headers.contains("sec-websocket-accept")||response.headers.at("sec-websocket-accept")!=websocket_accept(key)||
          response.headers.contains("sec-websocket-extensions")||response.headers.contains("sec-websocket-protocol"))
        throw failure("Chrome WebSocket upgrade validation failed");
      wake_pipe=pipe_pair();
      input=fcntl(socket->fd,F_DUPFD_CLOEXEC,10);
      if(input<0)throw failure("Could not duplicate Chrome WebSocket writer");
      output=socket->fd;network=std::move(socket);
      wake_read=wake_pipe[0];wake_pipe[0]=-1;wake_write=wake_pipe[1];wake_pipe[1]=-1;
      for(int descriptor:{input,output,wake_read,wake_write})nonblocking(descriptor);
      ws_buffer=std::move(response.body);
    }catch(...){for(auto& descriptor:wake_pipe)close_fd(descriptor);throw;}
  }
  void websocket_receive() {
    std::size_t consumed=0;
    while(ws_buffer.size()-consumed>=2) {
      const auto* bytes=reinterpret_cast<const unsigned char*>(ws_buffer.data()+consumed);
      const bool final=(bytes[0]&0x80)!=0;
      const unsigned opcode=bytes[0]&15;
      if((bytes[0]&0x70)||(bytes[1]&0x80))throw failure("Invalid server WebSocket frame flags");
      std::size_t header=2;
      std::uint64_t length=bytes[1]&127;
      if(length==126) {
        if(ws_buffer.size()-consumed<4)break;
        length=(static_cast<unsigned>(bytes[2])<<8)|bytes[3];header=4;
        if(length<126)throw failure("Noncanonical WebSocket frame length");
      }else if(length==127){
        if(ws_buffer.size()-consumed<10)break;
        if(bytes[2]&0x80)throw failure("Invalid WebSocket frame length");
        length=0;for(int i=2;i<10;++i)length=(length<<8)|bytes[i];header=10;
        if(length<=65535)throw failure("Noncanonical WebSocket frame length");
      }
      const bool control=opcode>=8;
      if(control&&(!final||length>125))throw failure("Invalid WebSocket control frame");
      if(!control&&length>options.max_payload_bytes)throw failure("CDP WebSocket frame exceeds byte budget");
      if(opcode!=0&&opcode!=1&&opcode!=8&&opcode!=9&&opcode!=10)throw failure("Unsupported CDP WebSocket frame opcode");
      if(length>ws_buffer.size()-consumed-header)break;
      const auto payload=std::string_view(ws_buffer).substr(consumed+header,static_cast<std::size_t>(length));
      if(opcode==8){
        if(length==1)throw failure("Invalid WebSocket close payload");
        ws_peer_closed=true;
        bool stopped;{std::lock_guard lock(mutex);stopped=closing;}
        if(!stopped)fail(std::make_exception_ptr(failure("Chrome WebSocket peer closed")));
        consumed+=header+static_cast<std::size_t>(length);break;
      }
      if(opcode==9){
        auto pong=websocket_frame(payload,10);
        std::lock_guard lock(mutex);
        if(!closing){
          if(pong.size()>outgoing_limit-outgoing_bytes||outgoing.size()>=options.max_pending+64)throw failure("WebSocket control queue overflow");
          outgoing_bytes+=pong.size();outgoing.push_back({0,std::move(pong),0});wake();
        }
      }else if(opcode==1||opcode==0){
        if((opcode==1&&ws_fragment)||(opcode==0&&!ws_fragment))throw failure("Invalid WebSocket text fragmentation");
        if(payload.size()>options.max_payload_bytes-ws_message.size())throw failure("CDP WebSocket message exceeds byte budget");
        if(opcode==1&&final)receive(payload);
        else{
          ws_message.append(payload);
          if(final){receive(ws_message);ws_message.clear();ws_fragment=false;}
          else ws_fragment=true;
        }
      }
      consumed+=header+static_cast<std::size_t>(length);
    }
    if(consumed)ws_buffer.erase(0,consumed);
  }
  void spawn_child() {
    if (access(options.executable.c_str(),X_OK)) throw failure("Chrome executable is not executable");
#if defined(__linux__)
    if (std::filesystem::equivalent(options.executable,"/proc/self/exe")) throw failure("Chrome executable cannot be this process");
#endif
    if (options.profile.empty()) {
      auto name = (std::filesystem::temp_directory_path()/"suitecut-cdp-XXXXXX").string();
      std::vector<char> buffer(name.begin(),name.end()); buffer.push_back(0);
      const auto created = mkdtemp(buffer.data());
      if (!created) throw failure("Could not create temporary Chrome profile");
      profile = created; owned_profile = true;
    } else { profile = options.profile; std::filesystem::create_directories(profile); }
    profile = std::filesystem::canonical(profile).string();
    std::array<int,2> in{-1,-1},out{-1,-1},err{-1,-1},wake_pipe{-1,-1};
    int null_fd = -1;
    posix_spawn_file_actions_t actions; posix_spawnattr_t attributes;
    bool actions_ready = false, attributes_ready = false;
    try {
      in = pipe_pair(); out = pipe_pair(); err = pipe_pair(); wake_pipe = pipe_pair();
      null_fd = open("/dev/null",O_RDWR|O_CLOEXEC);
      if (null_fd < 0) throw failure("Could not open Chrome null stream");
      if (null_fd < 10) {
        int replacement = fcntl(null_fd,F_DUPFD_CLOEXEC,10);
        if (replacement < 0) throw failure("Could not relocate Chrome null descriptor");
        close_fd(null_fd); null_fd = replacement;
      }
      if (posix_spawn_file_actions_init(&actions)) throw failure("Could not initialize Chrome file actions");
      actions_ready = true;
      if (posix_spawnattr_init(&attributes)) throw failure("Could not initialize Chrome spawn attributes");
      attributes_ready = true;
      for (auto pair : {std::pair{null_fd,0},std::pair{null_fd,1},std::pair{err[1],2},std::pair{in[0],3},std::pair{out[1],4}})
        if (posix_spawn_file_actions_adddup2(&actions,pair.first,pair.second)) throw failure("Could not route Chrome CDP pipe");
      for (auto* pair : {&in,&out,&err,&wake_pipe}) for (int fd : *pair)
        if (posix_spawn_file_actions_addclose(&actions,fd)) throw failure("Could not close child CDP descriptor");
      if (posix_spawn_file_actions_addclose(&actions,null_fd)) throw failure("Could not close child null descriptor");
      sigset_t empty,defaults; sigemptyset(&empty); sigemptyset(&defaults);
      for (int sig : {SIGTERM,SIGINT,SIGHUP,SIGPIPE}) sigaddset(&defaults,sig);
      short flags = POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_SETSID
      flags |= POSIX_SPAWN_SETSID;
#else
      flags |= POSIX_SPAWN_SETPGROUP;
      if (posix_spawnattr_setpgroup(&attributes,0)) throw failure("Could not isolate Chrome process group");
#endif
      if (posix_spawnattr_setsigmask(&attributes,&empty) || posix_spawnattr_setsigdefault(&attributes,&defaults) ||
          posix_spawnattr_setflags(&attributes,flags)) throw failure("Could not configure Chrome process signals");
      std::vector<std::string> args{options.executable};
      args.insert(args.end(),options.arguments.begin(),options.arguments.end());
      // Chromium enables AutomationControlled for this private pipe even in
      // visible mode. Keep that native behavior and avoid opening a TCP port.
      args.insert(args.end(),{"--remote-debugging-pipe","--user-data-dir="+profile,
          "--window-size="+std::to_string(options.width)+","+std::to_string(options.height),
          "--force-device-scale-factor=1","--no-first-run","--no-default-browser-check"});
      if (options.headless) args.push_back("--headless=new");
      args.push_back("about:blank");
      std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
      int error = posix_spawn(&pid,options.executable.c_str(),&actions,&attributes,argv.data(),environ);
      if (error) { pid = -1; throw failure("Could not launch Chrome: " + std::string(std::strerror(error))); }
      original = identity(pid);
      input = in[1]; in[1] = -1; output = out[0]; out[0] = -1; error_output = err[0]; err[0] = -1;
      wake_read = wake_pipe[0]; wake_pipe[0] = -1; wake_write = wake_pipe[1]; wake_pipe[1] = -1;
      for (int fd : {input,output,error_output,wake_read,wake_write}) nonblocking(fd);
    } catch (...) {
      if (pid > 0) { (void)::kill(-pid,SIGKILL); while (waitpid(pid,nullptr,0) < 0 && errno == EINTR) {} pid = -1; }
      for (auto* pair : {&in,&out,&err,&wake_pipe}) for (auto& fd : *pair) close_fd(fd);
      close_fd(null_fd);
      if (actions_ready) posix_spawn_file_actions_destroy(&actions);
      if (attributes_ready) posix_spawnattr_destroy(&attributes);
      throw;
    }
    for (auto* pair : {&in,&out,&err,&wake_pipe}) for (auto& fd : *pair) close_fd(fd);
    close_fd(null_fd); posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
  }
  bool live_group() const {
    if (pid < 1 || (::kill(-pid,0) < 0 && errno == ESRCH)) return false;
#if defined(__linux__)
    if (original) {
      const auto leader = identity(pid);
      if (leader && leader->start != original->start) return false;
      if (auto* dir = opendir("/proc")) {
        bool live = false, conflict = false;
        while (auto* entry = readdir(dir)) {
          char* end = nullptr; long number = std::strtol(entry->d_name,&end,10);
          if (!end || *end || number < 1 || number > INT_MAX) continue;
          const auto candidate = static_cast<pid_t>(number);
          // Shared hosts can contain thousands of unrelated zombies: prefilter
          // with getpgid before opening any per-process stat file.
          if (getpgid(candidate) != pid) continue;
          auto member = identity(candidate);
          if (!member || member->group != pid) continue;
          if (member->session != original->session || member->start < original->start) { conflict = true; break; }
          if (member->state != 'Z' && member->state != 'X' && member->state != 'x') { live = true; break; }
        }
        closedir(dir); return live && !conflict;
      }
    }
#endif
    return true;
  }
  void signal_group(int sig) {
    if (live_group() && ::kill(-pid,sig) < 0 && errno != ESRCH) throw failure("Could not signal owned Chrome process group");
  }
  void reap_group_children() {
    // An embedding application may itself be a subreaper. Once the actual
    // Chrome owner has been waited, retire only adopted children in its group;
    // never use waitpid(-1), which could steal application subprocess statuses.
    if (!child_exited || pid < 1) return;
    for (;;) {
      const auto result = waitpid(-pid,nullptr,WNOHANG);
      if (result > 0 || (result < 0 && errno == EINTR)) continue;
      if (result < 0 && errno != ECHILD) throw failure("Could not reap adopted Chrome group child");
      break;
    }
  }
  void flush() {
    std::lock_guard lock(mutex);
    while (input >= 0 && !outgoing.empty()) {
      auto& front = outgoing.front();
      auto count = network?network->write(front.bytes.data()+front.offset,front.bytes.size()-front.offset):
          ::write(input,front.bytes.data()+front.offset,front.bytes.size()-front.offset);
      if (count < 0) {
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        throw failure("Chrome CDP command pipe closed");
      }
      if (!count) throw failure("Chrome CDP writer made no progress");
      front.offset += static_cast<std::size_t>(count); outgoing_bytes -= static_cast<std::size_t>(count);
      if (front.offset == front.bytes.size()) outgoing.pop_front();
    }
  }
  void run() noexcept {
    sigset_t blocked; sigemptyset(&blocked); sigaddset(&blocked,SIGPIPE);
    (void)pthread_sigmask(SIG_BLOCK,&blocked,nullptr);
    std::string frame;
    for (;;) {
      try {
        if (attached && !ws_buffer.empty()) websocket_receive();
        bool stop,queued; Deadline shutdown; int timeout = 100;
        {
          std::lock_guard lock(mutex);
          std::vector<int> expired;
          for (const auto& [id,p] : pending) {
            if (p.deadline <= Clock::now()) expired.push_back(id);
            else timeout = std::min(timeout,static_cast<int>(std::clamp<long long>(std::chrono::ceil<std::chrono::milliseconds>(p.deadline-Clock::now()).count(),0,100)));
          }
          for (int id : expired) cancel_locked(id);
          stop = closing; shutdown = close_started; queued = !outgoing.empty();
          if (stop) timeout = 10;
        }
        if (!attached && !child_exited) {
          int status = 0; auto result = waitpid(pid,&status,WNOHANG);
          if (result == pid || (result < 0 && errno == ECHILD)) {
            child_exited = true; exited_at = Clock::now(); close_fd(input);
            if (!stop) { fail(std::make_exception_ptr(failure("Chrome process exited"))); stop = true; std::lock_guard lock(mutex); shutdown = close_started; }
          } else if (result < 0 && errno != EINTR) throw failure("Could not reap Chrome process");
        }
        if (attached && stop) {
          // The peer is caller-owned. Only finish our close frame briefly;
          // never waitpid, signal a process, or send CDP Browser.close here.
          if (ws_peer_closed || output < 0 || !queued ||
              Clock::now()-shutdown >= std::min(options.shutdown_timeout,std::chrono::milliseconds(250))) break;
        }
        if (!attached && stop) {
          reap_group_children();
          if (child_exited && !live_group()) { reap_group_children(); break; }
          const auto now = Clock::now();
          if (!sent_term && (now-shutdown >= options.shutdown_timeout*3/5 ||
              (child_exited && now-exited_at >= std::chrono::milliseconds(250)))) {
            signal_group(SIGTERM); sent_term = true; close_fd(input);
          }
          if (!sent_kill && (now-shutdown >= options.shutdown_timeout*9/10 ||
              (sent_term && child_exited && now-exited_at >= std::chrono::milliseconds(350)))) {
            signal_group(SIGKILL); sent_kill = true; close_fd(input);
          }
        }
        std::array<pollfd,4> fds{{{output,static_cast<short>(network?network->read_want:POLLIN),0},{error_output,POLLIN,0},{wake_read,POLLIN,0},
                                  {input,static_cast<short>(queued ? (network?network->write_want:POLLOUT) : 0),0}}};
        if(network&&network->buffered())timeout=0;
        int count = ::poll(fds.data(),fds.size(),timeout);
        if (count < 0 && errno != EINTR) throw failure("CDP pipe poll failed");
        if (fds[2].revents) { std::array<char,256> bytes; while (::read(wake_read,bytes.data(),bytes.size()) > 0) {} }
        if (fds[3].revents & (POLLIN|POLLOUT|POLLERR|POLLHUP)) flush();
        for (int stream = 0; stream < 2; ++stream) {
          int& fd = stream == 0 ? output : error_output;
          if (fd < 0 || (!fds[stream].revents && !(stream==0&&network&&network->buffered()))) continue;
          std::array<char,65536> bytes;
          for (int chunk = 0; chunk < 16; ++chunk) {
            auto n = stream==0&&network?network->read(bytes.data(),bytes.size()): ::read(fd,bytes.data(),bytes.size());
            if (n > 0) {
              if (stream == 1) {
                std::lock_guard lock(mutex); stderr_tail.append(bytes.data(),static_cast<std::size_t>(n));
                if (stderr_tail.size() > stderr_limit) stderr_tail.erase(0,stderr_tail.size()-stderr_limit);
              } else if (attached) {
                const auto limit=options.max_payload_bytes+65536+14;
                if(static_cast<std::size_t>(n)>limit-ws_buffer.size())throw failure("CDP WebSocket buffer exceeds byte budget");
                ws_buffer.append(bytes.data(),static_cast<std::size_t>(n));
                websocket_receive();
              } else {
                std::size_t begin = 0, size = static_cast<std::size_t>(n);
                for (std::size_t i = 0; i < size; ++i) if (bytes[i] == '\0') {
                  if (i-begin > options.max_payload_bytes-frame.size()) throw failure("CDP response exceeds byte budget");
                  frame.append(bytes.data()+begin,i-begin);
                  if (frame.empty()) throw failure("Empty CDP response frame");
                  receive(frame); frame.clear(); begin = i+1;
                }
                if (size-begin > options.max_payload_bytes-frame.size()) throw failure("CDP response exceeds byte budget");
                frame.append(bytes.data()+begin,size-begin);
              }
            } else if (!n) {
              if(stream==0)close_output();else close_fd(fd);
              if (stream == 0) {
                bool stopped; { std::lock_guard lock(mutex); stopped = closing; }
                if (!stopped) throw failure(frame.empty() ? "Chrome closed its CDP response pipe" : "Truncated CDP response");
              }
              break;
            } else if (errno == EINTR) continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            else throw failure("Could not read Chrome CDP pipe");
          }
        }
      } catch (...) {
        fail(std::current_exception()); close_fd(input); close_output(); frame.clear();
        ws_buffer.clear(); ws_message.clear();
      }
    }
    close_fd(input); close_output(); close_fd(error_output);
    { std::lock_guard lock(mutex); finished = true; begin_close_locked(false); changed.notify_all(); }
  }
#endif
};

CdpConnection::CdpConnection(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CdpConnection::~CdpConnection() { try { close(); } catch (...) {} }
std::unique_ptr<CdpConnection> CdpConnection::launch(const LaunchOptions& options) {
  validate(options);
#ifdef _WIN32
  throw failure("Native Chrome pipe transport currently requires POSIX");
#else
  auto connection = std::unique_ptr<CdpConnection>(new CdpConnection(std::make_unique<Impl>(options)));
  auto& state = *connection->impl_;
  const auto deadline=Clock::now()+options.startup_timeout;
  if(state.attached)state.attach_socket(deadline);
  else state.spawn_child();
  try {
    state.io = std::thread([impl=&state] { impl->run(); });
    connection->call("Browser.getVersion",Json::object{},"",deadline);
  } catch (...) {
    auto error = std::current_exception(); connection->close();
    std::string diagnostics; { std::lock_guard lock(state.mutex); diagnostics = state.stderr_tail; }
    try { std::rethrow_exception(error); }
    catch (const std::exception& cause) { throw failure(std::string(cause.what())+(diagnostics.empty() ? "" : ": "+diagnostics)); }
  }
  return connection;
#endif
}
std::unique_ptr<CdpConnection> CdpConnection::connect_websocket(const LaunchOptions& options) {
  if(options.cdp_endpoint.empty())throw failure("A WebSocket endpoint is required");
  validate(options);
#ifdef _WIN32
  throw failure("Native browser WebSocket transport currently requires POSIX");
#else
  auto connection=std::unique_ptr<CdpConnection>(new CdpConnection(std::make_unique<Impl>(options)));
  auto& state=*connection->impl_;
  state.attach_socket(Clock::now()+options.startup_timeout,false);
  try{state.io=std::thread([impl=&state]{impl->run();});}
  catch(...){connection->close();throw;}
  return connection;
#endif
}
Json CdpConnection::call(const std::string& method, Json params, const std::string& session, Deadline deadline) {
  if (method.empty() || method.size() > 65536 || nul(method) || nul(session) || session.size() > 65536 || !params.is_object())
    throw failure("Invalid CDP method, params or session");
  if (deadline <= Clock::now()) throw failure("CDP request timed out");
  auto& state = *impl_; int id; std::future<Json> result;
  {
    std::lock_guard lock(state.mutex);
    if (state.terminal) std::rethrow_exception(state.terminal);
    if (state.closing) throw failure("CDP connection closed");
    if (state.pending.size() >= state.options.max_pending) throw failure("CDP pending request budget exceeded");
    id = state.allocate();
    Json request{{"id",id},{"method",method},{"params",std::move(params)}};
    if (!session.empty()) request["sessionId"] = session;
    auto bytes = state.encode(request.dump({command_limit-1,64,1024*1024,command_limit-1}));
    if (bytes.size() > outgoing_limit-state.outgoing_bytes || state.outgoing.size() >= state.options.max_pending+64)
      throw failure("CDP command writer is full");
    Impl::Pending item; item.deadline = deadline; result = item.result.get_future();
    state.pending.emplace(id,std::move(item));
    const auto size = bytes.size();
    try { state.outgoing.push_back({id,std::move(bytes),0}); }
    catch (...) { state.pending.erase(id); throw; }
    state.outgoing_bytes += size; ++state.commands; state.wake();
  }
  if (result.wait_until(deadline) == std::future_status::timeout) {
    std::lock_guard lock(state.mutex); state.cancel_locked(id); state.wake();
  }
  return result.get();
}
std::optional<Json> CdpConnection::poll(std::chrono::milliseconds timeout) {
  if (timeout.count() < 0 || timeout > std::chrono::hours(24)) throw failure("Invalid CDP event timeout");
  auto& state = *impl_; std::unique_lock lock(state.mutex);
  state.changed.wait_for(lock,timeout,[&] { return !state.events.empty() || state.closing || state.terminal; });
  if (!state.events.empty()) {
    auto event = std::move(state.events.front()); state.events.pop_front(); state.event_bytes -= event.bytes;
    return std::move(event.value);
  }
  if (state.terminal) std::rethrow_exception(state.terminal);
  return std::nullopt;
}
ClientStats CdpConnection::stats() const {
  const auto& state = *impl_;
  std::lock_guard lock(state.mutex);
  ClientStats result;
  result.commands = state.commands;
  result.responses = state.responses;
  result.pending = state.pending.size();
  result.queued_event_bytes = state.event_bytes;
  result.dropped_events = state.dropped_events;
  return result;
}
void CdpConnection::set_event_handler(std::function<void(const Json&)> handler) {
  if(!handler){set_event_handler_with_disposition({});return;}
  set_event_handler_with_disposition([observer=std::move(handler)](const Json& event){observer(event);return false;});
}
void CdpConnection::set_event_handler_with_disposition(std::function<bool(const Json&)> handler) {
  std::lock_guard lock(impl_->mutex);
  if(impl_->closing && handler)throw failure("CDP connection closed");
  impl_->event_handler=std::move(handler);
}
void CdpConnection::close() {
  if (!impl_) return;
  auto& state = *impl_; std::lock_guard close_lock(state.close_mutex);
  { std::lock_guard lock(state.mutex); state.begin_close_locked(true); }
#ifndef _WIN32
  if (state.io.joinable()) state.io.join();
  else if (state.pid > 0 && !state.child_exited) {
    state.signal_group(SIGKILL);
    while (waitpid(state.pid,nullptr,0) < 0 && errno == EINTR) {}
    state.child_exited = true;
    while (state.live_group()) {
      state.signal_group(SIGKILL); state.reap_group_children();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    state.reap_group_children();
  }
  {
    std::lock_guard lock(state.mutex);
    state.close_output();
    for(int* fd:{&state.input,&state.error_output,&state.wake_write,&state.wake_read})close_fd(*fd);
    // Retained Page views may keep a closed Transport alive. Release queued
    // telemetry and writer buffers now; caller-owned response packets survive.
    state.events.clear(); state.event_bytes = 0;
    state.outgoing.clear(); state.outgoing_bytes = 0;
    state.event_handler = {};
  }
#endif
  if (state.owned_profile) {
    std::error_code error; std::filesystem::remove_all(state.profile,error);
    if (error) throw failure("Could not remove owned Chrome profile: "+error.message());
    state.owned_profile = false;
  }
}
} // namespace suitecut::detail
