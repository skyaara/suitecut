// SPDX-License-Identifier: MIT
#include "media_pipeline.h"
#include "worker_budget.h"
#include "libyuv/scale_argb.h"
#include "libyuv/convert_from_argb.h"
#include "libyuv/convert.h"
#include "libyuv/convert_argb.h"
#include "capture_canvas.h"

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
#include <sys/socket.h>
#include <sys/resource.h>
#ifdef __APPLE__
#include "media_activity.h"
#include "mac_capture.h"
#include "mac_encoder.h"
#include <pthread/qos.h>
#include <sys/event.h>
#include <stdio.h>
#endif
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
constexpr size_t kVideoQueueBytes = 64ULL * 1024 * 1024;
constexpr size_t kLookaheadBytes = 64ULL * 1024 * 1024;
constexpr auto kCaptureLookahead = std::chrono::milliseconds(100);
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
  for (const auto& fd : pipe) {
    if (fcntl(fd.Get(), F_SETFD, FD_CLOEXEC) != 0)
      throw std::runtime_error("Could not secure native recording pipe");
#ifdef F_SETNOSIGPIPE
    if (fcntl(fd.Get(), F_SETNOSIGPIPE, 1) != 0)
      throw std::runtime_error("Could not protect native recording pipe writes");
#endif
  }
  return pipe;
}
std::array<FD, 2> VideoPipe() {
#ifdef __APPLE__
  int raw[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, raw) != 0)
    throw std::runtime_error("Could not create native recording video stream");
  std::array<FD, 2> stream{FD(raw[0]), FD(raw[1])};
  // A Darwin pipe is too small for multi-megabyte raw video frames: every
  // paint repeatedly waits for the encoder reader to be scheduled. A bounded
  // socket stream allows bulk writes while preserving ordinary stdin/EOF.
  for (const auto& fd : stream) {
    if (fcntl(fd.Get(), F_SETFD, FD_CLOEXEC) != 0)
      throw std::runtime_error("Could not secure native video stream");
    const int yes = 1;
    if (setsockopt(fd.Get(), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) != 0)
      throw std::runtime_error("Could not protect native video stream writes");
    for (const int option : {SO_SNDBUF, SO_RCVBUF}) {
      // Respect a host's smaller socket limit without requesting a global
      // sysctl change. Defaults remain usable when no enlargement is allowed.
      for (int bytes : {4 * 1024 * 1024, 1024 * 1024, 256 * 1024})
        if (setsockopt(fd.Get(), SOL_SOCKET, option, &bytes, sizeof(bytes)) == 0) break;
    }
  }
  return stream;
#else
  return Pipe();
#endif
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
  uint64_t encoded_frames = 0;
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
      for (size_t pos = 0; (pos = text.find("frame=", pos)) != std::string::npos; pos += 6) {
        if (pos && text[pos - 1] != '\n') continue;
        const auto end = text.find('\n', pos);
        if (end == std::string::npos) break;
        uint64_t frames = 0;
        const auto parsed = std::from_chars(text.data() + pos + 6, text.data() + end, frames);
        if (parsed.ec == std::errc() && parsed.ptr == text.data() + end)
          encoded_frames = std::max(encoded_frames, frames);
      }
    }
  }
  bool Reap() {
    if (reaped) return true;
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) { reaped = true; Read(); return true; }
    if (result < 0 && errno != EINTR) throw std::runtime_error("Could not settle native encoder process");
    return false;
  }
  std::string Messages() const {
    if (text.find("progress=") == std::string::npos) return text;
    constexpr std::array<std::string_view, 12> keys{"frame", "fps", "stream_0_0_q", "bitrate", "total_size",
      "out_time_us", "out_time_ms", "out_time", "dup_frames", "drop_frames", "speed", "progress"};
    std::istringstream input(text); std::string line, messages;
    bool truncated = text.size() == 8192;
    while (std::getline(input, line)) {
      if (truncated) { truncated = false; continue; }
      const auto equals = line.find('=');
      if (equals != std::string::npos && std::find(keys.begin(), keys.end(),
          std::string_view(line).substr(0, equals)) != keys.end()) continue;
      messages += line + '\n';
    }
    return messages;
  }
  void Spawn(const std::string& launcher, const std::string& executable, const std::vector<std::string>& args,
      bool video, const std::array<int, 3>& files, uint64_t file_limit) {
    auto in = video ? VideoPipe() : Pipe();
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
#elif defined(__APPLE__)
    // Darwin has no O_TMPFILE. Unlink private mkstemp files immediately; the
    // encoder inherits only their descriptors, and crashes leave no raw media.
    for (auto& file : files) {
      auto pattern = (root / ".suitecut-media-XXXXXX").string();
      std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
      file.Reset(mkstemp(name.data()));
      if (file.Get() < 0) throw std::runtime_error("Could not create private native recording file");
      const int removed = unlink(name.data());
      if (removed || fcntl(file.Get(), F_SETFD, FD_CLOEXEC))
        throw std::runtime_error("Could not secure private native recording file");
    }
#else
    throw std::runtime_error("Native recording anonymous-file ownership requires Linux");
#endif
  }
  std::string File(const char* file) const {
    const int index = std::string_view(file) == "video.webm" ? 0 : std::string_view(file) == "audio.pcm" ? 1 : 2;
#ifdef __APPLE__
    return "/dev/fd/" + std::to_string(index + 3);
#else
    return "/proc/self/fd/" + std::to_string(index + 3);
#endif
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
#elif defined(__APPLE__)
      // Darwin cannot relink an unlinked descriptor. Copy into an exclusive,
      // private sibling, then atomically publish without replacing user files.
      FD destination(openat(parent.Get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
      if (destination.Get() < 0) {
        if (errno == EEXIST) continue;
        throw std::runtime_error("Could not stage native recording output");
      }
      commit_name = name;
      std::array<char, 65536> buffer;
      off_t offset = 0;
      for (;;) {
        const auto count = pread(fd, buffer.data(), buffer.size(), offset);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::runtime_error("Could not read native recording output");
        if (!count) break;
        size_t written = 0;
        while (written < static_cast<size_t>(count)) {
          const auto size = write(destination.Get(), buffer.data() + written, static_cast<size_t>(count) - written);
          if (size < 0 && errno == EINTR) continue;
          if (size <= 0) throw std::runtime_error("Could not publish complete native recording output");
          written += static_cast<size_t>(size);
        }
        offset += count;
      }
      if (fsync(destination.Get()) || renameatx_np(parent.Get(), name.c_str(), parent.Get(), output_name.c_str(), RENAME_EXCL))
        throw std::runtime_error("Could not commit native recording output");
      commit_name.clear(); return;
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
#elif defined(__APPLE__)
  if (getppid() != parent) return fail("Native encoder parent ownership ended before launch\n");
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
#ifdef __APPLE__
  MediaActivity activity;
  (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
  // Keep a small native supervisor around FFmpeg. A kernel process-exit event
  // retires the owned encoder if the browser dies, including SIGKILL.
  const int queue = kqueue();
  if (queue < 0 || fcntl(queue, F_SETFD, FD_CLOEXEC)) return fail("Could not monitor native encoder parent\n");
  struct kevent change;
  EV_SET(&change, parent, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
  if (kevent(queue, &change, 1, nullptr, 0, nullptr) < 0 || getppid() != parent) {
    close(queue); return fail("Native encoder parent ownership ended before launch\n");
  }
  const pid_t child = fork();
  if (child < 0) { close(queue); return fail("Could not launch supervised native encoder\n"); }
  if (!child) { execve(argv[4], argv + 4, environ); _exit(127); }
  int status = 0;
  for (;;) {
    const pid_t finished = waitpid(child, &status, WNOHANG);
    if (finished == child) { close(queue); return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status); }
    if (finished < 0 && errno != EINTR) { close(queue); return 127; }
    struct kevent event;
    const timespec timeout{0, 100000000};
    const int events = kevent(queue, nullptr, 0, &event, 1, &timeout);
    if (getppid() != parent || events > 0 || (events < 0 && errno != EINTR)) {
      kill(child, SIGKILL);
      while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
      close(queue); return 127;
    }
  }
#else
  execve(argv[4], argv + 4, environ);
#endif
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
  if (o.hardware_h264) {
#ifndef __APPLE__
    throw std::runtime_error("Hardware H.264 recording requires macOS");
#endif
    if (std::filesystem::path(o.output_path).extension() != ".mp4")
      throw std::runtime_error("Hardware H.264 recording requires MP4 output");
  }
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
  if ((o.worker_canvas_fit && (o.i420 || o.dynamic_viewport)) ||
      ((o.worker_i420 || o.worker_nv12) && (o.i420 || o.dynamic_viewport || o.source_width % 2 || o.source_height % 2)) || (o.worker_i420 && o.worker_nv12))
    throw std::runtime_error("Invalid native worker pixel conversion options");
  if ((o.max_content_width || o.max_content_height) &&
      (o.max_content_width < 2 || o.max_content_height < 2 ||
       o.max_content_width > o.output_width || o.max_content_height > o.output_height))
    throw std::runtime_error("Native recording content bounds must fit the encoder canvas");
  MediaSlotCount(0, o.frames_per_second);
  if (!std::isfinite(o.capture_frames_per_second) || o.capture_frames_per_second < 0 ||
      o.capture_frames_per_second > 120)
    throw std::runtime_error("Invalid native capture cadence");
  if (!std::isfinite(o.start_timestamp_ms) || o.start_timestamp_ms < 0 || o.max_file_bytes < 1024 ||
      o.max_file_bytes > 1024ULL * 1024 * 1024)
    throw std::runtime_error("Invalid native recording resource bounds");
}

struct MediaPipeline::Impl {
  struct Sample {
    std::vector<uint8_t> data; double timestamp = 0;
    int width = 0, height = 0;
    double css_width = 0, css_height = 0;
    uint64_t sequence = 0;
    Clock::time_point arrived{};
    bool nv12=false;
    std::shared_ptr<MacCaptureFrame> surface{};
    std::function<void(std::vector<uint8_t>)> painted{};
    size_t Bytes()const{return surface?static_cast<size_t>(width)*height*(nv12?3:8)/2:data.size();}
  };
  MediaPipelineOptions options;
  Clock::time_point admitted_at=Clock::now();
  double capture_fence_ms=0;
  Completion completion;
  Started started;
  Completion progress;
  mutable std::mutex mutex;
  std::mutex join_mutex;
  std::array<FD, 2> wake = Pipe();
  std::condition_variable changed;
  std::deque<Sample> pending;
  size_t pending_bytes = 0;
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
  uint64_t capture_sequence = 0;
  double end = 0;
  std::string cancel_reason;
  MediaPipelineResult result;
  bool lease = false;
  bool disk_lease = false;
  bool committed = false;
  bool stop_requested=false,encoder_prepared=false,audio_seen=false,video_admitted=false,audio_gate_notified=false;
  Clock::time_point audio_wait_until{};
  bool AudioPreroll(){
    if(options.capture_timestamps_exact&&encoder_prepared&&options.audio&&!audio_seen&&Clock::now()<audio_wait_until){++result.preparation_frames;return true;}
    return false;
  }
  double capture_cutoff_ms=std::numeric_limits<double>::infinity();
  double observed_video_ms=-1,observed_audio_end_ms=-1,native_pause_base=0;
  Clock::time_point capture_drain_until{};
  struct PauseRange {double begin,end;};
  std::deque<PauseRange> native_pauses;
  void DrainState(){
    if(stop_requested && ((observed_video_ms>capture_cutoff_ms && (!options.audio||observed_audio_end_ms>=capture_cutoff_ms)) || Clock::now()>=capture_drain_until))stopping=true;
  }
  void PruneNative(){
    const double old=options.start_timestamp_ms+std::chrono::duration<double,std::milli>(Clock::now()-admitted_at).count()-500;
    while(!native_pauses.empty()&&native_pauses.front().end<old){
      const auto range=native_pauses.front();native_pauses.pop_front();native_pause_base+=range.end-range.begin;capture_fence_ms=std::max(capture_fence_ms,range.end);
    }
  }
  bool NativeTime(double raw,double& mapped)const{
    if(raw<capture_fence_ms || raw>capture_cutoff_ms || (paused&&raw>=paused_at))return false;
    double skipped=native_pause_base;
    for(const auto& range:native_pauses){if(raw<range.begin)break;if(raw<range.end)return false;skipped+=range.end-range.begin;}
    mapped=raw-skipped;return true;
  }
  void Enqueue(Sample sample) {
    sample.arrived = Clock::now();
    // Absorb up to 200 ms of encoder scheduling jitter, without growing an
    // unbounded raw-pixel backlog. On sustained overload retire oldest input;
    // never block CEF's paint thread or silently reset the capture clock.
    const size_t capacity = static_cast<size_t>(std::ceil(options.frames_per_second / 5));
    while (!pending.empty() && (pending.size() >= capacity ||
           pending_bytes + sample.Bytes() > kVideoQueueBytes)) {
      pending_bytes -= pending.front().Bytes(); pending.pop_front();
      ++result.superseded_frames;
    }
    pending_bytes += sample.Bytes();
    pending.push_back(std::move(sample));
  }
  Impl(MediaPipelineOptions value, Completion done, Started ready, Completion update)
      : options(std::move(value)), completion(std::move(done)), started(std::move(ready)), progress(std::move(update)) {
    // Explicit absolute missing paths are left to the actual helper exec error,
    // preserving its native diagnostics. Bare/relative programs resolve here.
    if (!options.ffmpeg_path.empty() && options.ffmpeg_path.front() != '/')
      options.ffmpeg_path = ResolveMediaExecutable(options.ffmpeg_path);
    ValidateMediaPipelineOptions(options);
    capture_fence_ms=options.start_timestamp_ms;
#ifdef __APPLE__
    // Warm the shared GPU converter before starting hardware preparation;
    // its cold compile cannot become a late first video frame or audio gap.
    if(options.worker_nv12)PrepareMacCapture();
#endif
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
    progress.preparation_frames=std::max(progress.preparation_frames,result.preparation_frames);
    progress.input_frames = result.input_frames;
    progress.gpu_input_frames = result.gpu_input_frames;
    progress.gpu_nv12_frames = result.gpu_nv12_frames;
    progress.superseded_frames = result.superseded_frames;
    progress.audio_input_frames = result.audio_input_frames;
    result = progress;
  }
  void Run() {
#ifdef __APPLE__
    MediaActivity activity;
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
    sigset_t blocked;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
    MediaPipelineResult finished;
    {
      std::lock_guard lock(mutex);
      finished = result;
    }
    finished.encoder = options.hardware_h264 ? "h264_videotoolbox" : "libvpx-vp9";
    finished.container = options.hardware_h264 ? "mp4" : "webm";
#ifdef __APPLE__
    const bool native_direct=options.hardware_h264&&options.worker_nv12&&options.capture_timestamps_exact&&options.source_width==options.output_width&&options.source_height==options.output_height&&!options.max_content_width;
#else
    const bool native_direct=false;
#endif
    finished.encoding_backend=native_direct?"native-videotoolbox":"ffmpeg";
    const bool clocked = options.capture_frames_per_second > 0 && !options.capture_timestamps_exact;
    finished.capture_timestamps_exact=options.capture_timestamps_exact;
    const bool encoded_i420 = options.i420 || options.worker_i420 || options.worker_nv12;
    const char* input_format=options.worker_nv12?"nv12":encoded_i420?"yuv420p":"bgra";
    if (clocked) finished.capture_lookahead_ms = kCaptureLookahead.count();
    const auto preparation_started = Clock::now();
    try {
      PrivateFiles files(options.output_path);
      Child encoder;
#ifdef __APPLE__
      std::unique_ptr<MacNativeEncoder> native_encoder;
#endif
      FD pcm;
      if (options.audio) {
        pcm.Reset(fcntl(files.Get("audio.pcm"), F_DUPFD_CLOEXEC, 6));
        if (pcm.Get() < 0) throw std::runtime_error("Could not prepare native audio spool");
      }
      std::vector<uint8_t> held, spare_pixels;
      std::shared_ptr<MacCaptureFrame> held_surface;
      const auto has_held=[&]{return !held.empty()||static_cast<bool>(held_surface);};
      const auto write_held=[&]{
#ifdef __APPLE__
        if(native_encoder){native_encoder->Write(held_surface,held);if(!finished.output_frames)native_encoder->Drain();return;}
#endif
        Write(encoder,held);
      };
      uint64_t held_generation = 0, emitted_generation = 0;
      uint64_t last_frame_slot = 0;
      double last_frame_timestamp = -1;
      uint64_t audio_frame = 0;
      double first = -1;
      bool encoder_spawned = false;
      uint64_t preparation_packets = 0;
      const auto prepared=[&]{
              {
                std::lock_guard lock(mutex);
                encoder_prepared=true;audio_wait_until=Clock::now()+std::chrono::seconds(1);audio_gate_notified=audio_seen;
                finished.preparation_frames = result.input_frames;
                finished.preparation_packets = preparation_packets;
                finished.preparation_ms = std::chrono::duration<double,std::milli>(Clock::now() - preparation_started).count();
                result.input_frames = result.gpu_input_frames = result.gpu_nv12_frames = result.superseded_frames = result.audio_input_frames = 0;
                result.preparation_frames = finished.preparation_frames;
                result.preparation_packets = preparation_packets;
                result.preparation_ms = finished.preparation_ms;
                pending.clear(); pending_bytes = 0;
                if(options.capture_timestamps_exact)
                  capture_fence_ms=options.start_timestamp_ms+std::chrono::duration<double,std::milli>(Clock::now()-admitted_at).count();
                // Native capture time can precede delivery. Preserve a bounded
                // 200 ms PCM reservoir across preparation so the first real
                // surface has its contemporaneous sound, then trim by its PTS.
                const double audio_cutoff=audio.empty()?0:audio.back().timestamp-200;
                while(!audio.empty() && audio.front().timestamp<audio_cutoff)audio.pop_front();
                audio_bytes=0;
                for(const auto& packet:audio){audio_bytes+=packet.data.size();result.audio_input_frames+=packet.data.size()/4;}
              }
              if (progress) progress(finished);  // One preparation receipt requests a fresh static-page paint.
      };
      std::deque<Sample> lookahead;
      size_t lookahead_bytes = 0;
      const size_t lookahead_count = clocked ? static_cast<size_t>(std::ceil(options.capture_frames_per_second / 10)) + 1 : 1;
      const size_t normalized_bytes = static_cast<size_t>(options.source_width) * options.source_height * (encoded_i420 ? 3 : 8) / 2;
      auto ready = [&](bool no_pending, bool ending) {
        return !lookahead.empty() && (lookahead.size() >= lookahead_count ||
            lookahead_bytes + normalized_bytes > kLookaheadBytes ||
            (no_pending && (ending || Clock::now() >= lookahead.front().arrived + kCaptureLookahead)));
      };
      for (;;) {
        Check();
        std::optional<Sample> input, frame;
        std::deque<Sample> packets;
        bool stop, no_pending;
        double stop_at;
        bool just_started = false,request_audio_paint=false;
        {
          std::unique_lock lock(mutex);
          DrainState();
          while (pending.empty() && !ready(true, stopping) && (first < 0 || audio.empty()) && !stopping && !canceled) {
            DrainState();if(stopping)break;
            auto deadline = finish_deadline.load();
            if(encoder_prepared&&options.audio&&!audio_seen&&!audio_gate_notified&&first<0){
              if(Clock::now()>=audio_wait_until){audio_gate_notified=true;request_audio_paint=true;break;}
              const auto gate=audio_wait_until.time_since_epoch().count();if(!deadline||gate<deadline)deadline=gate;
            }
            if(stop_requested && (!deadline||capture_drain_until.time_since_epoch().count()<deadline))deadline=capture_drain_until.time_since_epoch().count();
            if (first < 0 && options.first_frame_deadline != Clock::time_point{} &&
                (!deadline || options.first_frame_deadline.time_since_epoch().count() < deadline))
              deadline = options.first_frame_deadline.time_since_epoch().count();
            if (!lookahead.empty()) {
              const auto due = (lookahead.front().arrived + kCaptureLookahead).time_since_epoch().count();
              if (!deadline || due < deadline) deadline = due;
            }
            if(native_direct && first>=0 && has_held() && !paused){
              const auto due=admitted_at+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(first-options.start_timestamp_ms+excluded+500+finished.output_frames*1000/options.frames_per_second+.001));
              if(Clock::now()>=due)break;
              const auto tick=due.time_since_epoch().count();if(!deadline||tick<deadline)deadline=tick;
            }
            if (!encoder_ready && options.first_frame_deadline != Clock::time_point{} && Clock::now() >= options.first_frame_deadline)
              throw std::runtime_error("Timeout waiting for first native recording frame and encoder");
            if (finish_deadline.load() && Clock::now().time_since_epoch().count() >= finish_deadline.load())
              throw std::runtime_error("Timeout while finalizing native recording");
            if (deadline) changed.wait_until(lock, Clock::time_point(Clock::duration(deadline)));
            else changed.wait(lock);
          }
          if (!pending.empty() && !ready(false, stopping)) {
            pending_bytes -= pending.front().Bytes();
            input = std::move(pending.front()); pending.pop_front();
          }
          no_pending = pending.empty(); stop = stopping; stop_at = end;
        }
        Check();
        if(request_audio_paint&&progress)progress(finished);
        if (input) {
#ifdef __APPLE__
          if(input->surface && (!native_direct||!input->nv12||input->painted)) {
            if(input->nv12)input->data=std::move(spare_pixels);
            if(!input->surface->Read(input->data))throw std::runtime_error("Native GPU surface readback failed");
            input->surface.reset();
          }
#endif
          std::vector<uint8_t> painted_pixels;
          if (!input->nv12 && options.worker_canvas_fit &&
              (input->width != options.source_width || input->height != options.source_height)) {
            std::vector<uint8_t> fitted;
            if (!SuiteCutFitCapture(input->data, input->width, input->height,
                options.source_width, options.source_height, fitted))
              throw std::runtime_error("Native worker canvas fitting failed");
            if(input->painted)painted_pixels=std::move(input->data);
            input->data = std::move(fitted);
          }
          if (!input->nv12 && (options.worker_i420 || options.worker_nv12)) {
            const size_t y = static_cast<size_t>(options.source_width) * options.source_height;
            auto planes=std::move(spare_pixels);planes.resize(y * 3 / 2);
            const int converted=options.worker_nv12?
              libyuv::ARGBToNV12Matrix(input->data.data(),options.source_width*4,planes.data(),options.source_width,planes.data()+y,options.source_width,&libyuv::kArgbH709Constants,options.source_width,options.source_height):
              libyuv::ARGBToI420Matrix(input->data.data(),options.source_width*4,planes.data(),options.source_width,planes.data()+y,options.source_width/2,planes.data()+y*5/4,options.source_width/2,&libyuv::kArgbH709Constants,options.source_width,options.source_height);
            if(converted)throw std::runtime_error("Native worker pixel conversion failed");
            if(input->painted && painted_pixels.empty())painted_pixels=std::move(input->data);
            input->data = std::move(planes);
          }
          if(input->painted) {
            if(input->nv12){
              painted_pixels.resize(size_t(input->width)*input->height*4);
              if(libyuv::NV12ToARGBMatrix(input->data.data(),input->width,input->data.data()+size_t(input->width)*input->height,input->width,painted_pixels.data(),input->width*4,&libyuv::kYuvH709Constants,input->width,input->height))throw std::runtime_error("Native cache pixel conversion failed");
            }
            if(painted_pixels.empty())painted_pixels=input->data;
            input->painted(std::move(painted_pixels));input->painted={};
          }
          if (!clocked || (options.hardware_h264 && !encoder_spawned)) frame = std::move(input);
          else { lookahead_bytes += input->data.size(); lookahead.push_back(std::move(*input)); }
        }
        if (!frame && ready(no_pending, stop)) {
          auto& front = lookahead.front();
          double timestamp = front.timestamp;
          if (front.sequence) for (const auto& next : lookahead) {
            if (next.sequence > front.sequence)
              timestamp = std::min(timestamp, next.timestamp -
                  (next.sequence-front.sequence) * 1000.0 / options.capture_frames_per_second);
          }
          timestamp = std::max({timestamp, front.timestamp-kCaptureLookahead.count(),
              options.start_timestamp_ms, last_frame_timestamp});
          finished.max_timestamp_correction_ms = std::max(finished.max_timestamp_correction_ms,
              front.timestamp-timestamp);
          front.timestamp = timestamp;
          lookahead_bytes -= front.data.size(); frame = std::move(front); lookahead.pop_front();
        }
        stop = stop && no_pending && lookahead.empty();
        {
          std::lock_guard lock(mutex);
          if (first >= 0 || frame) { packets.swap(audio); audio_bytes = 0; }
        }
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
#ifdef __APPLE__
            if(!encoder_spawned&&native_direct){
              native_encoder=std::make_unique<MacNativeEncoder>(files.Get("video.webm"),options.source_width,options.source_height,options.frames_per_second,options.audio?options.max_file_bytes/3:options.max_file_bytes,[this]{Check();});
              native_encoder->Write(frame->surface,frame->data,true);native_encoder->Drain();
              preparation_packets=1;encoder_spawned=true;prepared();continue;
            }
#endif
            if (!encoder_spawned) {
            const bool bounded_content=options.max_content_width!=0;
            const int fit_width=bounded_content?options.max_content_width:options.output_width;
            const int fit_height=bounded_content?options.max_content_height:options.output_height;
            const bool native_size = options.source_width == options.output_width &&
                options.source_height == options.output_height && !bounded_content;
            const std::string scale = options.dynamic_viewport || native_size ? "setsar=1" : "scale=" + std::to_string(fit_width) + ":" + std::to_string(fit_height) +
                ":force_original_aspect_ratio=decrease" + (bounded_content?":force_divisible_by=2":"") +
                ",pad=" + std::to_string(options.output_width) + ":" + std::to_string(options.output_height) + (options.top_left_padding ? ":0:0:black,setsar=1" : ":(ow-iw)/2:(oh-ih)/2:black,setsar=1");
            // Share the existing CPU budget with other active recordings and
            // leave capacity for CEF. Filters must not create an additional
            // hardware-sized thread pool for this sequential raw-video input.
            const auto threads = std::max<size_t>(1, AutomaticWorkerCount() /
                std::max(1u, active_pipelines.load()));
            std::vector<std::string> args{"-hide_banner", "-loglevel", "error", "-nostdin", "-filter_threads", "1", "-f", "rawvideo", "-pixel_format", input_format,
              "-video_size", std::to_string(options.source_width) + "x" + std::to_string(options.source_height), "-framerate", Decimal(options.frames_per_second), "-i", "pipe:0",
              "-an", "-vf", scale, "-c:v", "libvpx-vp9", "-crf", "18", "-b:v", "0", "-deadline", "realtime", "-cpu-used", "8", "-lag-in-frames", "0", "-row-mt", "1", "-threads", std::to_string(threads), "-pix_fmt", "yuv420p"};
            if (options.hardware_h264) {
              args = {"-hide_banner", "-loglevel", "error", "-nostdin", "-filter_threads", "1",
                "-probesize", "32", "-fpsprobesize", "0", "-analyzeduration", "0",
                "-f", "rawvideo", "-pixel_format", input_format,
                "-video_size", std::to_string(options.source_width) + "x" + std::to_string(options.source_height),
                "-framerate", Decimal(options.frames_per_second), "-i", "pipe:0", "-an", "-vf", scale,
                "-c:v", "h264_videotoolbox", "-allow_sw", "0", "-realtime", "1", "-flags", "+low_delay",
                "-prio_speed", "1", "-bf", "0", "-b:v", "24M", "-pix_fmt", options.worker_nv12?"nv12":"yuv420p",
                "-force_key_frames", "expr:lte(n,32)", "-progress", "pipe:2", "-stats_period", "0.05"};
            }
            if (encoded_i420) {
              // Raw CEF planes already contain BT.709 limited-range samples.
              // Mark the decoder input before scale negotiates its matrix;
              // output tags alone would reinterpret these bytes as BT.601.
              args.insert(std::find(args.begin(), args.end(), "-i"), {"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-color_range", "tv"});
              args.insert(args.end(), {"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-color_range", "tv"});
            }
            args.insert(args.end(), {"-f", options.hardware_h264 ? "mp4" : "webm", "-y", files.File("video.webm")});
            encoder.Spawn(options.launcher_path, options.ffmpeg_path, args, true, files.ChildFiles(),
                options.audio ? options.max_file_bytes / 3 : options.max_file_bytes);
            finished.physical_encoder_pid = encoder.pid;
            encoder_spawned = true;
            if (options.hardware_h264) {
              // Prepare VideoToolbox with a genuine paint in a private frame.
              // Start acknowledges only after hardware initialization and a
              // subsequent real paint. The warm-up packet is an isolated GOP
              // and is removed at final mux; it is never a reported frame.
              auto due = preparation_started + std::chrono::seconds(30);
              if (options.first_frame_deadline != Clock::time_point{}) due = std::min(due, options.first_frame_deadline);
              while (encoder.encoded_frames < 1) {
                if (Clock::now() >= due) throw std::runtime_error("Timeout preparing native hardware encoder");
                encoder.Read();
                if (encoder.encoded_frames >= 1) break;
                // Once the bounded preparation GOP is queued, wait for the
                // encoder's receipt rather than racing its progress pipe.
                if (preparation_packets >= 32) { Poll(encoder, false); continue; }
                Write(encoder, frame->data); ++preparation_packets;
                Poll(encoder, false);
              }
              prepared();
              continue;
            }
            }
            first = frame->timestamp;
            finished.first_timestamp_ms = first;
          }
          // Paint timestamps jitter around CFR boundaries. Ceil maps one
          // slightly-late paint forward, then puts its successor in the same
          // slot: an artificial hold/drop pair even with zero queue loss.
          // Assign paints to the nearest slot; only the terminal duration uses
          // ceil, because a partially occupied final slot must be retained.
          const double slot = std::max(0.0, frame->timestamp - first) * options.frames_per_second / 1000;
          uint64_t before = static_cast<uint64_t>(std::llround(slot));
          // Native matching-rate captures have a quantized microsecond clock.
          // Round each genuine interval, retaining bounded sub-slot phase
          // ambiguity instead of accumulating it until a distinct paint is
          // coalesced. Use the interval's slot count so a real missing capture
          // still inserts its CFR hold; faster-than-output inputs still merge.
          if(options.capture_timestamps_exact&&options.capture_frames_per_second==options.frames_per_second&&last_frame_timestamp>=0){
            const auto advance=static_cast<uint64_t>(std::llround(std::max(0.0,frame->timestamp-last_frame_timestamp)*options.frames_per_second/1000));
            const auto candidate=last_frame_slot+advance;
            if(std::abs(slot-static_cast<double>(candidate))<1-1e-7)before=candidate;
          }
          // Preserve an established cadence across phase jitter near a half
          // slot too. A bounded 3/4-slot hysteresis avoids oscillating between
          // duplicate and skipped slots when the initial paint arrived early.
          // Faster-than-output paints still coalesce; actual missing paints
          // and sustained clock drift leave this window and retain CFR holds.
          if (!clocked && !options.capture_timestamps_exact && last_frame_timestamp >= 0 &&
              (frame->timestamp - last_frame_timestamp) * options.frames_per_second / 1000 >= .5 &&
              std::abs(slot - static_cast<double>(last_frame_slot + 1)) <= .75)
            before = last_frame_slot + 1;
          // Lookahead removes scheduling lag; sub-frame phase noise around
          // nearest-slot boundaries still needs bounded quantization hysteresis.
          // Keep sub-slot phase noise ambiguous; one complete missing source
          // interval is sufficient evidence to insert a hold.
          if (clocked && options.capture_frames_per_second == options.frames_per_second &&
              frame->sequence && last_frame_timestamp >= 0 &&
              std::abs(slot-static_cast<double>(last_frame_slot+1)) < 1-1e-7)
            before = last_frame_slot+1;
          last_frame_slot = before; last_frame_timestamp = frame->timestamp;
          while (has_held() && finished.output_frames < before) {
            write_held();
            if (emitted_generation == held_generation) ++finished.held_frames;
            emitted_generation = held_generation;
            ++finished.output_frames;
            Publish(finished);
            DiskBound(files);
          }
          // A newer paint replacing an image that never occupied an output
          // slot is a resampling discard, independently of queue overload.
          if (has_held() && emitted_generation != held_generation) ++finished.coalesced_frames;
          if(options.worker_i420||options.worker_nv12)spare_pixels=std::move(held);
          held_surface=std::move(frame->surface);
          held = std::move(frame->data); ++held_generation;
          if (!finished.output_frames) {
            write_held(); ++finished.output_frames; emitted_generation = held_generation;
            Check(); encoder_ready = true; just_started = true;
            { std::lock_guard lock(mutex); result.first_timestamp_ms = first; result.physical_encoder_pid = native_direct?0:encoder.pid; }
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
        // Advance static native recordings while capture runs. A bounded
        // half-second playout margin absorbs late capture callbacks; finalizing
        // never has to encode an entire static recording in one burst.
        if(native_direct && first>=0 && !frame && no_pending && !stop && has_held()){
          double live=0;{std::lock_guard lock(mutex);if(!paused)live=options.start_timestamp_ms+std::chrono::duration<double,std::milli>(Clock::now()-admitted_at).count()-excluded-first-500;if(stop_requested)live=std::min(live,end-first);}
          const auto count=live>0?MediaSlotCount(live,options.frames_per_second):0;
          while(finished.output_frames<count){write_held();if(emitted_generation==held_generation)++finished.held_frames;emitted_generation=held_generation;++finished.output_frames;DiskBound(files);}
        }
        finished.audio_output_frames = options.audio ? audio_frame : 0;
        Publish(finished);
        if (just_started && started) started(first);
        if (progress) progress(finished);
        if (stop) {
          if (first < 0 || !has_held()) throw std::runtime_error("Native recording received no video frames");
          finished.duration_ms = std::max(0.0, stop_at - first);
          const uint64_t count = std::max<uint64_t>(1, MediaSlotCount(finished.duration_ms, options.frames_per_second));
          while (finished.output_frames < count) {
            write_held();
            if (emitted_generation == held_generation) ++finished.held_frames;
            emitted_generation = held_generation;
            ++finished.output_frames;
            Publish(finished);
            DiskBound(files);
          }
          // Nearest-slot assignment or bounded phase jitter can place the
          // final accepted paint just beyond the rounded stop slot. Retain
          // that genuine image rather than silently discarding it at stop.
          if (emitted_generation != held_generation) {
            write_held(); ++finished.output_frames;
            emitted_generation = held_generation;
            Publish(finished); DiskBound(files);
          }
          finished.encoded_duration_ms = finished.output_frames * 1000.0 / options.frames_per_second;
          // The CFR timeline rounds the stop boundary up to a complete slot.
          // Muxing to the unrounded wall-clock boundary can discard the last
          // packet after WebM/Opus timestamp rounding. Trim/pad PCM separately
          // and copy every video packet; reported frames then remain physical.
          const double mux_duration_ms = finished.encoded_duration_ms;
          const uint64_t target_audio = static_cast<uint64_t>(std::ceil(mux_duration_ms * 48));
          if (options.audio && audio_frame < target_audio) {
            Silence(pcm.Get(), target_audio - audio_frame, files);
            finished.audio_gap_frames += target_audio - audio_frame; audio_frame = target_audio;
          }
          // Preserve captured tail PCM in the bounded private spool; the mux
          // clips it to the encoded video boundary instead of pretending none
          // was captured. Audio overlap/gap counters disclose alignment.
          finished.audio_output_frames = options.audio ? std::min(audio_frame, target_audio) : 0;
          if (pcm.Get() >= 0 && fsync(pcm.Get()) != 0) throw std::runtime_error("Could not flush native audio spool");
          pcm.Reset();
#ifdef __APPLE__
          if(native_encoder)native_encoder->Drain();else
#endif
          Wait(encoder, files);
          finished.physical_encoder_pid = 0; finished.diagnostics = encoder.Messages();
          DiskBound(files);
          const char* final_file = "video.webm";
          if (options.audio || options.hardware_h264) {
            Child mux;
            std::vector<std::string> args{"-hide_banner", "-loglevel", "error", "-nostdin"};
            if (options.hardware_h264 && !native_direct) args.insert(args.end(), {"-ss", Decimal((std::ceil(preparation_packets * 1000000 / options.frames_per_second) + 1) / 1000000)});
            if(native_direct)args.insert(args.end(),{"-f","h264","-r",Decimal(options.frames_per_second)});
            args.insert(args.end(), {"-i", files.File("video.webm")});
            if (options.audio) args.insert(args.end(), {"-f", "s16le", "-ar", "48000", "-ac", "2", "-i", files.File("audio.pcm")});
            args.insert(args.end(), {"-map", "0:v:0", "-c:v", "copy"});
            if (options.audio) args.insert(args.end(), {"-map", "1:a:0", "-af", "atrim=end_sample=" + std::to_string(target_audio),
                "-c:a", options.hardware_h264 ? "aac" : "libopus", "-b:a", "160k"});
            args.insert(args.end(), {"-f", options.hardware_h264 ? "mp4" : "webm", "-y", files.File("final.webm")});
            mux.Spawn(options.launcher_path, options.ffmpeg_path, args, false, files.ChildFiles(), options.max_file_bytes / 3);
            Wait(mux, files); finished.diagnostics += mux.Messages(); final_file = "final.webm";
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
      finished.preparation_frames=std::max(finished.preparation_frames,result.preparation_frames);
      finished.input_frames = result.input_frames;
      finished.gpu_input_frames = result.gpu_input_frames;
      finished.gpu_nv12_frames = result.gpu_nv12_frames;
      finished.superseded_frames = result.superseded_frames;
      finished.audio_input_frames = result.audio_input_frames;
      if (finished.diagnostics.size() > 8192) finished.diagnostics.erase(0, finished.diagnostics.size() - 8192);
      result = finished;
      pending.clear(); pending_bytes = 0; audio.clear(); audio_bytes = 0;
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
  if (std::isfinite(timestamp) && timestamp>=0 && timestamp<impl_->options.start_timestamp_ms) return;
  if (impl_->options.worker_canvas_fit) {
    impl_->Cancel("Native deferred recording requires BGRA canvas paints"); return;
  }
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
    if (impl_->settled || impl_->canceled || impl_->stopping || (impl_->paused&&!impl_->options.capture_timestamps_exact)) return;
    if(impl_->options.capture_timestamps_exact){
      impl_->observed_video_ms=std::max(impl_->observed_video_ms,timestamp);impl_->DrainState();impl_->PruneNative();
      double mapped;if(!impl_->NativeTime(timestamp,mapped)){impl_->changed.notify_one();return;}timestamp=mapped;
    }else timestamp-=impl_->excluded;
    if (timestamp < impl_->latest_frame) return;
    if(impl_->AudioPreroll())return;
    if(impl_->encoder_prepared)impl_->video_admitted=true;
    ++impl_->result.input_frames;
    impl_->latest_frame = timestamp;
    Impl::Sample sample{std::move(data), timestamp};
    sample.sequence = ++impl_->capture_sequence;
    impl_->Enqueue(std::move(sample));
  }
  impl_->changed.notify_one();
}
bool MediaPipeline::NativeCanvasFrame(std::shared_ptr<MacCaptureFrame> surface,double timestamp,
    std::function<void(std::vector<uint8_t>)> painted) {
#ifdef __APPLE__
  if(!surface || !impl_->options.worker_canvas_fit)return false;
  if(std::isfinite(timestamp)&&timestamp>=0&&timestamp<impl_->options.start_timestamp_ms)return false;
  if(!std::isfinite(timestamp)||timestamp<0||timestamp-impl_->options.start_timestamp_ms>kMaxDurationMs){impl_->Cancel("Invalid native GPU capture time");return false;}
  Impl::Sample sample;sample.width=surface->width;sample.height=surface->height;sample.nv12=surface->nv12;
  sample.surface=std::move(surface);sample.painted=std::move(painted);
  {
    std::lock_guard lock(impl_->mutex);
    if(impl_->settled||impl_->canceled||impl_->stopping||(impl_->paused&&!impl_->options.capture_timestamps_exact))return false;
    impl_->observed_video_ms=std::max(impl_->observed_video_ms,timestamp);impl_->DrainState();impl_->PruneNative();
    if(!impl_->NativeTime(timestamp,sample.timestamp)){impl_->changed.notify_one();return false;}
    if(sample.timestamp<impl_->latest_frame)return false;
    if(impl_->AudioPreroll())return true;
    if(impl_->encoder_prepared)impl_->video_admitted=true;
    impl_->latest_frame=sample.timestamp;sample.sequence=++impl_->capture_sequence;
    ++impl_->result.input_frames;++impl_->result.gpu_input_frames;if(sample.nv12)++impl_->result.gpu_nv12_frames;impl_->Enqueue(std::move(sample));
  }
  impl_->changed.notify_one();return true;
#else
  (void)surface;(void)timestamp;(void)painted;return false;
#endif
}
bool MediaPipeline::WantsFrames() const {
  std::lock_guard lock(impl_->mutex);
  if(impl_->settled||impl_->canceled||impl_->stopping)return false;
  if(impl_->paused||impl_->stop_requested)return impl_->options.capture_timestamps_exact&&impl_->observed_video_ms<=impl_->capture_cutoff_ms&&Clock::now()<impl_->capture_drain_until;
  return true;
}
bool MediaPipeline::UsesCanvasFrames() const { return impl_->options.worker_canvas_fit; }
void MediaPipeline::CanvasFrame(const std::vector<uint8_t>& bgra, int width, int height,
    double timestamp, uint64_t capture_sequence) {
  if (std::isfinite(timestamp) && timestamp>=0 && timestamp<impl_->options.start_timestamp_ms) return;
  if (!impl_->options.worker_canvas_fit) return;
  if (width < 1 || width > 3840 || height < 1 || height > 2160 ||
      bgra.size() != static_cast<size_t>(width) * height * 4 ||
      !std::isfinite(timestamp) || timestamp < impl_->options.start_timestamp_ms ||
      timestamp - impl_->options.start_timestamp_ms > kMaxDurationMs) {
    impl_->Cancel("Malformed native BGRA canvas paint or capture timestamp"); return;
  }
  // The CEF buffer is temporary. Own its snapshot before returning, but do
  // the large copy outside the audio/encoder state mutex.
  Impl::Sample sample{bgra, timestamp, width, height};
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled || impl_->canceled || impl_->stopping || (impl_->paused&&!impl_->options.capture_timestamps_exact)) return;
    if(impl_->options.capture_timestamps_exact){
      impl_->observed_video_ms=std::max(impl_->observed_video_ms,timestamp);impl_->DrainState();impl_->PruneNative();
      if(!impl_->NativeTime(timestamp,sample.timestamp)){impl_->changed.notify_one();return;}
    }else sample.timestamp-=impl_->excluded;
    if (sample.timestamp < impl_->latest_frame) return;
    // Number admitted genuine view paints, excluding paused/offline periods.
    // Queue retirement happens after numbering and therefore retains holes.
    sample.sequence = capture_sequence ? ++impl_->capture_sequence : 0;
    if(impl_->AudioPreroll())return;
    if(impl_->encoder_prepared)impl_->video_admitted=true;
    ++impl_->result.input_frames; impl_->latest_frame = sample.timestamp;
    impl_->Enqueue(std::move(sample));
  }
  impl_->changed.notify_one();
}
void MediaPipeline::ViewportFrame(const std::vector<uint8_t>& bgra, int width, int height,
    double css_width, double css_height, double timestamp) {
  if (std::isfinite(timestamp) && timestamp>=0 && timestamp<impl_->options.start_timestamp_ms) return;
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
    if (impl_->settled || impl_->canceled || impl_->stopping || (impl_->paused&&!impl_->options.capture_timestamps_exact)) return;
    if(impl_->options.capture_timestamps_exact){
      impl_->observed_video_ms=std::max(impl_->observed_video_ms,timestamp);impl_->DrainState();impl_->PruneNative();
      double mapped;if(!impl_->NativeTime(timestamp,mapped)){impl_->changed.notify_one();return;}timestamp=mapped;
    }else timestamp-=impl_->excluded;
    if (timestamp < impl_->latest_frame) return;
    ++impl_->result.input_frames;
    impl_->latest_frame = timestamp;
    impl_->Enqueue(Impl::Sample{bgra, timestamp, width, height, css_width, css_height});
  }
  impl_->changed.notify_one();
}
void MediaPipeline::Audio(std::vector<uint8_t> bytes, double timestamp) {
  if (!impl_->options.audio) return;
  if (bytes.empty() || bytes.size() % 4 || bytes.size() > 4096 * 4 || !std::isfinite(timestamp) || timestamp < 0 ||
      timestamp - impl_->options.start_timestamp_ms > kMaxDurationMs) {
    impl_->Cancel("Malformed native stereo PCM or capture timestamp"); return;
  }
  bool overflow = false,request_audio_paint=false;
  MediaPipelineResult notice;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled || impl_->canceled || impl_->stopping || (impl_->paused&&!impl_->options.capture_timestamps_exact)) return;
    if (timestamp < impl_->options.start_timestamp_ms) return;
    if(!impl_->audio_seen){
      impl_->audio_seen=true;
      if(impl_->options.capture_timestamps_exact&&impl_->encoder_prepared&&!impl_->video_admitted){
        impl_->capture_fence_ms=std::max(impl_->capture_fence_ms,timestamp);impl_->audio_gate_notified=true;
        request_audio_paint=true;notice=impl_->result;
      }
    }
    if(impl_->options.capture_timestamps_exact){
      const auto frames=bytes.size()/4;
      impl_->observed_audio_end_ms=std::max(impl_->observed_audio_end_ms,timestamp+frames/48.0);impl_->DrainState();impl_->PruneNative();
      std::vector<Impl::Sample> chunks;Impl::Sample chunk;double previous=-1;
      for(size_t i=0;i<frames;++i){double mapped;const double raw=timestamp+i/48.0;
        const bool accept=raw<impl_->capture_cutoff_ms&&impl_->NativeTime(raw,mapped);
        if(!accept){if(!chunk.data.empty()){chunks.push_back(std::move(chunk));chunk={};}previous=-1;continue;}
        if(chunk.data.empty())chunk.timestamp=mapped;
        else if(std::abs(mapped-previous-1.0/48)>.001){chunks.push_back(std::move(chunk));chunk={};chunk.timestamp=mapped;}
        chunk.data.insert(chunk.data.end(),bytes.data()+i*4,bytes.data()+i*4+4);previous=mapped;
      }
      if(!chunk.data.empty())chunks.push_back(std::move(chunk));
      size_t total=0;for(const auto& item:chunks)total+=item.data.size();
      if(impl_->audio_bytes+total>kAudioQueueBytes)overflow=true;
      else for(auto& item:chunks){impl_->result.audio_input_frames+=item.data.size()/4;impl_->audio_bytes+=item.data.size();impl_->audio.push_back(std::move(item));}
    }else{
      timestamp-=impl_->excluded;
      if(impl_->audio_bytes+bytes.size()>kAudioQueueBytes)overflow=true;
      else{impl_->result.audio_input_frames+=bytes.size()/4;impl_->audio_bytes+=bytes.size();impl_->audio.push_back(Impl::Sample{std::move(bytes),timestamp});}
    }
  }
  if(request_audio_paint&&impl_->progress)impl_->progress(notice);
  if (overflow) impl_->Cancel("Native recording audio queue exceeded 30 seconds");
  else impl_->changed.notify_one();
}
void MediaPipeline::Pause(double timestamp) {
  if (!std::isfinite(timestamp)) { impl_->Cancel("Invalid native recording pause clock"); return; }
  std::lock_guard lock(impl_->mutex);
  if (impl_->settled || impl_->stopping || impl_->stop_requested || impl_->paused) return;
  if (timestamp < impl_->options.start_timestamp_ms || timestamp - impl_->excluded < impl_->latest_frame) {
    impl_->cancel_reason = "Native recording pause clock moved backwards"; impl_->canceled = true; impl_->changed.notify_one(); return;
  }
  impl_->paused = true; impl_->paused_at = timestamp;
  if(impl_->options.capture_timestamps_exact){impl_->capture_cutoff_ms=timestamp;impl_->capture_drain_until=Clock::now()+std::chrono::milliseconds(500);}
  impl_->changed.notify_one();
}
void MediaPipeline::Resume(double timestamp) {
  if (!std::isfinite(timestamp)) { impl_->Cancel("Invalid native recording resume clock"); return; }
  std::lock_guard lock(impl_->mutex);
  if (!impl_->paused || impl_->settled || impl_->stopping || impl_->stop_requested) return;
  if (timestamp < impl_->paused_at) {
    impl_->cancel_reason = "Native recording resume clock moved backwards"; impl_->canceled = true; impl_->changed.notify_one(); return;
  }
  if(impl_->options.capture_timestamps_exact){
    impl_->PruneNative();if(impl_->native_pauses.size()>=256)throw std::runtime_error("Too many native pause transitions awaiting capture");
    impl_->native_pauses.push_back({impl_->paused_at,timestamp});impl_->capture_cutoff_ms=std::numeric_limits<double>::infinity();
  }
  impl_->excluded += timestamp - impl_->paused_at; impl_->paused = false;
  impl_->changed.notify_one();
}
void MediaPipeline::Stop(double timestamp, Clock::time_point deadline) {
  if (!std::isfinite(timestamp) || timestamp < impl_->options.start_timestamp_ms ||
      timestamp - impl_->options.start_timestamp_ms > kMaxDurationMs) {
    impl_->Cancel("Invalid native recording stop clock"); return;
  }
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->settled) return;
    if(!impl_->stopping&&!impl_->stop_requested){
    impl_->end = (impl_->paused ? impl_->paused_at : timestamp) - impl_->excluded;
    if (impl_->end < impl_->latest_frame) {
      impl_->cancel_reason = "Native recording stop clock moved backwards"; impl_->canceled = true;
    }
    if(impl_->options.capture_timestamps_exact){
      impl_->stop_requested=true;impl_->capture_cutoff_ms=impl_->paused?impl_->paused_at:timestamp;
      impl_->capture_drain_until=Clock::now()+std::chrono::milliseconds(500);impl_->DrainState();
    }else impl_->stopping = true;
    }
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
