// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace suitecut {
bool EncoderAvailable(const std::string& name);
struct EncoderInfo {
  std::string name, kind, codec;
  bool linked = false, hardware = false, lossless = false;
  std::vector<std::string> containers;
};
std::vector<EncoderInfo> ListMediaEncoders();
using Pixels = std::shared_ptr<const std::vector<uint8_t>>;
using Samples = std::shared_ptr<const std::vector<int16_t>>;
struct MediaOptions {
  std::string destination;
  std::string video_codec = "libx264";
  std::string audio_codec = "aac";
  std::string container;  // Empty infers MP4/WebM/Matroska/MOV from the filename.
  int width = 1920, height = 1080, fps = 30, bitrate_kbps = 4500, audio_bitrate_kbps = 160;
  double crf = -1;  // Encoder-specific default; FFV1 and hardware do not use CRF.
  int audio_startup_timeout_ms = 5000;
  bool streaming = false, audio = true, offline = false;
};
struct MediaStatus {
  bool ready = false, stopped = false, paused = false;
  uint64_t video_frames = 0, audio_frames = 0, repeated_frames = 0, audio_dropped = 0;
  // Frames per channel: input coverage missing at encode time; input arriving
  // after its timeline was encoded. Source-provided silent PCM is covered.
  uint64_t audio_missing = 0, audio_late = 0;
  size_t queued_video_bytes = 0, queued_audio_bytes = 0;
  double started_ms = 0, duration_ms = 0, maximum_encode_ms = 0, buffered_delay_ms = 150;
  std::string error;
};
// Independent bounded audio/video workers per output, with serialized muxer I/O.
// CEF callbacks never perform encoder I/O.
// Media does not travel through the automation/control transport.
class MediaOutput {
 public:
  explicit MediaOutput(MediaOptions options, std::function<double()> clock);
  ~MediaOutput();
  MediaOutput(const MediaOutput&) = delete;
  MediaOutput& operator=(const MediaOutput&) = delete;
  void Video(Pixels pixels, int width, int height, double at_ms);
  void Audio(Samples samples, double at_ms);
  void AudioState(bool active);
  void Pause(bool paused);
  void Fail(const std::string& error);
  MediaStatus Status() const;
  MediaStatus Stop();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
