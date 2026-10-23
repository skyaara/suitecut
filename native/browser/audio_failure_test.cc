// SPDX-License-Identifier: MIT
#include <atomic>
#include <cassert>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_browser.h"
#include "include/cef_task.h"
#include "devtools_utf16.h"
#include "tests/support/cef_library.h"

namespace {
bool on_ui = false, accept_post = true;
std::deque<CefRefPtr<CefTask>> scheduled;
class Task : public CefTask {
 public:
  explicit Task(std::function<void()> work) : work_(std::move(work)) {}
  void Execute() override { assert(on_ui); work_(); }
 private:
  std::function<void()> work_;
  IMPLEMENT_REFCOUNTING(Task);
};
bool TestCurrentlyOn(cef_thread_id_t thread) { assert(thread == TID_UI); return on_ui; }
bool TestPostTask(cef_thread_id_t thread, CefRefPtr<CefTask> task) {
  assert(thread == TID_UI);
  if (!accept_post) return false;
  scheduled.push_back(task);
  return true;
}
struct Contexts {
  bool closing = false;
  uint64_t unload = 0;
  bool PageClosePending(int) const { assert(on_ui); return closing || unload; }
  uint64_t BeforeUnloadCloseToken(int) const { assert(on_ui); return unload; }
};
struct Engine {
  Contexts state;
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);
  std::shared_ptr<bool> Alive() const { return alive; }
  Contexts* contexts() { assert(on_ui); return &state; }
};
struct Observation {
  std::vector<std::string> errors;
  int destroyed = 0;
};
class Source : public CefBaseRefCounted {
 public:
  explicit Source(std::shared_ptr<Observation> observation) : observation_(std::move(observation)) {}
  ~Source() override { ++observation_->destroyed; }
  void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString&);
  void Error(const char* message) { observation_->errors.emplace_back(message); }
  std::atomic<bool> audio_valid_{true};
  std::atomic<bool> stopped_{false};
  bool closing_page_ = false;
  const int page_id_ = 1;
  Engine engine_;
 private:
  std::shared_ptr<Observation> observation_;
  IMPLEMENT_REFCOUNTING(Source);
};
#define CefCurrentlyOn TestCurrentlyOn
#define CefPostTask TestPostTask
#include "controllers/media/audio_failure.inc"
#undef CefPostTask
#undef CefCurrentlyOn

void Drain() {
  on_ui = true;
  while (!scheduled.empty()) {
    auto task = scheduled.front();
    scheduled.pop_front();
    task->Execute();
  }
  on_ui = false;
}
}  // namespace

int main() {
  LoadSuiteCutCEFForTest();
  // Genuine active-stream failure survives dispatch and retains its Source.
  auto active = std::make_shared<Observation>();
  CefRefPtr<Source> source = new Source(active);
  source->OnAudioStreamError(nullptr, "capture device stopped");
  assert(!source->audio_valid_ && active->errors.empty());
  source = nullptr;
  assert(active->destroyed == 0);
  Drain();
  assert(active->destroyed == 1 && active->errors.size() == 1);
  assert(active->errors[0] == "Native audio capture failed: capture device stopped");

  // An error queued from capture is retired after a committed UI close.
  auto closed = std::make_shared<Observation>();
  source = new Source(closed);
  source->OnAudioStreamError(nullptr, "stream disconnected during close");
  source->engine_.state.closing = true;
  Drain();
  assert(closed->errors.empty());

  // A beforeunload decision may be canceled: it cannot hide a real failure.
  auto pending = std::make_shared<Observation>();
  source = new Source(pending);
  source->engine_.state.unload = 7;
  source->OnAudioStreamError(nullptr, "active capture failed during confirmation");
  Drain();
  assert(pending->errors.size() == 1);

  // UI-thread creation failures report immediately; completed close is inert.
  auto ui = std::make_shared<Observation>();
  source = new Source(ui);
  on_ui = true;
  source->OnAudioStreamError(nullptr, "creation failed");
  assert(ui->errors.size() == 1 && scheduled.empty());
  source->closing_page_ = true;
  source->OnAudioStreamError(nullptr, "already closed");
  assert(ui->errors.size() == 1);
  on_ui = false;

  // A queued capture callback cannot access a destroyed Engine's state.
  auto retired = std::make_shared<Observation>();
  source = new Source(retired);
  source->OnAudioStreamError(nullptr, "owner retired");
  *source->engine_.alive = false;
  Drain();
  assert(retired->errors.empty());

  // Rejected dispatch is a visible failure, not a lost asynchronous error.
  accept_post = false;
  auto rejected = std::make_shared<Observation>();
  source = new Source(rejected);
  source->OnAudioStreamError(nullptr, "capture failed");
  assert(rejected->errors.size() == 1 && scheduled.empty());
  assert(rejected->errors[0] == "Native audio capture failed: capture failed (UI dispatch rejected)");

  // A stopping source's rejected dispatch is teardown, not a capture failure.
  auto stopping = std::make_shared<Observation>();
  source = new Source(stopping);
  source->stopped_ = true;
  source->OnAudioStreamError(nullptr, "stream ended during shutdown");
  assert(stopping->errors.empty() && !source->audio_valid_ && scheduled.empty());

  // The same teardown error on the UI thread, or dispatched there, is also ignored.
  accept_post = true;
  auto stopping_ui = std::make_shared<Observation>();
  source = new Source(stopping_ui);
  source->stopped_ = true;
  on_ui = true;
  source->OnAudioStreamError(nullptr, "stream ended on UI during shutdown");
  on_ui = false;
  source->OnAudioStreamError(nullptr, "stream ended off UI during shutdown");
  Drain();
  assert(stopping_ui->errors.empty());
}
