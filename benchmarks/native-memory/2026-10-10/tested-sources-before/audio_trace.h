// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

// Opt-in, buffered local diagnostics. No PCM crosses the SDK transport.
class SuiteCutAudioTrace {
 public:
  bool Enabled() const { return csv_ || pcm_; }
  void Open(const std::string& name, const char* columns, bool pcm = false) {
    const char* directory = std::getenv("SUITECUT_AUDIO_TRACE_DIRECTORY");
    if (!directory || csv_) return;
    const auto path = std::filesystem::path(directory) / name;
    csv_ = std::fopen((path.string()+".csv").c_str(), "w");
    if (csv_) std::fprintf(csv_, "%s\n", columns);
    if (pcm) pcm_ = std::fopen((path.string()+".s16").c_str(), "wb");
  }
  template<class... Args> void Row(const char* format, Args... args) {
    if (csv_) std::fprintf(csv_, format, args...);
  }
  void PCM(const void* data, size_t size) { if (pcm_) std::fwrite(data, 1, size, pcm_); }
  ~SuiteCutAudioTrace() { if(csv_) std::fclose(csv_); if(pcm_) std::fclose(pcm_); }
 private:
  FILE *csv_ = nullptr, *pcm_ = nullptr;
};
