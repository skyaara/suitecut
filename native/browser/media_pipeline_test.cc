// SPDX-License-Identifier: MIT
#include "media_pipeline.h"
#include <cassert>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <spawn.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
extern char** environ;
#include <iostream>
#include <mutex>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using suitecut::MediaPipeline;
using suitecut::MediaPipelineOptions;
using suitecut::MediaPipelineResult;
using Clock = std::chrono::steady_clock;
namespace {
std::vector<uint8_t> Frame(bool red) {
  std::vector<uint8_t> bytes(64 * 64 * 4);
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    bytes[offset + (red ? 2 : 0)] = 255;
    bytes[offset + 3] = 255;
  }
  return bytes;
}
std::vector<uint8_t> PCM() {
  std::vector<uint8_t> bytes(960 * 4);
  for (int frame = 0; frame < 960; ++frame) {
    const int value = static_cast<int>(std::sin(frame * 2 * 3.141592653589793 * 440 / 48000) * 20000);
    for (int channel = 0; channel < 2; ++channel) {
      bytes[frame * 4 + channel * 2] = static_cast<uint8_t>(value & 255);
      bytes[frame * 4 + channel * 2 + 1] = static_cast<uint8_t>((value >> 8) & 255);
    }
  }
  return bytes;
}
struct Started {
  std::mutex mutex;
  std::condition_variable changed;
  bool ready = false;
  void Signal(double timestamp) {
    assert(timestamp == 100);
    { std::lock_guard lock(mutex); ready = true; }
    changed.notify_one();
  }
  void Wait() {
    std::unique_lock lock(mutex);
    assert(changed.wait_until(lock, Clock::now() + std::chrono::seconds(10), [this] { return ready; }));
  }
};
void NoPrivateFiles(const std::filesystem::path& directory) {
  for (const auto& entry : std::filesystem::directory_iterator(directory))
    assert(!entry.path().filename().string().starts_with(".suitecut-media-"));
}
void CheckSuccess(const MediaPipelineResult& result) {
  if (!result.failure.empty()) std::cerr << result.failure << '\n';
  assert(result.failure.empty());
  assert(result.output_bytes > 0);
  assert(result.input_frames == result.superseded_frames + result.coalesced_frames +
      result.output_frames - result.held_frames);
  struct stat info{};
  assert(stat(result.output_path.c_str(), &info) == 0);
  assert((info.st_mode & 0777) == 0600);
}
uint64_t DecodedFrames(const char* ffmpeg, const std::string& video, const std::vector<bool>& colors = {}) {
  const auto raw = video + ".gray";
  posix_spawn_file_actions_t actions;
  assert(posix_spawn_file_actions_init(&actions) == 0);
  assert(posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, raw.c_str(),
      O_WRONLY | O_CREAT | O_TRUNC, 0600) == 0);
  std::vector<std::string> args{ffmpeg, "-v", "error", "-i", video, "-an",
      "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "gray", "pipe:1"};
  std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
  pid_t pid = -1;
  assert(posix_spawn(&pid, ffmpeg, &actions, nullptr, argv.data(), environ) == 0);
  posix_spawn_file_actions_destroy(&actions);
  int status = -1; assert(waitpid(pid, &status, 0) == pid);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  const auto bytes = std::filesystem::file_size(raw);
  if(!colors.empty()){
    assert(bytes==colors.size()*64*64);std::ifstream input(raw,std::ios::binary);
    for(size_t i=0;i<colors.size();++i){input.seekg(i*64*64);char pixel;input.read(&pixel,1);assert(input);assert(colors[i]?(static_cast<uint8_t>(pixel)>40):(static_cast<uint8_t>(pixel)<40));}
  }
  std::filesystem::remove(raw); assert(bytes % (64 * 64) == 0);
  return bytes / (64 * 64);
}
}
int main(int argc, char** argv) {
  const int helper = suitecut::MediaEncoderHelperMain(argc, argv);
  if (helper >= 0) return helper;
  if (std::filesystem::path(argv[0]).filename() == "stalled-encoder") {
    const auto marker = std::filesystem::path(argv[0]).parent_path() / "encoder-ready";
    const int fd = open(marker.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, "ready", 5) != 5) return 127;
    close(fd);
    for (;;) pause();
  }
  if (argc == 5 && std::string_view(argv[1]) == "--parent-kill-worker") {
#ifdef __linux__
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != std::stoi(argv[4])) return 127;
#endif
    MediaPipelineOptions o;
    o.launcher_path = std::filesystem::canonical(argv[0]).string();
    o.ffmpeg_path = argv[2]; o.output_path = (std::filesystem::path(argv[3]) / "parent-killed.webm").string();
    o.source_width = o.source_height = o.output_width = o.output_height = 64;
    o.audio = true; o.start_timestamp_ms = 100;
    Started started;
    bool notified = false;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); },
      [&](const auto& result) {
        if (result.output_frames >= 60 && !notified) {
          notified = true; const auto message = "ready " + std::to_string(result.physical_encoder_pid) + "\n";
          const auto wrote = write(STDOUT_FILENO, message.data(), message.size()); (void)wrote;
        }
      });
    pipeline.Audio(PCM(), 100); pipeline.Frame(Frame(true), 100); started.Wait();
    pipeline.Frame(Frame(false), 1100);
    std::mutex mutex; std::condition_variable wait; std::unique_lock lock(mutex);
    wait.wait(lock, [] { return false; });
  }
  assert(argc == 3);
  const auto directory = std::filesystem::path(argv[2]) / ("run-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  auto options = [&](const char* name) {
    MediaPipelineOptions o;
    o.launcher_path = std::filesystem::canonical(argv[0]).string();
    o.ffmpeg_path = argv[1]; o.output_path = (directory / name).string();
    o.source_width = o.source_height = o.output_width = o.output_height = 64;
    o.start_timestamp_ms = 100; o.first_frame_deadline = Clock::now() + std::chrono::seconds(10);
    return o;
  };
  assert(suitecut::MediaSlotCount(1000, 120) == 120);
  assert(suitecut::MediaSlotCount(1000 / 120.0, 120) == 1);
  assert(suitecut::MediaSlotCount(1000, 59.94) == 60);
  assert(suitecut::MediaSlotCount(0, 120) == 0);
  for (double rate : {0.0, -1.0, 120.1, static_cast<double>(INFINITY), static_cast<double>(NAN)}) {
    bool failed = false;
    try { suitecut::MediaSlotCount(1, rate); } catch (...) { failed = true; }
    assert(failed);
  }
  {
#ifdef __APPLE__
    for (double rate : {120.0, 59.94}) for (bool with_audio : {false, true}) {
    // Hardware startup belongs to preparation, before the real capture clock.
    // The private preparation GOP must never inflate the published frame count.
    const auto name = "hardware-" + std::to_string(rate) + (with_audio ? "-audio.mp4" : "-silent.mp4");
    auto o = options(name.c_str()); o.hardware_h264 = true;
    o.audio = with_audio; o.frames_per_second = rate; o.capture_frames_per_second = 120;
    o.first_frame_deadline = Clock::now() + std::chrono::seconds(20);
    std::atomic<bool> ready{false};
    MediaPipeline pipeline(o, {}, [&](double) { ready = true; });
    const auto began = Clock::now(); int frames = 0;
    for (; !ready && !pipeline.Settled() && frames < 2400; ++frames) {
      const double timestamp = 100 + frames * 1000.0 / 120;
      pipeline.Frame(Frame(frames % 2), timestamp); if (frames % 3 == 0) pipeline.Audio(PCM(), timestamp);
      std::this_thread::sleep_until(began + std::chrono::microseconds((frames + 1) * 1000000LL / 120));
    }
    if (!ready) std::cerr << pipeline.Snapshot().failure << '\n';
    assert(ready);
    for (int count = 0; count < 12; ++count, ++frames) {
      const double timestamp = 100 + frames * 1000.0 / 120;
      pipeline.Frame(Frame(frames % 2), timestamp); pipeline.Audio(PCM(), timestamp);
      std::this_thread::sleep_until(began + std::chrono::microseconds((frames + 1) * 1000000LL / 120));
    }
    pipeline.Stop(100 + frames * 1000.0 / 120, Clock::now() + std::chrono::seconds(20)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.preparation_frames > 0 && result.preparation_ms > 0);
    assert(result.encoder == "h264_videotoolbox" && result.container == "mp4");
    assert(DecodedFrames(argv[1], o.output_path) == result.output_frames);
    assert(result.diagnostics.empty());
    }
#endif
  }
#ifdef __APPLE__
  {
    auto o=options("native-vt-pipeline.mp4");
    o.frames_per_second=o.capture_frames_per_second=120;
    o.worker_canvas_fit=o.worker_nv12=o.hardware_h264=o.capture_timestamps_exact=true;
    o.first_frame_deadline=Clock::now()+std::chrono::seconds(20);
    std::atomic<bool> ready{false};
    const auto began=Clock::now();
    MediaPipeline pipeline(o,{},[&](double){ready=true;});
    auto stamp=[&]{return 100+std::chrono::duration<double,std::milli>(Clock::now()-began).count();};
    for(int i=0;!ready&&!pipeline.Settled()&&i<2400;++i){pipeline.CanvasFrame(Frame(i%2),64,64,stamp(),i+1);std::this_thread::sleep_for(std::chrono::milliseconds(8));}
    if(!ready)std::cerr<<pipeline.Snapshot().failure<<'\n';assert(ready);
    for(int i=0;i<12;++i){pipeline.CanvasFrame(Frame(i%2),64,64,stamp(),3000+i);std::this_thread::sleep_for(std::chrono::milliseconds(8));}
    std::this_thread::sleep_for(std::chrono::milliseconds(650));
    assert(pipeline.Snapshot().output_frames>=25);
    pipeline.Stop(stamp(),Clock::now()+std::chrono::seconds(20));pipeline.Join();
    auto result=pipeline.Snapshot();CheckSuccess(result);
    assert(result.encoding_backend=="native-videotoolbox"&&result.capture_lookahead_ms==0);
    assert(result.preparation_packets==1&&result.physical_encoder_pid==0);
    assert(DecodedFrames(argv[1],o.output_path)==result.output_frames);
  }
#endif
#ifdef __APPLE__
  {
    auto o=options("native-vt-audio-admission.mp4");
    o.frames_per_second=o.capture_frames_per_second=120;
    o.worker_canvas_fit=o.worker_nv12=o.hardware_h264=o.capture_timestamps_exact=o.audio=true;
    o.first_frame_deadline=Clock::now()+std::chrono::seconds(20);
    std::atomic<bool> prepared{false},ready{false};const auto began=Clock::now();
    MediaPipeline pipeline(o,{},[&](double){ready=true;},[&](const auto& result){if(result.preparation_packets&&result.first_timestamp_ms<0)prepared=true;});
    auto stamp=[&]{return 100+std::chrono::duration<double,std::milli>(Clock::now()-began).count();};
    int sequence=0;
    for(;!prepared&&!pipeline.Settled()&&sequence<2400;++sequence){pipeline.CanvasFrame(Frame(sequence%2),64,64,stamp(),sequence+1);std::this_thread::sleep_for(std::chrono::milliseconds(8));}
    assert(prepared&&!ready);
    for(int i=0;i<8;++i){pipeline.CanvasFrame(Frame(i%2),64,64,stamp(),++sequence);std::this_thread::sleep_for(std::chrono::milliseconds(8));}
    assert(!ready);const auto audio_started=stamp();pipeline.Audio(PCM(),audio_started);
    for(int i=0;!ready&&!pipeline.Settled()&&i<2400;++i){pipeline.CanvasFrame(Frame(i%2),64,64,stamp(),++sequence);pipeline.Audio(PCM(),stamp());std::this_thread::sleep_for(std::chrono::milliseconds(8));}
    assert(ready);
    for(int i=0;i<12;++i){pipeline.CanvasFrame(Frame(i%2),64,64,stamp(),++sequence);pipeline.Audio(PCM(),stamp());std::this_thread::sleep_for(std::chrono::milliseconds(8));}
    pipeline.Stop(stamp(),Clock::now()+std::chrono::seconds(20));pipeline.Join();auto result=pipeline.Snapshot();CheckSuccess(result);
    assert(result.first_timestamp_ms>=audio_started&&result.preparation_frames>=8);
    assert(DecodedFrames(argv[1],o.output_path)==result.output_frames);
  }
#endif
  {
    // Hold the worker while CEF delivers a short burst. Accepted paints must
    // survive both scheduling jitter and an immediate stop request.
    auto o = options("queued-stop.webm"); o.frames_per_second = 120;
    Started started;
    std::mutex gate_mutex; std::condition_variable gate;
    bool blocked = false, release = false;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); },
      [&](const auto&) {
        std::unique_lock lock(gate_mutex);
        if (!blocked) {
          blocked = true; gate.notify_one();
          gate.wait(lock, [&] { return release; });
        }
      });
    pipeline.Frame(Frame(true), 100); started.Wait();
    {
      std::unique_lock lock(gate_mutex);
      assert(gate.wait_for(lock, std::chrono::seconds(5), [&] { return blocked; }));
    }
    // Jitter straddles both exact and half-slot boundaries. A steady cadence
    // must not oscillate between held/dropped slots after an early first paint.
    for (int i = 1; i <= 8; ++i)
      pipeline.Frame(Frame(i % 2), 100 + i * 1000.0 / 120 + (i % 2 ? 5 : 3));
    pipeline.Stop(175, Clock::now() + std::chrono::seconds(15));
    { std::lock_guard lock(gate_mutex); release = true; } gate.notify_one();
    pipeline.Join(); const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames == 9 && result.output_frames == 9);
    assert(result.superseded_frames == 0 && result.coalesced_frames == 0 && result.held_frames == 0);
    assert(DecodedFrames(argv[1], o.output_path) == result.output_frames);
  }
  {
    // Multiple genuine paints in one output slot must not disappear from
    // accounting merely because the encoder queue never overflowed.
    auto o = options("coalesced.webm"); o.frames_per_second = 30;
    Started started;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); });
    pipeline.Frame(Frame(true), 100); started.Wait();
    pipeline.Frame(Frame(false), 101); pipeline.Frame(Frame(true), 102);
    pipeline.Stop(150, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames == 3 && result.output_frames == 2);
    assert(result.superseded_frames == 0 && result.coalesced_frames == 1);
    assert(DecodedFrames(argv[1], o.output_path) == result.output_frames);
  }
  {
    // A delayed UI callback can deliver all genuine 120 Hz paints in a short
    // burst. Preserve them within 25 ms rather than introducing a hold/drop
    // pair and then losing their successors while the source clock catches up.
    auto o = options("matched-burst-stop.webm");
    o.frames_per_second = o.capture_frames_per_second = 120;
    Started started;
    std::mutex gate_mutex; std::condition_variable gate; bool release = false;
    MediaPipeline pipeline(o, {}, [&](double timestamp) {
      started.Signal(timestamp);
      std::unique_lock lock(gate_mutex); gate.wait(lock, [&] { return release; });
    });
    pipeline.Frame(Frame(true), 100); started.Wait();
    const double timestamps[]{108.333333,134.666667,135,135.333333,141.666667,150,158.333333,166.666667};
    for (int i = 0; i < 8; ++i) pipeline.Frame(Frame(i % 2), timestamps[i]);
    pipeline.Stop(175, Clock::now() + std::chrono::seconds(15));
    { std::lock_guard lock(gate_mutex); release = true; } gate.notify_one();
    pipeline.Join(); const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames == 9 && result.output_frames == 9);
    assert(result.superseded_frames == 0 && result.coalesced_frames == 0 && result.held_frames == 0);
    assert(DecodedFrames(argv[1], o.output_path) == result.output_frames);
  }
  {
    // All images arrive, but two delayed callbacks accumulate 43 ms of lag,
    // then recover in a burst. Future evidence must undo arrival jitter.
    auto o = options("lookahead-burst.webm");
    o.frames_per_second = o.capture_frames_per_second = 120;
    Started started;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); });
    pipeline.Frame(Frame(true), 100); started.Wait();
    const auto began = Clock::now();
    for (int i = 1; i < 60; ++i) {
      double delay = 0;
      if (i == 20) delay = 21;
      if (i >= 21 && i <= 30) delay = 43.0 * (31-i) / 10;
      const double elapsed = i * 1000.0 / 120 + delay;
      std::this_thread::sleep_until(began + std::chrono::microseconds(static_cast<int64_t>(elapsed*1000)));
      pipeline.Frame(Frame(i%2), 100+elapsed);
    }
    pipeline.Stop(600, Clock::now()+std::chrono::seconds(15)); pipeline.Join();
    const auto result=pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames==60 && result.output_frames==60);
    assert(result.held_frames==0 && result.coalesced_frames==0 && result.superseded_frames==0);
    assert(result.max_timestamp_correction_ms>40 && result.capture_lookahead_ms==100);
    assert(DecodedFrames(argv[1],o.output_path)==60);
  }
  {
    auto o=options("lookahead-subslot-phase.webm");
    o.frames_per_second=o.capture_frames_per_second=120;
    Started started;
    MediaPipeline pipeline(o,{},[&](double timestamp){started.Signal(timestamp);});
    pipeline.Frame(Frame(true),100); started.Wait();
    for(int i=1;i<10;++i) pipeline.Frame(Frame(i%2),100+i*1000.0/120+7.5);
    pipeline.Stop(100+10000.0/120,Clock::now()+std::chrono::seconds(15)); pipeline.Join();
    const auto result=pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames==10 && result.output_frames==10 && result.held_frames==0);
    assert(result.coalesced_frames==0 && result.superseded_frames==0);
  }
  {
    // Native capture time needs no arrival lookahead.
    // A pre-admission surface delivered late is stale input, not corruption.
    auto o=options("native-capture-clock.webm");
    o.frames_per_second=o.capture_frames_per_second=120; o.capture_timestamps_exact=true;
    Started started;
    MediaPipeline pipeline(o,{},[&](double timestamp){started.Signal(timestamp);});
    pipeline.Frame(Frame(true),99);
    pipeline.Frame(Frame(true),100); started.Wait();
    for(int i=1;i<10;++i) pipeline.Frame(Frame(i%2),100+i*1000.0/120);
    pipeline.Stop(100+10000.0/120,Clock::now()+std::chrono::seconds(15)); pipeline.Join();
    const auto result=pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames==10 && result.output_frames==10 && result.held_frames==0);
    assert(result.capture_timestamps_exact && result.capture_lookahead_ms==0);
    assert(result.coalesced_frames==0 && DecodedFrames(argv[1],o.output_path)==10);
  }
  {
    // Accumulated sub-slot quantization must not coalesce genuine native
    // cadence. A subsequent two-slot source interval remains a genuine gap.
    auto o=options("native-cfr-phase.webm");
    o.frames_per_second=o.capture_frames_per_second=120;o.capture_timestamps_exact=true;
    Started started;MediaPipeline pipeline(o,{},[&](double timestamp){started.Signal(timestamp);});
    pipeline.Frame(Frame(true),100);started.Wait();
    for(int i=1;i<10;++i)pipeline.Frame(Frame(i%2),100+i*1000.0/120-i*5.0/9);
    const double after_gap=100+11*1000.0/120-5;
    pipeline.Frame(Frame(true),after_gap);
    pipeline.Stop(after_gap+1000.0/120,Clock::now()+std::chrono::seconds(15));pipeline.Join();
    const auto result=pipeline.Snapshot();CheckSuccess(result);
    assert(result.input_frames==11&&result.output_frames==12&&result.held_frames==1);
    assert(result.superseded_frames==0&&result.coalesced_frames==0);
    assert(DecodedFrames(argv[1],o.output_path)==12);
  }
  {
    // Stop seals capture time, then accepts already-captured video/PCM that
    // arrive late. Repeated stop must preserve its original cutoff.
    auto o=options("native-stop-drain.webm");o.frames_per_second=o.capture_frames_per_second=120;
    o.capture_timestamps_exact=o.audio=true;Started started;
    MediaPipeline pipeline(o,{},[&](double timestamp){started.Signal(timestamp);});
    pipeline.Frame(Frame(true),100);pipeline.Audio(PCM(),100);started.Wait();
    pipeline.Stop(150,Clock::now()+std::chrono::seconds(15));assert(pipeline.WantsFrames());
    pipeline.Stop(160,Clock::now()+std::chrono::seconds(10));
    pipeline.Frame(Frame(false),100+4000.0/120);pipeline.Audio(PCM(),120);pipeline.Audio(PCM(),140);
    pipeline.Frame(Frame(true),100+7000.0/120);assert(!pipeline.WantsFrames());pipeline.Join();
    auto result=pipeline.Snapshot();CheckSuccess(result);
    assert(result.duration_ms==50&&result.input_frames==2&&result.output_frames==6);
    assert(result.audio_input_frames==2400&&result.audio_output_frames==2400&&result.audio_gap_frames==0);
    assert(DecodedFrames(argv[1],o.output_path,{true,true,true,true,false,false})==6);
  }
  {
    // Delivery after a rapid pause/resume must use the segment at capture
    // time. Split PCM across both boundaries without shifting older samples.
    auto o=options("native-pause-segments.webm");o.frames_per_second=o.capture_frames_per_second=120;
    o.capture_timestamps_exact=o.audio=true;Started started;
    MediaPipeline pipeline(o,{},[&](double timestamp){started.Signal(timestamp);});
    pipeline.Frame(Frame(true),100);pipeline.Audio(PCM(),100);started.Wait();
    pipeline.Pause(120);pipeline.Resume(125);
    pipeline.Frame(Frame(false),100+2000.0/120);pipeline.Frame(Frame(true),122);
    pipeline.Frame(Frame(true),105+4000.0/120);
    pipeline.Audio(PCM(),110);pipeline.Audio(PCM(),130);
    pipeline.Stop(150,Clock::now()+std::chrono::seconds(15));pipeline.Frame(Frame(false),151);pipeline.Join();
    auto result=pipeline.Snapshot();CheckSuccess(result);
    assert(result.duration_ms==45&&result.input_frames==3&&result.output_frames==6);
    assert(result.audio_input_frames==2640&&result.audio_overlap_frames==480&&result.audio_gap_frames==240);
    assert(DecodedFrames(argv[1],o.output_path,{true,true,false,false,true,true})==6);
  }
  {
    // A complete missing source interval must insert a hold even though
    // sub-slot callback phase noise is retained at matching capture cadence.
    auto o=options("lookahead-one-missing.webm");
    o.frames_per_second=o.capture_frames_per_second=120;
    Started started;
    MediaPipeline pipeline(o,{},[&](double timestamp){started.Signal(timestamp);});
    pipeline.Frame(Frame(true),100); started.Wait();
    pipeline.Frame(Frame(false),100+2000.0/120);
    pipeline.Stop(125,Clock::now()+std::chrono::seconds(15)); pipeline.Join();
    const auto result=pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames==2 && result.output_frames==3 && result.held_frames==1);
    assert(result.coalesced_frames==0);
  }
  {
    // Timing reconstruction must retain the sequence holes caused by bounded
    // ingress overload, rather than presenting dropped input as full cadence.
    auto o=options("lookahead-queue-drop.webm");
    o.frames_per_second=o.capture_frames_per_second=120;
    Started started;
    std::mutex gate_mutex; std::condition_variable gate; bool release=false;
    MediaPipeline pipeline(o,{},[&](double timestamp){
      started.Signal(timestamp);
      std::unique_lock lock(gate_mutex); gate.wait(lock,[&]{return release;});
    });
    pipeline.Frame(Frame(true),100); started.Wait();
    for(int i=1;i<=50;++i) pipeline.Frame(Frame(i%2),100+i*1000.0/120);
    pipeline.Stop(525,Clock::now()+std::chrono::seconds(15));
    {std::lock_guard lock(gate_mutex); release=true;} gate.notify_one();
    pipeline.Join(); const auto result=pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames==51 && result.output_frames==51);
    assert(result.superseded_frames==26 && result.held_frames==26 && result.coalesced_frames==0);
    assert(DecodedFrames(argv[1],o.output_path)==51);
  }
  {
    auto o=options("lookahead-cancel.webm");
    o.frames_per_second=o.capture_frames_per_second=120;
    MediaPipeline pipeline(o); pipeline.Frame(Frame(true),100);
    pipeline.Cancel(); pipeline.Join();
    assert(pipeline.Snapshot().failure.find("canceled")!=std::string::npos);
    assert(!std::filesystem::exists(o.output_path));
  }
  {
    // Source view counters continue while paused. Their paused range must not
    // become an apparent capture burst on the pause-excluded media timeline.
    auto o=options("worker-pause-clock.webm");
    o.frames_per_second=o.capture_frames_per_second=120; o.worker_canvas_fit=o.worker_i420=true;
    Started started;
    MediaPipeline pipeline(o,{},[&](double timestamp){started.Signal(timestamp);});
    pipeline.CanvasFrame(Frame(true),64,64,100,1000); started.Wait();
    pipeline.CanvasFrame(Frame(false),64,64,140,1001);
    pipeline.Pause(150); pipeline.Resume(250);
    pipeline.CanvasFrame(Frame(true),64,64,260,1030);
    pipeline.Stop(400,Clock::now()+std::chrono::seconds(15)); pipeline.Join();
    const auto result=pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames==3 && result.output_frames==24 && result.held_frames==21);
    assert(result.max_timestamp_correction_ms==0);
  }
  {
    // BGRA fitting and BT.709 I420 conversion share the existing media worker.
    auto o=options("worker-canvas.webm");
    o.frames_per_second=o.capture_frames_per_second=120;
    o.worker_canvas_fit=o.worker_i420=true;
    MediaPipeline pipeline(o);
    auto pixels=Frame(true); pixels.resize(32*64*4);
    pipeline.CanvasFrame(pixels,32,64,100,1);
    pipeline.Stop(125,Clock::now()+std::chrono::seconds(15)); pipeline.Join();
    const auto result=pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames==1 && result.output_frames==3);
    assert(DecodedFrames(argv[1],o.output_path)==3);
  }
  {
    // A fractional stop just after the final packet's timestamp must not let
    // Opus/WebM timestamp offsets trim one video packet during stream copy.
    auto o = options("fractional-audio.webm"); o.frames_per_second = 120; o.audio = true;
    MediaPipeline pipeline(o); pipeline.Frame(Frame(true), 100); pipeline.Audio(PCM(), 100);
    pipeline.Stop(241.915, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.output_frames == 18 && result.audio_output_frames == 7200);
    assert(DecodedFrames(argv[1], o.output_path) == result.output_frames);
  }
  {
    // A genuine final paint nearest to the next CFR slot must survive stop,
    // even when ceil(stop duration) alone would end before that image's slot.
    auto o = options("terminal-paint.webm");
    o.frames_per_second = o.capture_frames_per_second = 120;
    Started started;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); });
    pipeline.Frame(Frame(true), 100); started.Wait();
    pipeline.Frame(Frame(false), 108.1);
    pipeline.Stop(108.2, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames == 2 && result.output_frames == 2);
    assert(result.coalesced_frames == 0 && result.held_frames == 0 && result.superseded_frames == 0);
    assert(DecodedFrames(argv[1], o.output_path) == 2);
  }
  {
    auto o = options("held-120.webm"); o.frames_per_second = o.capture_frames_per_second = 120;
    Started started;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); });
    pipeline.Frame(Frame(true), 100); started.Wait();
    assert(pipeline.Snapshot().output_frames >= 1 && pipeline.Snapshot().input_frames == 1);
    pipeline.Frame(Frame(false), 200);
    pipeline.Stop(400, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames == 2 && result.output_frames == 36 && result.held_frames == 34);
    assert(result.duration_ms == 300 && result.encoded_duration_ms == 300);
  }
  {
    auto o = options("pause-audio.webm"); o.frames_per_second = 120; o.audio = true;
    Started started;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); });
    pipeline.Frame(Frame(true), 100); started.Wait(); pipeline.Audio(PCM(), 100);
    pipeline.Pause(150); pipeline.Frame(Frame(false), 200); pipeline.Audio(PCM(), 200);
    pipeline.Resume(250); pipeline.Frame(Frame(false), 300); pipeline.Audio(PCM(), 300);
    pipeline.Stop(400, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.input_frames == 2 && result.output_frames == 24 && result.duration_ms == 200);
    assert(result.audio_input_frames == 1920 && result.audio_output_frames == 9600);
    assert(result.audio_gap_frames == 7680 && result.audio_overlap_frames == 0);
  }
  {
    auto o = options("fractional.webm"); o.frames_per_second = 59.94;
    Started started;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); });
    pipeline.Frame(Frame(true), 100); started.Wait();
    pipeline.Stop(1100, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.output_frames == 60 && result.held_frames == 59);
  }
  {
    auto o=options("worker-nv12.webm");o.frames_per_second=120;
    o.worker_canvas_fit=o.worker_nv12=true;
    MediaPipeline pipeline(o);pipeline.CanvasFrame(Frame(true),64,64,100,1);
    pipeline.Stop(125,Clock::now()+std::chrono::seconds(15));pipeline.Join();
    auto result=pipeline.Snapshot();CheckSuccess(result);
    assert(result.output_frames==3&&DecodedFrames(argv[1],o.output_path)==3);
  }
  {
    // Known BT.709 limited-range plane values from a real CEF red capture.
    auto o = options("i420-709.webm"); o.frames_per_second = 29.97; o.i420 = true;
    std::vector<uint8_t> planes(64 * 64 * 3 / 2, 85);
    std::fill(planes.begin() + 64 * 64, planes.begin() + 64 * 64 * 5 / 4, 122);
    std::fill(planes.begin() + 64 * 64 * 5 / 4, planes.end(), 215);
    MediaPipeline pipeline(o); pipeline.Frame(std::move(planes), 100);
    pipeline.Stop(400, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
    const auto result = pipeline.Snapshot(); CheckSuccess(result);
    assert(result.output_frames == 9 && result.input_frames == 1 && result.held_frames == 8);
  }
  {
    auto o = options("missing-encoder.webm"); o.ffmpeg_path = "/does-not-exist/suitecut-ffmpeg";
    MediaPipeline pipeline(o); pipeline.Frame(Frame(true), 100); pipeline.Stop(200, Clock::now() + std::chrono::seconds(5));
    pipeline.Join(); assert(!pipeline.Snapshot().failure.empty());
    assert(!std::filesystem::exists(o.output_path));
  }
  {
    auto o = options("encoder-failure.webm"); o.ffmpeg_path = "/bin/false";
    MediaPipeline pipeline(o); pipeline.Frame(Frame(true), 100); pipeline.Stop(200, Clock::now() + std::chrono::seconds(5));
    pipeline.Join(); assert(!pipeline.Snapshot().failure.empty()); assert(!std::filesystem::exists(o.output_path));
  }
  {
    const auto stall = directory / "stalled-encoder";
    const auto marker = directory / "encoder-ready";
    std::filesystem::remove(stall); std::filesystem::remove(marker);
    std::filesystem::create_symlink(std::filesystem::canonical(argv[0]), stall);
    assert(mkfifo(marker.c_str(), 0600) == 0);
    const int fd = open(marker.c_str(), O_RDWR | O_CLOEXEC); assert(fd >= 0);
    auto o = options("canceled.webm"); o.ffmpeg_path = stall;
    MediaPipeline pipeline(o);
    pipeline.Frame(Frame(true), 100);
    char ready[5]; assert(read(fd, ready, sizeof(ready)) == sizeof(ready)); close(fd);
    pipeline.Cancel(); pipeline.Join();
    assert(pipeline.Settled()); assert(pipeline.Snapshot().failure.find("canceled") != std::string::npos);
    assert(!std::filesystem::exists(o.output_path));
    std::filesystem::remove(stall); std::filesystem::remove(marker);
  }
  {
    auto o = options("no-first-frame.webm"); o.first_frame_deadline = Clock::now();
    MediaPipeline pipeline(o); pipeline.Join();
    assert(pipeline.Snapshot().failure.find("first native") != std::string::npos);
  }
  {
    auto o = options("expired-finish.webm");
    MediaPipeline pipeline(o); pipeline.Frame(Frame(true), 100); pipeline.Stop(200, Clock::now()); pipeline.Join();
    assert(pipeline.Snapshot().failure.find("Timeout") != std::string::npos);
    assert(!std::filesystem::exists(o.output_path));
  }
  {
    auto o = options("overflow.webm"); o.audio = true;
    // No first frame: genuine pending audio is bounded, not silently discarded.
    MediaPipeline pipeline(o);
    for (int packet = 0; packet < 1600 && !pipeline.Settled(); ++packet)
      pipeline.Audio(std::vector<uint8_t>(4096 * 4), 100);
    pipeline.Join(); assert(pipeline.Snapshot().failure.find("queue exceeded") != std::string::npos);
  }
  {
    auto o = options("invalid.webm");
    MediaPipeline pipeline(o); pipeline.Frame(Frame(true), 1e100); pipeline.Join();
    assert(pipeline.Snapshot().failure.find("Malformed") != std::string::npos);
  }
  {
    auto o = options("invalid-audio.webm"); o.audio = true;
    MediaPipeline pipeline(o); pipeline.Audio({1, 2, 3}, 100); pipeline.Join();
    assert(pipeline.Snapshot().failure.find("Malformed") != std::string::npos);
  }
  {
    auto victim = directory / "victim.txt";
    { std::ofstream file(victim); file << "unchanged"; }
    auto o = options("link.webm");
    std::filesystem::remove(o.output_path); std::filesystem::create_symlink(victim, o.output_path);
    Started started;
    MediaPipeline pipeline(o, {}, [&](double timestamp) { started.Signal(timestamp); });
    pipeline.Frame(Frame(true), 100); started.Wait(); pipeline.Stop(200, Clock::now() + std::chrono::seconds(15)); pipeline.Join();
#ifdef __APPLE__
    assert(!pipeline.Snapshot().failure.empty());
    std::ifstream original(victim); std::string text; original >> text;
    assert(text == "unchanged" && std::filesystem::is_symlink(o.output_path));
#else
    CheckSuccess(pipeline.Snapshot()); std::ifstream original(victim); std::string text; original >> text;
    assert(text == "unchanged" && !std::filesystem::is_symlink(o.output_path));
#endif
  }
  {
    auto o = options("quota.webm"); o.max_file_bytes = 1024;
    MediaPipeline pipeline(o); pipeline.Frame(Frame(true), 100);
    pipeline.Stop(1100, Clock::now() + std::chrono::seconds(10)); pipeline.Join();
    assert(!pipeline.Snapshot().failure.empty()); assert(!std::filesystem::exists(o.output_path));
  }
  {
    auto o = options("pcm-quota.webm"); o.audio = true; o.max_file_bytes = 1024;
    MediaPipeline pipeline(o); pipeline.Audio(PCM(), 100); pipeline.Frame(Frame(true), 100);
    pipeline.Stop(200, Clock::now() + std::chrono::seconds(10)); pipeline.Join();
    assert(pipeline.Snapshot().failure.find("PCM disk quota") != std::string::npos);
    assert(!std::filesystem::exists(o.output_path));
  }
#ifdef __linux__
  {
    // Adopt the killed worker's real encoder so this test can waitpid its
    // physical death, instead of accepting a zombie or guessing disappearance.
    assert(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
    int ready_pipe[2]; assert(pipe(ready_pipe) == 0);
    posix_spawn_file_actions_t actions; assert(posix_spawn_file_actions_init(&actions) == 0);
    assert(posix_spawn_file_actions_adddup2(&actions, ready_pipe[1], STDOUT_FILENO) == 0);
    assert(posix_spawn_file_actions_addclose(&actions, ready_pipe[0]) == 0);
    assert(posix_spawn_file_actions_addclose(&actions, ready_pipe[1]) == 0);
    const auto launcher = std::filesystem::canonical(argv[0]).string();
    const std::string path = directory.string();
    const std::string expected_parent = std::to_string(getpid());
    std::vector<char*> args{const_cast<char*>(launcher.c_str()), const_cast<char*>("--parent-kill-worker"),
      argv[1], const_cast<char*>(path.c_str()), const_cast<char*>(expected_parent.c_str()), nullptr};
    pid_t worker; assert(posix_spawn(&worker, launcher.c_str(), &actions, nullptr, args.data(), environ) == 0);
    posix_spawn_file_actions_destroy(&actions); close(ready_pipe[1]);
    char ready[128]; const auto count = read(ready_pipe[0], ready, sizeof(ready)); assert(count > 0); close(ready_pipe[0]);
    std::istringstream actual(std::string(ready, static_cast<size_t>(count)));
    std::string marker; pid_t encoder = 0; actual >> marker >> encoder;
    assert(marker == "ready" && encoder > 1);
    assert(std::filesystem::canonical("/proc/" + std::to_string(encoder) + "/exe") == std::filesystem::canonical(argv[1]));
    int anonymous = 0; bool pcm_bytes = false;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/" + std::to_string(worker) + "/fd")) {
      struct stat info{};
      if (stat(entry.path().c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 0) {
        ++anonymous; pcm_bytes = pcm_bytes || info.st_size >= 3840;
      }
    }
    assert(anonymous >= 3 && pcm_bytes);
    assert(kill(worker, SIGKILL) == 0);
    int status; assert(waitpid(worker, &status, 0) == worker && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    assert(waitpid(encoder, &status, 0) == encoder && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    assert(!std::filesystem::exists(directory / "parent-killed.webm"));
    assert(!std::filesystem::exists("/proc/" + std::to_string(worker) + "/fd"));
    assert(!std::filesystem::exists("/proc/" + std::to_string(encoder) + "/fd"));
  }
#endif
  NoPrivateFiles(directory);
  int status = 0; assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
  std::cout << "native media pipeline core: encoded video/audio, cadence, pause, cancellation, deadlines and cleanup passed\n";
}
