// SPDX-License-Identifier: MIT
#include "stream_pipeline.h"
#include "media_pipeline.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
struct Receiver {
  int server = -1;
  std::thread worker;
  std::string error;
  std::filesystem::path output;
  uint16_t port = 0;
  std::mutex mutex; std::condition_variable changed;
  bool stall = false, connected = false, released = false;
  uint64_t close_after = 0;
  Receiver(std::filesystem::path path, bool blocked = false, uint64_t disconnect = 0) : output(std::move(path)), stall(blocked), close_after(disconnect) {
    server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0); assert(server >= 0);
    const int buffer = 4096; assert(setsockopt(server, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer)) == 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0); assert(listen(server, 1) == 0);
    socklen_t length = sizeof(address); assert(getsockname(server, reinterpret_cast<sockaddr*>(&address), &length) == 0); port = ntohs(address.sin_port);
    worker = std::thread([this] {
      try {
        pollfd listening{server, POLLIN, 0}; if (poll(&listening, 1, 10000) <= 0) throw std::runtime_error("Receiver did not get actual publisher connection");
        const int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC); if (client < 0) throw std::runtime_error("Receiver accept failed");
        {
          std::unique_lock lock(mutex); connected = true; changed.notify_all();
          if (stall) { changed.wait_until(lock, Clock::now() + 10s, [this] { return released; }); close(client); return; }
        }
        std::ofstream file(output, std::ios::binary);
        std::array<char, 65536> data; size_t bytes = 0;
        for (;;) {
          pollfd active{client, POLLIN, 0}; if (poll(&active, 1, 10000) <= 0) { close(client); throw std::runtime_error("Receiver stalled before native EOF"); }
          const ssize_t count = recv(client, data.data(), data.size(), 0);
          if (!count) break;
          if (count < 0) { if (errno == EINTR) continue; close(client); throw std::runtime_error("Receiver read failed"); }
          bytes += static_cast<size_t>(count); if (bytes > 64 * 1024 * 1024) { close(client); throw std::runtime_error("Receiver byte bound exceeded"); }
          file.write(data.data(), count);
          if (close_after && bytes >= close_after) break;
        }
        close(client);
      } catch (const std::exception& failure) { error = failure.what(); }
    });
  }
  ~Receiver() { Release(); if (worker.joinable()) worker.join(); if (server >= 0) close(server); }
  void Release() { { std::lock_guard lock(mutex); released = true; } changed.notify_all(); }
  bool Connected() { std::lock_guard lock(mutex); return connected; }
  void Join() { if (worker.joinable()) worker.join(); if (!error.empty()) std::cerr << error << "\n"; assert(error.empty()); if (!stall) assert(std::filesystem::file_size(output) > 0); }
};
std::vector<uint8_t> Frame(bool blue) {
  std::vector<uint8_t> data(64 * 64 * 4);
  for (size_t index = 0; index < data.size(); index += 4) { data[index] = blue ? 240 : 30; data[index + 1] = 30; data[index + 2] = blue ? 30 : 240; data[index + 3] = 255; }
  return data;
}
std::vector<uint8_t> Noise(unsigned seed) {
  std::vector<uint8_t> data(160 * 96 * 4);
  for (auto& byte : data) { seed = seed * 1664525 + 1013904223; byte = static_cast<uint8_t>(seed >> 24); }
  return data;
}
std::vector<uint8_t> Audio() {
  std::vector<uint8_t> data(1600 * 4); // A real deterministic square-wave PCM source.
  for (size_t index = 0; index < 1600; ++index) { const int16_t sample = index % 48 < 24 ? 4000 : -4000; std::memcpy(data.data() + index * 4, &sample, 2); std::memcpy(data.data() + index * 4 + 2, &sample, 2); }
  return data;
}
suitecut::StreamPipelineOptions Options(const char* launcher, const char* ffmpeg, uint16_t port) {
  suitecut::StreamPipelineOptions options;
  options.launcher_path = launcher; options.ffmpeg_path = ffmpeg;
  options.source_width = options.source_height = options.output_width = options.output_height = 64;
  options.frames_per_second = 120; options.audio = true;
  options.video_codec = "vp9"; options.audio_codec = "opus"; options.container = "webm";
  options.destination = "tcp://127.0.0.1:" + std::to_string(port);
  options.first_frame_deadline = Clock::now() + 5s;
  return options;
}
}
int main(int argc, char** argv) {
  // Genuine stalled subprocess fixture: it keeps the admitted pipe open,
  // emits no encoded progress, and exits only through the native owner's kill.
  if (std::filesystem::path(argv[0]).filename() == "native-stream-stalled-encoder") {
    for (;;) poll(nullptr, 0, -1);
  }
  const int helper = suitecut::MediaEncoderHelperMain(argc, argv); if (helper >= 0) return helper;
  assert(argc == 3);
  const auto directory = std::filesystem::absolute(argv[2]); std::filesystem::create_directories(directory);
  Receiver receiver(directory / "actual-stream.webm");
  auto options = Options(std::filesystem::absolute(argv[0]).c_str(), argv[1], receiver.port);
  std::mutex mutex; std::condition_variable changed; bool complete = false, ready = false;
  suitecut::StreamPipelineResult final;
  suitecut::StreamPipeline pipeline(options,
    [&](const auto& state) { { std::lock_guard lock(mutex); final = state; complete = true; } changed.notify_all(); },
    [&](const auto& state) { { std::lock_guard lock(mutex); assert(state.encoded_frames > 0); ready = true; } changed.notify_all(); });
  const auto origin = Clock::now();
  pipeline.Frame(Frame(false), 0);
  for (int tick = 0; tick < 30; ++tick) {
    {
      std::unique_lock lock(mutex);
      changed.wait_until(lock, origin + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(tick / 30.0)), [&] { return complete; });
      if (complete) std::cerr << final.failure << "\n" << final.diagnostics << "\n";
      assert(!complete);
    }
    if (tick == 9) pipeline.Pause(true);
    if (tick == 13) pipeline.Pause(false);
    if (tick == 18) pipeline.SwitchSource();
    const double at = std::chrono::duration<double, std::milli>(Clock::now() - origin).count();
    // Generated capture runs at30FPS; the native output clock requests120FPS.
    pipeline.Frame(Frame(tick >= 18), at);
    pipeline.Audio(Audio(), at);
  }
  const auto active = pipeline.Snapshot();
  if (active.submitted_frames <= 80) std::cerr << "submitted=" << active.submitted_frames << " encoded=" << active.encoded_frames << " diagnostics=" << active.diagnostics << "\n";

  pipeline.Stop(Clock::now() + 5s);
  { std::unique_lock lock(mutex); assert(changed.wait_until(lock, Clock::now() + 6s, [&] { return complete; })); }
  pipeline.Join();
  if (!final.failure.empty()) std::cerr << final.failure << "\n";
  receiver.Join();
  assert(active.submitted_frames > 80); assert(active.held_frames > 30);
  assert(active.audio_submitted_frames > 32000); assert(active.paused_audio_frames > 0);
  assert(ready); assert(final.failure.empty()); if(!final.diagnostics.empty())std::cerr<<final.diagnostics; assert(final.diagnostics.empty()); assert(final.physical_encoder_pid == 0);
  assert(final.encoded_frames == final.submitted_frames); assert(final.muxed_bytes > 0);
  assert(pipeline.Settled());
  std::ofstream report(directory / "native-stream-core.json");
  report << "{\"submittedFrames\":" << final.submitted_frames << ",\"encodedFrames\":" << final.encoded_frames << ",\"heldFrames\":" << final.held_frames << ",\"audioInputFrames\":" << final.audio_input_frames << ",\"audioOutputFrames\":" << final.audio_submitted_frames << ",\"audioGapFrames\":" << final.audio_gap_frames << ",\"pausedAudioFrames\":" << final.paused_audio_frames << ",\"muxedBytes\":" << final.muxed_bytes << "}";
  report.close();
  {
    auto failed_options = options; failed_options.ffmpeg_path = "/bin/false"; failed_options.first_frame_deadline = Clock::now() + 2s;
    suitecut::StreamPipeline failed(failed_options); failed.Frame(Frame(false), 0); failed.Stop(Clock::now() + 2s); failed.Join();
    assert(failed.Settled()); assert(!failed.Snapshot().failure.empty()); assert(failed.Snapshot().physical_encoder_pid == 0);
  }
  {
    auto expired_options = options; expired_options.first_frame_deadline = Clock::now() + 50ms;
    suitecut::StreamPipeline expired(expired_options); expired.Join(); assert(expired.Snapshot().failure.find("Timeout") != std::string::npos);
  }
  {
    auto pending_options = options; pending_options.first_frame_deadline = Clock::time_point{};
    suitecut::StreamPipeline canceled(pending_options); canceled.Cancel("Real native publisher cancellation"); canceled.Join(); assert(canceled.Snapshot().failure == "Real native publisher cancellation");
  }
  {
    auto invalid = options; invalid.audio_codec = "aac";
    bool rejected = false; try { suitecut::ValidateStreamPipelineOptions(invalid); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected);
  }
  {
    Receiver flv(directory / "actual-stream.flv");
    auto flv_options = options; flv_options.video_codec = "h264"; flv_options.audio_codec = "aac"; flv_options.container = "flv";
    flv_options.frames_per_second = 60; flv_options.destination = "tcp://127.0.0.1:" + std::to_string(flv.port);
    flv_options.first_frame_deadline = Clock::now() + 5s;
    suitecut::StreamPipeline publisher(flv_options);
    const auto start = Clock::now(); publisher.Frame(Frame(false), 0);
    std::mutex tick_mutex; std::condition_variable tick;
    for (int index = 0; index < 18; ++index) {
      { std::unique_lock lock(tick_mutex); tick.wait_until(lock, start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(index / 30.0))); }
      const double at = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
      publisher.Frame(Frame(index >= 9), at); publisher.Audio(Audio(), at);
    }
    publisher.Stop(Clock::now() + 5s); publisher.Join(); flv.Join();
    const auto result = publisher.Snapshot();
    if (!result.failure.empty()) std::cerr << result.failure << "\n";
    assert(result.failure.empty()); if(!result.diagnostics.empty())std::cerr<<result.diagnostics; assert(result.diagnostics.empty()); assert(result.encoded_frames == result.submitted_frames); assert(result.physical_encoder_pid == 0);
    std::ofstream report(directory / "native-stream-flv.json"); report << "{\"frames\":" << result.submitted_frames << ",\"audioFrames\":" << result.audio_submitted_frames << "}";
  }
  {
    auto no_frame = options; no_frame.first_frame_deadline = Clock::time_point{};
    suitecut::StreamPipeline overflow(no_frame);
    for (int packet = 0; packet < 400; ++packet) overflow.Audio(std::vector<uint8_t>(4096 * 4, 42), packet * 1000.0 / 12);
    overflow.Join(); assert(overflow.Snapshot().failure.find("PCM queue exceeded") != std::string::npos); assert(overflow.Snapshot().physical_encoder_pid == 0);
  }
  {
    auto unlimited = options; unlimited.first_frame_deadline = Clock::time_point{};
    std::vector<std::unique_ptr<suitecut::StreamPipeline>> pipelines;
    // The completed primary object retains no capacity lease.
    for (int index = 0; index < 4; ++index) pipelines.push_back(std::make_unique<suitecut::StreamPipeline>(unlimited));
    bool rejected = false; try { suitecut::StreamPipeline extra(unlimited); } catch (const std::runtime_error&) { rejected = true; } assert(rejected);
    for (auto& live : pipelines) { live->Cancel(); live->Join(); assert(live->Snapshot().physical_encoder_pid == 0); }
  }
  {
    // This real publisher is starved of audio during FFmpeg input probing. The
    // first encoded-readiness deadline is shorter than the pipe-lag policy.
    auto initial = options; initial.source_width = 640; initial.source_height = 480;
    const auto stalled = directory / "native-stream-stalled-encoder";
    std::filesystem::remove(stalled); std::filesystem::create_symlink(std::filesystem::absolute(argv[0]), stalled);
    initial.ffmpeg_path = stalled;
    initial.first_frame_deadline = Clock::now() + 40ms; initial.max_write_lag = 2s;
    bool ready_called = false;
    suitecut::StreamPipeline early(initial, {}, [&](const auto&) { ready_called = true; });
    const auto began = Clock::now(); early.Frame(std::vector<uint8_t>(640 * 480 * 4, 42), 0); early.Join();
    const auto state = early.Snapshot(); std::filesystem::remove(stalled);
    assert(!ready_called); assert(state.failure.find("initial encoded readiness deadline") != std::string::npos);
    assert(state.physical_encoder_pid == 0); assert(Clock::now() - began < 1s);
    std::ofstream proof(directory / "short-start-native-failure.txt"); proof << state.failure;
  }
  for (bool blocked : {false, true}) {
    Receiver boundary(directory / (blocked ? "blocked-publisher.bin" : "disconnected-publisher.webm"), blocked, blocked ? 0 : 1024);
    auto network_options = options;
    network_options.audio = false; network_options.source_width = network_options.output_width = 160;
    network_options.source_height = network_options.output_height = 96;
    network_options.destination = "tcp://127.0.0.1:" + std::to_string(boundary.port);
    network_options.tcp_send_buffer_bytes = 4096;
    network_options.max_write_lag = 250ms; network_options.first_frame_deadline = Clock::now() + 5s;
    std::mutex network_mutex; std::condition_variable network_changed; bool done = false;
    suitecut::StreamPipelineResult network_result;
    suitecut::StreamPipeline network(network_options, [&](const auto& state) {
      { std::lock_guard lock(network_mutex); network_result = state; done = true; } network_changed.notify_all();
    });
    const auto start = Clock::now(); network.Frame(Noise(1), 0);
    for (int tick = 1; tick <= 90; ++tick) {
      { std::unique_lock lock(network_mutex); if (network_changed.wait_until(lock, start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(tick / 30.0)), [&] { return done; })) break; }
      network.Frame(Noise(static_cast<unsigned>(tick)), std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    { std::unique_lock lock(network_mutex); if (!done) { lock.unlock(); network.Cancel("Publisher boundary proof did not reach its expected real failure"); } }
    network.Join(); if (!boundary.Connected()) std::cerr << "Before actual TCP connection: " << network_result.failure << "\n"; boundary.Release(); boundary.Join();
    assert(boundary.Connected()); assert(network_result.physical_encoder_pid == 0);
    assert(!network_result.failure.empty());
    std::cerr << (blocked ? "blocked: " : "disconnected: ") << network_result.failure << " frames=" << network_result.submitted_frames << " encoded=" << network_result.encoded_frames << " bytes=" << network_result.muxed_bytes << "\n";
    if (blocked) assert(network_result.failure.find("bounded pipe lag") != std::string::npos || network_result.failure.find("cannot keep up") != std::string::npos);
    else assert(network_result.failure.find("pipe failed") != std::string::npos || network_result.failure.find("exited") != std::string::npos);
    std::ofstream state(directory / (blocked ? "blocked-native-failure.txt" : "disconnected-native-failure.txt")); state << network_result.failure;
  }
  return 0;
}
