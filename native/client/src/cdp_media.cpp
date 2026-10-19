// SPDX-License-Identifier: MIT
#include "cdp_media.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace suitecut::detail {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::size_t queue_limit=16*1024*1024,spool_limit=256*1024*1024;
constexpr std::size_t artifact_limit=32*1024*1024,frame_limit=4*1024*1024;
std::string text(const Json& j,const char* key,std::string fallback={}) {
  return j.contains(key)?j.at(key).as_string():std::move(fallback);
}
int integer(const Json& j,const char* key,int fallback,int low,int high) {
  const auto n=j.contains(key)?j.at(key).as_int():fallback;
  if(n<low||n>high)throw std::invalid_argument(std::string("Invalid media ")+key);
  return static_cast<int>(n);
}
std::string media_executable(const Json& fields,const char* key,const char* fallback) {
  auto value=text(fields,key,fallback);
  if(value.empty()||value.size()>4096||value.find('\0')!=std::string::npos||
     !std::filesystem::path(value).is_absolute())
    throw std::invalid_argument(std::string(key)+" must be an absolute executable path");
#ifndef _WIN32
  std::error_code error;
  if(!std::filesystem::is_regular_file(value,error)||error||::access(value.c_str(),X_OK))
    throw std::invalid_argument(std::string(key)+" must name an executable file");
#endif
  return value;
}
double seconds(Clock::duration d){return std::chrono::duration<double>(d).count();}
std::string decimal(double v){std::ostringstream s;s<<std::setprecision(15)<<v;return s.str();}
std::string decode(const std::string& input,std::size_t limit) {
  if(input.empty()||input.size()%4||input.size()/4*3>limit+2)
    throw std::runtime_error("Invalid or oversized media base64 frame");
  auto digit=[](unsigned char c)->int {
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    if(c=='+')return 62;
    if(c=='/')return 63;
    return -1;
  };
  std::string out;out.reserve(input.size()/4*3);
  for(std::size_t i=0;i<input.size();i+=4) {
    const bool last=i+4==input.size();int a=digit(input[i]),b=digit(input[i+1]);
    int c=input[i+2]=='='?-1:digit(input[i+2]),d=input[i+3]=='='?-1:digit(input[i+3]);
    if(a<0||b<0||(c<0&&input[i+2]!='=')||(d<0&&input[i+3]!='=')||
       (input[i+2]=='='&&(!last||input[i+3]!='='||(b&15)))||
       (input[i+3]=='='&&(!last||(c>=0&&(c&3)))))
      throw std::runtime_error("Invalid media base64 padding");
    out.push_back(static_cast<char>((a<<2)|(b>>4)));
    if(c>=0)out.push_back(static_cast<char>((b<<4)|(c>>2)));
    if(d>=0)out.push_back(static_cast<char>((c<<6)|d));
  }
  if(out.size()>limit)throw std::runtime_error("Decoded media frame exceeds byte budget");
  return out;
}
std::pair<int,int> jpeg_size(const std::string& data) {
  if(data.size()<4||static_cast<unsigned char>(data[0])!=255||static_cast<unsigned char>(data[1])!=216)
    throw std::runtime_error("Screencast source is not JPEG");
  std::size_t at=2;
  while(at+3<data.size()) {
    if(static_cast<unsigned char>(data[at++])!=255)continue;
    unsigned marker=static_cast<unsigned char>(data[at++]);
    while(marker==255&&at<data.size())marker=static_cast<unsigned char>(data[at++]);
    if(marker==0||marker==216||marker==217||(marker>=208&&marker<=215))continue;
    if(at+2>data.size())break;
    const auto n=(static_cast<unsigned char>(data[at])<<8)|static_cast<unsigned char>(data[at+1]);
    if(n<2||at+static_cast<std::size_t>(n)>data.size())break;
    if((marker>=192&&marker<=195)||(marker>=197&&marker<=199)||(marker>=201&&marker<=203)||(marker>=205&&marker<=207)) {
      if(n<7)break;
      return {(static_cast<unsigned char>(data[at+5])<<8)|static_cast<unsigned char>(data[at+6]),
              (static_cast<unsigned char>(data[at+3])<<8)|static_cast<unsigned char>(data[at+4])};
    }
    at+=static_cast<std::size_t>(n);
  }
  throw std::runtime_error("JPEG frame dimensions are unavailable");
}
std::string read_file(const std::filesystem::path& path,std::size_t limit) {
  const auto size=std::filesystem::file_size(path);
  if(size>limit)throw std::runtime_error("Media artifact exceeds byte budget");
  std::ifstream f(path,std::ios::binary);std::string s(static_cast<std::size_t>(size),'\0');
  if(!f.read(s.data(),static_cast<std::streamsize>(s.size())))throw std::runtime_error("Cannot read media artifact");
  return s;
}
void write_file(const std::filesystem::path& path,const std::string& bytes) {
  std::ofstream f(path,std::ios::binary|std::ios::trunc);
  if(!f||!f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()))||!f.flush())
    throw std::runtime_error("Cannot write private media artifact");
}
#ifndef _WIN32
struct Destination {
  int directory=-1;
  std::filesystem::path path,spool;
  std::string basename;
  explicit Destination(const std::string& input) {
    if(input.empty()||input.size()>4096||input.find('\0')!=std::string::npos||!std::filesystem::path(input).is_absolute())
      throw std::invalid_argument("Media output requires an absolute path without null bytes");
    path=std::filesystem::path(input).lexically_normal();basename=path.filename().string();
    if(basename.empty()||basename=="."||basename=="..")throw std::invalid_argument("Media output requires a filename");
    directory=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(directory<0)throw std::runtime_error("Cannot open media output root");
    try {
      for(const auto& component:path.parent_path().relative_path()) {
        int next=::openat(directory,component.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(next<0)throw std::runtime_error("Media output parent must exist and contain no symlink components");
        ::close(directory);directory=next;
      }
      struct stat st{};
      if(::fstatat(directory,basename.c_str(),&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)
        throw std::runtime_error("Media output already exists or cannot be inspected");
      static std::atomic<unsigned long long> nonce{0};
      for(int attempt=0;attempt<100;++attempt) {
        auto name=".suitecut-media-"+std::to_string(::getpid())+"-"+std::to_string(++nonce);
        if(::mkdirat(directory,name.c_str(),0700)==0){
          // Keep file operations bound to the opened parent directory, even if
          // an ancestor is renamed or replaced after validation.
#ifdef __linux__
          spool=std::filesystem::path("/proc")/std::to_string(::getpid())/"fd"/std::to_string(directory)/name;
#else
          spool=path.parent_path()/name;
#endif
          return;
        }
        if(errno!=EEXIST)throw std::runtime_error("Cannot create private media spool");
      }
      throw std::runtime_error("Cannot allocate private media spool");
    }catch(...){::close(directory);directory=-1;throw;}
  }
  ~Destination(){std::error_code e;if(!spool.empty())std::filesystem::remove_all(spool,e);if(directory>=0)::close(directory);}
  Destination(const Destination&)=delete;
  void commit(const std::filesystem::path& source,const std::string& name={}) {
    const auto& output=name.empty()?basename:name;
    if(::linkat(AT_FDCWD,source.c_str(),directory,output.c_str(),0))
      throw std::runtime_error("Cannot commit media artifact without overwriting an existing file");
  }
  void undo(const std::filesystem::path& source,const std::string& name) noexcept {
    struct stat a{},b{};
    if(::stat(source.c_str(),&a)==0&&::fstatat(directory,name.c_str(),&b,AT_SYMLINK_NOFOLLOW)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino)
      (void)::unlinkat(directory,name.c_str(),0);
  }
};
#endif
} // namespace

struct CdpMedia::Impl {
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::function<void(CdpAutomatic)> enqueue;
  bool closed=false;
  std::thread worker;
  struct Source {double received=0,cdp_timestamp=0;std::string name;std::size_t bytes=0;int width=0,height=0;};
  struct Recording {
    std::string session,path,error,ffmpeg,ffprobe;
    double fps=30;
    Clock::time_point start=Clock::now(),expires;
    bool accepting=true,stopping=false,expired=false;
    std::size_t received=0,dropped=0,spool_bytes=0,queued=0,working=0;
    std::vector<Source> sources;
#ifndef _WIN32
    std::unique_ptr<Destination> destination;
#endif
  };
  struct Frame {std::shared_ptr<Recording> recording;std::string data;double received=0,timestamp=0;};
  std::map<std::string,std::shared_ptr<Recording>> recordings;
  std::map<std::string,Json> last_recordings;
  std::deque<Frame> frames;
  std::size_t queue_bytes=0,peak_queue_bytes=0;
  struct Profile {Clock::time_point expires;bool expired=false;};
  std::map<std::string,Profile> profiles;
  bool tracing=false,trace_ending=false;
  std::string trace_stream;
  Clock::time_point trace_expires;
#ifndef _WIN32
  pid_t helper=0;
#endif
  explicit Impl(std::function<void(CdpAutomatic)> f):enqueue(std::move(f)){}
  ~Impl(){close();}
  void ensure_worker_locked() {
    if(!worker.joinable())worker=std::thread([this]{work();});
    changed.notify_all();
  }
  void cancel_all() {
    {
      std::lock_guard lock(mutex);if(closed)return;closed=true;
#ifndef _WIN32
      if(helper>0)::kill(-helper,SIGKILL);
#endif
      for(auto& [session,r]:recordings){(void)session;r->accepting=false;}
      for(auto& f:frames){++f.recording->dropped;--f.recording->queued;}
      frames.clear();queue_bytes=0;changed.notify_all();
    }
  }
  void close() {
    cancel_all();
    if(worker.joinable())worker.join();
  }
  Json status(const Recording& r)const {
    return Json{{"state",r.stopping?"stopping":r.expired?"expired":"recording"},{"path",r.path},
      {"fps",r.fps},{"receivedFrames",r.received},{"sourceFrames",r.sources.size()},
      {"droppedFrames",r.dropped},{"spoolBytes",r.spool_bytes},{"queuedFrameBytes",queue_bytes},
      {"peakQueuedFrameBytes",peak_queue_bytes},{"queueByteLimit",queue_limit},{"spoolByteLimit",spool_limit},
      {"error",r.error.empty()?Json{}:Json(r.error)}};
  }
  void work() {
    for(;;) {
      Frame frame;
      std::vector<CdpAutomatic> expired;
      {
        std::unique_lock lock(mutex);
        if(closed)return;
        const auto now=Clock::now();
        for(auto& [session,r]:recordings)if(r->accepting&&now>=r->expires) {
          r->accepting=false;r->expired=true;expired.push_back({"Page.stopScreencast",session,Json::object{}});
        }
        if(tracing&&!trace_ending&&now>=trace_expires) {
          trace_ending=true;expired.push_back({"Tracing.end","",Json::object{}});
        }
        for(auto& [session,p]:profiles)if(!p.expired&&now>=p.expires) {
          p.expired=true;expired.push_back({"Profiler.stop",session,Json::object{}});
        }
        if(!frames.empty()) {
          frame=std::move(frames.front());frames.pop_front();queue_bytes-=frame.data.size();
          --frame.recording->queued;++frame.recording->working;
        }else if(expired.empty()) {
          auto next=Clock::time_point::max();
          for(const auto&[session,r]:recordings){(void)session;if(r->accepting)next=std::min(next,r->expires);}
          if(tracing&&!trace_ending)next=std::min(next,trace_expires);
          for(const auto&[session,p]:profiles){(void)session;if(!p.expired)next=std::min(next,p.expires);}
          if(next==Clock::time_point::max())changed.wait(lock);
          else changed.wait_until(lock,next);
          continue;
        }
      }
      for(auto& a:expired)try{enqueue(std::move(a));}catch(...){}
      if(!frame.recording)continue;
      auto r=frame.recording;
      bool failed=false;
      try {
        const auto bytes=decode(frame.data,frame_limit);
        const auto [width,height]=jpeg_size(bytes);
        if(width<2||height<2||width>3840||height>2160)
          throw std::runtime_error("Recording frames must be within 2..3840 by 2..2160");
#ifndef _WIN32
        std::size_t index;
        {std::lock_guard lock(mutex);index=r->sources.size();
          if(r->spool_bytes+bytes.size()>spool_limit||index>=72000)
            throw std::runtime_error("Recording source spool budget exceeded");}
        const auto name="source-"+std::to_string(index)+".jpg";
        write_file(r->destination->spool/name,bytes);
        std::lock_guard lock(mutex);r->sources.push_back({frame.received,frame.timestamp,name,bytes.size(),width,height});r->spool_bytes+=bytes.size();
#else
        throw std::runtime_error("Native media recording requires POSIX process support");
#endif
      }catch(const std::exception& error) {
        std::lock_guard lock(mutex);if(r->error.empty())r->error=error.what();r->accepting=false;++r->dropped;failed=true;
      }
      if(failed)try{enqueue({"Page.stopScreencast",r->session,Json::object{}});}catch(...){}
      {std::lock_guard lock(mutex);--r->working;changed.notify_all();}
    }
  }
#ifndef _WIN32
  void run_tool(std::vector<std::string> arguments,const std::filesystem::path& output,
                const std::filesystem::path& error,Deadline deadline,
                const std::filesystem::path& artifact={},std::size_t base_bytes=0) {
    cdp_check_active(deadline);
    int out=::open(output.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    int err=::open(error.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(out<0||err<0){if(out>=0)::close(out);if(err>=0)::close(err);throw std::runtime_error("Cannot open private encoder logs");}
    posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;
    if(posix_spawn_file_actions_init(&actions)){::close(out);::close(err);throw std::runtime_error("Cannot initialize encoder descriptors");}
    if(posix_spawnattr_init(&attributes)){posix_spawn_file_actions_destroy(&actions);::close(out);::close(err);throw std::runtime_error("Cannot initialize encoder attributes");}
    pid_t pid=0;
    try {
      sigset_t mask,defaults;sigemptyset(&mask);sigemptyset(&defaults);
      for(int sig:{SIGPIPE,SIGINT,SIGTERM,SIGQUIT})sigaddset(&defaults,sig);
      if(posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF)||
         posix_spawnattr_setpgroup(&attributes,0)||posix_spawnattr_setsigmask(&attributes,&mask)||posix_spawnattr_setsigdefault(&attributes,&defaults)||
         posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0)||
         posix_spawn_file_actions_adddup2(&actions,out,1)||posix_spawn_file_actions_adddup2(&actions,err,2))
        throw std::runtime_error("Cannot configure encoder process");
      std::vector<char*> argv;for(auto& a:arguments)argv.push_back(a.data());argv.push_back(nullptr);
      const int result=posix_spawn(&pid,arguments.front().c_str(),&actions,&attributes,argv.data(),environ);
      if(result)throw std::runtime_error("Cannot spawn media tool: "+std::string(std::strerror(result)));
      {std::lock_guard lock(mutex);if(closed){::kill(-pid,SIGKILL);}helper=pid;}
      posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);::close(out);::close(err);out=err=-1;
    }catch(...){posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);::close(out);::close(err);throw;}
    int status=0;
    try {
      for(;;) {
        const auto waited=::waitpid(pid,&status,WNOHANG);
        if(waited==pid)break;
        if(waited<0&&errno!=EINTR)throw std::runtime_error("Cannot reap media tool");
        cdp_check_active(deadline);
        {std::lock_guard lock(mutex);if(closed)throw std::runtime_error("Media runtime closed during encoding");}
        std::error_code error_code;
        if(std::filesystem::file_size(error,error_code)>65536&&!error_code)
          throw std::runtime_error("Media encoder error log exceeds 64 KiB");
        if(std::filesystem::file_size(output,error_code)>1024*1024&&!error_code)
          throw std::runtime_error("Media tool report exceeds 1 MiB");
        if(!artifact.empty()&&std::filesystem::file_size(artifact,error_code)>spool_limit-base_bytes&&!error_code)
          throw std::runtime_error("Recording source and encoded artifact exceed 256 MiB spool budget");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }catch(...) {
      ::kill(-pid,SIGKILL);while(::waitpid(pid,&status,0)<0&&errno==EINTR){}
      {std::lock_guard lock(mutex);helper=0;}throw;
    }
    {std::lock_guard lock(mutex);helper=0;}
    if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)
      throw std::runtime_error("Media tool failed: "+read_file(error,65536));
  }
#endif
};

CdpMedia::CdpMedia(std::function<void(CdpAutomatic)> enqueue):impl_(std::make_unique<Impl>(std::move(enqueue))){}
CdpMedia::~CdpMedia()=default;
void CdpMedia::close(){impl_->close();}
void CdpMedia::cancel_all(){impl_->cancel_all();}
void CdpMedia::forget(const std::string& session) {
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  const auto record=s.recordings.find(session);
  if(record!=s.recordings.end()){record->second->accepting=false;record->second->stopping=true;}
  for(auto frame=s.frames.begin();frame!=s.frames.end();) {
    if(frame->recording->session!=session){++frame;continue;}
    s.queue_bytes-=frame->data.size();--frame->recording->queued;++frame->recording->dropped;
    frame=s.frames.erase(frame);
  }
  s.recordings.erase(session);s.profiles.erase(session);s.last_recordings.erase(session);s.changed.notify_all();
}
void CdpMedia::shutdown(CdpRuntime& runtime,Deadline deadline) {
  auto& s=*impl_;std::vector<CdpAutomatic> cleanup;
  {
    std::lock_guard lock(s.mutex);
    for(const auto&[session,r]:s.recordings){(void)r;cleanup.push_back({"Page.stopScreencast",session,Json::object{}});}
    for(const auto&[session,p]:s.profiles){(void)p;cleanup.push_back({"Profiler.stop",session,Json::object{}});}
    if(s.tracing&&!s.trace_ending)cleanup.push_back({"Tracing.end","",Json::object{}});
    if(!s.trace_stream.empty())cleanup.push_back({"IO.close","",Json{{"handle",s.trace_stream}}});
  }
  CdpCleanupScope scope;
  for(auto& c:cleanup)try{runtime.wire->call(c.method,std::move(c.params),c.session,deadline);}catch(...){}
}
bool CdpMedia::observe(const Json& event) {
  if(!event.contains("method")||!event.contains("params"))return false;
  auto& s=*impl_;const auto method=event.at("method").as_string();
  const auto& params=event.at("params");
  if(method=="Tracing.tracingComplete") {
    std::lock_guard lock(s.mutex);
    if(s.tracing){s.trace_stream=text(params,"stream");s.trace_ending=true;s.changed.notify_all();}return false;
  }
  if(method!="Page.screencastFrame")return false;
  const auto session=text(event,"sessionId");
  bool stop=false;
  {
    std::lock_guard lock(s.mutex);const auto found=s.recordings.find(session);
    if(found==s.recordings.end()||s.closed)return false;
    auto& r=*found->second;++r.received;
    if(r.accepting) {
      const auto& data=params.at("data").as_string();
      if(data.size()>frame_limit*4/3+4) {r.error="Screencast encoded frame exceeds 4 MiB decoded budget";r.accepting=false;++r.dropped;stop=true;}
      else {
        while(!s.frames.empty()&&(s.frames.size()>=8||s.queue_bytes+data.size()>queue_limit)) {
          auto& old=s.frames.front();s.queue_bytes-=old.data.size();++old.recording->dropped;--old.recording->queued;s.frames.pop_front();
        }
        double timestamp=0;
        if(params.contains("metadata")&&params.at("metadata").contains("timestamp"))
          timestamp=params.at("metadata").at("timestamp").as_double();
        s.frames.push_back({found->second,data,seconds(Clock::now()-r.start),timestamp});
        s.queue_bytes+=data.size();s.peak_queue_bytes=std::max(s.peak_queue_bytes,s.queue_bytes);++r.queued;s.changed.notify_all();
      }
    }else ++r.dropped;
  }
  // The protocol envelope session and the numeric frame acknowledgement ID
  // are different identifiers. Acknowledgement never waits on this I/O thread.
  s.enqueue({"Page.screencastFrameAck",session,Json{{"sessionId",params.at("sessionId")}}});
  if(stop)s.enqueue({"Page.stopScreencast",session,Json::object{}});
  return true;
}

Json CdpMedia::command(CdpRuntime& runtime,CdpPage& page,const Json& fields,Deadline deadline) {
#ifdef _WIN32
  (void)runtime;(void)page;(void)fields;(void)deadline;
  throw std::runtime_error("Native media helpers require POSIX process support");
#else
  cdp_check_active(deadline);auto& s=*impl_;
  const auto family=fields.at("command").as_string(),action=text(fields,"action","status");
  if(!page.frame_id.empty()||fields.contains("frame")||
      (fields.contains("framePath")&&(!fields.at("framePath").is_array()||!fields.at("framePath").as_array().empty())))
    throw std::invalid_argument("Media commands require a page, not a selected frame");
  std::vector<std::string> allowed{"command","action","pageId","id","version","timeoutMs","api","framePath"};
  if(action=="start"&&family=="recording")allowed.insert(allowed.end(),{"path","fps","quality","maxWidth","maxHeight","maxDurationMs","ffmpegPath","ffprobePath"});
  if(action=="start"&&family=="tracing")allowed.insert(allowed.end(),{"categories","maxDurationMs"});
  if(action=="start"&&family=="profiler")allowed.insert(allowed.end(),{"samplingIntervalUs","maxDurationMs"});
  if(action=="stop"&&(family=="tracing"||family=="profiler"))allowed.push_back("path");
  for(const auto&[key,value]:fields.as_object()) {
    (void)value;
    if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())
      throw std::invalid_argument("Unsupported native media field: "+key);
  }
  {std::lock_guard lock(s.mutex);if(s.closed)throw std::runtime_error("Media runtime is closed");}
  if(family=="recording") {
    if(action=="start") {
      double fps=fields.contains("fps")?fields.at("fps").as_double():30;
      if(!std::isfinite(fps)||fps<1||fps>120)throw std::invalid_argument("Recording fps must be within 1..120");
      const auto path=fields.at("path").as_string();
      const auto extension=std::filesystem::path(path).extension().string();
      if(extension!=".mp4"&&extension!=".webm")throw std::invalid_argument("Recording output must be .mp4 or .webm");
      const auto ffmpeg=media_executable(fields,"ffmpegPath","/usr/bin/ffmpeg");
      const auto ffprobe=media_executable(fields,"ffprobePath","/usr/bin/ffprobe");
      auto r=std::make_shared<Impl::Recording>();r->session=page.session;r->path=path;r->fps=fps;
      r->ffmpeg=ffmpeg;r->ffprobe=ffprobe;
      r->destination=std::make_unique<Destination>(path);
      r->path=r->destination->path.string();
      struct stat st{};const auto sidecar=r->destination->basename+".json";
      if(::fstatat(r->destination->directory,sidecar.c_str(),&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)
        throw std::runtime_error("Recording metadata output already exists or cannot be inspected");
      const auto duration=integer(fields,"maxDurationMs",600000,1,600000);
      r->start=Clock::now();r->expires=Clock::time_point::max();
      Json params{{"format","jpeg"},{"quality",integer(fields,"quality",80,1,100)},{"everyNthFrame",1}};
      if(fields.contains("maxWidth"))params["maxWidth"]=integer(fields,"maxWidth",1280,2,3840);
      if(fields.contains("maxHeight"))params["maxHeight"]=integer(fields,"maxHeight",720,2,2160);
      {
        std::lock_guard lock(s.mutex);
        if(s.recordings.contains(page.session))throw std::runtime_error("This page already has an active recording");
        if(s.recordings.size()>=4)throw std::runtime_error("At most four page recordings may be active");
        s.recordings.emplace(page.session,r);s.last_recordings.erase(page.session);s.ensure_worker_locked();
      }
      try{runtime.call(page,"Page.startScreencast",std::move(params),deadline);}
      catch(...){
        {CdpCleanupScope cleanup;try{runtime.call(page,"Page.stopScreencast",Json::object{},Clock::now()+std::chrono::milliseconds(250));}catch(...){}}
        std::lock_guard lock(s.mutex);r->accepting=false;s.recordings.erase(page.session);throw;
      }
      std::lock_guard lock(s.mutex);r->expires=r->start+std::chrono::milliseconds(duration);s.changed.notify_all();return s.status(*r);
    }
    std::shared_ptr<Impl::Recording> r;
    {
      std::lock_guard lock(s.mutex);const auto found=s.recordings.find(page.session);
      if(found==s.recordings.end()) {
        if(action=="status")return s.last_recordings.contains(page.session)?s.last_recordings.at(page.session):Json{{"state","idle"}};
        throw std::runtime_error("This page has no active recording");
      }
      r=found->second;if(action=="status")return s.status(*r);
      if(action!="stop"&&action!="cancel")throw std::invalid_argument("Unknown recording action: "+action);
      r->accepting=false;r->stopping=true;
    }
    const auto stopped=Clock::now();
    auto erase=[&]{std::lock_guard lock(s.mutex);s.recordings.erase(page.session);};
    try {
      runtime.call(page,"Page.stopScreencast",Json::object{},deadline);
      {
        std::unique_lock lock(s.mutex);
        while(r->queued||r->working){lock.unlock();cdp_check_active(deadline);lock.lock();s.changed.wait_for(lock,std::chrono::milliseconds(10));}
      }
      if(action=="cancel"){erase();return Json{{"state","canceled"}};}
      if(!r->error.empty())throw std::runtime_error("Recording failed: "+r->error);
      if(r->sources.empty())throw std::runtime_error("Chrome delivered no screencast frames");
      const double duration=std::min(seconds(stopped-r->start),seconds(r->expires-r->start));
      const auto count=static_cast<std::size_t>(std::max(1.0,std::ceil(duration*r->fps)));
      if(count>72000)throw std::runtime_error("Recording output exceeds 72000 resampled frames");
      std::ofstream manifest(r->destination->spool/"frames.ffconcat",std::ios::binary);
      if(!manifest)throw std::runtime_error("Cannot create recording frame manifest");
      manifest<<"ffconcat version 1.0\n";
      Json::array runs,sources;std::size_t source=0,last_source=static_cast<std::size_t>(-1),used=0;
      for(std::size_t i=0;i<count;++i) {
        cdp_check_active(deadline);const double pts=static_cast<double>(i)/r->fps;
        while(source+1<r->sources.size()&&r->sources[source+1].received<=pts)++source;
        manifest<<"file '"<<r->sources[source].name<<"'\noption framerate "<<decimal(r->fps)<<"\n";
        if(source!=last_source){runs.emplace_back(Json{{"firstOutputFrame",i},{"count",1},{"sourceFrame",source}});++used;last_source=source;}
        else runs.back()["count"]=runs.back().at("count").as_int()+1;
      }
      manifest.close();if(!manifest)throw std::runtime_error("Cannot finish recording manifest");
      const auto width=r->sources.front().width/2*2,height=r->sources.front().height/2*2;
      const auto video=r->destination->spool/("encoded"+std::filesystem::path(r->path).extension().string());
      const auto manifest_bytes=std::filesystem::file_size(r->destination->spool/"frames.ffconcat");
      if(manifest_bytes>spool_limit-r->spool_bytes)throw std::runtime_error("Recording manifest exceeds spool budget");
      std::vector<std::string> args{r->ffmpeg,"-nostdin","-hide_banner","-loglevel","error","-threads","2","-filter_threads","1",
        "-f","concat","-safe","0","-i",(r->destination->spool/"frames.ffconcat").string(),"-an","-vf",
        "scale="+std::to_string(width)+":"+std::to_string(height)+":force_original_aspect_ratio=decrease,pad="+std::to_string(width)+":"+std::to_string(height)+":(ow-iw)/2:(oh-ih)/2",
        "-r",decimal(r->fps),"-frames:v",std::to_string(count),"-pix_fmt","yuv420p"};
      if(std::filesystem::path(r->path).extension()==".mp4")args.insert(args.end(),{"-c:v","libx264","-preset","ultrafast","-threads","2","-movflags","+faststart"});
      else args.insert(args.end(),{"-c:v","libvpx-vp9","-deadline","realtime","-cpu-used","8","-threads","2"});
      args.push_back(video.string());
      s.run_tool(std::move(args),r->destination->spool/"encoder.stdout",r->destination->spool/"encoder.stderr",deadline,video,r->spool_bytes+manifest_bytes);
      s.run_tool({r->ffprobe,"-v","error","-count_frames","-select_streams","v:0","-show_entries",
        "stream=codec_name,width,height,r_frame_rate,avg_frame_rate,nb_read_frames:format=duration","-of","json",video.string()},
        r->destination->spool/"probe.json",r->destination->spool/"probe.stderr",deadline);
      const auto probe=Json::parse(read_file(r->destination->spool/"probe.json",1024*1024));
      const auto& stream=probe.at("streams").at(0);
      if(stream.at("nb_read_frames").as_string()!=std::to_string(count)||stream.at("width").as_int()!=width||stream.at("height").as_int()!=height)
        throw std::runtime_error("Encoded recording frame count or dimensions failed ffprobe verification");
      const auto rate=stream.at("avg_frame_rate").as_string();const auto slash=rate.find('/');
      const auto numerator=std::stod(rate.substr(0,slash));
      const auto denominator=slash==std::string::npos?1:std::stod(rate.substr(slash+1));
      if(!std::isfinite(numerator)||!std::isfinite(denominator)||denominator<=0||
         std::abs(numerator/denominator-r->fps)>1e-5*std::max(1.0,r->fps))
        throw std::runtime_error("Encoded recording cadence failed ffprobe verification");
      for(const auto& f:r->sources)sources.emplace_back(Json{{"receivedAtSeconds",f.received},{"cdpTimestampSeconds",f.cdp_timestamp},{"bytes",f.bytes},{"width",f.width},{"height",f.height}});
      std::size_t received,dropped,peak_queue;
      {std::lock_guard lock(s.mutex);received=r->received;dropped=r->dropped;peak_queue=s.peak_queue_bytes;}
      Json result{{"state","completed"},{"path",r->path},{"metadataPath",r->path+".json"},{"fps",r->fps},
        {"durationSeconds",static_cast<double>(count)/r->fps},{"captureDurationSeconds",duration},{"outputFrames",count},
        {"receivedFrames",received},{"sourceFrames",r->sources.size()},{"usedSourceFrames",used},{"duplicatedFrames",count-used},
        {"droppedFrames",dropped},{"spoolBytes",r->spool_bytes},{"peakQueuedFrameBytes",peak_queue},
        {"queueByteLimit",queue_limit},{"spoolByteLimit",spool_limit},{"resampling","sample-and-hold"},
        {"outputTimestampRule","frameIndex / fps, starting at zero; first source frame backfills earlier ticks"},
        {"sourceDelivery","Chrome compositor screencast; output cadence does not imply unique rendered frames"},
        {"expired",r->expired},{"verification",probe}};
      auto metadata=result;metadata["sourceTimestamps"]=Json(std::move(sources));metadata["sampleRuns"]=Json(std::move(runs));
      const auto metadata_file=r->destination->spool/"metadata.json";
      write_file(metadata_file,metadata.dump({16*1024*1024,64,1024*1024,16*1024*1024})+"\n");
      std::uintmax_t disk_bytes=0;
      for(const auto& file:std::filesystem::directory_iterator(r->destination->spool))
        if(file.is_regular_file())disk_bytes+=file.file_size();
      if(disk_bytes>spool_limit)throw std::runtime_error("Recording artifact and metadata exceed total 256 MiB spool budget");
      result["peakSpoolBytes"]=disk_bytes;
      cdp_check_active(deadline);
      r->destination->commit(metadata_file,r->destination->basename+".json");
      try{r->destination->commit(video);}catch(...){r->destination->undo(metadata_file,r->destination->basename+".json");throw;}
      {std::lock_guard lock(s.mutex);s.last_recordings.insert_or_assign(page.session,result);}
      erase();return result;
    }catch(...) {
      {CdpCleanupScope cleanup;try{runtime.call(page,"Page.stopScreencast",Json::object{},Clock::now()+std::chrono::milliseconds(250));}catch(...){}}
      erase();throw;
    }
  }
  if(family=="tracing") {
    if(action=="status"){std::lock_guard lock(s.mutex);return Json{{"active",s.tracing},{"ending",s.trace_ending},{"streamReady",!s.trace_stream.empty()}};}
    if(action=="start") {
      const auto categories=text(fields,"categories","devtools.timeline,v8,blink.user_timing");
      if(categories.size()>4096)throw std::invalid_argument("Tracing categories exceed 4096 bytes");
      const auto duration=integer(fields,"maxDurationMs",600000,1,600000);
      {std::lock_guard lock(s.mutex);if(s.tracing)throw std::runtime_error("Browser tracing is already active");s.tracing=true;s.trace_ending=false;s.trace_stream.clear();s.trace_expires=Clock::time_point::max();s.ensure_worker_locked();}
      try{runtime.wire->call("Tracing.start",Json{{"categories",categories},{"options","record-continuously"},{"transferMode","ReturnAsStream"},{"streamFormat","json"},{"streamCompression","none"}},"",deadline);}
      catch(...){std::lock_guard lock(s.mutex);s.tracing=false;throw;}
      {std::lock_guard lock(s.mutex);s.trace_expires=Clock::now()+std::chrono::milliseconds(duration);s.changed.notify_all();}
      return Json{{"active",true},{"scope","browser"},{"bufferMode","record-continuously"},{"artifactByteLimit",artifact_limit}};
    }
    if(action!="stop")throw std::invalid_argument("Unknown tracing action: "+action);
    Destination destination(fields.at("path").as_string());
    bool end=false;
    {std::lock_guard lock(s.mutex);if(!s.tracing)throw std::runtime_error("Browser tracing is not active");end=!s.trace_ending;s.trace_ending=true;}
    std::string stream;
    try {
      if(end)runtime.wire->call("Tracing.end",Json::object{},"",deadline);
      {std::unique_lock lock(s.mutex);while(s.trace_stream.empty()){if(s.closed)throw std::runtime_error("Media runtime closed during tracing");lock.unlock();cdp_check_active(deadline);lock.lock();s.changed.wait_for(lock,std::chrono::milliseconds(10));}stream=s.trace_stream;}
      std::ofstream out(destination.spool/"trace.json",std::ios::binary);std::size_t bytes=0;
      for(;;) {
        cdp_check_active(deadline);
        const auto part=runtime.wire->call("IO.read",Json{{"handle",stream},{"size",65536}},"",deadline);
        const auto data=part.contains("base64Encoded")&&part.at("base64Encoded").as_bool()?decode(part.at("data").as_string(),65536):part.at("data").as_string();
        if(data.size()>artifact_limit-bytes)throw std::runtime_error("Trace exceeds 32 MiB artifact budget");
        bytes+=data.size();out.write(data.data(),static_cast<std::streamsize>(data.size()));
        if(!out)throw std::runtime_error("Cannot write trace artifact");
        if(part.contains("eof")&&part.at("eof").as_bool())break;
      }
      out.close();if(!out)throw std::runtime_error("Cannot finish trace artifact");
      runtime.wire->call("IO.close",Json{{"handle",stream}},"",deadline);
      const auto parsed=Json::parse(read_file(destination.spool/"trace.json",artifact_limit),{artifact_limit,64,524288,artifact_limit});
      if(!parsed.is_object()||!parsed.contains("traceEvents")||!parsed.at("traceEvents").is_array())
        throw std::runtime_error("Chrome trace is missing traceEvents");
      cdp_check_active(deadline);destination.commit(destination.spool/"trace.json");
      {std::lock_guard lock(s.mutex);s.tracing=false;s.trace_ending=false;s.trace_stream.clear();}
      return Json{{"path",destination.path.string()},{"bytes",bytes},{"traceEvents",parsed.at("traceEvents").as_array().size()},{"scope","browser"}};
    }catch(...) {
      if(!stream.empty()){CdpCleanupScope cleanup;try{runtime.wire->call("IO.close",Json{{"handle",stream}},"",Clock::now()+std::chrono::milliseconds(250));}catch(...){}}
      // An unobserved tracingComplete stream remains a cleanup debt and can be
      // collected by retrying stop with a fresh deadline.
      {std::lock_guard lock(s.mutex);if(!stream.empty()){s.tracing=false;s.trace_ending=false;s.trace_stream.clear();}}throw;
    }
  }
  if(family=="profiler") {
    if(action=="status"){std::lock_guard lock(s.mutex);const auto it=s.profiles.find(page.session);return Json{{"active",it!=s.profiles.end()&&!it->second.expired},{"expired",it!=s.profiles.end()&&it->second.expired}};}
    if(action=="start") {
      const auto interval=integer(fields,"samplingIntervalUs",1000,100,1000000);
      const auto duration=integer(fields,"maxDurationMs",60000,1,600000);
      {std::lock_guard lock(s.mutex);if(s.profiles.contains(page.session))throw std::runtime_error("Page CPU profiler is already active");if(s.profiles.size()>=16)throw std::runtime_error("CPU profiler session budget exceeded");s.profiles.emplace(page.session,Impl::Profile{Clock::time_point::max(),false});s.ensure_worker_locked();}
      try{runtime.call(page,"Profiler.enable",Json::object{},deadline);runtime.call(page,"Profiler.setSamplingInterval",Json{{"interval",interval}},deadline);runtime.call(page,"Profiler.start",Json::object{},deadline);}
      catch(...){
        {CdpCleanupScope cleanup;const auto until=Clock::now()+std::chrono::milliseconds(250);
          try{runtime.call(page,"Profiler.stop",Json::object{},until);}catch(...){}
          try{runtime.call(page,"Profiler.disable",Json::object{},until);}catch(...){}}
        std::lock_guard lock(s.mutex);s.profiles.erase(page.session);throw;
      }
      {std::lock_guard lock(s.mutex);s.profiles.at(page.session).expires=Clock::now()+std::chrono::milliseconds(duration);s.changed.notify_all();}
      return Json{{"active",true},{"samplingIntervalUs",interval},{"maxDurationMs",duration}};
    }
    if(action!="stop")throw std::invalid_argument("Unknown profiler action: "+action);
    Destination destination(fields.at("path").as_string());
    {std::lock_guard lock(s.mutex);const auto it=s.profiles.find(page.session);if(it==s.profiles.end())throw std::runtime_error("Page CPU profiler is not active");if(it->second.expired){s.profiles.erase(it);throw std::runtime_error("CPU profiling exceeded maxDurationMs and was stopped automatically");}}
    try {
      const auto profile=runtime.call(page,"Profiler.stop",Json::object{},deadline).at("profile");
      if(!profile.is_object()||!profile.contains("nodes")||!profile.at("nodes").is_array())throw std::runtime_error("Chrome CPU profile has no nodes");
      const auto data=profile.dump({artifact_limit,64,524288,artifact_limit});
      write_file(destination.spool/"profile.cpuprofile",data+"\n");cdp_check_active(deadline);destination.commit(destination.spool/"profile.cpuprofile");
      {std::lock_guard lock(s.mutex);s.profiles.erase(page.session);}
      return Json{{"path",destination.path.string()},{"bytes",data.size()+1},{"nodes",profile.at("nodes").as_array().size()}};
    }catch(...){
      {CdpCleanupScope cleanup;const auto until=Clock::now()+std::chrono::milliseconds(250);
        try{runtime.call(page,"Profiler.stop",Json::object{},until);}catch(...){}
        try{runtime.call(page,"Profiler.disable",Json::object{},until);}catch(...){}}
      std::lock_guard lock(s.mutex);s.profiles.erase(page.session);throw;
    }
  }
  throw std::invalid_argument("Unsupported native media family: "+family);
#endif
}
} // namespace suitecut::detail
