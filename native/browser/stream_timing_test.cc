// SPDX-License-Identifier: MIT
#include "stream_pipeline.h"
#include "media_pipeline.h"
#include <array>
#include <atomic>
#include <cmath>
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
struct Receiver {
  int server; uint16_t port; std::thread worker; std::string error;
  explicit Receiver(const std::filesystem::path& output) {
    server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0); assert(server >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0); assert(listen(server, 1) == 0);
    socklen_t length = sizeof(address); assert(getsockname(server, reinterpret_cast<sockaddr*>(&address), &length) == 0); port = ntohs(address.sin_port);
    worker = std::thread([this, output] {
      pollfd listener{server, POLLIN, 0};
      if (poll(&listener, 1, 10000) <= 0) { error = "Publisher did not connect"; return; }
      const int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
      if (client < 0) { error = "Publisher accept failed"; return; }
      std::ofstream file(output, std::ios::binary); std::array<char, 65536> bytes; uint64_t total = 0;
      for (;;) {
        pollfd active{client, POLLIN, 0}; if (poll(&active, 1, 10000) <= 0) { error = "Publisher did not settle physical socket"; break; }
        const auto count = recv(client, bytes.data(), bytes.size(), 0); if (count == 0) break;
        if (count < 0) { if (errno == EINTR) continue; error = "Publisher socket failed"; break; }
        total += count; if (total > 64 * 1024 * 1024) { error = "Publisher exceeds test byte bound"; break; }
        file.write(bytes.data(), count);
      }
      close(client);
    });
  }
  void Join() { if (worker.joinable()) worker.join(); if (!error.empty()) std::cerr << error << '\n'; assert(error.empty()); }
  ~Receiver() { Join(); close(server); }
};

}
int main(int argc,char**argv) {
 const int helper=suitecut::MediaEncoderHelperMain(argc,argv);if(helper>=0)return helper;assert(argc==3);
 const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
 for(const double rate:{120.,59.94,29.97}) {
  const std::string name="rate-"+std::to_string(static_cast<int>(rate*100));Receiver receiver(directory/(name+".webm"));
  suitecut::StreamPipelineOptions o;o.launcher_path=std::filesystem::canonical(argv[0]).string();o.ffmpeg_path=argv[1];o.destination="tcp://127.0.0.1:"+std::to_string(receiver.port);o.source_width=o.source_height=o.output_width=o.output_height=64;o.i420=true;o.audio=true;o.video_codec="vp9";o.audio_codec="opus";o.container="webm";o.frames_per_second=rate;o.first_frame_deadline=Clock::now()+5s;
  std::atomic<bool> ready{false};suitecut::StreamPipeline stream(o,{},[&](const auto&){ready=true;});
  const auto origin=Clock::now();
  auto timestamp=[&]{return std::chrono::duration<double,std::milli>(Clock::now()-origin).count();};
  std::thread images([&]{auto due=origin;while(timestamp()<4800&&!stream.Settled()) {
    const double at=timestamp();const bool pulse=at>=500&&std::fmod(at,500.)<80;
    std::vector<uint8_t> frame(64*64*3/2,128);std::fill(frame.begin(),frame.begin()+64*64,pulse?235:16);stream.Frame(frame,at);
    due+=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1./120));std::this_thread::sleep_until(due);
  }});
  std::thread audio([&]{for(uint64_t at=0;at<4800*48&&!stream.Settled();at+=960){std::this_thread::sleep_until(origin+std::chrono::milliseconds((at+960)/48));std::vector<uint8_t> pcm(960*4);
    for(size_t i=0;i<960;++i){const double t=(at+i)/48000.;const double ms=t*1000;const bool pulse=ms>=500&&std::fmod(ms,500.)<80;const int16_t value=static_cast<int16_t>(std::sin(t*2*3.141592653589793*660)*4915+(pulse?std::sin(t*2*3.141592653589793*1320)*9830:0));
      for(size_t c=0;c<2;++c){pcm[i*4+c*2]=value&255;pcm[i*4+c*2+1]=(value>>8)&255;}}
    stream.Audio(std::move(pcm),at/48.);
  }});
  std::this_thread::sleep_until(origin+1800ms);const double pause=timestamp();stream.Pause(true);
  std::this_thread::sleep_until(origin+2100ms);const double resume=timestamp();stream.Pause(false);
  std::this_thread::sleep_until(origin+3250ms);const double selected=timestamp();stream.SwitchSource();
  images.join();audio.join();stream.Stop(Clock::now()+5s);stream.Join();receiver.Join();const auto result=stream.Snapshot();
  std::ofstream report(directory/(name+".json"));report<<"{\"rate\":"<<rate<<",\"ready\":"<<(ready?"true":"false")<<",\"frames\":"<<result.submitted_frames<<",\"encoded\":"<<result.encoded_frames<<",\"audio\":"<<result.audio_submitted_frames<<",\"gap\":"<<result.audio_gap_frames<<",\"overlap\":"<<result.audio_overlap_frames<<",\"pausedAudio\":"<<result.paused_audio_frames<<",\"pausedInputAudio\":"<<result.paused_input_audio_frames<<",\"switchedAudio\":"<<result.switched_audio_frames<<",\"peakPictures\":"<<result.peak_captured_frame_bytes<<",\"picturesAtEnd\":"<<result.captured_frame_bytes<<",\"stopAudioTrim\":"<<result.stopped_audio_frames<<",\"stopVideoTrim\":"<<result.stopped_video_frames<<",\"first\":"<<result.first_timestamp_ms<<",\"pause\":"<<pause<<",\"resume\":"<<resume<<",\"select\":"<<selected<<"}\n";report.close();
  std::cerr<<name<<" ready="<<ready<<" frames="<<result.submitted_frames<<" gap="<<result.audio_gap_frames<<" overlap="<<result.audio_overlap_frames<<" failure="<<result.failure<<" diagnostics="<<result.diagnostics<<"\n";
  assert(ready&&result.failure.empty()&&result.diagnostics.empty()&&result.submitted_frames==result.encoded_frames&&result.physical_encoder_pid==0&&result.captured_frame_bytes==0);
 }
}
