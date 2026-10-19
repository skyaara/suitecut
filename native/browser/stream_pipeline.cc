// SPDX-License-Identifier: MIT
#include "stream_pipeline.h"
#include "media_pipeline.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iomanip>
#include <mutex>
#include <optional>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace suitecut {
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t kAudioBytes = 48000 * 4 * 30;
std::atomic<unsigned> stream_count{0};
std::atomic<size_t> stream_frame_bytes{0};
constexpr size_t kPublisherFrameBytes = 256u * 1024 * 1024;
constexpr size_t kEngineFrameBytes = 1024u * 1024 * 1024;
struct StreamPicture {
  std::vector<uint8_t> data;
  std::shared_ptr<std::atomic<size_t>> owner;
  size_t reserved = 0;
  std::atomic<bool> submitted{false};
  ~StreamPicture() {
    // Release the actual allocation before making its byte reservation reusable.
    std::vector<uint8_t>().swap(data);
    if (reserved) { owner->fetch_sub(reserved); stream_frame_bytes.fetch_sub(reserved); }
  }
  static std::shared_ptr<StreamPicture> Capture(const std::vector<uint8_t>& data, const std::shared_ptr<std::atomic<size_t>>& owner) {
    auto picture = std::make_shared<StreamPicture>();
    size_t bytes = owner->load();
    do { if (data.size() > kPublisherFrameBytes - bytes) throw std::runtime_error("Native publisher captured-image quota exceeded"); }
    while (!owner->compare_exchange_weak(bytes, bytes + data.size()));
    bytes = stream_frame_bytes.load();
    do { if (data.size() > kEngineFrameBytes - bytes) { owner->fetch_sub(data.size()); throw std::runtime_error("Native Engine captured-image quota exceeded"); } }
    while (!stream_frame_bytes.compare_exchange_weak(bytes, bytes + data.size()));
    picture->owner = owner; picture->reserved = data.size(); picture->data = data;
    return picture;
  }
};
class StreamFD {
 public:
  explicit StreamFD(int fd = -1) : fd_(fd) {}
  ~StreamFD() { Reset(); }
  StreamFD(const StreamFD&) = delete;
  StreamFD& operator=(const StreamFD&) = delete;
  StreamFD(StreamFD&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  StreamFD& operator=(StreamFD&& other) noexcept { if (this != &other) { Reset(other.fd_); other.fd_ = -1; } return *this; }
  int Get() const { return fd_; }
  void Reset(int fd = -1) { if (fd_ >= 0) close(fd_); fd_ = fd; }
 private:
  int fd_;
};
std::array<StreamFD, 2> StreamPipe() {
  int raw[2];
  if (pipe2(raw, O_CLOEXEC) != 0) throw std::runtime_error("Native publisher pipe creation failed");
  return {StreamFD(raw[0]), StreamFD(raw[1])};
}
void StreamNonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) throw std::runtime_error("Native publisher pipe configuration failed");
}
std::string StreamDecimal(double value) {
  std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(15) << value; return out.str();
}
uint64_t AudioBoundary(uint64_t frame, double fps) {
  return static_cast<uint64_t>(std::llround(static_cast<long double>(frame) * 48000 / fps));
}
std::string ChildStatus(int status) {
  if (WIFEXITED(status)) return "exit code " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
  return "unavailable exit status";
}
struct StreamRetryableError : std::runtime_error { using std::runtime_error::runtime_error; };
bool StreamNetworkFailure(const std::string& message) {
  for (const char* text : {"Connection refused", "Connection reset", "Broken pipe", "Connection timed out", "Network is unreachable", "No route to host", "Input/output error"})
    if (message.find(text) != std::string::npos) return true;
  return false;
}
struct StreamChild {
  pid_t pid = -1;
  int status = -1;
  bool reaped = false, exit_observed = false;
  std::string wait_failure;
  StreamFD video, audio, diagnostics, progress, identity;
  std::string text, progress_buffer;
  ~StreamChild() { FinishPhysical(); }
  void FinishPhysical() {
    video.Reset(); audio.Reset();
    if (pid > 0 && !reaped) {
      if(identity.Get()>=0){
        const auto signaled=syscall(SYS_pidfd_send_signal,identity.Get(),SIGKILL,nullptr,0);
        if(signaled<0&&errno!=ESRCH)std::terminate();
      }else kill(-pid,SIGKILL);
      pid_t ended; do { ended = waitpid(pid, &status, 0); } while (ended < 0 && errno == EINTR);
      if (ended == pid) { reaped = true; exit_observed = true; }
      else {
        wait_failure = "Native publisher lost child waitpid ownership: " + std::string(strerror(errno));
        // An actual pidfd cannot refer to a recycled PID. Retain this physical
        // owner until that original process has genuinely exited, even if an
        // external process reaper took its wait status.
        if (identity.Get() >= 0) {
          const auto signaled = syscall(SYS_pidfd_send_signal, identity.Get(), SIGKILL, nullptr, 0); (void)signaled;
          pollfd ended{identity.Get(), POLLIN, 0};
          for (;;) {
            const auto observed = poll(&ended, 1, -1);
            if (observed > 0 && (ended.revents & POLLIN)) { exit_observed = true; break; }
            if (observed < 0 && errno == EINTR) continue;
            std::terminate();  // Never publish a false physical settlement.
          }
        } else std::terminate();
      }
    }
  }
  void Spawn(const StreamPipelineOptions& options) {
    auto v = StreamPipe(), a = StreamPipe(), d = StreamPipe(), p = StreamPipe();
    StreamFD null(open("/dev/null", O_RDWR | O_CLOEXEC));
    if (null.Get() < 0) throw std::runtime_error("Native publisher null stream unavailable");
    // Move source handles above the explicit child destinations before dup2.
    std::array<StreamFD, 5> inputs;
    const std::array<int, 5> originals{v[0].Get(), null.Get(), d[1].Get(), options.audio ? a[0].Get() : null.Get(), p[1].Get()};
    for (size_t index = 0; index < inputs.size(); ++index) {
      inputs[index].Reset(fcntl(originals[index], F_DUPFD_CLOEXEC, 10));
      if (inputs[index].Get() < 0) throw std::runtime_error("Native publisher descriptor admission failed");
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions)) throw std::runtime_error("Native publisher spawn actions failed");
    if (posix_spawnattr_init(&attributes)) { posix_spawn_file_actions_destroy(&actions); throw std::runtime_error("Native publisher spawn attributes failed"); }
    int error = 0;
    for (size_t index = 0; index < inputs.size(); ++index)
      error |= posix_spawn_file_actions_adddup2(&actions, inputs[index].Get(), static_cast<int>(index));
#if defined(__GLIBC__) && defined(__linux__)
    error |= posix_spawn_file_actions_addclosefrom_np(&actions, 5);
#else
#error Native stream subprocess ownership currently requires Linux glibc
#endif
    error |= posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    error |= posix_spawnattr_setpgroup(&attributes, 0);
    const std::string filter = "scale=" + std::to_string(options.output_width) + ":" + std::to_string(options.output_height) +
      ":force_original_aspect_ratio=decrease,pad=" + std::to_string(options.output_width) + ":" + std::to_string(options.output_height) + ":(ow-iw)/2:(oh-ih)/2:black,setsar=1";
    std::vector<std::string> args{options.launcher_path, "--suitecut-media-encoder", std::to_string(getpid()), "1073741824", options.ffmpeg_path,
      "-hide_banner", "-loglevel", "error", "-xerror", "-nostdin", "-stats_period", "0.1", "-progress", "pipe:4",
      "-thread_queue_size", "2", "-f", "rawvideo", "-pixel_format", options.i420 ? "yuv420p" : "bgra", "-video_size",
      std::to_string(options.source_width) + "x" + std::to_string(options.source_height), "-framerate", StreamDecimal(options.frames_per_second),
      "-probesize", "32", "-analyzeduration", "0", "-i", "pipe:0"};
    if (options.audio) args.insert(args.end(), {"-thread_queue_size", "2", "-f", "s16le", "-ar", "48000", "-ac", "2", "-probesize", "32", "-analyzeduration", "0", "-blocksize", "4096", "-i", "pipe:3"});
    args.insert(args.end(), {"-map", "0:v:0"});
    if (options.audio) args.insert(args.end(), {"-map", "1:a:0", "-c:a", options.audio_codec == "aac" ? "aac" : "libopus", "-b:a", "160k"});
    else args.push_back("-an");
    args.insert(args.end(), {"-vf", filter, "-c:v", options.video_codec == "h264" ? "libx264" : "libvpx-vp9", "-pix_fmt", "yuv420p", "-threads", "4", "-b:v", std::to_string(options.bitrate_kbps) + "k"});
    if (options.i420) {
      // Preserve the actual Source BT.709 limited-range I420 interpretation at
      // raw decoder admission as well as tagging the encoded stream.
      const std::vector<std::string> color{"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-color_range", "tv"};
      args.insert(std::find(args.begin(), args.end(), "-i"), color.begin(), color.end());
      args.insert(args.end(), color.begin(), color.end());
    }
    if (options.video_codec == "h264") args.insert(args.end(), {"-preset", "veryfast", "-tune", "zerolatency", "-g", std::to_string(static_cast<int>(std::ceil(options.frames_per_second * 2))), "-sc_threshold", "0"});
    else args.insert(args.end(), {"-deadline", "realtime", "-cpu-used", "5", "-row-mt", "1", "-lag-in-frames", "0"});
    if (options.container == "webm") args.insert(args.end(), {"-live", "1", "-cluster_time_limit", "100", "-cluster_size_limit", "65536"});
    if (options.tcp_send_buffer_bytes) args.insert(args.end(), {"-send_buffer_size", std::to_string(options.tcp_send_buffer_bytes)});
    args.insert(args.end(), {"-f", options.container, options.destination});
    std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    if (!error) error = posix_spawn(&pid, options.launcher_path.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
    if (error) { pid = -1; throw std::runtime_error("Native publisher spawn failed: " + std::string(strerror(error))); }
    identity.Reset(static_cast<int>(syscall(SYS_pidfd_open, pid, 0)));
    if (identity.Get() < 0) throw std::runtime_error("Native publisher process identity descriptor unavailable");
    video = std::move(v[1]); audio = std::move(a[1]); diagnostics = std::move(d[0]); progress = std::move(p[0]);
    if (!options.audio) audio.Reset();
    StreamNonblocking(video.Get()); if (options.audio) StreamNonblocking(audio.Get()); StreamNonblocking(diagnostics.Get()); StreamNonblocking(progress.Get());
  }
  bool Reap() {
    if (reaped) return true;
    const auto result = waitpid(pid, &status, WNOHANG);
    if (result == pid) { reaped = true; exit_observed = true; return true; }
    if (result < 0 && errno != EINTR) throw std::runtime_error("Native publisher waitpid failed");
    return false;
  }
  void Read(StreamPipelineResult& result) {
    char data[4096]; ssize_t bytes;
    while ((bytes = read(diagnostics.Get(), data, sizeof(data))) > 0) { text.append(data, static_cast<size_t>(bytes)); if (text.size() > 8192) text.erase(0, text.size() - 8192); }
    while ((bytes = read(progress.Get(), data, sizeof(data))) > 0) {
      progress_buffer.append(data, static_cast<size_t>(bytes));
      if (progress_buffer.size() > 16384) throw std::runtime_error("Native publisher progress exceeded its bound");
      for (;;) {
        const auto newline = progress_buffer.find('\n'); if (newline == std::string::npos) break;
        const auto line = progress_buffer.substr(0, newline); progress_buffer.erase(0, newline + 1);
        const auto equals = line.find('='); if (equals == std::string::npos) continue;
        uint64_t value = 0; const auto parsed = std::from_chars(line.data() + equals + 1, line.data() + line.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size()) continue;
        if (line.substr(0, equals) == "frame") { result.encoded_frames = std::max(result.encoded_frames, value); result.encoded_progress_observed = true; }
        if (line.substr(0, equals) == "total_size") { result.muxed_bytes = std::max(result.muxed_bytes, value); result.muxed_progress_observed = true; }
      }
    }
    result.diagnostics = text;
  }
};
}
void ValidateStreamPipelineOptions(const StreamPipelineOptions& o) {
  if (o.launcher_path.empty() || o.launcher_path.front() != '/' || o.launcher_path.size() > 4096 || o.launcher_path.find('\0') != std::string::npos ||
      o.ffmpeg_path.empty() || o.ffmpeg_path.front() != '/' || o.ffmpeg_path.size() > 4096 || o.ffmpeg_path.find('\0') != std::string::npos)
    throw std::invalid_argument("Native publisher requires absolute executable paths");
  if (o.source_width <= 0 || o.source_height <= 0 || o.output_width <= 0 || o.output_height <= 0 ||
      o.source_width % 2 || o.source_height % 2 || o.output_width % 2 || o.output_height % 2 ||
      static_cast<uint64_t>(o.source_width) * o.source_height > 16777216 || static_cast<uint64_t>(o.output_width) * o.output_height > 16777216)
    throw std::invalid_argument("Native publisher dimensions are invalid or exceed the pixel bound");
  if (!std::isfinite(o.frames_per_second) || o.frames_per_second <= 0 || o.frames_per_second > 120 ||
      !std::isfinite(o.start_timestamp_ms) || o.start_timestamp_ms < 0 || o.bitrate_kbps < 100 || o.bitrate_kbps > 50000 ||
      o.capture_latency.count() < 20 || o.capture_latency.count() > 1000 || o.max_write_lag.count() < 1 || o.max_write_lag.count() > 30000 || o.tcp_send_buffer_bytes < 0 || o.tcp_send_buffer_bytes > 4194304)
    throw std::invalid_argument("Native publisher rate, clock, bitrate or lag bound is invalid");
  if (o.reconnect && (o.reconnect->max_attempts < 1 || o.reconnect->max_attempts > 8 ||
      o.reconnect->initial_delay.count() < 0 || o.reconnect->initial_delay.count() > 5000 ||
      o.reconnect->max_delay < o.reconnect->initial_delay || o.reconnect->max_delay.count() > 30000 ||
      o.reconnect->timeout.count() < 1 || o.reconnect->timeout.count() > 120000))
    throw std::invalid_argument("Native publisher reconnect policy exceeds its bounded scope");
  const bool flv = o.video_codec == "h264" && o.container == "flv" && (!o.audio || o.audio_codec == "aac");
  const bool webm = o.video_codec == "vp9" && o.container == "webm" && (!o.audio || o.audio_codec == "opus");
  if (!flv && !webm) throw std::invalid_argument("Native publisher codec/container pair is unsupported");
  const auto scheme = o.destination.find("://");
  if (scheme == std::string::npos || o.destination.size() > 4096 || o.destination.find_first_of("\r\n\t ") != std::string::npos ||
      o.destination.find('\0') != std::string::npos)
    throw std::invalid_argument("Native publisher destination is malformed");
  const auto protocol = o.destination.substr(0, scheme);
  if (protocol != "rtmp" && protocol != "rtmps" && protocol != "tcp") throw std::invalid_argument("Native publisher protocol is unsupported");
  if (o.tcp_send_buffer_bytes && protocol != "tcp") throw std::invalid_argument("Native publisher explicit socket buffer requires TCP transport");
  if ((protocol == "rtmp" || protocol == "rtmps") && !flv) throw std::invalid_argument("RTMP publisher requires h264/aac FLV");
  if (o.destination.size() <= scheme + 3) throw std::invalid_argument("Native publisher destination has no host");
}
struct StreamPipeline::Impl {
  struct Sample { std::vector<uint8_t> data; double timestamp = 0; size_t offset = 0; uint64_t generation = 0; };
  struct FrameSample { std::shared_ptr<StreamPicture> picture; double timestamp = 0; uint64_t generation = 0, identity = 0; };
  struct Control { double timestamp; bool paused; uint64_t generation; };
  Clock::time_point created = Clock::now();
  std::shared_ptr<std::atomic<size_t>> frame_bytes = std::make_shared<std::atomic<size_t>>(0);
  uint64_t generation = 0, frame_identity = 0;
  double last_frame_timestamp = -1, stop_at_ms = -1;
  std::deque<Control> controls;
  std::deque<FrameSample> frames;
  StreamPipelineOptions options;
  Completion completed, ready;
  mutable std::mutex mutex;
  std::mutex join_mutex;
  std::array<StreamFD, 2> wake = StreamPipe();
  std::deque<Sample> packets;
  size_t packet_bytes = 0;
  bool paused = false, stopping = false, encoder_acknowledged = false;
  Clock::time_point stop_deadline{};
  std::atomic<bool> canceled{false}, settled{false};
  std::string cancel_reason;
  StreamPipelineResult result;
  std::optional<FrameSample> latest_frame;
  bool offline = false;
  Clock::time_point outage_deadline{}, attempt_failure_at{};
  uint64_t attempt_number = 0, reconnect_attempts = 0;
  StreamPipelineResult completed_output;
  std::vector<StreamAttemptResult> attempts;
  std::string last_attempt_failure;
  double attempt_not_before_ms = -1;
  std::thread worker;
  bool lease = false;
  Impl(StreamPipelineOptions value, Completion done, Completion started) : options(std::move(value)), completed(std::move(done)), ready(std::move(started)) {
    if (!options.ffmpeg_path.empty() && options.ffmpeg_path.front() != '/') options.ffmpeg_path = ResolveMediaExecutable(options.ffmpeg_path);
    ValidateStreamPipelineOptions(options); StreamNonblocking(wake[0].Get()); StreamNonblocking(wake[1].Get());
    unsigned count = stream_count.load();
    do { if (count >= 4) throw std::runtime_error("Native publisher capacity exceeded"); } while (!stream_count.compare_exchange_weak(count, count + 1));
    lease = true; controls.push_back({options.start_timestamp_ms, false, 0});
    try { worker = std::thread([this] { Run(); }); }
    catch (...) { stream_count.fetch_sub(1); lease = false; throw; }
  }
  ~Impl() { Cancel("Native publisher owner closed"); if (worker.joinable()) worker.join(); if (lease) stream_count.fetch_sub(1); }
  void Wake() { const uint8_t byte = 1; const auto wrote = write(wake[1].Get(), &byte, 1); (void)wrote; }
  void DrainWake() { uint8_t bytes[256]; while (read(wake[0].Get(), bytes, sizeof(bytes)) > 0) {} }
  void Cancel(const std::string& reason) { { std::lock_guard lock(mutex); if (settled || canceled) return; cancel_reason = reason.substr(0, 8192); canceled = true; } Wake(); }
  void Check(Clock::time_point deadline) {
    std::lock_guard lock(mutex);
    if (canceled) throw std::runtime_error(cancel_reason);
    if (!encoder_acknowledged && options.first_frame_deadline != Clock::time_point{} && Clock::now() >= options.first_frame_deadline)
      throw std::runtime_error("Timeout: native publisher initial encoded readiness deadline expired");
    if (outage_deadline != Clock::time_point{} && Clock::now() >= outage_deadline)
      throw std::runtime_error("Timeout: native publisher reconnect outage deadline expired");
    if (deadline != Clock::time_point{} && Clock::now() >= deadline) throw std::runtime_error("Timeout: native publisher exceeded its original deadline or bounded pipe lag");
    if (stop_deadline != Clock::time_point{} && Clock::now() >= stop_deadline) throw std::runtime_error("Timeout: native publisher stop deadline expired");
  }
  void Publish(StreamPipelineResult& state) {
    std::lock_guard lock(mutex);
    state.paused_input_video_frames = result.paused_input_video_frames; state.input_frames = result.input_frames; state.superseded_frames = result.superseded_frames; state.audio_input_frames = result.audio_input_frames; state.paused_input_audio_frames = result.paused_input_audio_frames; state.switched_audio_frames = result.switched_audio_frames;
    state.captured_frame_bytes = frame_bytes->load(); state.peak_captured_frame_bytes = std::max(state.peak_captured_frame_bytes, result.peak_captured_frame_bytes);
    state.attempt_number = attempt_number; state.reconnect_attempts = reconnect_attempts;
    state.disconnected_video_frames = result.disconnected_video_frames; state.disconnected_audio_frames = result.disconnected_audio_frames;
    state.cumulative_submitted_frames = completed_output.submitted_frames + state.submitted_frames;
    state.cumulative_encoded_frames = completed_output.encoded_frames + state.encoded_frames;
    state.cumulative_audio_submitted_frames = completed_output.audio_submitted_frames + state.audio_submitted_frames;
    state.cumulative_muxed_bytes = completed_output.muxed_bytes + state.muxed_bytes;
    state.last_attempt_failure = last_attempt_failure; state.attempts = attempts;
    result = state;
  }
  int Timeout(Clock::time_point deadline) {
    // A readiness/pipe wait cannot extend its original command owner deadline.
    std::lock_guard lock(mutex);
    const auto limit = [&deadline](Clock::time_point value) {
      if (value != Clock::time_point{} && (deadline == Clock::time_point{} || value < deadline)) deadline = value;
    };
    if (!encoder_acknowledged) limit(options.first_frame_deadline);
    limit(stop_deadline); limit(outage_deadline);
    if (deadline == Clock::time_point{}) return 50;
    const auto remaining = std::chrono::duration<double, std::milli>(deadline - Clock::now()).count();
    return static_cast<int>(std::clamp(std::ceil(remaining), 0.0, 50.0));
  }
  double NativeNow() const { return options.start_timestamp_ms + std::chrono::duration<double, std::milli>(Clock::now() - created).count(); }
  Control At(double timestamp) const {
    auto found = std::upper_bound(controls.begin(), controls.end(), timestamp, [](double time, const Control& c) { return time < c.timestamp; });
    return found == controls.begin() ? controls.front() : *std::prev(found);
  }
  std::vector<uint8_t> Audio(uint64_t begin, uint64_t end, double origin, StreamPipelineResult& state) {
    const size_t count = static_cast<size_t>(end - begin);
    std::vector<uint8_t> data(count * 4), covered(count), paused_samples(count);
    std::vector<uint64_t> generations(count);
    std::lock_guard lock(mutex);
    for (size_t i = 0; i < count; ++i) {
      const auto control = At(origin + (begin + i) * 1000.0 / 48000);
      paused_samples[i] = control.paused; generations[i] = control.generation;
      if (control.paused) ++state.paused_audio_frames;
    }
    while (!packets.empty()) {
      auto& packet = packets.front();
      const int64_t start = static_cast<int64_t>(std::llround((packet.timestamp - origin) * 48));
      int64_t at = start + static_cast<int64_t>(packet.offset / 4);
      if (at >= static_cast<int64_t>(end)) break;
      const size_t available = (packet.data.size() - packet.offset) / 4;
      const size_t trim = at < static_cast<int64_t>(begin) ? std::min<size_t>(available, static_cast<size_t>(static_cast<int64_t>(begin) - at)) : 0;
      packet.offset += trim * 4; at += static_cast<int64_t>(trim); state.audio_overlap_frames += trim;
      const size_t take = std::min<size_t>((packet.data.size() - packet.offset) / 4, static_cast<size_t>(static_cast<int64_t>(end) - at));
      for (size_t i = 0; i < take; ++i) {
        const size_t target = static_cast<size_t>(at - static_cast<int64_t>(begin)) + i;
        if (packet.generation != generations[target]) { ++result.switched_audio_frames; continue; }
        if (paused_samples[target]) { ++result.paused_input_audio_frames; continue; }
        if (covered[target]) { ++state.audio_overlap_frames; continue; }
        std::memcpy(data.data() + target * 4, packet.data.data() + packet.offset + i * 4, 4); covered[target] = 1;
      }
      packet.offset += take * 4;
      if (packet.offset == packet.data.size()) { packet_bytes -= packet.data.size(); packets.pop_front(); }
      else break;
    }
    for (size_t i = 0; i < count; ++i) if (!covered[i] && !paused_samples[i]) ++state.audio_gap_frames;
    return data;
  }
  StreamPipelineResult RunAttempt(bool& retryable) {
    attempt_failure_at = Clock::time_point{};
    StreamPipelineResult state;
    state.reconnecting = reconnect_attempts > 0;
    StreamChild child;
    int attempt_pid = 0;
    try {
      struct VideoWrite { std::shared_ptr<StreamPicture> picture; size_t offset = 0; Clock::time_point due; };
      std::deque<VideoWrite> video;
      std::shared_ptr<StreamPicture> held;
      uint64_t held_identity = 0, prior_identity = 0, queued_frames = 0, audio_cursor = 0, audio_written_bytes = 0;
      std::vector<uint8_t> pcm; size_t pcm_offset = 0;
      Clock::time_point pcm_due{};
      bool started = false, spawned = false, inputs_closed = false;
      Clock::time_point origin;
      const auto latency = options.audio ? options.capture_latency : std::chrono::milliseconds(0);
      state.capture_latency_ms = latency.count(); state.video_interval_closure_ms = 1000 / options.frames_per_second;
      for (;;) {
        Check(Clock::time_point{}); child.Read(state);
        bool stop; double stop_at;
        { std::lock_guard lock(mutex); stop = stopping; stop_at = stop_at_ms;
          if (!spawned && !frames.empty()) {
            held = frames.front().picture; held_identity = frames.front().identity;
            state.first_captured_timestamp_ms = frames.front().timestamp;
            state.first_timestamp_ms = std::max(state.first_captured_timestamp_ms, attempt_not_before_ms);
            origin = created + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(state.first_timestamp_ms - options.start_timestamp_ms));
            frames.pop_front(); spawned = true;
          }
        }
        if (!spawned) {
          if (stop) throw std::runtime_error("Native publisher stopped before receiving a frame");
          pollfd notification{wake[0].Get(), POLLIN, 0};
          if (poll(&notification, 1, Timeout(options.first_frame_deadline)) < 0 && errno != EINTR) throw std::runtime_error("Native publisher frame wait failed");
          DrainWake(); continue;
        }
        if (child.pid < 0) { child.Spawn(options); state.physical_encoder_pid = child.pid; attempt_pid = child.pid; }
        const auto now = Clock::now();
        const uint64_t stop_frames = stop ? static_cast<uint64_t>(std::max(0.0, std::floor((stop_at - state.first_timestamp_ms) * options.frames_per_second / 1000))) : UINT64_MAX;
        const uint64_t stop_audio = stop ? AudioBoundary(stop_frames, options.frames_per_second) : UINT64_MAX;
        const auto video_due = origin + latency + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>((queued_frames + 1) / options.frames_per_second));
        if (!inputs_closed && queued_frames < stop_frames && now >= video_due && video.size() < 2) {
          const double capture_at = state.first_timestamp_ms + queued_frames * 1000 / options.frames_per_second;
          { std::lock_guard lock(mutex);
            uint64_t picked = 0;
            while (!frames.empty() && frames.front().timestamp <= capture_at) {
              const auto control = At(frames.front().timestamp);
              if (!control.paused && control.generation == frames.front().generation) {
                if (picked) ++result.superseded_frames;
                held = frames.front().picture; held_identity = frames.front().identity; ++picked;
              } else ++result.superseded_frames;
              frames.pop_front();
            }
          }
          if (!held) throw std::runtime_error("Native publisher lacks an actual causal captured image");
          if (held_identity == prior_identity) ++state.held_frames;
          prior_identity = held_identity; video.push_back({held, 0, video_due}); ++queued_frames;
        }
        const uint64_t next_audio = std::min<uint64_t>(stop_audio, audio_cursor + 960);
        const auto audio_due = origin + latency + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(next_audio / 48000.0));
        if (options.audio && !inputs_closed && pcm_offset == pcm.size() && audio_cursor < stop_audio && now >= audio_due) {
          pcm = Audio(audio_cursor, next_audio, state.first_timestamp_ms, state); pcm_offset = 0; audio_cursor = next_audio; pcm_due = audio_due;
        }
        if (!video.empty()) {
          auto& pending = video.front();
          if (now > pending.due + options.max_write_lag) throw std::runtime_error("Timeout: native publisher exceeded its bounded pipe lag (video)");
          const auto count = write(child.video.Get(), pending.picture->data.data() + pending.offset, pending.picture->data.size() - pending.offset);
          if (count > 0) pending.offset += static_cast<size_t>(count);
          else if (count < 0 && errno != EINTR && errno != EAGAIN) { child.Read(state); throw StreamRetryableError("Native publisher input pipe failed (video): " + child.text); }
          if (pending.offset == pending.picture->data.size()) { state.max_capture_to_pipe_ms = std::max(state.max_capture_to_pipe_ms, std::chrono::duration<double,std::milli>(Clock::now() - origin).count() - state.submitted_frames * 1000 / options.frames_per_second); pending.picture->submitted = true; ++state.submitted_frames; state.duration_ms = state.submitted_frames * 1000 / options.frames_per_second; video.pop_front(); }
        }
        if (pcm_offset < pcm.size()) {
          if (now > pcm_due + options.max_write_lag) throw std::runtime_error("Timeout: native publisher exceeded its bounded pipe lag (audio)");
          const auto count = write(child.audio.Get(), pcm.data() + pcm_offset, pcm.size() - pcm_offset);
          if (count > 0) { pcm_offset += static_cast<size_t>(count); audio_written_bytes += static_cast<uint64_t>(count); state.audio_submitted_frames = audio_written_bytes / 4; }
          else if (count < 0 && errno != EINTR && errno != EAGAIN) { child.Read(state); throw StreamRetryableError("Native publisher input pipe failed (PCM): " + child.text); }
        }
        if (stop && queued_frames == stop_frames && video.empty() && (!options.audio || (audio_cursor == stop_audio && pcm_offset == pcm.size())) && !inputs_closed) {
          if (!state.submitted_frames) throw std::runtime_error("Native publisher stopped before a complete captured interval");
          child.video.Reset(); child.audio.Reset(); inputs_closed = true;
        }
        { std::lock_guard lock(mutex);
          const double video_consumed = queued_frames ? (queued_frames - 1) * 1000 / options.frames_per_second : 0;
          const double consumed = state.first_timestamp_ms + (options.audio ? std::min(video_consumed, audio_cursor * 1000.0 / 48000) : video_consumed);
          while (controls.size() > 1 && controls[1].timestamp < consumed) controls.pop_front();
        }
        child.Read(state); Publish(state);
        if (child.Reap()) {
          if (!inputs_closed) throw StreamRetryableError("Native publisher exited before stop (" + ChildStatus(child.status) + "): " + child.text);
          child.Read(state);
          if (!WIFEXITED(child.status) || WEXITSTATUS(child.status)) throw std::runtime_error("Native publisher failed (" + ChildStatus(child.status) + "): " + child.text);
          state.physical_encoder_pid = 0; break;
        }
        if (!started && state.encoded_frames) {
          Check(encoder_acknowledged ? outage_deadline : options.first_frame_deadline);
          started = true; encoder_acknowledged = true; outage_deadline = Clock::time_point{};
          state.reconnecting = false; Publish(state); if (ready) ready(state);
        }
        auto next = now + std::chrono::milliseconds(50);
        if (!inputs_closed && queued_frames < stop_frames && video.size() < 2) next = std::min(next, video_due);
        if (options.audio && !inputs_closed && audio_cursor < stop_audio && pcm_offset == pcm.size()) next = std::min(next, audio_due);
        if (!video.empty()) next = std::min(next, video.front().due + options.max_write_lag);
        if (pcm_offset < pcm.size()) next = std::min(next, pcm_due + options.max_write_lag);
        std::array<pollfd, 5> fds{{{child.video.Get(), static_cast<short>(!video.empty() ? POLLOUT : 0), 0},
          {child.audio.Get(), static_cast<short>(pcm_offset < pcm.size() ? POLLOUT : 0), 0}, {child.progress.Get(), POLLIN, 0}, {child.diagnostics.Get(), POLLIN, 0}, {wake[0].Get(), POLLIN, 0}}};
        if (poll(fds.data(), fds.size(), Timeout(next)) < 0 && errno != EINTR) throw std::runtime_error("Native publisher independent pipe polling failed");
        if (fds.back().revents) DrainWake();
      }
    } catch (const StreamRetryableError& error) { attempt_failure_at = Clock::now(); state.failure = error.what(); retryable = true; }
      catch (const std::exception& error) { state.failure = error.what(); }
    // No retry or terminal callback can observe a physically live old child.
    child.FinishPhysical();
    try { child.Read(state); } catch (const std::exception& error) { state.failure = error.what(); retryable = false; }
    if (!child.wait_failure.empty()) { state.failure = child.wait_failure; retryable = false; }
    if (child.pid > 0 && !child.exit_observed) std::terminate();
    if (retryable && !encoder_acknowledged && !StreamNetworkFailure(state.failure + child.text)) retryable = false;
    state.physical_encoder_pid = 0;
    StreamAttemptResult attempt; attempt.number = attempt_number; attempt.encoder_pid = child.pid>0?child.pid:attempt_pid;
    attempt.first_timestamp_ms = state.first_timestamp_ms; attempt.first_captured_timestamp_ms = state.first_captured_timestamp_ms; attempt.finished_timestamp_ms = NativeNow();
    attempt.child_reaped = child.reaped; attempt.child_exit_observed = child.exit_observed;
    attempt.submitted_frames = state.submitted_frames; attempt.encoded_frames = state.encoded_frames;
    attempt.audio_submitted_frames = state.audio_submitted_frames; attempt.muxed_bytes = state.muxed_bytes;
    attempt.diagnostic_truncated = state.failure.size() > 1024 || state.diagnostics.size() > 1024;
    attempt.failure = state.failure.substr(0,1024); attempt.diagnostics = state.diagnostics.substr(state.diagnostics.size() > 1024 ? state.diagnostics.size()-1024 : 0);
    if (attempts.size() == 8) attempts.erase(attempts.begin());
    attempts.push_back(std::move(attempt));
    if (!state.failure.empty()) last_attempt_failure = state.failure;
    Publish(state);
    return state;

  }
  void EnterOffline() {
    std::lock_guard lock(mutex); offline = true; result.reconnecting = true; result.failure.clear();
    for (const auto& frame : frames)
      if ((!latest_frame || frame.identity != latest_frame->identity) && !frame.picture->submitted)
        ++result.disconnected_video_frames;
    frames.clear();
    if (latest_frame) frames.push_back(*latest_frame);
    for (const auto& packet : packets) result.disconnected_audio_frames += (packet.data.size()-packet.offset)/4;
    packets.clear(); packet_bytes = 0;
  }
  void WaitRetry(Clock::time_point until) {
    for (;;) {
      Check(Clock::time_point{});
      { std::lock_guard lock(mutex); if (stopping) throw std::runtime_error("Native publisher stopped while disconnected: " + last_attempt_failure);
        const auto at = NativeNow(); while (controls.size() > 1 && controls[1].timestamp < at) controls.pop_front(); }
      if (Clock::now() >= until) return;
      pollfd notification{wake[0].Get(),POLLIN,0};
      if (poll(&notification,1,Timeout(until)) < 0 && errno != EINTR) throw std::runtime_error("Native publisher reconnect polling failed");
      DrainWake();
    }
  }
  void Run() {
    sigset_t mask; sigemptyset(&mask); sigaddset(&mask,SIGPIPE); pthread_sigmask(SIG_BLOCK,&mask,nullptr);
    StreamPipelineResult state; unsigned outage_retries = 0;
    for (;;) {
      ++attempt_number; bool retryable = false; state = RunAttempt(retryable);
      if (state.encoded_frames && outage_deadline == Clock::time_point{}) outage_retries = 0;
      bool stop; { std::lock_guard lock(mutex); stop = stopping; }
      if (state.failure.empty() || canceled || stop || !retryable || !options.reconnect || outage_retries >= options.reconnect->max_attempts) break;
      const auto& policy = *options.reconnect;
      if (outage_deadline == Clock::time_point{}) { outage_deadline = attempt_failure_at + policy.timeout; outage_retries = 0; }
      EnterOffline();
      const auto multiplier = uint64_t{1} << std::min(outage_retries, unsigned{8});
      const auto delay = std::chrono::milliseconds(std::min<int64_t>(policy.max_delay.count(),policy.initial_delay.count()*static_cast<int64_t>(multiplier)));
      try { WaitRetry(Clock::now()+delay); }
      catch (const std::exception& error) { state.failure = error.what(); break; }
      ++outage_retries; ++reconnect_attempts;
      completed_output.submitted_frames += state.submitted_frames; completed_output.encoded_frames += state.encoded_frames;
      completed_output.audio_submitted_frames += state.audio_submitted_frames; completed_output.muxed_bytes += state.muxed_bytes;
      { std::lock_guard lock(mutex); offline = false; attempt_not_before_ms = NativeNow(); }
    }
    state.reconnecting = false; state.physical_encoder_pid = 0; Publish(state);
    { std::lock_guard lock(mutex);
      result.stopped_video_frames += frames.size(); for (const auto& packet : packets) result.stopped_audio_frames += (packet.data.size()-packet.offset)/4;
      frames.clear(); latest_frame.reset(); packets.clear(); packet_bytes = 0; result.captured_frame_bytes = frame_bytes->load();
      state = result; settled = true; if (lease) { stream_count.fetch_sub(1); lease = false; }
    }
    if (completed) { try { completed(state); } catch (...) {} }
  }

};
StreamPipeline::StreamPipeline(StreamPipelineOptions options, Completion completed, Completion ready) : impl_(std::make_unique<Impl>(std::move(options), std::move(completed), std::move(ready))) {}
StreamPipeline::~StreamPipeline() = default;
void StreamPipeline::Frame(const std::vector<uint8_t>& data, double timestamp) {
  const auto expected = static_cast<size_t>(impl_->options.source_width) * impl_->options.source_height * (impl_->options.i420 ? 3 : 8) / 2;
  if (data.size() != expected || !std::isfinite(timestamp) || timestamp < impl_->options.start_timestamp_ms) { impl_->Cancel("Malformed native publisher capture frame"); return; }
  try {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled || impl_->canceled || impl_->stopping) return;
    ++impl_->result.input_frames;
    if (timestamp < impl_->last_frame_timestamp) { ++impl_->result.superseded_frames; return; }
    impl_->last_frame_timestamp = timestamp;
    if (impl_->At(timestamp).paused) { ++impl_->result.paused_input_video_frames; return; }
    auto picture = StreamPicture::Capture(data, impl_->frame_bytes);
    Impl::FrameSample captured{std::move(picture), timestamp, impl_->generation, ++impl_->frame_identity};
    impl_->latest_frame = captured;
    if (impl_->offline) { for (const auto& frame : impl_->frames) if (!frame.picture->submitted) ++impl_->result.disconnected_video_frames; impl_->frames.clear(); }
    impl_->frames.push_back(std::move(captured));
    impl_->result.peak_captured_frame_bytes = std::max<uint64_t>(impl_->result.peak_captured_frame_bytes, impl_->frame_bytes->load());
  } catch (const std::exception& error) { impl_->Cancel(error.what()); }
  impl_->Wake();
}
void StreamPipeline::Audio(std::vector<uint8_t> data, double timestamp) {
  if (!impl_->options.audio) return;
  if (data.empty() || data.size() % 4 || data.size() > 4096 * 4 || !std::isfinite(timestamp) || timestamp < 0) { impl_->Cancel("Malformed native publisher PCM"); return; }
  bool overflow = false;
  { std::lock_guard lock(impl_->mutex); if (impl_->settled || impl_->canceled || timestamp < impl_->options.start_timestamp_ms || (impl_->stopping && timestamp > impl_->stop_at_ms)) return;
    if (impl_->offline) { impl_->result.audio_input_frames += data.size()/4; impl_->result.disconnected_audio_frames += data.size()/4; }
    else if (impl_->packet_bytes + data.size() > kAudioBytes) overflow = true;
    else { impl_->result.audio_input_frames += data.size() / 4; impl_->packet_bytes += data.size(); auto position = std::upper_bound(impl_->packets.begin(), impl_->packets.end(), timestamp, [](double time, const auto& packet) { return time < packet.timestamp; }); impl_->packets.insert(position, Impl::Sample{std::move(data), timestamp, 0, impl_->generation}); } }
  if (overflow) impl_->Cancel("Native publisher PCM queue exceeded its thirty-second bound"); else impl_->Wake();
}
void StreamPipeline::Pause(bool paused) {
  bool exhausted = false;
  { std::lock_guard lock(impl_->mutex); if (impl_->paused == paused || impl_->settled || impl_->stopping) return;
    if (impl_->controls.size() >= 1024) exhausted = true;
    else { impl_->paused = paused; impl_->controls.push_back({impl_->NativeNow(), paused, impl_->generation}); }
  }
  if (exhausted) impl_->Cancel("Native publisher timeline control bound exceeded"); else impl_->Wake();
}
void StreamPipeline::SwitchSource() {
  bool exhausted = false;
  { std::lock_guard lock(impl_->mutex); if (impl_->settled || impl_->stopping) return;
    if (impl_->controls.size() >= 1024 || impl_->generation == UINT64_MAX) exhausted = true;
    else { ++impl_->generation; impl_->controls.push_back({impl_->NativeNow(), impl_->paused, impl_->generation}); }
  }
  if (exhausted) impl_->Cancel("Native publisher source timeline bound exceeded"); else impl_->Wake();
}
void StreamPipeline::Stop(Clock::time_point deadline) {
  {
    std::lock_guard lock(impl_->mutex); if (impl_->settled) return;
    if (!impl_->stopping) impl_->stop_at_ms = impl_->NativeNow();
    impl_->stopping = true;
    // Repeated logical closure cannot extend a prior owner's deadline; a
    // later finite deadline still bounds a prior unlimited stop.
    if (deadline != Clock::time_point{} && (impl_->stop_deadline == Clock::time_point{} || deadline < impl_->stop_deadline)) impl_->stop_deadline = deadline;
  }
  impl_->Wake();
}
void StreamPipeline::Cancel(const std::string& reason) { impl_->Cancel(reason); }
bool StreamPipeline::Settled() const { return impl_->settled; }
StreamPipelineResult StreamPipeline::Snapshot() const { std::lock_guard lock(impl_->mutex); auto result=impl_->result; result.captured_frame_bytes=impl_->frame_bytes->load(); return result; }
void StreamPipeline::Join() { std::lock_guard lock(impl_->join_mutex); if (impl_->worker.joinable()) impl_->worker.join(); }
}  // namespace suitecut
