// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace suitecut {
class MacCaptureFrame;

// Capture clocks are milliseconds from the native monotonic Source clock.
// Manual frames use the Source's fixed capture canvas. Automatic viewport
// paints retain actual native pixels and geometry until worker-side fitting.
struct MediaPipelineOptions {
  std::string ffmpeg_path;
  std::string launcher_path;
  std::string output_path;
  int source_width = 0;
  int source_height = 0;
  int output_width = 0;
  int output_height = 0;
  bool i420 = false;
  // Manual BGRA snapshots are fitted/converted by the existing media worker.
  bool worker_canvas_fit = false;
  bool worker_i420 = false;
  bool worker_nv12 = false;
  bool audio = false;
  // macOS MP4 capture requires hardware H.264; never silently fall back to CPU.
  bool hardware_h264 = false;
  double frames_per_second = 60;
  double capture_frames_per_second = 0;
  bool capture_timestamps_exact = false;
  double start_timestamp_ms = 0;
  std::chrono::steady_clock::time_point first_frame_deadline{};
  uint64_t max_file_bytes = 512ULL * 1024 * 1024;
  // Context videos anchor fitted content at the viewport origin. Manual
  // recordings preserve their existing centered letterbox by default.
  bool top_left_padding = false;
  // Automatic recordings receive current, unfitted BGRA paints. Their native
  // worker fits every paint using its own CSS extent, including after resize.
  bool dynamic_viewport = false;
  // Optional fitted-content box, separate from the padded output canvas.
  // Zero preserves the manual recorder's existing scale-to-canvas behavior.
  int max_content_width = 0;
  int max_content_height = 0;
};

struct MediaPipelineResult {
  std::string output_path;
  std::string failure;
  std::string diagnostics;
  std::string encoder;
  std::string encoding_backend;
  std::string container;
  uint64_t preparation_frames = 0;
  uint64_t preparation_packets = 0;
  double preparation_ms = 0;
  double first_timestamp_ms = -1;
  double duration_ms = 0;
  double encoded_duration_ms = 0;
  double frames_per_second = 0;
  uint64_t input_frames = 0;
  uint64_t gpu_input_frames = 0;
  uint64_t gpu_nv12_frames = 0;
  uint64_t output_frames = 0;
  uint64_t held_frames = 0;
  uint64_t coalesced_frames = 0;
  bool capture_timestamps_exact = false;
  double capture_lookahead_ms = 0;
  double max_timestamp_correction_ms = 0;
  uint64_t superseded_frames = 0;
  uint64_t audio_input_frames = 0;
  uint64_t audio_output_frames = 0;
  uint64_t audio_overlap_frames = 0;
  uint64_t audio_gap_frames = 0;
  uint64_t output_bytes = 0;
  // Live physical PID for diagnostics; terminal results report zero after waitpid.
  int physical_encoder_pid = 0;
};

// Owns subprocesses and private files until waitpid and file cleanup settle.
// All APIs except construction/destruction are thread-safe; Stop/Cancel are
// nonblocking. A completed callback runs on the worker; embedders must marshal
// it to their owning thread. At most sixteen pipelines exist engine-wide.
class MediaPipeline {
 public:
  using Completion = std::function<void(const MediaPipelineResult&)>;
  using Started = std::function<void(double first_timestamp_ms)>;
  explicit MediaPipeline(MediaPipelineOptions options, Completion completion = {}, Started started = {}, Completion progress = {});
  ~MediaPipeline();
  MediaPipeline(const MediaPipeline&) = delete;
  MediaPipeline& operator=(const MediaPipeline&) = delete;

  void Frame(std::vector<uint8_t> data, double timestamp_ms);
  bool UsesCanvasFrames() const;
  bool WantsFrames() const;
  bool NativeCanvasFrame(std::shared_ptr<MacCaptureFrame> surface, double timestamp_ms,
                         std::function<void(std::vector<uint8_t>)> painted);
  void CanvasFrame(const std::vector<uint8_t>& bgra, int width, int height,
                   double timestamp_ms, uint64_t capture_sequence);
  void ViewportFrame(const std::vector<uint8_t>& bgra, int width, int height,
                     double css_width, double css_height, double timestamp_ms);
  void Audio(std::vector<uint8_t> stereo_s16, double timestamp_ms);
  void Pause(double timestamp_ms);
  void Resume(double timestamp_ms);
  void Stop(double timestamp_ms, std::chrono::steady_clock::time_point deadline);
  // Tighten physical lifetime without ending capture before a genuine frame.
  void LimitDeadline(std::chrono::steady_clock::time_point deadline);
  void Cancel(const std::string& reason = "Native recording canceled");
  bool Settled() const;
  MediaPipelineResult Snapshot() const;
  void Join();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// The number of constant-rate slots in [0,duration). Decimal FPS is supported;
// floating error at an exact boundary never creates an extra output frame.
uint64_t MediaSlotCount(double duration_ms, double fps);
void ValidateMediaPipelineOptions(const MediaPipelineOptions& options);
std::string ResolveMediaExecutable(const std::string& executable);

// Call before any CEF initialization in the native executable entry point.
// Returns -1 for normal startup; a helper invocation execs FFmpeg or returns127.
int MediaEncoderHelperMain(int argc, char** argv);

}  // namespace suitecut
