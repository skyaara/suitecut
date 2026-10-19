// SPDX-License-Identifier: MIT
#include "stream_pipeline.h"
#include "media_pipeline.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <signal.h>
#include <unistd.h>
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
class Queue {
 public:
  void Push(std::function<void()> task) { { std::lock_guard lock(mutex); tasks.push_back(std::move(task)); } changed.notify_one(); }
  void Until(const std::function<bool()>& done) { assert(TryUntil(done, Clock::now() + 10s)); }
  bool TryUntil(const std::function<bool()>& done, Clock::time_point deadline) {
    while (!done()) {
      std::function<void()> task;
      { std::unique_lock lock(mutex); if (!changed.wait_until(lock, deadline, [&] { return !tasks.empty(); })) return done(); task = std::move(tasks.front()); tasks.pop_front(); }
      task();
    }
    return true;
  }
 private:
  std::mutex mutex; std::condition_variable changed; std::deque<std::function<void()>> tasks;
};

}
int main(int argc,char**argv) {
 const int helper=suitecut::MediaEncoderHelperMain(argc,argv);if(helper>=0)return helper;
 if(std::filesystem::path(argv[0]).filename()=="native-stream-held-encoder"){for(;;)pause();}
 assert(argc==3);const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
 const auto held=directory/"native-stream-held-encoder";std::filesystem::copy_file(std::filesystem::canonical(argv[0]),held,std::filesystem::copy_options::overwrite_existing);std::filesystem::permissions(held,std::filesystem::perms::owner_all);
 auto options=[&]{suitecut::StreamPipelineOptions o;o.launcher_path=std::filesystem::canonical(argv[0]).string();o.ffmpeg_path=held.string();o.destination="tcp://127.0.0.1:1";o.source_width=o.source_height=o.output_width=o.output_height=64;o.frames_per_second=29.97;o.audio=true;o.video_codec="vp9";o.audio_codec="opus";o.container="webm";o.first_frame_deadline=Clock::now()+40ms;return o;};
 {
  auto o=options();const auto began=Clock::now();suitecut::StreamPipeline stream(o);stream.Frame(std::vector<uint8_t>(64*64*4,0),0);stream.Join();const auto result=stream.Snapshot();const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
  std::ofstream(directory/"deadline.json")<<"{\"elapsedMs\":"<<ms<<",\"physicalPid\":"<<result.physical_encoder_pid<<",\"pictureBytes\":"<<result.captured_frame_bytes<<"}\n";
  std::cerr<<"deadline="<<result.failure<<" elapsed="<<ms<<"\n";assert(result.failure.find("initial encoded readiness deadline")!=std::string::npos&&ms<500&&result.physical_encoder_pid==0&&result.captured_frame_bytes==0);
 }
 {
  auto o=options();o.source_width=o.source_height=o.output_width=o.output_height=4096;o.capture_latency=1000ms;o.first_frame_deadline=Clock::now()+5s;suitecut::StreamPipeline stream(o);
  std::vector<uint8_t> frame(4096u*4096*4);for(int i=0;i<6&&!stream.Settled();++i)stream.Frame(frame,i);stream.Join();const auto result=stream.Snapshot();
  std::ofstream(directory/"quota.json")<<"{\"peakPictureBytes\":"<<result.peak_captured_frame_bytes<<",\"pictureBytes\":"<<result.captured_frame_bytes<<",\"physicalPid\":"<<result.physical_encoder_pid<<"}\n";
  std::cerr<<"quota="<<result.failure<<" peak="<<result.peak_captured_frame_bytes<<" end="<<result.captured_frame_bytes<<"\n";assert(result.failure.find("captured-image quota")!=std::string::npos&&result.peak_captured_frame_bytes==256u*1024*1024&&result.captured_frame_bytes==0&&result.physical_encoder_pid==0);
 }
 int status;assert(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD);
}
