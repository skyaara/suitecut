// SPDX-License-Identifier: MIT
#include "stream_pipeline.h"
#include "streaming_controls.h"
#include "media_pipeline.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <spawn.h>
#include <sstream>
#include <sys/prctl.h>
#include <unistd.h>
extern char** environ;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
template<class Predicate> void Until(Predicate done, Clock::time_point deadline=Clock::now()+5s) {
  while(!done()&&Clock::now()<deadline)std::this_thread::yield();
  assert(done());
}
struct CallbackQueue {
  std::mutex mutex;std::deque<std::function<void()>> pending;
  void Push(std::function<void()> action){std::lock_guard lock(mutex);pending.push_back(std::move(action));}
  size_t Size(){std::lock_guard lock(mutex);return pending.size();}
  void One(){std::function<void()> action;{std::lock_guard lock(mutex);if(pending.empty())return;action=std::move(pending.front());pending.pop_front();}action();}
};
struct Receiver {
  int server=-1; uint16_t port=0; std::atomic<int> peer{-1};
  std::atomic<unsigned> connections{0}, closed{0}, eof{0}; std::atomic<uint64_t> bytes{0};
  std::atomic<bool> stopped{false};std::thread worker;
  std::filesystem::path directory;std::string prefix,error;
  Receiver(const std::filesystem::path& d,const std::string& p,bool listen):directory(d),prefix(p) {
    server=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);assert(server>=0);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    socklen_t size=sizeof(address);assert(getsockname(server,reinterpret_cast<sockaddr*>(&address),&size)==0);port=ntohs(address.sin_port);
    if(listen)Listen();
  }
  void Listen(){assert(!worker.joinable());assert(listen(server,4)==0);worker=std::thread([this]{
    while(!stopped){pollfd incoming{server,POLLIN,0};if(poll(&incoming,1,100)<=0)continue;
      const int socket=accept4(server,nullptr,nullptr,SOCK_CLOEXEC);if(socket<0){if(!stopped)error="Actual accept failed";return;}
      peer=socket;const auto number=++connections;std::ofstream file(directory/(prefix+"-"+std::to_string(number)+".webm"),std::ios::binary);std::array<char,65536> input;
      while(!stopped){pollfd available{socket,POLLIN,0};const int ready=poll(&available,1,100);if(ready<0&&errno!=EINTR){error="Actual socket poll failed";break;}if(ready<=0)continue;
        const auto count=recv(socket,input.data(),input.size(),0);if(count<=0){if(count==0)++eof;break;}
        bytes+=count;assert(bytes<=64*1024*1024);file.write(input.data(),count);
      }
      file.flush();file.close();peer=-1;close(socket);++closed;
    }
  });}
  void Break(){const int socket=peer;assert(socket>=0);linger reset{1,0};assert(setsockopt(socket,SOL_SOCKET,SO_LINGER,&reset,sizeof(reset))==0);assert(shutdown(socket,SHUT_RDWR)==0);}
  ~Receiver(){stopped=true;const int socket=peer;if(socket>=0)shutdown(socket,SHUT_RDWR);shutdown(server,SHUT_RDWR);if(worker.joinable())worker.join();close(server);assert(error.empty());}
};
std::vector<uint8_t> Picture(bool blue=false){std::vector<uint8_t> image(64*64*4);for(size_t i=0;i<image.size();i+=4){image[i]=blue?240:30;image[i+1]=30;image[i+2]=blue?30:240;image[i+3]=255;}return image;}
void Dump(std::ofstream& out,const char* name,const suitecut::StreamPipelineResult& r){out<<"{\"case\":\""<<name<<"\",\"attemptNumber\":"<<r.attempt_number<<",\"reconnectAttempts\":"<<r.reconnect_attempts<<",\"submittedFrames\":"<<r.submitted_frames<<",\"encodedFrames\":"<<r.encoded_frames<<",\"cumulativeSubmittedFrames\":"<<r.cumulative_submitted_frames<<",\"cumulativeEncodedFrames\":"<<r.cumulative_encoded_frames<<",\"physicalPid\":"<<r.physical_encoder_pid<<",\"attempts\":[";bool first=true;for(const auto&a:r.attempts){assert(a.child_reaped&&a.child_exit_observed);if(!first)out<<',';first=false;out<<"{\"number\":"<<a.number<<",\"pid\":"<<a.encoder_pid<<",\"firstTimestampMs\":"<<a.first_timestamp_ms<<",\"firstCapturedTimestampMs\":"<<a.first_captured_timestamp_ms<<",\"childReaped\":"<<(a.child_reaped?"true":"false")<<",\"childExitObserved\":"<<(a.child_exit_observed?"true":"false")<<",\"finishedTimestampMs\":"<<a.finished_timestamp_ms<<",\"submittedFrames\":"<<a.submitted_frames<<",\"encodedFrames\":"<<a.encoded_frames<<",\"failed\":"<<(!a.failure.empty()?"true":"false")<<'}';}out<<"]}\n";}
}
int main(int argc,char**argv){const int helper=suitecut::MediaEncoderHelperMain(argc,argv);if(helper>=0)return helper;
 if(argc==5&&std::string_view(argv[1])=="--reconnect-parent-death-worker"){
  assert(prctl(PR_SET_PDEATHSIG,SIGKILL)==0&&getppid()==std::stoi(argv[4]));
  const auto clock=[] {return std::chrono::duration<double,std::milli>(Clock::now().time_since_epoch()).count();};
  suitecut::StreamPipelineOptions o;o.launcher_path=std::filesystem::canonical(argv[0]).string();o.ffmpeg_path=argv[2];o.destination="tcp://127.0.0.1:"+std::string(argv[3]);
  o.source_width=o.source_height=o.output_width=o.output_height=64;o.video_codec="vp9";o.container="webm";o.frames_per_second=29.97;o.start_timestamp_ms=clock();o.first_frame_deadline=Clock::now()+4s;o.reconnect=suitecut::StreamReconnectOptions{3,50ms,200ms,3000ms};
  suitecut::StreamPipeline live(o,{},[](const auto&result){std::cout<<"ready "<<result.attempt_number<<' '<<result.physical_encoder_pid<<std::endl;});live.Frame(Picture(),clock());
  for(;;)pause();
 }
 assert(argc==3);const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);std::ofstream records(directory/"actual-reconnect-cases.jsonl");
 const auto epoch=Clock::now();const auto now=[&]{return std::chrono::duration<double,std::milli>(Clock::now()-epoch).count();};
 const auto options=[&](uint16_t port){suitecut::StreamPipelineOptions o;o.launcher_path=std::filesystem::canonical(argv[0]).string();o.ffmpeg_path=argv[1];o.source_width=o.source_height=o.output_width=o.output_height=64;o.video_codec="vp9";o.audio_codec="opus";o.container="webm";o.frames_per_second=29.97;o.start_timestamp_ms=now();o.destination="tcp://127.0.0.1:"+std::to_string(port);o.first_frame_deadline=Clock::now()+4s;o.reconnect=suitecut::StreamReconnectOptions{3,50ms,200ms,3000ms};return o;};
 {
  Receiver receiver(directory,"initial-refused",false);std::atomic<unsigned> ready{0};suitecut::StreamPipeline live(options(receiver.port),{},[&](const auto&r){assert(r.encoded_frames>0);++ready;});live.Frame(Picture(),now());
  Until([&]{auto s=live.Snapshot();return s.reconnecting&&s.physical_encoder_pid==0;});auto disconnected=live.Snapshot();assert(disconnected.failure.empty()&&!disconnected.last_attempt_failure.empty());assert(disconnected.attempts.size()==1);const auto old=disconnected.attempts.front().encoder_pid;assert(old>0&&kill(old,0)==-1&&errno==ESRCH);
  receiver.Listen();Until([&]{return ready.load()==1;});Until([&]{return receiver.bytes.load()>0;});live.Stop(Clock::now()+5s);live.Join();Until([&]{return receiver.closed==receiver.connections;});assert(receiver.eof>=1);const auto result=live.Snapshot();if(!result.failure.empty())std::cerr<<result.failure<<'\n'<<result.diagnostics;assert(result.failure.empty()&&result.attempt_number==2&&result.reconnect_attempts==1);assert(result.attempts.size()==2&&!result.attempts.front().failure.empty()&&result.attempts.back().failure.empty());assert(result.physical_encoder_pid==0&&result.captured_frame_bytes==0);assert(result.cumulative_submitted_frames>=result.submitted_frames);Dump(records,"initial-refused",result);
 }
 {
  Receiver receiver(directory,"established-reset",true);std::atomic<unsigned> ready{0};suitecut::StreamPipeline live(options(receiver.port),{},[&](const auto&r){assert(r.encoded_frames>0);++ready;});live.Frame(Picture(),now());Until([&]{return ready.load()==1&&receiver.bytes.load()>0;});receiver.Break();Until([&]{return ready.load()==2;});assert(receiver.connections==2);live.Stop(Clock::now()+5s);live.Join();Until([&]{return receiver.closed==receiver.connections;});assert(receiver.eof>=1);const auto result=live.Snapshot();if(!result.failure.empty())std::cerr<<result.failure<<'\n'<<result.diagnostics;assert(result.failure.empty()&&result.attempts.size()==2&&result.attempt_number==2);assert(result.physical_encoder_pid==0&&result.reconnect_attempts==1);Dump(records,"established-reset",result);
 }
 {
  Receiver receiver(directory,"exhausted",false);auto o=options(receiver.port);o.reconnect->initial_delay=10ms;o.reconnect->max_delay=10ms;suitecut::StreamPipeline live(o);live.Frame(Picture(),now());live.Join();auto result=live.Snapshot();assert(!result.failure.empty()&&result.attempt_number==4&&result.reconnect_attempts==3);assert(result.physical_encoder_pid==0&&result.captured_frame_bytes==0);assert(receiver.connections==0);Dump(records,"exhausted",result);
 }
 {
  Receiver receiver(directory,"original-deadline",false);auto o=options(receiver.port);o.first_frame_deadline=Clock::now()+40ms;o.reconnect->initial_delay=500ms;o.reconnect->max_delay=500ms;const auto begin=Clock::now();suitecut::StreamPipeline live(o);live.Frame(Picture(),now());live.Join();const auto result=live.Snapshot();assert(result.failure.find("initial encoded readiness deadline")!=std::string::npos);assert(Clock::now()-begin<1s);assert(result.physical_encoder_pid==0&&result.attempt_number==1);Dump(records,"original-deadline",result);
 }
 for(const bool cancel:{true,false}){
  Receiver receiver(directory,cancel?"cancel":"stop",false);auto o=options(receiver.port);o.reconnect->initial_delay=3s;o.reconnect->max_delay=3s;suitecut::StreamPipeline live(o);live.Frame(Picture(),now());Until([&]{return live.Snapshot().reconnecting;});const auto begin=Clock::now();if(cancel)live.Cancel("Actual native reconnect cancellation");else live.Stop(Clock::now()+40ms);live.Join();const auto result=live.Snapshot();assert(!result.failure.empty()&&result.physical_encoder_pid==0&&result.attempt_number==1);assert(Clock::now()-begin<1s);Dump(records,cancel?"cancel":"stop",result);
 }
 {
  Receiver receiver(directory,"configuration-failure",false);auto o=options(receiver.port);o.ffmpeg_path="/bin/false";suitecut::StreamPipeline live(o);live.Frame(Picture(),now());live.Join();const auto result=live.Snapshot();assert(!result.failure.empty()&&result.attempt_number==1&&result.reconnect_attempts==0&&result.physical_encoder_pid==0);Dump(records,"configuration-failure",result);
 }
 {
  Receiver receiver(directory,"outage-budget",false);auto o=options(receiver.port);o.reconnect->timeout=80ms;o.reconnect->initial_delay=500ms;o.reconnect->max_delay=500ms;const auto begin=Clock::now();suitecut::StreamPipeline live(o);live.Frame(Picture(),now());live.Join();const auto result=live.Snapshot();assert(result.failure.find("reconnect outage deadline")!=std::string::npos);assert(Clock::now()-begin<1s);assert(result.physical_encoder_pid==0&&result.reconnect_attempts==0);Dump(records,"outage-budget",result);
 }
 {
  Receiver receiver(directory,"offline-input",false);auto o=options(receiver.port);o.audio=true;o.reconnect->initial_delay=3s;o.reconnect->max_delay=3s;suitecut::StreamPipeline live(o);live.Frame(Picture(),now());
  Until([&]{return live.Snapshot().reconnecting;});live.Frame(Picture(true),now());const auto first=live.Snapshot();assert(first.captured_frame_bytes==64*64*4);live.Frame(Picture(),now());
  for(int packet=0;packet<10;++packet)live.Audio(std::vector<uint8_t>(960*4,42),now());
  const auto current=live.Snapshot();assert(current.disconnected_video_frames>=1&&current.audio_input_frames>=9600&&current.disconnected_audio_frames>=9600);assert(current.captured_frame_bytes==64*64*4);live.Cancel("Actual bounded offline input proof");live.Join();const auto result=live.Snapshot();assert(result.physical_encoder_pid==0&&result.captured_frame_bytes==0);Dump(records,"offline-input",result);
 }
 {
  Receiver receiver(directory,"four-offline-owners",false);std::vector<std::unique_ptr<suitecut::StreamPipeline>> owners;
  for(int index=0;index<4;++index){auto o=options(receiver.port);o.reconnect->initial_delay=3s;o.reconnect->max_delay=3s;auto live=std::make_unique<suitecut::StreamPipeline>(o);live->Frame(Picture(),now());owners.push_back(std::move(live));}
  Until([&]{for(const auto& live:owners)if(!live->Snapshot().reconnecting)return false;return true;});
  bool refused=false;try{suitecut::StreamPipeline excess(options(receiver.port));}catch(const std::exception& error){refused=std::string(error.what()).find("capacity")!=std::string::npos;}assert(refused);
  for(const auto& live:owners)live->Cancel("Actual offline native owner close");
  for(const auto& live:owners){live->Join();const auto result=live->Snapshot();assert(result.physical_encoder_pid==0&&result.captured_frame_bytes==0);}
  Dump(records,"four-offline-owners",owners.front()->Snapshot());
 }
 {
  Receiver receiver(directory,"bounded-history",true);std::atomic<unsigned> ready{0};auto o=options(receiver.port);suitecut::StreamPipeline live(o,{},[&](const auto&){++ready;});live.Frame(Picture(),now());
  Until([&]{return ready==1;});
  for(unsigned cycle=1;cycle<=9;++cycle){receiver.Break();Until([&]{return ready==cycle+1;});}
  live.Stop(Clock::now()+5s);live.Join();Until([&]{return receiver.closed==10;});assert(receiver.eof>=1);const auto result=live.Snapshot();assert(result.failure.empty()&&result.attempt_number==10&&result.reconnect_attempts==9&&result.attempts.size()==8);assert(result.attempts.front().number==3&&result.attempts.back().number==10);assert(result.first_captured_timestamp_ms<result.first_timestamp_ms);assert(result.physical_encoder_pid==0&&result.captured_frame_bytes==0);Dump(records,"bounded-history",result);
 }
 {
  Receiver receiver(directory,"bounded-owning-thread-callbacks",true);CallbackQueue queue;suitecut::StreamingControls::Hooks hooks;hooks.dispatch=[&](auto action){queue.Push(std::move(action));};auto controls=std::make_shared<suitecut::StreamingControls>(hooks);bool acknowledged=false,complete=false;auto o=options(receiver.port);
  const auto id=controls->Start(901,902,903,o,[&](const auto&,const auto&result){assert(result.encoded_progress_observed);acknowledged=true;});controls->Frame(902,Picture(),now());
  Until([&]{return controls->Status(id).encoded_frames>0&&queue.Size()==1;});
  for(unsigned cycle=1;cycle<=9;++cycle){Until([&]{return receiver.connections==cycle;});receiver.Break();Until([&]{const auto actual=controls->Status(id);return actual.attempt_number==cycle+1&&actual.encoded_frames>0;});assert(queue.Size()==1);}
  assert(!acknowledged&&controls->PhysicalCount()==1&&!controls->ContextReady(903));
  controls->Stop(904,id,Clock::now()+5s,[&](const auto&,const auto&result){assert(result.failure.empty()&&result.physical_encoder_pid==0);complete=true;});Until([&]{return controls->Status(id).physical_encoder_pid==0&&queue.Size()==2;});assert(controls->PhysicalCount()==1&&!controls->ContextReady(903));
  Until([&]{queue.One();return acknowledged&&complete;});Until([&]{return receiver.closed==10;});assert(queue.Size()==0&&controls->PhysicalCount()==0&&controls->ContextReady(903));const auto result=controls->Status(id);assert(result.attempt_number==10&&result.attempts.size()==8);Dump(records,"bounded-owning-thread-callbacks",result);
 }
 {
  Receiver receiver(directory,"parent-death",true);assert(prctl(PR_SET_CHILD_SUBREAPER,1)==0);int reports[2];assert(pipe(reports)==0);
  posix_spawn_file_actions_t actions;assert(posix_spawn_file_actions_init(&actions)==0);assert(posix_spawn_file_actions_adddup2(&actions,reports[1],STDOUT_FILENO)==0);assert(posix_spawn_file_actions_addclose(&actions,reports[0])==0);assert(posix_spawn_file_actions_addclose(&actions,reports[1])==0);
  auto launcher=std::filesystem::canonical(argv[0]).string(),port=std::to_string(receiver.port),parent=std::to_string(getpid());
  std::array<char*,6> arguments{launcher.data(),const_cast<char*>("--reconnect-parent-death-worker"),argv[1],port.data(),parent.data(),nullptr};pid_t worker=-1;assert(posix_spawn(&worker,launcher.c_str(),&actions,nullptr,arguments.data(),environ)==0);posix_spawn_file_actions_destroy(&actions);close(reports[1]);
  const auto read_ready=[&](unsigned expected){std::string line;for(;;){pollfd ready{reports[0],POLLIN,0};assert(poll(&ready,1,5000)>0);char value;assert(read(reports[0],&value,1)==1);if(value=='\n')break;line.push_back(value);assert(line.size()<=128);}std::istringstream parsed(line);std::string marker;unsigned attempt=0;pid_t encoder=-1;parsed>>marker>>attempt>>encoder;assert(marker=="ready"&&attempt==expected&&encoder>1);assert(std::filesystem::canonical("/proc/"+std::to_string(encoder)+"/exe")==std::filesystem::canonical(argv[1]));return encoder;};
  const auto first=read_ready(1);Until([&]{return receiver.connections==1;});receiver.Break();const auto replacement=read_ready(2);assert(first!=replacement);assert(kill(first,0)==-1&&errno==ESRCH);
  assert(kill(worker,SIGKILL)==0);int physical;assert(waitpid(worker,&physical,0)==worker&&WIFSIGNALED(physical)&&WTERMSIG(physical)==SIGKILL);assert(waitpid(replacement,&physical,0)==replacement&&WIFSIGNALED(physical)&&WTERMSIG(physical)==SIGKILL);close(reports[0]);Until([&]{return receiver.closed==2;});assert(receiver.eof>=1);assert(!std::filesystem::exists("/proc/"+std::to_string(worker)+"/fd"));assert(!std::filesystem::exists("/proc/"+std::to_string(replacement)+"/fd"));
  records<<"{\"case\":\"parent-death-after-real-replacement\",\"workerPid\":"<<worker<<",\"firstEncoderPid\":"<<first<<",\"replacementEncoderPid\":"<<replacement<<",\"actualSubreaperWaitpidSIGKILL\":true,\"actualReceiverEOF\":true,\"physicalPid\":0}\n";
 }
 int status=0;assert(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD);std::cout<<"Native reconnect real refusal/reset/replacement, original deadline, cancel/stop and physical waitpid proof passed\n";
}
