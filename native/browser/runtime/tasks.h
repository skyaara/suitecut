// SPDX-License-Identifier: MIT
#pragma once
#include "work/accepted_task_work.h"
#include "work/creation_watchdog.h"
#include "include/cef_task.h"
namespace suitecut::browser {
suitecut::NativeWorkOrigin LocatorHandlerMonitorOrigin() noexcept;
class Task : public CefTask {
 public:
  explicit Task(std::function<void()> callback,
                suitecut::AcceptedTaskWork::Kind kind = suitecut::AcceptedTaskWork::Kind::Work,
                suitecut::NativeWorkOrigin origin = suitecut::CurrentNativeWorkOrigin(),
                std::source_location where = std::source_location::current())
      : native_work_(kind, suitecut::NativeProcessTaskWork(), origin, where),
        callback_(std::move(callback)) {}
  void Execute() override {
    const suitecut::AcceptedTaskFrame native_frame(native_work_);
    callback_();
  }

 private:
  suitecut::AcceptedTaskReceipt native_work_;  // Captures below destroy first.
  std::function<void()> callback_;
  IMPLEMENT_REFCOUNTING(Task);
};

// This dedicated wrapper owns no Source captures. Its UI-owned payload is
// genuinely destroyed on completed creation, while CEF may retain this inert
// delayed object until the unchanged original 30-second due time.
class CreationWatchdogTask : public CefTask {
 public:
  explicit CreationWatchdogTask(std::shared_ptr<suitecut::NativeCreationWatchdog> state)
      : state_(std::move(state)) {}
  void Execute() override {
    if (!CefCurrentlyOn(TID_UI))
      throw std::runtime_error("Native creation watchdog executed outside UI");
    state_->Execute();
  }

 private:
  std::shared_ptr<suitecut::NativeCreationWatchdog> state_;
  IMPLEMENT_REFCOUNTING(CreationWatchdogTask);
};

}  // namespace suitecut::browser
