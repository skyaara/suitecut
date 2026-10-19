// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace suitecut {
// A continuous native clock feeds an actual FFmpeg network publisher. Codec
// pairs are explicit: h264/aac in FLV, or vp9/opus in WebM. These settings never
// imply that output frames contain distinct captured browser images.
struct StreamReconnectOptions {
  unsigned max_attempts = 3;  // Replacement attempts per actual outage.
  std::chrono::milliseconds initial_delay{250}, max_delay{5000}, timeout{30000};
};
struct StreamAttemptResult {
  uint64_t number = 0;
  int encoder_pid = 0;
  double first_timestamp_ms = -1, first_captured_timestamp_ms = -1, finished_timestamp_ms = -1;
  bool child_reaped = false, child_exit_observed = false;
  uint64_t submitted_frames = 0, encoded_frames = 0, audio_submitted_frames = 0, muxed_bytes = 0;
  std::string failure, diagnostics;
  bool diagnostic_truncated = false;
};
struct StreamPipelineOptions {
  std::string launcher_path;
  std::string ffmpeg_path;
  std::string destination;
  std::string video_codec = "h264";
  std::string audio_codec = "aac";
  std::string container = "flv";
  int source_width = 0, source_height = 0;
  int output_width = 0, output_height = 0;
  bool i420 = false, audio = false;
  double frames_per_second = 60;
  double start_timestamp_ms = 0;
  int bitrate_kbps = 4500;
  std::chrono::milliseconds capture_latency{120};
  // Explicit TCP transport buffer for diagnostic/control destinations; zero
  // preserves the operating system default, whose bytes are not acknowledged
  // as remote delivery. RTMP transport tuning remains a separate capability.
  int tcp_send_buffer_bytes = 0;
  // Bounded publisher lag is a failure policy, rather than silent PCM loss.
  std::chrono::milliseconds max_write_lag{2000};
  std::chrono::steady_clock::time_point first_frame_deadline{};
  std::optional<StreamReconnectOptions> reconnect;
};
struct StreamPipelineResult {
  // Existing output counters and media origin describe the current attempt.
  // Cumulative output and bounded real attempt records are explicit.
  uint64_t attempt_number = 0, reconnect_attempts = 0;
  bool reconnecting = false;
  uint64_t cumulative_submitted_frames = 0, cumulative_encoded_frames = 0;
  uint64_t cumulative_audio_submitted_frames = 0, cumulative_muxed_bytes = 0;
  uint64_t disconnected_video_frames = 0, disconnected_audio_frames = 0;
  std::string last_attempt_failure;
  std::vector<StreamAttemptResult> attempts;
  std::string failure, diagnostics;
  // The output clock origin can follow a retained image during reconnect.
  // first_captured_timestamp_ms remains that image's genuine capture time.
  double first_timestamp_ms = -1, first_captured_timestamp_ms = -1;
  double duration_ms = 0;
  uint64_t input_frames = 0, submitted_frames = 0, held_frames = 0;
  uint64_t superseded_frames = 0, encoded_frames = 0;
  uint64_t audio_input_frames = 0, audio_submitted_frames = 0;
  uint64_t audio_gap_frames = 0, audio_overlap_frames = 0, paused_audio_frames = 0;
  // FFmpeg progress reports actual muxed bytes, not remote receipt or decoding.
  uint64_t muxed_bytes = 0;
  bool encoded_progress_observed = false, muxed_progress_observed = false;
  uint64_t paused_input_audio_frames = 0, switched_audio_frames = 0;
  uint64_t stopped_audio_frames = 0, stopped_video_frames = 0, paused_input_video_frames = 0;
  uint64_t captured_frame_bytes = 0, peak_captured_frame_bytes = 0;
  double capture_latency_ms = 0, video_interval_closure_ms = 0, max_capture_to_pipe_ms = 0;
  int physical_encoder_pid = 0;
};
class StreamPipeline {
 public:
  using Completion = std::function<void(const StreamPipelineResult&)>;
  explicit StreamPipeline(StreamPipelineOptions options, Completion completed = {}, Completion ready = {});
  ~StreamPipeline();
  StreamPipeline(const StreamPipeline&) = delete;
  StreamPipeline& operator=(const StreamPipeline&) = delete;
  // Input ownership is taken only after the Engine selects this actual page.
  void Frame(const std::vector<uint8_t>& data, double native_timestamp_ms);
  void Audio(std::vector<uint8_t> stereo_s16, double native_timestamp_ms);
  // Live pause holds the last image and outputs explicit silence on the
  // continuing publisher clock. It does not compress a broadcast's timeline.
  void Pause(bool paused);
  // Clearing queued input at a genuine page switch avoids playing old PCM.
  // The owner must deliver the actual new page capture; no DOM/event simulation.
  void SwitchSource();
  void Stop(std::chrono::steady_clock::time_point original_deadline);
  void Cancel(const std::string& reason = "Native stream canceled");
  bool Settled() const;
  StreamPipelineResult Snapshot() const;
  void Join();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
void ValidateStreamPipelineOptions(const StreamPipelineOptions& options);
}  // namespace suitecut
