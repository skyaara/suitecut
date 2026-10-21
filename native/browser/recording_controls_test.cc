// SPDX-License-Identifier: MIT
#include "recording_controls.h"
#include <unistd.h>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <sys/wait.h>

using suitecut::RecordingControls;
using suitecut::MediaPipelineOptions;
using suitecut::MediaPipelineResult;
using Clock = std::chrono::steady_clock;
class UIQueue {
 public:
  void Push(std::function<void()> task) {
    { std::lock_guard lock(mutex_); tasks_.push_back(std::move(task)); }
    changed_.notify_one();
  }
  void Until(const std::function<bool()>& done) {
    const auto deadline = Clock::now() + std::chrono::seconds(20);
    while (!done()) {
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        assert(changed_.wait_until(lock, deadline, [this] { return !tasks_.empty(); }));
        task = std::move(tasks_.front()); tasks_.pop_front();
      }
      task();
    }
  }
 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::function<void()>> tasks_;
};
int main(int argc, char** argv) {
  const int helper = suitecut::MediaEncoderHelperMain(argc, argv);
  if (helper >= 0) return helper;
  assert(argc == 3 || (argc == 4 && std::string(argv[3]) == "automatic-fast-close"));
  const auto directory = std::filesystem::path(argv[2]) / ("run-" + std::to_string(getpid())); std::filesystem::create_directories(directory);
  UIQueue queue;
  int settled = 0;
  RecordingControls::Hooks hooks;
  hooks.now = [] { return 400.0; };
  hooks.dispatch = [&](std::function<void()> task) { queue.Push(std::move(task)); };
  hooks.physical_settled = [&](int page, int context) { assert(page > 0 && context > 0); ++settled; };
  auto controls = std::make_shared<RecordingControls>(hooks);
  assert(!controls->NeedsFrame(2) && !controls->NeedsFrame(2, true));
  auto options = [&](const char* name) {
    MediaPipelineOptions o;
    o.launcher_path = std::filesystem::canonical(argv[0]).string();
    o.ffmpeg_path = argv[1]; o.output_path = (directory / name).string();
    o.source_width = o.source_height = o.output_width = o.output_height = 64;
    assert(!o.top_left_padding); // Manual option default is unchanged.
    o.start_timestamp_ms = 100; o.first_frame_deadline = Clock::now() + std::chrono::seconds(10);
    return o;
  };
  const std::vector<uint8_t> frame(64 * 64 * 4, 255);
  if (argc == 4) {
    auto automatic_options=options("automatic-fast-close.webm");
    automatic_options.output_width=automatic_options.output_height=128;
    automatic_options.max_content_width=automatic_options.max_content_height=64;
    automatic_options.top_left_padding=true;
    const auto automatic = controls->StartAutomatic(30,31,std::move(automatic_options));
    assert(controls->Phase(automatic)=="starting"&&controls->Status(automatic).first_timestamp_ms<0);
    assert(controls->PhysicalCount()==1&&!controls->ContextReady(31));
    const auto close_deadline=Clock::now()+std::chrono::seconds(10);
    // Context finalization precedes per-page close in the actual Engine. Both
    // must retain the admitted recorder while no genuine paint has arrived.
    controls->FinalizeContext(31,100,close_deadline);
    bool closed=false;
    controls->BeforePageClose(30,[&]{assert(controls->ContextReady(31)&&controls->PhysicalCount()==0);closed=true;},close_deadline);
    assert(!closed&&controls->Phase(automatic)=="starting");
    assert(controls->Status(automatic).first_timestamp_ms<0);
    // This is a real MediaPipeline input buffer after the real close request,
    // not a synthetic CEF-paint or callback settlement receipt.
    controls->Frame(30,frame,100);
    queue.Until([&]{return closed;});
    const auto result=controls->Status(automatic);
    assert(result.failure.empty()&&result.first_timestamp_ms==100&&result.output_frames>0&&result.output_bytes>0);
    assert(controls->Phase(automatic)=="complete"&&std::filesystem::exists(result.output_path));
    assert(settled==1);
    auto no_frame_options=options("automatic-no-frame.webm");
    no_frame_options.first_frame_deadline=Clock::now()+std::chrono::seconds(30);
    const auto original_first_frame=no_frame_options.first_frame_deadline;
    const auto no_frame=controls->StartAutomatic(32,33,std::move(no_frame_options));
    bool failed_closed=false;
    // A genuine already-expired close cap must wake the real worker, not wait
    // for the later first-frame cap or fabricate a successful encoder start.
    controls->BeforePageClose(32,[&]{assert(controls->ContextReady(33));failed_closed=true;},Clock::now());
    assert(!failed_closed);
    queue.Until([&]{return failed_closed;});
    const auto failed=controls->Status(no_frame);
    assert(!failed.failure.empty()&&failed.first_timestamp_ms<0&&failed.output_frames==0);
    assert(Clock::now()<original_first_frame&&controls->Phase(no_frame)=="failed");
    assert(!std::filesystem::exists(failed.output_path)&&controls->PhysicalCount()==0&&settled==2);
    controls.reset();
    int status=0;assert(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD);
    for(const auto& entry:std::filesystem::directory_iterator(directory))assert(!entry.path().filename().string().starts_with(".suitecut-media-"));
    std::cout<<"automatic native close-before-first-frame: genuine encoder frame, artifact and physical closure passed\n";
    return 0;
  }
  bool started = false, stopped = false;
  std::string id;
  id = controls->Start(1, 2, 3, options("normal.webm"), [&](const std::string& actual, const MediaPipelineResult& result) {
    assert(actual == id); assert(result.failure.empty() && result.first_timestamp_ms == 100 && result.output_frames >= 1); started = true;
  });
  assert(controls->PhysicalCount() == 1 && !controls->ContextReady(3));
  assert(!controls->CaptureClockFrozen(2));
  assert(controls->NeedsFrame(2) && !controls->NeedsFrame(2, true) && !controls->NeedsFrame(4));
  bool duplicate = false;
  try { controls->Start(4, 2, 3, options("duplicate.webm"), [](const auto&, const auto&) {}); } catch (...) { duplicate = true; }
  assert(duplicate);
  duplicate = false;
  try { controls->Start(5, 4, 3, options("normal.webm"), [](const auto&, const auto&) {}); } catch (...) { duplicate = true; }
  assert(duplicate);
  controls->Frame(2, frame, 100); queue.Until([&] { return started; });
  assert(controls->Status(id).first_timestamp_ms == 100);
  assert(controls->CaptureClockFrozen(2));
  assert(controls->Status(id).output_frames >= 1);
  controls->Stop(2, id, 400, Clock::now() + std::chrono::seconds(10), [&](const auto& actual, const auto& result) {
    assert(actual == id && result.failure.empty() && result.output_frames == 18); stopped = true;
  });
  assert(!controls->NeedsFrame(2) && controls->PhysicalCount()==1);
  queue.Until([&] { return stopped; });
  assert(controls->PhysicalCount() == 0 && controls->ContextReady(3) && controls->Phase(id) == "complete");
  assert(controls->MetadataCount() == 1 && settled == 1);
  assert(!controls->NeedsFrame(2) && !controls->NeedsFrame(2, true));
  // Retained complete metadata does not own a subprocess. Context closure
  // leaves the caller's already committed artifact intact.
  controls->CloseContext(3); assert(std::filesystem::exists(controls->Status(id).output_path));

  bool failed_start = false;
  const auto pending = controls->Start(3, 5, 6, options("pending.webm"), [&](const auto&, const auto& result) {
    assert(result.failure.find("context has been closed") != std::string::npos); failed_start = true;
  });
  controls->CloseContext(6);
  assert(!controls->ContextReady(6));
  queue.Until([&] { return controls->ContextReady(6); });
  assert(failed_start && controls->Phase(pending) == "failed");
  assert(!std::filesystem::exists(controls->Status(pending).output_path));

  bool forbidden_reply = false;
  controls->Start(4, 7, 8, options("command-cancel.webm"), [&](const auto&, const auto&) { forbidden_reply = true; });
  controls->CancelCommand(4); queue.Until([&] { return controls->ContextReady(8); });
  assert(!forbidden_reply);

  bool owner_close = false;
  controls->Start(5, 9, 10, options("page-cancel.webm"), [&](const auto&, const auto& result) {
    assert(result.failure.empty()); owner_close = true;
  });
  controls->Frame(9, frame, 100);
  queue.Until([&] { return owner_close; });
  bool actual_page_close = false;
  controls->BeforePageClose(9, [&] { assert(controls->PhysicalCount() == 0); actual_page_close = true; });
  assert(!actual_page_close);
  queue.Until([&] { return controls->ContextReady(10); });
  assert(owner_close && actual_page_close);
  assert(std::filesystem::exists(directory / "page-cancel.webm"));

  // Automatic admission returns a genuine registry identity before the first
  // real frame, with no invented command or ready callback. The same physical
  // pipeline and close barrier are then exercised with actual FFmpeg.
  auto automatic_options=options("automatic.webm");automatic_options.dynamic_viewport=true;
  const auto automatic=controls->StartAutomatic(20,21,std::move(automatic_options));
  assert(!controls->NeedsFrame(20) && controls->NeedsFrame(20, true));
  assert(controls->Owns(automatic,20)&&controls->Phase(automatic)=="starting");
  assert(controls->Status(automatic).first_timestamp_ms<0&&!controls->ContextReady(21));
  bool manual_conflict=false;
  try {controls->Start(20,20,21,options("conflict.webm"),[](const auto&,const auto&){});}catch(...){manual_conflict=true;}
  assert(manual_conflict);
  controls->CancelCommand(0); // No public command owns the automatic pipeline.
  assert(!controls->ContextReady(21));
  controls->ViewportFrame(20,frame,64,64,64,64,100);queue.Until([&]{return controls->Phase(automatic)=="recording";});
  bool automatic_closed=false;
  controls->BeforePageClose(20,[&]{assert(controls->ContextReady(21));automatic_closed=true;});
  assert(!automatic_closed);queue.Until([&]{return automatic_closed;});
  assert(controls->Phase(automatic)=="complete"&&controls->Status(automatic).output_frames>0);
  assert(std::filesystem::exists(directory/"automatic.webm"));
  assert(!controls->NeedsFrame(20) && !controls->NeedsFrame(20, true));

  // A closed engine rejects new starts, and all physical workers settle.
  controls->Start(6, 11, 12, options("engine-cancel.webm"), [](const auto&, const auto& result) {
    assert(result.failure.find("engine has been closed") != std::string::npos);
  });
  controls->CloseAll(); queue.Until([&] { return controls->PhysicalCount() == 0; });
  bool rejected = false;
  try { controls->Start(7, 13, 14, options("late.webm"), [](const auto&, const auto&) {}); } catch (...) { rejected = true; }
  assert(rejected);
  controls.reset();
  for (const auto& entry : std::filesystem::directory_iterator(directory))
    assert(!entry.path().filename().string().starts_with(".suitecut-media-"));
  int status = 0; assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
  std::cout << "native recording ownership: actual worker completion, page/context/engine barriers and cancellation passed\n";
}
