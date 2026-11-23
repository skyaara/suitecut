// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace suitecut {
// Plays 48 kHz stereo signed 16-bit PCM to the default output. CEF's audio
// capture mutes the page it captures, so a recording that takes a page's sound
// plays it back through this to keep it audible. Play is thread-safe; at most
// about 200 ms is buffered, and older sound is dropped rather than delayed.
class AudioMonitor {
 public:
  AudioMonitor();
  ~AudioMonitor();
  AudioMonitor(const AudioMonitor&) = delete;
  AudioMonitor& operator=(const AudioMonitor&) = delete;
  void Play(const std::vector<uint8_t>& pcm);
  bool Ok() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace suitecut
