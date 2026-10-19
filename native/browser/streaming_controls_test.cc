// SPDX-License-Identifier: MIT
#include "streaming_controls.h"
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
std::vector<uint8_t> Frame(bool blue = false) {
  std::vector<uint8_t> frame(64 * 64 * 4);
  for (size_t i = 0; i < frame.size(); i += 4) { frame[i] = blue ? 240 : 30; frame[i + 1] = 30; frame[i + 2] = blue ? 30 : 240; frame[i + 3] = 255; }
  return frame;
}
}
int main(int argc, char** argv) {
  const int helper = suitecut::MediaEncoderHelperMain(argc, argv); if (helper >= 0) return helper;
  assert(argc == 3); const auto directory = std::filesystem::absolute(argv[2]); std::filesystem::create_directories(directory);
  const auto media_epoch = Clock::now();
  const auto media_now = [&] { return std::chrono::duration<double,std::milli>(Clock::now()-media_epoch).count(); };
  Queue queue; int physically_settled = 0;
  suitecut::StreamingControls::Hooks hooks;
  hooks.dispatch = [&](auto task) { queue.Push(std::move(task)); };
  hooks.physical_settled = [&](int owner_context, int selected_context) { assert(owner_context > 0 && selected_context > 0); ++physically_settled; };
  auto controls = std::make_shared<suitecut::StreamingControls>(hooks);
  auto options = [&](uint16_t port) {
    suitecut::StreamPipelineOptions o; o.launcher_path = std::filesystem::canonical(argv[0]).string(); o.ffmpeg_path = argv[1];
    o.source_width = o.source_height = o.output_width = o.output_height = 64;
    o.start_timestamp_ms = media_now(); o.frames_per_second = 29.97; o.audio = false; o.video_codec = "vp9"; o.audio_codec = "opus"; o.container = "webm";
    o.destination = "tcp://127.0.0.1:" + std::to_string(port); o.first_frame_deadline = Clock::now() + 5s; return o;
  };
  {
    Receiver receiver(directory / "selected-source.webm"); bool ready = false, complete = false;
    const auto settings = options(receiver.port);
    const auto id = controls->Start(1, 2, 3, settings, [&](const auto&, const auto& result) { assert(result.failure.empty() && result.encoded_progress_observed); ready = true; }, "first-real-client-token");
    assert(controls->PhysicalCount() == 1 && !controls->ContextReady(3));
    assert(controls->NeedsFrame(2) && !controls->NeedsFrame(4));
    assert(controls->ClientToken(id) == "first-real-client-token");
    bool token_duplicate = false; try { controls->Start(99, 6, 7, options(1), [](const auto&, const auto&) {}, "first-real-client-token"); } catch (...) { token_duplicate = true; } assert(token_duplicate);
    bool token_malformed = false; try { controls->Start(99, 6, 7, options(1), [](const auto&, const auto&) {}, std::string("bad\0token", 9)); } catch (...) { token_malformed = true; } assert(token_malformed);
    bool duplicate = false; try { controls->Start(2, 4, 3, settings, [](const auto&, const auto&) {}); } catch (...) { duplicate = true; } assert(duplicate);
    controls->Frame(2, Frame(), media_now()); queue.Until([&] { return ready; });
    assert(controls->Status(id).encoded_frames > 0);
    controls->Select(id, 4, 5, 64, 64, false);
    assert(!controls->NeedsFrame(2) && controls->NeedsFrame(4));
    const auto before = controls->Status(id).input_frames;
    controls->Frame(2, Frame(), media_now()); assert(controls->Status(id).input_frames == before);
    controls->Frame(4, Frame(true), media_now()); assert(controls->Status(id).input_frames == before + 1);
    assert(!controls->ContextReady(3) && !controls->ContextReady(5) && controls->SelectedPage(id) == 4);
    bool incompatible = false; try { controls->Select(id, 6, 5, 66, 64, false); } catch (...) { incompatible = true; } assert(incompatible);
    const auto prior_frames = controls->Status(id).submitted_frames;
    const auto actual_frame_deadline = Clock::now() + 2s;
    while (controls->Status(id).submitted_frames < prior_frames + 3 && Clock::now() < actual_frame_deadline) std::this_thread::yield();
    assert(controls->Status(id).submitted_frames >= prior_frames + 3);
    controls->Pause(id, true); controls->Pause(id, false);
    controls->BeforePageClose(4, Clock::now() + 5s, [&] { assert(controls->PhysicalCount() == 0); complete = true; });
    assert(!complete && controls->Phase(id) == "stopping");
    bool closing_start = false; try { controls->Start(8, 4, 5, options(1), [](const auto&, const auto&) {}); } catch (...) { closing_start = true; } assert(closing_start);
    bool stopped_select = false; try { controls->Select(id, 6, 5, 64, 64, false); } catch (...) { stopped_select = true; } assert(stopped_select);
    queue.Until([&] { return complete; }); receiver.Join();
    assert(controls->Phase(id) == "complete" && controls->ContextReady(3) && controls->ContextReady(5));
    assert(!controls->NeedsFrame(2) && !controls->NeedsFrame(4));
    assert(controls->Status(id).physical_encoder_pid == 0 && controls->Status(id).muxed_bytes > 0);
    controls->PageGone(4);
  }
  {
    auto pending = options(1); pending.first_frame_deadline = Clock::time_point{};
    bool forbidden_reply = false; controls->Start(3, 7, 8, pending, [&](const auto&, const auto&) { forbidden_reply = true; });
    controls->CancelCommand(3); queue.Until([&] { return controls->ContextReady(8); }); assert(!forbidden_reply);
  }
  {
    Receiver receiver(directory / "context-close.webm"); bool ready = false;
    auto id = controls->Start(4, 9, 10, options(receiver.port), [&](const auto&, const auto& result) { assert(result.failure.empty()); ready = true; });
    controls->Frame(9, Frame(), media_now()); queue.Until([&] { return ready; });
    controls->FinalizeContext(10, Clock::now() + 5s); assert(controls->Phase(id) == "stopping" && !controls->ContextReady(10));
    queue.Until([&] { return controls->ContextReady(10); }); receiver.Join(); assert(controls->Phase(id) == "complete");
  }
  {
    Receiver receiver(directory / "i420-709.webm"); bool ready = false;
    auto settings = options(receiver.port); settings.i420 = true;
    const auto id = controls->Start(7, 15, 16, settings, [&](const auto&, const auto& result) { assert(result.failure.empty()); ready = true; });
    std::vector<uint8_t> actual709(64 * 64 * 3 / 2, 85);
    std::fill(actual709.begin() + 64 * 64, actual709.begin() + 64 * 64 * 5 / 4, 122);
    std::fill(actual709.begin() + 64 * 64 * 5 / 4, actual709.end(), 215);
    controls->Frame(15, actual709, media_now()); queue.Until([&] { return ready; });
    controls->FinalizeContext(16, Clock::now() + 5s); queue.Until([&] { return controls->ContextReady(16); });
    receiver.Join(); assert(controls->Phase(id) == "complete");
  }
  {
    // An actual admitted FFmpeg process is stopped by the OS, so EOF alone
    // cannot settle it. The later finite owner must bound an unlimited stop.
    Receiver receiver(directory / "shorter-stop.webm"); bool ready = false, first_stop = false, second_stop = false, recovering = false;
    auto settings = options(receiver.port);
    const auto id = controls->Start(10, 17, 18, settings, [&](const auto&, const auto& result) { assert(result.failure.empty()); ready = true; });
    controls->Frame(17, Frame(), media_now()); queue.Until([&] { return ready; });
    const auto pid = controls->Status(id).physical_encoder_pid; assert(pid > 0); assert(kill(pid, SIGSTOP) == 0);
    controls->Stop(11, id, Clock::time_point{}, [&](const auto&, const auto& result) { if (!recovering) assert(result.failure.find("stop deadline") != std::string::npos); first_stop = true; });
    const auto began = Clock::now();
    controls->Stop(12, id, began + 40ms, [&](const auto&, const auto& result) { if (!recovering) assert(result.failure.find("stop deadline") != std::string::npos); second_stop = true; });
    const bool timely = queue.TryUntil([&] { return controls->PhysicalCount() == 0; }, began + 1s);
    if (!timely) { recovering = true; controls->Cancel(id, "Controlled test recovery after ignored later stop deadline"); queue.Until([&] { return controls->PhysicalCount() == 0; }); }
    receiver.Join();
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
    std::ofstream(directory / "shorter-stop.json") << "{\"actualPid\":" << pid << ",\"laterDeadlineMs\":40,\"elapsedMs\":" << elapsed << ",\"deadlineHonored\":" << (timely ? "true" : "false") << ",\"physicalCount\":" << controls->PhysicalCount() << "}\n";
    assert(timely && first_stop && second_stop); assert(Clock::now() - began < 1s);
    assert(controls->Status(id).physical_encoder_pid == 0);
  }
  {
    // Renderer failure closes admission, but real recovery can admit a fresh
    // publisher without reviving old jobs or reopening a closing page.
    controls->FailPage(21);
    bool rejected = false;
    try { controls->Start(20, 21, 22, options(1), [](const auto&, const auto&) {}); } catch (...) { rejected = true; }
    assert(rejected && controls->PhysicalCount() == 0);
    controls->RecoverPage(21);
    bool forbidden_reply = false;
    controls->Start(20, 21, 22, options(1), [&](const auto&, const auto&) { forbidden_reply = true; });
    assert(controls->PhysicalCount() == 1);
    controls->CancelCommand(20); queue.Until([&] { return controls->ContextReady(22); }); assert(!forbidden_reply);
    bool physically_closed = false;
    controls->BeforePageClose(21, Clock::now() + 5s, [&] { physically_closed = true; });
    assert(physically_closed);
    controls->RecoverPage(21);
    rejected = false;
    try { controls->Start(21, 21, 22, options(1), [](const auto&, const auto&) {}); } catch (...) { rejected = true; }
    assert(rejected && controls->PhysicalCount() == 0);
    controls->PageGone(21);
  }
  {
    auto pending = options(1); pending.first_frame_deadline = Clock::time_point{};
    bool failed = false; controls->Start(5, 11, 12, pending, [&](const auto&, const auto& result) { assert(!result.failure.empty()); failed = true; });
    controls->CloseAll(); queue.Until([&] { return controls->PhysicalCount() == 0; }); assert(failed);
    bool rejected = false; try { controls->Start(6, 13, 14, pending, [](const auto&, const auto&) {}); } catch (...) { rejected = true; } assert(rejected);
  }
  assert(physically_settled == 7 && controls->MetadataCount() == 7); controls.reset();
  int status = 0; assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
  std::cout << "Native streaming manager: physical EOF, original ownership, source switch and close/cancel passed\n";
}
