// SPDX-License-Identifier: MIT
#include "media_pipeline.h"
#include <cassert>
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
  struct stat info{};
  assert(stat(result.output_path.c_str(), &info) == 0);
  assert((info.st_mode & 0777) == 0600);
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
  const std::filesystem::path directory(argv[2]);
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
    auto o = options("held-120.webm"); o.frames_per_second = 120;
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
    pipeline.Join(); assert(pipeline.Snapshot().failure.find("launch") != std::string::npos);
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
    CheckSuccess(pipeline.Snapshot()); std::ifstream original(victim); std::string text; original >> text;
    assert(text == "unchanged" && !std::filesystem::is_symlink(o.output_path));
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
