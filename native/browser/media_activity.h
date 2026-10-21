// SPDX-License-Identifier: MIT
#pragma once

namespace suitecut {
void RegisterMediaMainThread();
// Scoped to actual recording work. No display/sleep assertion and no
// persistent process priority change after the recording has settled.
class MediaActivity {
 public:
  MediaActivity();
  ~MediaActivity();
  MediaActivity(const MediaActivity&) = delete;
  MediaActivity& operator=(const MediaActivity&) = delete;
 private:
  void* token_ = nullptr;
  void* ui_override_ = nullptr;
};
}  // namespace suitecut
