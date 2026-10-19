// SPDX-License-Identifier: MIT
#include "media_pipeline.h"
#include "libyuv/scale_argb.h"

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
#include <filesystem>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace suitecut {
namespace {
using Clock = std::chrono::steady_clock;
std::atomic<unsigned> active_pipelines{0};
std::atomic<uint64_t> reserved_disk_bytes{0};
constexpr uint64_t kGlobalDiskBytes = 1024ULL * 1024 * 1024;
constexpr size_t kAudioQueueBytes = 48000 * 4 * 30;
constexpr uint64_t kMaxDurationMs = 24ULL * 60 * 60 * 1000;
std::string Decimal(double value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(15) << value;
  return out.str();
}
class FD {
 public:
  explicit FD(int value = -1) : value_(value) {}
  ~FD() { Reset(); }
  FD(const FD&) = delete;
  FD& operator=(const FD&) = delete;
  FD(FD&& other) noexcept : value_(other.value_) { other.value_ = -1; }
  FD& operator=(FD&& other) noexcept {
    if (this != &other) { Reset(); value_ = other.value_; other.value_ = -1; }
    return *this;
  }
  int Get() const { return value_; }
  void Reset(int value = -1) {
    if (value_ >= 0) close(value_);
    value_ = value;
  }
 private:
  int value_;
};
std::array<FD, 2> Pipe() {
  int raw[2];
  if (pipe(raw) != 0) throw std::runtime_error("Could not create native recording pipe");
  std::array<FD, 2> pipe{FD(raw[0]), FD(raw[1])};
  for (const auto& fd : pipe)
    if (fcntl(fd.Get(), F_SETFD, FD_CLOEXEC) != 0)
      throw std::runtime_error("Could not secure native recording pipe");
  return pipe;
}
void Nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
    throw std::runtime_error("Could not configure native recording pipe");
}
struct Child {
  pid_t pid = -1;
  FD input;
  FD diagnostics;
  std::string text;
  int status = -1;
  bool reaped = false;
  ~Child() {
    input.Reset();
    if (pid > 0 && !reaped) {
      kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
  }
  void Read() {
    char bytes[4096];
    for (;;) {
      const auto count = read(diagnostics.Get(), bytes, sizeof(bytes));
      if (count <= 0) break;
      text.append(bytes, static_cast<size_t>(count));
      if (text.size() > 8192) text.erase(0, text.size() - 8192);
    }
  }
  bool Reap() {
    if (reaped) return true;
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) { reaped = true; Read(); return true; }
    if (result < 0 && errno != EINTR) throw std::runtime_error("Could not settle native encoder process");
    return false;
  }
  void Spawn(const std::string& launcher, const std::string& executable, const std::vector<std::string>& args,
      bool video, const std::array<int, 3>& files, uint64_t file_limit) {
    auto in = Pipe();
    auto err = Pipe();
    FD null(open("/dev/null", O_RDWR | O_CLOEXEC));
    if (null.Get() < 0) throw std::runtime_error("Could not open encoder null stream");
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions) != 0)
      throw std::runtime_error("Could not initialize encoder file actions");
    if (posix_spawnattr_init(&attributes) != 0) {
      posix_spawn_file_actions_destroy(&actions);
      throw std::runtime_error("Could not initialize encoder process attributes");
    }
    int error = posix_spawn_file_actions_adddup2(&actions, video ? in[0].Get() : null.Get(), STDIN_FILENO);
    error |= posix_spawn_file_actions_adddup2(&actions, null.Get(), STDOUT_FILENO);
    error |= posix_spawn_file_actions_adddup2(&actions, err[1].Get(), STDERR_FILENO);
    for (size_t index = 0; index < files.size(); ++index)
      error |= posix_spawn_file_actions_adddup2(&actions, files[index], static_cast<int>(index) + 3);
    for (int fd : {in[0].Get(), in[1].Get(), err[0].Get(), err[1].Get(), null.Get()})
      if (fd > STDERR_FILENO) error |= posix_spawn_file_actions_addclose(&actions, fd);
#if defined(__GLIBC__) && defined(__linux__)
    // CEF and embedder descriptors must never keep protocol/browser resources
    // alive in an encoder child. The three intended streams are already duped.
    error |= posix_spawn_file_actions_addclosefrom_np(&actions, 6);
#endif
    short spawn_flags = POSIX_SPAWN_SETPGROUP;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    spawn_flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    error |= posix_spawnattr_setflags(&attributes, spawn_flags);
    error |= posix_spawnattr_setpgroup(&attributes, 0);
    const std::vector<std::string> prefix{launcher, "--suitecut-media-encoder", std::to_string(getpid()), std::to_string(file_limit), executable};
    std::vector<char*> argv;
    for (const auto& arg : prefix) argv.push_back(const_cast<char*>(arg.c_str()));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    if (!error) error = posix_spawn(&pid, launcher.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error) { pid = -1; throw std::runtime_error("Could not launch native encoder: " + std::string(strerror(error))); }
    input = std::move(in[1]);
    diagnostics = std::move(err[0]);
    Nonblocking(input.Get());
    Nonblocking(diagnostics.Get());
    if (!video) input.Reset();
  }
};
struct PrivateFiles {
  FD parent;
  std::array<FD, 3> files;
  std::string output_name;
  std::string commit_name;
  ~PrivateFiles() {
    if (!commit_name.empty()) unlinkat(parent.Get(), commit_name.c_str(), 0);
  }
  explicit PrivateFiles(const std::string& output) {
    const std::filesystem::path target(output);
    auto root = std::filesystem::canonical(target.parent_path());
    parent.Reset(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (parent.Get() < 0) throw std::runtime_error("Could not open native recording output directory");
    output_name = target.filename().string();
#ifdef __linux__
    // Anonymous real files disappear when parent/encoder fds close, including
    // a forced parent death. No named capture/PCM/mux files survive SIGKILL.
    for (auto& file : files) {
      file.Reset(openat(parent.Get(), ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600));
      if (file.Get() < 0) throw std::runtime_error("Native recording output filesystem must support anonymous temporary files");
    }
#else
    throw std::runtime_error("Native recording anonymous-file ownership requires Linux");
#endif
  }
  std::string File(const char* file) const {
    const int index = std::string_view(file) == "video.webm" ? 0 : std::string_view(file) == "audio.pcm" ? 1 : 2;
    return "/proc/self/fd/" + std::to_string(index + 3);
  }
  int Get(const char* file) const {
    const int index = std::string_view(file) == "video.webm" ? 0 : std::string_view(file) == "audio.pcm" ? 1 : 2;
    return files[static_cast<size_t>(index)].Get();
  }
  std::array<int, 3> ChildFiles() const { return {files[0].Get(), files[1].Get(), files[2].Get()}; }
  void Commit(int fd) {
    static std::atomic<uint64_t> sequence{0};
    for (int attempt = 0; attempt < 256; ++attempt) {
      const auto name = ".suitecut-media-commit-" + std::to_string(getpid()) + "-" + std::to_string(++sequence);
#ifdef __linux__
      int result = linkat(fd, "", parent.Get(), name.c_str(), AT_EMPTY_PATH);
      if (result != 0 && errno == EPERM) {
        const auto owned = "/proc/self/fd/" + std::to_string(fd);
        result = linkat(AT_FDCWD, owned.c_str(), parent.Get(), name.c_str(), AT_SYMLINK_FOLLOW);
      }
      if (result == 0) {
        commit_name = name;
        if (renameat(parent.Get(), name.c_str(), parent.Get(), output_name.c_str()) != 0)
          throw std::runtime_error("Could not commit native recording output");
        commit_name.clear(); return;
      }
      if (errno != EEXIST) throw std::runtime_error("Could not link native recording output");
#endif
    }
    throw std::runtime_error("Native recording commit name capacity exceeded");
  }
};
}  // namespace

int MediaEncoderHelperMain(int argc, char** argv) {
  if (argc < 2 || std::string_view(argv[1]) != "--suitecut-media-encoder") return -1;
  auto fail = [](const char* message) {
    const auto result = write(STDERR_FILENO, message, strlen(message));
    (void)result; return 127;
  };
  if (argc < 5 || argv[4][0] != '/') return fail("Invalid native encoder helper arguments\n");
  int parent = 0; uint64_t limit = 0;
  const std::string_view parent_text(argv[2]), limit_text(argv[3]);
  const auto parsed_parent = std::from_chars(parent_text.data(), parent_text.data() + parent_text.size(), parent);
  const auto parsed_limit = std::from_chars(limit_text.data(), limit_text.data() + limit_text.size(), limit);
  if (parsed_parent.ec != std::errc() || parsed_parent.ptr != parent_text.data() + parent_text.size() || parent <= 1 ||
      parsed_limit.ec != std::errc() || parsed_limit.ptr != limit_text.data() + limit_text.size() || limit < 1 || limit > kGlobalDiskBytes)
    return fail("Invalid native encoder helper ownership or quota\n");
#ifdef __linux__
  if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent)
    return fail("Native encoder parent ownership ended before launch\n");
#else
  return fail("Native encoder parent-death ownership requires Linux\n");
#endif
  const rlimit file_limit{static_cast<rlim_t>(limit), static_cast<rlim_t>(limit)};
  if (setrlimit(RLIMIT_FSIZE, &file_limit) != 0) return fail("Could not enforce native encoder disk quota\n");
  sigset_t mask; sigemptyset(&mask);
  if (pthread_sigmask(SIG_SETMASK, &mask, nullptr) != 0) return fail("Could not reset native encoder signal mask\n");
  for (int signal : {SIGPIPE, SIGTERM, SIGINT, SIGXFSZ}) {
    struct sigaction action{}; action.sa_handler = SIG_DFL; sigemptyset(&action.sa_mask);
    if (sigaction(signal, &action, nullptr) != 0) return fail("Could not reset native encoder signals\n");
  }
  execve(argv[4], argv + 4, environ);
  return fail("Could not launch native FFmpeg executable\n");
}

uint64_t MediaSlotCount(double duration_ms, double fps) {
  if (!std::isfinite(duration_ms) || duration_ms < 0 || duration_ms > kMaxDurationMs ||
      !std::isfinite(fps) || fps <= 0 || fps > 120)
    throw std::runtime_error("Invalid native recording cadence");
  const long double slots = static_cast<long double>(duration_ms) * fps / 1000;
  const long double rounded = std::round(slots);
  // The tolerance covers only floating representation error, not a real slot.
  const long double tolerance = std::numeric_limits<double>::epsilon() * std::max(1.L, slots) * 16;
  return static_cast<uint64_t>(std::ceil(std::abs(slots - rounded) <= tolerance ? rounded : slots));
}
std::string ResolveMediaExecutable(const std::string& executable) {
  if (executable.empty() || executable.size() > 4096 || executable.find('\0') != std::string::npos)
    throw std::runtime_error("Invalid native encoder executable");
  auto usable = [](const std::filesystem::path& path) -> std::string {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error || access(path.c_str(), X_OK) != 0) return {};
    const auto result = std::filesystem::canonical(path, error);
    return error ? std::string{} : result.string();
  };
  if (executable.find('/') != std::string::npos) {
    const auto resolved = usable(std::filesystem::absolute(executable));
    if (!resolved.empty()) return resolved;
  } else {
    const char* environment = std::getenv("PATH");
    const std::string path = environment ? environment : "/usr/bin:/bin";
    for (size_t begin = 0; begin <= path.size();) {
      const auto end = path.find(':', begin);
      const auto directory = path.substr(begin, end == std::string::npos ? end : end - begin);
      const auto resolved = usable(std::filesystem::absolute(directory.empty() ? "." : directory) / executable);
      if (!resolved.empty()) return resolved;
      if (end == std::string::npos) break;
      begin = end + 1;
    }
  }
  throw std::runtime_error("Native FFmpeg executable is unavailable or not executable");
}
void ValidateMediaPipelineOptions(const MediaPipelineOptions& o) {
  if (o.launcher_path.empty() || o.launcher_path.front() != '/' || o.launcher_path.size() > 4096 ||
      o.launcher_path.find('\0') != std::string::npos || o.ffmpeg_path.empty() || o.ffmpeg_path.front() != '/' || o.ffmpeg_path.size() > 4096 ||
      o.ffmpeg_path.find('\0') != std::string::npos || o.output_path.empty() || o.output_path.front() != '/' ||
      o.output_path.size() > 4096 || o.output_path.find('\0') != std::string::npos)
    throw std::runtime_error("Native encoder and output paths must be absolute paths");
  if (std::filesystem::path(o.output_path).filename().empty() ||
      std::filesystem::path(o.output_path).filename() == "." || std::filesystem::path(o.output_path).filename() == "..")
    throw std::runtime_error("Invalid native recording output filename");
  for (int value : {o.source_width, o.source_height, o.output_width, o.output_height})
    if (value < (o.dynamic_viewport ? 1 : 2) || value > 3840) throw std::runtime_error("Invalid native recording canvas");
  if (o.source_height > 2160 || o.output_height > 2160 ||
      (!o.dynamic_viewport && (o.output_width % 2 || o.output_height % 2)) ||
      (o.i420 && (o.source_width % 2 || o.source_height % 2)))
    throw std::runtime_error("Native recording requires an even encoder canvas");
  if (o.dynamic_viewport && (o.i420 || o.source_width != o.output_width ||
      o.source_height != o.output_height || o.max_content_width || o.max_content_height))
    throw std::runtime_error("Native viewport recording requires its fixed BGRA output canvas");
  if ((o.max_content_width || o.max_content_height) &&
      (o.max_content_width < 2 || o.max_content_height < 2 ||
       o.max_content_width > o.output_width || o.max_content_height > o.output_height))
    throw std::runtime_error("Native recording content bounds must fit the encoder canvas");
  MediaSlotCount(0, o.frames_per_second);
  if (!std::isfinite(o.start_timestamp_ms) || o.start_timestamp_ms < 0 || o.max_file_bytes < 1024 ||
      o.max_file_bytes > 1024ULL * 1024 * 1024)
    throw std::runtime_error("Invalid native recording resource bounds");
}

struct MediaPipeline::Impl {
  struct Sample {
    std::vector<uint8_t> data; double timestamp = 0;
    int width = 0, height = 0;
    double css_width = 0, css_height = 0;
  };
  MediaPipelineOptions options;
  Completion completion;
  Started started;
  Completion progress;
  mutable std::mutex mutex;
  std::mutex join_mutex;
  std::array<FD, 2> wake = Pipe();
  std::condition_variable changed;
  std::optional<Sample> pending;
  std::deque<Sample> audio;
  size_t audio_bytes = 0;
  std::thread worker;
  std::atomic<bool> canceled{false};
  std::atomic<bool> settled{false};
  std::atomic<bool> encoder_ready{false};
  std::atomic<int64_t> finish_deadline{0};
  bool stopping = false;
  bool paused = false;
  double paused_at = 0;
  double excluded = 0;
  double latest_frame = -1;
  double end = 0;
  std::string cancel_reason;
  MediaPipelineResult result;
  bool lease = false;
  bool disk_lease = false;
  bool committed = false;
  Impl(MediaPipelineOptions value, Completion done, Started ready, Completion update)
      : options(std::move(value)), completion(std::move(done)), started(std::move(ready)), progress(std::move(update)) {
    // Explicit absolute missing paths are left to the actual helper exec error,
    // preserving its native diagnostics. Bare/relative programs resolve here.
    if (!options.ffmpeg_path.empty() && options.ffmpeg_path.front() != '/')
      options.ffmpeg_path = ResolveMediaExecutable(options.ffmpeg_path);
    ValidateMediaPipelineOptions(options);
    Nonblocking(wake[0].Get()); Nonblocking(wake[1].Get());
    unsigned count = active_pipelines.load();
    do {
      if (count >= 16) throw std::runtime_error("Native recording pipeline capacity exceeded");
    } while (!active_pipelines.compare_exchange_weak(count, count + 1));
    lease = true;
    uint64_t bytes = reserved_disk_bytes.load();
    do {
      if (bytes > kGlobalDiskBytes - options.max_file_bytes) {
        active_pipelines.fetch_sub(1); lease = false;
        throw std::runtime_error("Native recording global disk reservation exceeded");
      }
    } while (!reserved_disk_bytes.compare_exchange_weak(bytes, bytes + options.max_file_bytes));
    disk_lease = true;
    result.output_path = options.output_path;
    result.frames_per_second = options.frames_per_second;
    try { worker = std::thread([this] { Run(); }); }
    catch (...) {
      active_pipelines.fetch_sub(1); lease = false;
      reserved_disk_bytes.fetch_sub(options.max_file_bytes); disk_lease = false;
      throw;
    }
  }
  ~Impl() {
    Cancel("Native recording owner closed");
    if (worker.joinable()) worker.join();
    if (lease) active_pipelines.fetch_sub(1);
    if (disk_lease) reserved_disk_bytes.fetch_sub(options.max_file_bytes);
  }
  void Cancel(const std::string& reason) {
    {
      std::lock_guard lock(mutex);
      if (settled || canceled || committed) return;
      cancel_reason = reason.substr(0, 8192);
      canceled = true;
      const uint8_t signal = 1;
      const auto signaled = write(wake[1].Get(), &signal, 1);
      (void)signaled;
    }
    changed.notify_all();
  }
  void Check() {
    if (canceled) {
      std::lock_guard lock(mutex);
      throw std::runtime_error(cancel_reason.empty() ? "Native recording canceled" : cancel_reason);
    }
    if (!encoder_ready && options.first_frame_deadline != Clock::time_point{} && Clock::now() >= options.first_frame_deadline)
      throw std::runtime_error("Timeout waiting for first native recording frame and encoder");
    const int64_t deadline = finish_deadline.load();
    if (deadline && Clock::now().time_since_epoch().count() >= deadline)
      throw std::runtime_error("Timeout while finalizing native recording");
  }
  void Poll(Child& child, bool writing) {
    Check();
    child.Read();
    if (child.Reap()) {
      if (writing || !WIFEXITED(child.status) || WEXITSTATUS(child.status))
        throw std::runtime_error("Native encoder exited unsuccessfully: " + child.text);
      return;
    }
    pollfd streams[3]{{child.diagnostics.Get(), POLLIN, 0},
      {writing ? child.input.Get() : -1, POLLOUT, 0}, {wake[0].Get(), POLLIN, 0}};
    // Poll only waits for actual pipe/child readiness. Cancellation has its own
    // wake fd; an original finish deadline bounds each resource wait exactly.
    int timeout = 50;
    auto deadline = finish_deadline.load();
    if (!encoder_ready && options.first_frame_deadline != Clock::time_point{} &&
        (!deadline || options.first_frame_deadline.time_since_epoch().count() < deadline))
      deadline = options.first_frame_deadline.time_since_epoch().count();
    if (deadline) {
      const auto remaining = std::chrono::duration<double, std::milli>(
          Clock::time_point(Clock::duration(deadline)) - Clock::now()).count();
      timeout = static_cast<int>(std::clamp(std::ceil(remaining), 0.0, 50.0));
    }
    const int count = poll(streams, 3, timeout);
    Check();
    if (count < 0 && errno != EINTR) throw std::runtime_error("Native encoder pipe polling failed");
  }
  void Write(Child& child, const std::vector<uint8_t>& bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
      Check();
      child.Read();
      const auto wrote = write(child.input.Get(), bytes.data() + offset, bytes.size() - offset);
      if (wrote > 0) offset += static_cast<size_t>(wrote);
      else if (wrote < 0 && errno != EAGAIN && errno != EINTR) {
        child.Read();
        throw std::runtime_error("Native encoder video pipe failed: " + child.text);
      }
      else Poll(child, true);
    }
  }
  void Wait(Child& child, const PrivateFiles& files) {
    child.input.Reset();
    while (!child.Reap()) { Poll(child, false); DiskBound(files); }
    Check();
    if (!WIFEXITED(child.status) || WEXITSTATUS(child.status))
      throw std::runtime_error("Native encoder exited unsuccessfully: " + child.text);
  }
  void DiskBound(const PrivateFiles& files) {
    uint64_t bytes = 0;
    for (const char* name : {"video.webm", "audio.pcm", "final.webm"}) {
      struct stat stat{};
      if (fstat(files.Get(name), &stat) == 0) {
        if (!S_ISREG(stat.st_mode) || stat.st_size < 0) throw std::runtime_error("Unsafe native encoder output");
        bytes += static_cast<uint64_t>(stat.st_size);
      } else if (errno != ENOENT) throw std::runtime_error("Could not inspect native encoder files");
    }
    if (bytes > options.max_file_bytes) throw std::runtime_error("Native recording disk quota exceeded");
  }
  void AudioWrite(int fd, const uint8_t* data, size_t length, const PrivateFiles& files) {
    while (length) {
      Check();
      struct stat stat{};
      const uint64_t audio_limit = options.max_file_bytes / 3;
      if (fstat(fd, &stat) != 0 || stat.st_size < 0 || static_cast<uint64_t>(stat.st_size) > audio_limit ||
          length > audio_limit - static_cast<uint64_t>(stat.st_size))
        throw std::runtime_error("Native recording PCM disk quota exceeded");
      const auto count = write(fd, data, length);
      if (count > 0) { data += count; length -= static_cast<size_t>(count); }
      else if (count < 0 && errno == EINTR) continue;
      else throw std::runtime_error("Native audio spool write failed");
    }
    DiskBound(files);
  }
  void Silence(int fd, uint64_t count, const PrivateFiles& files) {
    const std::array<uint8_t, 3840> zero{};
    while (count) {
      const size_t chunk = static_cast<size_t>(std::min<uint64_t>(count, 960));
      AudioWrite(fd, zero.data(), chunk * 4, files);
      count -= chunk;
    }
  }
  void Publish(MediaPipelineResult& progress) {
    std::lock_guard lock(mutex);
    // Preserve counters written by concurrent CEF frame/audio ingestion while
    // publishing the worker's actual delivered CFR/PCM progress.
    progress.input_frames = result.input_frames;
    progress.superseded_frames = result.superseded_frames;
    progress.audio_input_frames = result.audio_input_frames;
    result = progress;
  }
  void Run() {
    sigset_t blocked;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
    MediaPipelineResult finished;
    {
      std::lock_guard lock(mutex);
      finished = result;
    }
    try {
      PrivateFiles files(options.output_path);
      Child encoder;
      FD pcm;
      if (options.audio) {
        pcm.Reset(fcntl(files.Get("audio.pcm"), F_DUPFD_CLOEXEC, 6));
        if (pcm.Get() < 0) throw std::runtime_error("Could not prepare native audio spool");
      }
      std::vector<uint8_t> held;
      uint64_t held_generation = 0, emitted_generation = 0;
      uint64_t audio_frame = 0;
      double first = -1;
      for (;;) {
        Check();
        std::optional<Sample> frame;
        std::deque<Sample> packets;
        bool stop;
        double stop_at;
        bool just_started = false;
        {
          std::unique_lock lock(mutex);
          while (!pending && (first < 0 || audio.empty()) && !stopping && !canceled) {
            auto deadline = finish_deadline.load();
            if (first < 0 && options.first_frame_deadline != Clock::time_point{} &&
                (!deadline || options.first_frame_deadline.time_since_epoch().count() < deadline))
              deadline = options.first_frame_deadline.time_since_epoch().count();
            if (deadline) {
              const auto due = Clock::time_point(Clock::duration(deadline));
              if (Clock::now() >= due) throw std::runtime_error("Timeout waiting for native recording input under its original deadline");
              // A newly tightened native close cap wakes this wait and is
              // re-read; it does not pretend a frame or stop has arrived.
              changed.wait_until(lock, due);
            } else changed.wait(lock);
          }
          frame = std::move(pending); pending.reset();
          if (first >= 0 || frame) { packets.swap(audio); audio_bytes = 0; }
          stop = stopping; stop_at = end;
        }
        Check();
        if (frame) {
          if (options.dynamic_viewport) {
            // The bounded newest-paint queue drops superseded input before any
            // scaling. Work stays off CEF's UI thread and uses the genuine
            // current paint extent rather than the page's launch geometry.
            const double fit = std::min({1., options.output_width / frame->css_width,
                                         options.output_height / frame->css_height});
            const int width = std::clamp(static_cast<int>(std::floor(frame->css_width * fit)), 1, options.output_width);
            const int height = std::clamp(static_cast<int>(std::floor(frame->css_height * fit)), 1, options.output_height);
            if (frame->width != options.output_width || frame->height != options.output_height ||
                width != options.output_width || height != options.output_height) {
              std::vector<uint8_t> canvas(static_cast<size_t>(options.output_width) * options.output_height * 4, 0);
              for (size_t index = 3; index < canvas.size(); index += 4) canvas[index] = 255;
              if (libyuv::ARGBScale(frame->data.data(), frame->width * 4, frame->width, frame->height,
                    canvas.data(), options.output_width * 4, width, height, libyuv::kFilterBilinear) != 0)
                throw std::runtime_error("Native viewport recording paint scaling failed");
              frame->data = std::move(canvas);
            }
            Check();
          }
          if (first < 0) {
            first = frame->timestamp;
            finished.first_timestamp_ms = first;
            const bool bounded_content=options.max_content_width!=0;
            const int fit_width=bounded_content?options.max_content_width:options.output_width;
            const int fit_height=bounded_content?options.max_content_height:options.output_height;
            const std::string scale = options.dynamic_viewport ? "setsar=1" : "scale=" + std::to_string(fit_width) + ":" + std::to_string(fit_height) +
                ":force_original_aspect_ratio=decrease" + (bounded_content?":force_divisible_by=2":"") +
                ",pad=" + std::to_string(options.output_width) + ":" + std::to_string(options.output_height) + (options.top_left_padding ? ":0:0:black,setsar=1" : ":(ow-iw)/2:(oh-ih)/2:black,setsar=1");
            std::vector<std::string> args{"-hide_banner", "-loglevel", "error", "-nostdin", "-f", "rawvideo", "-pixel_format", options.i420 ? "yuv420p" : "bgra",
              "-video_size", std::to_string(options.source_width) + "x" + std::to_string(options.source_height), "-framerate", Decimal(options.frames_per_second), "-i", "pipe:0",
              "-an", "-vf", scale, "-c:v", "libvpx-vp9", "-crf", "18", "-b:v", "0", "-deadline", "realtime", "-cpu-used", "5", "-row-mt", "1", "-threads", "4", "-pix_fmt", "yuv420p"};
            if (options.i420) {
              // Raw CEF planes already contain BT.709 limited-range samples.
              // Mark the decoder input before scale negotiates its matrix;
              // output tags alone would reinterpret these bytes as BT.601.
              args.insert(std::find(args.begin(), args.end(), "-i"), {"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-color_range", "tv"});
              args.insert(args.end(), {"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-color_range", "tv"});
            }
            args.insert(args.end(), {"-f", "webm", "-y", files.File("video.webm")});
            encoder.Spawn(options.launcher_path, options.ffmpeg_path, args, true, files.ChildFiles(),
                options.audio ? options.max_file_bytes / 3 : options.max_file_bytes);
            finished.physical_encoder_pid = encoder.pid;

          }
          const uint64_t before = MediaSlotCount(std::max(0.0, frame->timestamp - first), options.frames_per_second);
          while (!held.empty() && finished.output_frames < before) {
            Write(encoder, held);
            if (emitted_generation == held_generation) ++finished.held_frames;
            emitted_generation = held_generation;
            ++finished.output_frames;
            Publish(finished);
            DiskBound(files);
          }
          held = std::move(frame->data); ++held_generation;
          if (!finished.output_frames) {
            Write(encoder, held); ++finished.output_frames; emitted_generation = held_generation;
            Check(); encoder_ready = true; just_started = true;
            { std::lock_guard lock(mutex); result.first_timestamp_ms = first; result.physical_encoder_pid = encoder.pid; }
          }
        }
        for (const auto& packet : packets) {
          if (first < 0) throw std::runtime_error("Native audio has no video clock");
          const int64_t packet_start = static_cast<int64_t>(std::llround((packet.timestamp - first) * 48));
          const uint64_t frames = packet.data.size() / 4;
          const uint64_t overlap = packet_start < static_cast<int64_t>(audio_frame) ?
              static_cast<uint64_t>(static_cast<int64_t>(audio_frame) - packet_start) : 0;
          finished.audio_overlap_frames += std::min(overlap, frames);
          if (overlap >= frames) continue;
          if (packet_start > static_cast<int64_t>(audio_frame)) {
            const uint64_t gap = static_cast<uint64_t>(packet_start) - audio_frame;
            Silence(pcm.Get(), gap, files); audio_frame += gap; finished.audio_gap_frames += gap;
          }
          const uint64_t remaining = frames - overlap;
          AudioWrite(pcm.Get(), packet.data.data() + overlap * 4, static_cast<size_t>(remaining * 4), files);
          audio_frame += remaining;
        }
        finished.audio_output_frames = options.audio ? audio_frame : 0;
        Publish(finished);
        if (just_started && started) started(first);
        if (progress) progress(finished);
        if (stop) {
          if (first < 0 || held.empty()) throw std::runtime_error("Native recording received no video frames");
          finished.duration_ms = std::max(0.0, stop_at - first);
          const uint64_t count = std::max<uint64_t>(1, MediaSlotCount(finished.duration_ms, options.frames_per_second));
          while (finished.output_frames < count) {
            Write(encoder, held);
            if (emitted_generation == held_generation) ++finished.held_frames;
            emitted_generation = held_generation;
            ++finished.output_frames;
            Publish(finished);
            DiskBound(files);
          }
          finished.encoded_duration_ms = finished.output_frames * 1000.0 / options.frames_per_second;
          const double mux_duration_ms = std::max(finished.duration_ms, 1000.0 / options.frames_per_second);
          const uint64_t target_audio = static_cast<uint64_t>(std::ceil(mux_duration_ms * 48));
          if (options.audio && audio_frame < target_audio) {
            Silence(pcm.Get(), target_audio - audio_frame, files);
            finished.audio_gap_frames += target_audio - audio_frame; audio_frame = target_audio;
          }
          // Preserve captured tail PCM in the bounded private spool; the mux
          // clips it to the original stop boundary instead of pretending none
          // was captured. Audio overlap/gap counters disclose alignment.
          finished.audio_output_frames = options.audio ? std::min(audio_frame, target_audio) : 0;
          if (pcm.Get() >= 0 && fsync(pcm.Get()) != 0) throw std::runtime_error("Could not flush native audio spool");
          pcm.Reset();
          Wait(encoder, files); finished.physical_encoder_pid = 0; finished.diagnostics = encoder.text;
          DiskBound(files);
          const char* final_file = "video.webm";
          if (options.audio) {
            Child mux;
            mux.Spawn(options.launcher_path, options.ffmpeg_path, {"-hide_banner", "-loglevel", "error", "-nostdin", "-i", files.File("video.webm"),
              "-f", "s16le", "-ar", "48000", "-ac", "2", "-i", files.File("audio.pcm"), "-map", "0:v:0", "-map", "1:a:0",
              "-c:v", "copy", "-c:a", "libopus", "-b:a", "160k", "-t", Decimal(mux_duration_ms / 1000), "-f", "webm", "-y", files.File("final.webm")}, false, files.ChildFiles(), options.max_file_bytes / 3);
            Wait(mux, files); finished.diagnostics += mux.text; final_file = "final.webm";
          }
          DiskBound(files); Check();
          FD file(fcntl(files.Get(final_file), F_DUPFD_CLOEXEC, 6));
          struct stat stat{};
          if (file.Get() < 0 || fstat(file.Get(), &stat) != 0 || !S_ISREG(stat.st_mode) || stat.st_size <= 0 || fchmod(file.Get(), 0600) != 0 || fsync(file.Get()) != 0)
            throw std::runtime_error("Could not validate native recording output");
          Check();
          {
            std::lock_guard lock(mutex);
            if (canceled) throw std::runtime_error(cancel_reason);
            files.Commit(file.Get());
            committed = true;
          }
          finished.output_bytes = static_cast<uint64_t>(stat.st_size);
          break;
        }
      }
    } catch (const std::exception& error) { finished.failure = error.what(); }
    catch (...) { finished.failure = "Unexpected native recording worker failure"; }
    {
      std::lock_guard lock(mutex);
      finished.physical_encoder_pid = 0;
      finished.input_frames = result.input_frames;
      finished.superseded_frames = result.superseded_frames;
      finished.audio_input_frames = result.audio_input_frames;
      if (finished.diagnostics.size() > 8192) finished.diagnostics.erase(0, finished.diagnostics.size() - 8192);
      result = finished;
      pending.reset(); audio.clear(); audio_bytes = 0;
      if (lease) { active_pipelines.fetch_sub(1); lease = false; }
      if (disk_lease) { reserved_disk_bytes.fetch_sub(options.max_file_bytes); disk_lease = false; }
      settled = true;
    }
    // The callback observes physical child settlement and private-file cleanup.
    if (completion) {
      try { completion(finished); } catch (...) { /* owner callback cannot unwind a worker */ }
    }
  }
};

MediaPipeline::MediaPipeline(MediaPipelineOptions options, Completion completion, Started started, Completion progress)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(completion), std::move(started), std::move(progress))) {}
MediaPipeline::~MediaPipeline() = default;
void MediaPipeline::Frame(std::vector<uint8_t> data, double timestamp) {
  if (impl_->options.dynamic_viewport) {
    impl_->Cancel("Native viewport recording requires genuine viewport paints"); return;
  }
  const size_t expected = static_cast<size_t>(impl_->options.source_width) * impl_->options.source_height * (impl_->options.i420 ? 3 : 8) / 2;
  if (data.size() != expected || !std::isfinite(timestamp) || timestamp < impl_->options.start_timestamp_ms ||
      timestamp - impl_->options.start_timestamp_ms > kMaxDurationMs) {
    impl_->Cancel("Malformed native recording frame or capture timestamp"); return;
  }
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled || impl_->canceled || impl_->stopping || impl_->paused) return;
    timestamp -= impl_->excluded;
    if (timestamp < impl_->latest_frame) return;
    ++impl_->result.input_frames;
    impl_->latest_frame = timestamp;
    if (impl_->pending) ++impl_->result.superseded_frames;
    impl_->pending = Impl::Sample{std::move(data), timestamp};
  }
  impl_->changed.notify_one();
}
void MediaPipeline::ViewportFrame(const std::vector<uint8_t>& bgra, int width, int height,
    double css_width, double css_height, double timestamp) {
  if (!impl_->options.dynamic_viewport) return;
  if (width < 1 || width > 3840 || height < 1 || height > 2160 ||
      !std::isfinite(css_width) || !std::isfinite(css_height) ||
      // Native viewports permit DPR down to 0.25. CSS bounds therefore exceed
      // the independently checked physical paint/canvas bounds.
      css_width < 1 || css_width > 15360 || css_height < 1 || css_height > 8640 ||
      bgra.size() != static_cast<size_t>(width) * height * 4 ||
      !std::isfinite(timestamp) || timestamp < impl_->options.start_timestamp_ms ||
      timestamp - impl_->options.start_timestamp_ms > kMaxDurationMs) {
    impl_->Cancel("Malformed native viewport recording paint or capture timestamp"); return;
  }
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled || impl_->canceled || impl_->stopping || impl_->paused) return;
    timestamp -= impl_->excluded;
    if (timestamp < impl_->latest_frame) return;
    ++impl_->result.input_frames;
    impl_->latest_frame = timestamp;
    if (impl_->pending) ++impl_->result.superseded_frames;
    impl_->pending = Impl::Sample{bgra, timestamp, width, height, css_width, css_height};
  }
  impl_->changed.notify_one();
}
void MediaPipeline::Audio(std::vector<uint8_t> bytes, double timestamp) {
  if (!impl_->options.audio) return;
  if (bytes.empty() || bytes.size() % 4 || bytes.size() > 4096 * 4 || !std::isfinite(timestamp) || timestamp < 0 ||
      timestamp - impl_->options.start_timestamp_ms > kMaxDurationMs) {
    impl_->Cancel("Malformed native stereo PCM or capture timestamp"); return;
  }
  bool overflow = false;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled || impl_->canceled || impl_->stopping || impl_->paused) return;
    if (timestamp < impl_->options.start_timestamp_ms) return;
    timestamp -= impl_->excluded;
    if (impl_->audio_bytes + bytes.size() > kAudioQueueBytes) overflow = true;
    else {
      impl_->result.audio_input_frames += bytes.size() / 4;
      impl_->audio_bytes += bytes.size();
      impl_->audio.push_back(Impl::Sample{std::move(bytes), timestamp});
    }
  }
  if (overflow) impl_->Cancel("Native recording audio queue exceeded 30 seconds");
  else impl_->changed.notify_one();
}
void MediaPipeline::Pause(double timestamp) {
  if (!std::isfinite(timestamp)) { impl_->Cancel("Invalid native recording pause clock"); return; }
  std::lock_guard lock(impl_->mutex);
  if (impl_->settled || impl_->stopping || impl_->paused) return;
  if (timestamp < impl_->options.start_timestamp_ms || timestamp - impl_->excluded < impl_->latest_frame) {
    impl_->cancel_reason = "Native recording pause clock moved backwards"; impl_->canceled = true; impl_->changed.notify_one(); return;
  }
  impl_->paused = true; impl_->paused_at = timestamp;
}
void MediaPipeline::Resume(double timestamp) {
  if (!std::isfinite(timestamp)) { impl_->Cancel("Invalid native recording resume clock"); return; }
  std::lock_guard lock(impl_->mutex);
  if (!impl_->paused || impl_->settled || impl_->stopping) return;
  if (timestamp < impl_->paused_at) {
    impl_->cancel_reason = "Native recording resume clock moved backwards"; impl_->canceled = true; impl_->changed.notify_one(); return;
  }
  impl_->excluded += timestamp - impl_->paused_at; impl_->paused = false;
}
void MediaPipeline::Stop(double timestamp, Clock::time_point deadline) {
  if (!std::isfinite(timestamp) || timestamp < impl_->options.start_timestamp_ms ||
      timestamp - impl_->options.start_timestamp_ms > kMaxDurationMs) {
    impl_->Cancel("Invalid native recording stop clock"); return;
  }
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled || impl_->stopping) return;
    impl_->end = (impl_->paused ? impl_->paused_at : timestamp) - impl_->excluded;
    if (impl_->end < impl_->latest_frame) {
      impl_->cancel_reason = "Native recording stop clock moved backwards"; impl_->canceled = true;
    }
    impl_->stopping = true;
    const auto previous = impl_->finish_deadline.load();
    if (deadline != Clock::time_point{} && (!previous || deadline.time_since_epoch().count() < previous))
      impl_->finish_deadline = deadline.time_since_epoch().count();
  }
  impl_->changed.notify_one();
}
void MediaPipeline::LimitDeadline(Clock::time_point deadline) {
  if (deadline == Clock::time_point{}) return;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled) return;
    const auto previous = impl_->finish_deadline.load();
    if (previous && previous <= deadline.time_since_epoch().count()) return;
    impl_->finish_deadline = deadline.time_since_epoch().count();
    const uint8_t signal = 1;
    const auto signaled = write(impl_->wake[1].Get(), &signal, 1);
    (void)signaled;
  }
  impl_->changed.notify_all();
}
void MediaPipeline::Cancel(const std::string& reason) { impl_->Cancel(reason); }
bool MediaPipeline::Settled() const { return impl_->settled; }
MediaPipelineResult MediaPipeline::Snapshot() const { std::lock_guard lock(impl_->mutex); return impl_->result; }
void MediaPipeline::Join() {
  std::lock_guard lock(impl_->join_mutex);
  if (impl_->worker.joinable()) impl_->worker.join();
}
}  // namespace suitecut
