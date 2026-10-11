// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <mutex>

// Opt-in, buffered local diagnostics. No PCM crosses the SDK transport.
class SuiteCutAudioTrace {
 public:
  SuiteCutAudioTrace() = default;
  SuiteCutAudioTrace(const SuiteCutAudioTrace&) = delete;
  SuiteCutAudioTrace& operator=(const SuiteCutAudioTrace&) = delete;
  bool Enabled() const { std::lock_guard lock(mutex_); return csv_ || pcm_; }
  void Open(const std::string& name, const char* columns, bool pcm = false) {
    std::lock_guard lock(mutex_);
    const char* directory = std::getenv("SUITECUT_AUDIO_TRACE_DIRECTORY");
    if (!directory || opened_) return;
    opened_ = true;
    const auto path = std::filesystem::path(directory) / name;
    csv_ = std::fopen((path.string()+".csv").c_str(), "w");
    if (csv_) std::fprintf(csv_, "%s\n", columns);
    if (pcm) pcm_ = std::fopen((path.string()+".s16").c_str(), "wb");
  }
  template<class... Args> void Row(const char* format, Args... args) {
    std::lock_guard lock(mutex_);
    if (csv_) std::fprintf(csv_, format, args...);
  }
  void PCM(const void* data, size_t size) { std::lock_guard lock(mutex_); if (pcm_) std::fwrite(data, 1, size, pcm_); }
  ~SuiteCutAudioTrace() { if(csv_) std::fclose(csv_); if(pcm_) std::fclose(pcm_); }
 private:
  mutable std::mutex mutex_;
  bool opened_ = false;
  FILE *csv_ = nullptr, *pcm_ = nullptr;
};
