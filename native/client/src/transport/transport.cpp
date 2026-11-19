// SPDX-License-Identifier: MIT
#include "suitecut/transport.hpp"
#include "suitecut/json.hpp"
#include "decoder.hpp"
#include "cdp_backend.hpp"
#include "bidi_backend.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace suitecut {
namespace {
using Clock = std::chrono::steady_clock;

std::runtime_error failure(const std::string& message) { return std::runtime_error(message); }
bool has_nul(const std::string& value) { return value.find('\0') != std::string::npos; }
void duration_valid(std::chrono::milliseconds value, const char* name, bool zero = false) {
  if (value.count() < (zero ? 0 : 1) || value > std::chrono::hours(24))
    throw failure(std::string(name) + " must be positive and at most 24 hours");
}
void validate(const LaunchOptions& options) {
  const bool attaching = (options.backend == BrowserBackend::chromium || options.backend == BrowserBackend::firefox) && !options.cdp_endpoint.empty();
  if (!attaching && (options.executable.empty() || has_nul(options.executable) ||
      !std::filesystem::path(options.executable).is_absolute()))
    throw failure("Engine executable must be an absolute path without null bytes");
  if (!options.profile.empty() && (has_nul(options.profile) ||
      !std::filesystem::path(options.profile).is_absolute()))
    throw failure("Profile must be an absolute path without null bytes");
  // No suitecut viewport, FPS or storage cap; the engine and CEF enforce theirs.
  if (options.width < 2 || options.height < 2) throw failure("Viewport must be at least 2 by 2");
  if (options.scale != 1 && options.scale != 2) throw failure("Native scale must be 1 or 2");
  if (!std::isfinite(options.frames_per_second) || options.frames_per_second < 1 ||
      options.frames_per_second > INT_MAX || std::floor(options.frames_per_second) != options.frames_per_second)
    throw failure("Native capture FPS must be a positive integer");
  duration_valid(options.startup_timeout, "Startup timeout");
  duration_valid(options.shutdown_timeout, "Shutdown timeout");
  for (const auto& argument : options.arguments)
    if (has_nul(argument) || argument == "--" || argument.starts_with("--suitecut-"))
      throw failure("Engine arguments cannot override managed SuiteCut switches");
}

#ifndef _WIN32
void close_fd(int& fd) noexcept { if (fd >= 0) { ::close(fd); fd = -1; } }
void nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    throw failure("Could not configure native transport pipe");
}
std::array<int, 2> pipe_pair() {
  std::array<int, 2> result{-1, -1};
#if defined(__linux__)
  if (pipe2(result.data(), O_CLOEXEC) < 0) throw failure("Could not create native transport pipe");
#else
  if (pipe(result.data()) < 0) throw failure("Could not create native transport pipe");
#endif
  try {
    for (auto& fd : result) {
      if (fd < 3) {
        const int replacement = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) throw failure("Could not duplicate native transport pipe");
        ::close(fd); fd = replacement;
      }
      if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) throw failure("Could not protect transport descriptor");
#ifdef F_SETNOSIGPIPE
      if (fcntl(fd, F_SETNOSIGPIPE, 1) < 0) throw failure("Could not protect transport pipe writes");
#endif
    }
  } catch (...) { for (auto& fd : result) close_fd(fd); throw; }
  return result;
}

struct ProcessIdentity {
  pid_t group = 0, session = 0;
  unsigned long long started = 0;
  char state = 0;
};
std::optional<ProcessIdentity> process_identity(pid_t pid) {
#if defined(__linux__)
  std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
  std::string text;
  if (!std::getline(file, text)) return std::nullopt;
  const auto end = text.rfind(") ");
  if (end == std::string::npos) return std::nullopt;
  std::istringstream fields(text.substr(end + 2));
  ProcessIdentity result;
  long long parent = 0;
  if (!(fields >> result.state >> parent >> result.group >> result.session)) return std::nullopt;
  std::string ignored;
  for (int field = 7; field <= 21; ++field) if (!(fields >> ignored)) return std::nullopt;
  if (!(fields >> result.started)) return std::nullopt;
  return result;
#else
  (void)pid;
  return std::nullopt;
#endif
}
#endif
} // namespace

struct Transport::Impl {
  LaunchOptions options;
  std::unique_ptr<detail::CdpBackend> cdp;
  std::unique_ptr<detail::BidiBackend> bidi;
  mutable std::mutex mutex;
  std::mutex close_mutex;
  std::condition_variable changed;
  struct Pending { std::promise<Packet> promise; Clock::time_point deadline; int page = 1; };
  struct Outgoing { int id; std::string line; std::size_t offset = 0; };
  std::map<int, Pending> pending;
  // Receipt controls must progress even when every public request slot is in
  // use. They run on the existing I/O thread outside the caller's max_pending.
  struct BodyReceipt { Clock::time_point deadline; std::string scope; };
  std::map<int, BodyReceipt> body_receipts;
  struct Body { std::size_t total, offset; Clock::time_point deadline; bool dropped; Json identity; };
  std::map<std::string, Body> bodies;
  std::deque<Outgoing> outgoing;
  struct Event { Packet packet; bool disposable; };
  std::deque<Event> events;
  std::map<int, Packet> page_ready;
  ClientStats counters;
  int next_id = 0;
  bool closing = false, finished = false, ready_seen = false;
  Packet ready_packet;
  std::exception_ptr terminal;
  const std::exception_ptr closed_error = std::make_exception_ptr(failure("Native transport closed"));
  std::string stderr_tail, profile;
  bool owned_profile = false;
  Clock::time_point close_started{};
  std::thread io;
#ifndef _WIN32
  pid_t pid = -1;
  std::optional<ProcessIdentity> identity;
  int input = -1, output = -1, error_output = -1, wake_read = -1, wake_write = -1;
  bool child_exited = false, sent_term = false, sent_kill = false;
  Clock::time_point exited_at{};
#endif

  explicit Impl(LaunchOptions value) : options(std::move(value)) {}
  ~Impl() {
#ifndef _WIN32
    for (int* fd : {&input, &output, &error_output, &wake_read, &wake_write}) close_fd(*fd);
#endif
    if (owned_profile) { std::error_code ignored; std::filesystem::remove_all(profile, ignored); }
  }
  int allocate_id() {
    // Never reuse IDs. Therefore arbitrarily late replies need no growing
    // tombstone set, and can never complete a later caller's request.
    if (next_id == INT_MAX) throw failure("Native request ID space exhausted; relaunch the engine");
    return ++next_id;
  }
  void wake() noexcept {
#ifndef _WIN32
    const char value = 1;
    if (wake_write >= 0) while (::write(wake_write, &value, 1) < 0 && errno == EINTR) {}
#endif
  }
  void begin_close_locked() {
    if (closing) return;
    closing = true; close_started = Clock::now();
    for (auto& [id, item] : pending) {
      (void)id;
      item.promise.set_exception(terminal ? terminal : closed_error);
    }
    pending.clear(); counters.pending = 0;
    body_receipts.clear(); bodies.clear();
    outgoing.clear();
    changed.notify_all();
  }
  void fail(std::exception_ptr error) {
    std::lock_guard lock(mutex);
    if (!terminal) terminal = std::move(error);
    begin_close_locked(); wake(); changed.notify_all();
  }
  void enqueue_locked(int id, std::string line) {
    outgoing.push_back({id, std::move(line), 0});
  }
  void cancel_locked(int target, const std::string& reason) {
    const auto found = pending.find(target);
    if (found == pending.end()) return;
    const int page = found->second.page;
    found->second.promise.set_exception(std::make_exception_ptr(failure(reason)));
    pending.erase(found); counters.pending = pending.size();
    // Unsent requests can be removed outright. A partial JSON line must finish
    // before its cancellation, otherwise later commands would become malformed.
    for (auto it = outgoing.begin(); it != outgoing.end(); ++it) {
      if (it->id == target && it->offset == 0) {
        outgoing.erase(it); return;
      }
    }
    const int id = allocate_id();
    Json message = Json::object{{"version", 1}, {"id", id}, {"command", "cancel"},
                                {"pageId", page}, {"target", target}};
    enqueue_locked(id, message.dump() + "\n");
    ++counters.commands;
  }
  void acknowledge_body(Json fields, const char* stage) {
    const int id = allocate_id();
    if (fields.at("command") == Json("context")) fields["params"]["stage"] = stage;
    else fields["stage"] = stage;
    fields["version"] = 1; fields["id"] = id; fields["timeoutMs"] = 30000;
    const auto scope = fields.at("command") == Json("context")
      ? "context:" + std::to_string(fields.at("params").at("contextId").as_int()) + ":" + fields.at("params").at("owner").as_string() + ":"
      : "page:" + std::to_string(fields.at("pageId").as_int()) + ":";
    body_receipts.emplace(id, BodyReceipt{Clock::now() + std::chrono::seconds(30),scope});
    enqueue_locked(id, fields.dump() + "\n"); ++counters.commands;
  }
  void forget_bodies(const std::string& scope) {
    for (auto it=bodies.begin();it!=bodies.end();) {
      if (it->first.starts_with(scope)) it=bodies.erase(it);
      else ++it;
    }
    // Closure retires outstanding receipts too. Already-written controls can
    // receive a normal "page closed" rejection; their IDs cannot be reused.
    for (auto it=body_receipts.begin();it!=body_receipts.end();)
      if(it->second.scope.starts_with(scope))it=body_receipts.erase(it);else ++it;
  }
  void queue_event_locked(Packet packet, bool disposable) {
    const std::size_t bytes = packet.header.size() + packet.payload.size();
    // No suitecut queue cap. Only a caller-set max_event_bytes (0 = no limit)
    // bounds the queue; then disposable media is lost before a dialog/binding/
    // network event. Byte accounting includes the header.
    const auto over = [&] { return options.max_event_bytes && bytes > options.max_event_bytes - counters.queued_event_bytes; };
    while (over()) {
      const auto old = std::find_if(events.begin(), events.end(), [](const Event& event) { return event.disposable; });
      if (old == events.end()) break;
      counters.queued_event_bytes -= old->packet.header.size() + old->packet.payload.size();
      events.erase(old); ++counters.dropped_events;
    }
    if (over()) {
      if (disposable) { ++counters.dropped_events; return; }
      throw failure("Critical native event queue overflow; poll events before continuing");
    }
    events.push_back({std::move(packet), disposable}); counters.queued_event_bytes += bytes; changed.notify_all();
  }
  // A bad or unexpected body costs only that body. The client drops it, hands
  // raw consumers a canceled chunk so they discard what they already hold, and
  // ignores its later chunks; the engine's own reservation deadline releases
  // its side. Only a garbled stream (framing, header JSON) closes the transport.
  void drop_body_locked(const std::string& scope, Json identity) {
    ++counters.dropped_bodies;
    bodies.insert_or_assign(scope, Body{0,0,Clock::now()+std::chrono::seconds(30),true,Json()});
    identity["version"]=1; identity["offset"]=0; identity["totalBytes"]=0; identity["bytes"]=0;
    identity["canceled"]=true; identity["dropped"]=true;
    queue_event_locked(Packet{identity.dump(),{}}, false);
  }
  std::optional<Json> receive_body(const Json& header, std::size_t length, bool& keep) {
    keep = false;
    const auto positive = [&](const char* field) -> std::int64_t {
      if (!header.contains(field) || !header.at(field).is_integer() ||
          header.at(field).as_int() < 1 || header.at(field).as_int() > INT_MAX) return 0;
      return header.at(field).as_int();
    };
    // Without a valid identity there is no body to drop or acknowledge.
    if (!header.contains("bodyId") || !header.at("bodyId").is_string()) { ++counters.dropped_bodies; return std::nullopt; }
    const auto& token = header.at("bodyId").as_string();
    std::uint64_t number = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), number);
    if (token.empty() || token.size() > 20 || token.front() == '0' || parsed.ec != std::errc{} ||
        parsed.ptr != token.data() + token.size() || !number) { ++counters.dropped_bodies; return std::nullopt; }
    Json control, identity; std::string scope;
    if (header.at("type") == Json("contextnetworkbody")) {
      const auto context = positive("contextId");
      const bool owned = header.contains("owner") && header.at("owner").is_string();
      const auto owner = owned ? header.at("owner").as_string() : std::string();
      if (!context || !owner.starts_with("broker:") || owner.size() < 8 ||
          owner[7] < '1' || owner[7] > '9' ||
          !std::all_of(owner.begin()+7, owner.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        ++counters.dropped_bodies; return std::nullopt;
      }
      scope = "context:" + std::to_string(context) + ":" + owner + ":" + token;
      control = Json{{"command","context"},{"pageId",1},{"action","workerNetworkBody"},
                     {"params",Json{{"contextId",context},{"owner",owner},{"bodyId",token}}}};
      identity = Json{{"type","contextnetworkbody"},{"contextId",context},{"owner",owner},{"bodyId",token}};
    } else {
      const auto page = positive("pageId");
      if (!page) { ++counters.dropped_bodies; return std::nullopt; }
      scope = "page:" + std::to_string(page) + ":" + token;
      control = Json{{"command","networkBody"},{"pageId",page},{"bodyId",token}};
      identity = Json{{"type","networkbody"},{"pageId",page},{"bodyId",token}};
    }
    auto found = bodies.find(scope);
    const auto integral = [&](const char* name) {
      return header.contains(name) && header.at(name).is_integer() && header.at(name).as_int() >= 0;
    };
    const bool shaped = integral("offset") && integral("totalBytes") &&
      (!header.contains("canceled") || header.at("canceled").is_bool());
    const auto offset = shaped ? static_cast<std::size_t>(header.at("offset").as_int()) : 0;
    const auto total = shaped ? static_cast<std::size_t>(header.at("totalBytes").as_int()) : 0;
    const bool canceled = shaped && header.contains("canceled") && header.at("canceled").as_bool();
    if (found != bodies.end() && found->second.dropped) {
      // Later chunks of a dropped body; its last or canceled chunk retires it.
      if (canceled || (shaped && offset <= total && length == total-offset)) bodies.erase(found);
      return std::nullopt;
    }
    if (canceled && !offset && !total && !length) {
      if (found != bodies.end()) bodies.erase(found);
      keep = true; return std::nullopt;
    }
    if (!shaped || canceled || offset > total || length > total-offset || (!length && total) ||
        (found == bodies.end() ? offset != 0 : found->second.total != total || found->second.offset != offset)) {
      drop_body_locked(scope, std::move(identity)); return std::nullopt;
    }
    if (found == bodies.end())
      found = bodies.emplace(scope, Body{total,0,Clock::now()+std::chrono::seconds(30),false,std::move(identity)}).first;
    found->second.offset += length; keep = true;
    if (found->second.offset != total) return std::nullopt;
    bodies.erase(found); return control;
  }
  void receive(Packet packet) {
    const auto header = Json::parse(packet.header);
    if (!header.is_object() || !header.contains("version") || header.at("version").as_int() != 1)
      throw failure("Unsupported native protocol version");
    const auto type = header.at("type").as_string();
    std::lock_guard lock(mutex);
    if (type == "response") {
      const auto number = header.at("id").as_int();
      if (number < 1 || number > next_id) throw failure("Invalid native response ID");
      ++counters.responses;
      if (body_receipts.erase(static_cast<int>(number))) {
        // The body was already delivered; a rejected receipt (a page that
        // closed meanwhile) only means the engine released it itself.
        if (!header.at("ok").as_bool()) ++counters.dropped_bodies;
        return;
      }
      const auto found = pending.find(static_cast<int>(number));
      if (found == pending.end()) return;
      // Native command failure is a valid protocol response. Raw callers need
      // its error header and structured payload; only transport failures reject.
      found->second.promise.set_value(std::move(packet));
      pending.erase(found); counters.pending = pending.size(); changed.notify_all(); return;
    }
    if (type == "error") throw failure(header.contains("error") ? header.at("error").as_string() : "Native engine failed");
    if (type == "ready") {
      const auto page = header.contains("pageId") ? header.at("pageId").as_int() : 1;
      if (page < 1 || page > INT_MAX) throw failure("Invalid ready page identity");
      page_ready.insert_or_assign(static_cast<int>(page), packet);
      if (!ready_seen && page == 1) {
        ready_packet = std::move(packet); ready_seen = true; changed.notify_all(); return;
      }
      changed.notify_all();
    }
    if (type == "context" && !packet.payload.empty()) {
      const auto event = Json::parse(std::string_view(reinterpret_cast<const char*>(packet.payload.data()), packet.payload.size()));
      if (event.is_object() && event.contains("event") && event.at("event").is_string() &&
          event.at("event").as_string() == "page-closed" && event.contains("pageId")) {
        const auto page = event.at("pageId").as_int();
        forget_bodies("page:"+std::to_string(page)+":");
        if (page > 0 && page <= INT_MAX && page_ready.erase(static_cast<int>(page))) changed.notify_all();
      }
      if(event.is_object()&&event.contains("event")&&event.at("event")==Json("context-closed")&&event.contains("contextId"))
        forget_bodies("context:"+std::to_string(event.at("contextId").as_int())+":");
    }
    if (type == "contextworker" && !packet.payload.empty()) {
      const auto info=Json::parse(std::string_view(reinterpret_cast<const char*>(packet.payload.data()),packet.payload.size()));
      if(info.is_object()&&info.contains("contextId")&&info.contains("workerEvent")) {
        const auto& event=info.at("workerEvent");
        if(event.is_object()&&event.contains("event")&&(event.at("event")==Json("close")||event.at("event")==Json("channelclosed"))&&event.contains("worker")&&event.at("worker").contains("id"))
          forget_bodies("context:"+std::to_string(info.at("contextId").as_int())+":"+event.at("worker").at("id").as_string()+":");
      }
    }
    if (closing) return;
    std::optional<Json> body_control;
    if (type == "networkbody" || type == "contextnetworkbody") {
      bool keep = false;
      body_control = receive_body(header, packet.payload.size(), keep);
      if (!keep) return;
    }
    queue_event_locked(std::move(packet), type == "frame" || type == "audio");
    if (body_control) {
      // All exact bytes now belong to the event queue (or have already
      // been returned by poll). Neither receipt waits for application polling.
      // Preserve the original chunks for raw consumers; release only the
      // engine's duplicate body reservation after safe local admission.
      acknowledge_body(*body_control, "received");
      acknowledge_body(std::move(*body_control), "taken");
      wake();
    }
  }

#ifndef _WIN32
  void spawn_child() {
    if (access(options.executable.c_str(), X_OK) != 0) throw failure("Engine executable is not executable: " + options.executable);
    if (options.profile.empty()) {
      auto name = (std::filesystem::absolute(std::filesystem::temp_directory_path()) / "suitecut-client-XXXXXX").string();
      std::vector<char> buffer(name.begin(), name.end()); buffer.push_back(0);
      const char* created = mkdtemp(buffer.data());
      if (!created) throw failure("Could not create temporary native profile");
      profile = created; owned_profile = true;
      profile = std::filesystem::canonical(profile).string();
    } else { profile = options.profile; std::filesystem::create_directories(profile); }
    auto in = pipe_pair();
    std::array<int, 2> out{-1,-1}, err{-1,-1}, wake_pipe{-1,-1};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    bool actions_ready = false, attributes_ready = false;
    try {
      out = pipe_pair(); err = pipe_pair(); wake_pipe = pipe_pair();
      if (posix_spawn_file_actions_init(&actions)) throw failure("Could not initialize native process actions");
      actions_ready = true;
      if (posix_spawnattr_init(&attributes)) throw failure("Could not initialize native process attributes");
      attributes_ready = true;
      for (const auto& pair : {std::pair{in[0], STDIN_FILENO}, {out[1], STDOUT_FILENO}, {err[1], STDERR_FILENO}})
        if (posix_spawn_file_actions_adddup2(&actions, pair.first, pair.second)) throw failure("Could not route native process streams");
      sigset_t empty, defaults;
      sigemptyset(&empty); sigemptyset(&defaults);
      for (int signal : {SIGTERM, SIGINT, SIGHUP, SIGPIPE}) sigaddset(&defaults, signal);
      short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_SETSID
      flags |= POSIX_SPAWN_SETSID;
#else
      flags |= POSIX_SPAWN_SETPGROUP;
      if (posix_spawnattr_setpgroup(&attributes, 0)) throw failure("Could not isolate native process group");
#endif
      if (posix_spawnattr_setsigmask(&attributes, &empty) || posix_spawnattr_setsigdefault(&attributes, &defaults) ||
          posix_spawnattr_setflags(&attributes, flags)) throw failure("Could not isolate native process signals");
      std::vector<std::string> args{options.executable};
      args.insert(args.end(), options.arguments.begin(), options.arguments.end());
      args.push_back("--suitecut-width=" + std::to_string(options.width));
      args.push_back("--suitecut-height=" + std::to_string(options.height));
      args.push_back("--suitecut-scale=" + std::to_string(options.scale));
      args.push_back("--suitecut-fps=" + std::to_string(static_cast<int>(options.frames_per_second)));
      // I420 keeps native recording bandwidth at 1.5 bytes/pixel. Preserve
      // exact odd viewport geometry through the existing BGRA path.
      args.push_back(options.width % 2 || options.height % 2 ?
          "--suitecut-pixel-format=bgra" : "--suitecut-pixel-format=i420");
      args.push_back("--suitecut-frame-transport=on-demand");
      args.push_back(std::string("--suitecut-headless=") + (options.headless ? "1" : "0"));
      args.push_back("--suitecut-profile=" + profile);
      if (!owned_profile) args.push_back("--suitecut-persistent-profile=1");
      std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
      const int error = posix_spawn(&pid, options.executable.c_str(), &actions, &attributes, argv.data(), environ);
      if (error) { pid = -1; throw failure("Could not launch native engine: " + std::string(std::strerror(error))); }
      identity = process_identity(pid);
      input = in[1]; in[1] = -1; output = out[0]; out[0] = -1;
      error_output = err[0]; err[0] = -1; wake_read = wake_pipe[0]; wake_pipe[0] = -1;
      wake_write = wake_pipe[1]; wake_pipe[1] = -1;
      for (int fd : {input, output, error_output, wake_read, wake_write}) nonblocking(fd);
    } catch (...) {
      if (pid > 0) { (void)::kill(-pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} pid = -1; }
      for (auto* pair : {&in,&out,&err,&wake_pipe}) for (auto& fd : *pair) close_fd(fd);
      if (actions_ready) posix_spawn_file_actions_destroy(&actions);
      if (attributes_ready) posix_spawnattr_destroy(&attributes);
      throw;
    }
    for (auto* pair : {&in,&out,&err,&wake_pipe}) for (auto& fd : *pair) close_fd(fd);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
  }

  bool live_group() const {
    if (pid < 1) return false;
    if (::kill(-pid, 0) < 0 && errno == ESRCH) return false;
#if defined(__linux__)
    if (identity) {
      const auto leader = process_identity(pid);
      if (leader && leader->started != identity->started) return false;
      DIR* directory = opendir("/proc");
      if (directory) {
        bool incomplete = false, alive = false;
        while (const auto* entry = readdir(directory)) {
          char* end = nullptr;
          const long candidate = std::strtol(entry->d_name, &end, 10);
          if (!end || *end || candidate < 1 || candidate > INT_MAX) continue;
          // Most host processes are unrelated. Avoid opening/parsing their
          // /proc stat files; matching members still need full identity checks.
          if (::getpgid(static_cast<pid_t>(candidate)) != pid) continue;
          const auto member = process_identity(static_cast<pid_t>(candidate));
          if (!member) continue;
          if (member->group != pid) continue;
          if (member->session != identity->session || member->started < identity->started) { incomplete = true; break; }
          if (member->state != 'Z' && member->state != 'X' && member->state != 'x') { alive = true; break; }
        }
        closedir(directory);
        if (!incomplete) return alive;
        // Conflicting membership proves this numeric group is no longer ours.
        return false;
      }
    }
#endif
    return true;
  }
  void signal_group(int signal) {
    if (!live_group()) return;
    if (::kill(-pid, signal) < 0 && errno != ESRCH) throw failure("Could not signal owned native process group");
  }
  void flush() {
    std::lock_guard lock(mutex);
    while (input >= 0 && !outgoing.empty()) {
      auto& front = outgoing.front();
      const auto written = ::write(input, front.line.data() + front.offset, front.line.size() - front.offset);
      if (written < 0) {
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        throw failure("Native command pipe closed");
      }
      if (!written) throw failure("Native command pipe made no progress");
      front.offset += static_cast<std::size_t>(written);
      if (front.offset == front.line.size()) outgoing.pop_front();
    }
  }
  void expire() {
    std::lock_guard lock(mutex);
    if (closing) return;
    // A late receipt or a body the engine never finished is that body's
    // problem, not the connection's: forget the receipt, drop the body.
    const auto now = Clock::now();
    for (auto it = body_receipts.begin(); it != body_receipts.end();) {
      if (now >= it->second.deadline) { it = body_receipts.erase(it); ++counters.dropped_bodies; }
      else ++it;
    }
    std::vector<std::pair<std::string, Json>> stalled;
    for (auto it = bodies.begin(); it != bodies.end();) {
      if (now < it->second.deadline) { ++it; continue; }
      if (!it->second.dropped) stalled.emplace_back(it->first, std::move(it->second.identity));
      it = bodies.erase(it);
    }
    for (auto& [scope, identity] : stalled) drop_body_locked(scope, std::move(identity));
    std::vector<int> expired;
    for (const auto& [id, item] : pending) if (Clock::now() >= item.deadline) expired.push_back(id);
    for (const int id : expired) cancel_locked(id, "Native request timed out");
  }
  void run() noexcept {
    // Only this private thread writes child stdin. Blocking SIGPIPE here avoids
    // process-wide signal changes and leaves every application thread untouched.
    sigset_t blocked; sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    (void)pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
    PacketDecoder decoder(0, options.max_payload_bytes);
    bool stdout_done = false;
    for (;;) {
      try {
        expire();
        bool stop, queued;
        int poll_timeout = 100;
        Clock::time_point shutdown;
        {
          std::lock_guard lock(mutex); stop = closing; queued = !outgoing.empty(); shutdown = close_started;
          if (stop) poll_timeout = 10;
          else for (const auto& [id, item] : pending) {
            (void)id;
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(item.deadline - Clock::now()).count();
            poll_timeout = std::min(poll_timeout, static_cast<int>(std::clamp<std::int64_t>(remaining, 0, 100)));
          }
        }
        if (stop) close_fd(input);
        int status = 0;
        if (!child_exited) {
          const auto result = waitpid(pid, &status, WNOHANG);
          if (result == pid || (result < 0 && errno == ECHILD)) {
            child_exited = true; exited_at = Clock::now(); close_fd(input);
            if (!stop) {
              if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                fail(std::make_exception_ptr(failure("Native engine exited abnormally")));
              else fail(std::make_exception_ptr(failure("Native engine exited")));
              stop = true; { std::lock_guard lock(mutex); shutdown = close_started; }
            }
          } else if (result < 0 && errno != EINTR) throw failure("Could not reap owned native engine");
        }
        if (stop) {
          const auto now = Clock::now();
          if (child_exited && !live_group()) break;
          const auto budget = options.shutdown_timeout;
          const bool descendant_grace = child_exited && now - exited_at >= std::chrono::milliseconds(250);
          if (!sent_term && (descendant_grace || now - shutdown >= budget * 3 / 5)) {
            signal_group(SIGTERM); sent_term = true;
          }
          if (!sent_kill && (now - shutdown >= budget * 9 / 10 ||
              (sent_term && child_exited && now - exited_at >= std::chrono::milliseconds(350)))) {
            signal_group(SIGKILL); sent_kill = true;
          }
        }
        std::array<pollfd, 4> descriptors{{{output, POLLIN, 0}, {error_output, POLLIN, 0},
          {wake_read, POLLIN, 0}, {input, static_cast<short>(queued && !stop ? POLLOUT : 0), 0}}};
        const int count = ::poll(descriptors.data(), descriptors.size(), poll_timeout);
        if (count < 0 && errno != EINTR) throw failure("Native transport poll failed");
        if (descriptors[2].revents) { std::array<char, 256> bytes; while (::read(wake_read, bytes.data(), bytes.size()) > 0) {} }
        if (descriptors[3].revents & (POLLOUT | POLLERR | POLLHUP)) flush();
        for (int stream = 0; stream < 2; ++stream) {
          int& fd = stream == 0 ? output : error_output;
          if (fd < 0 || !descriptors[stream].revents) continue;
          std::array<std::uint8_t, 65536> bytes;
          for (int chunk = 0; chunk < 16; ++chunk) {
            const auto read = ::read(fd, bytes.data(), bytes.size());
            if (read > 0) {
              if (stream == 0) decoder.feed(std::span<const std::uint8_t>(bytes.data(), static_cast<std::size_t>(read)),
                [this](Packet&& packet) { receive(std::move(packet)); });
              else {
                // Whole stderr up to readiness: it is only read for startup diagnostics.
                std::lock_guard lock(mutex);
                if (!ready_seen) stderr_tail.append(reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(read));
              }
            } else if (!read) {
              close_fd(fd);
              if (stream == 0) {
                stdout_done = true; decoder.finish();
                std::lock_guard lock(mutex);
                if (!closing) throw failure("Native engine closed its response stream");
              }
              break;
            } else if (errno == EINTR) continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            else throw failure("Native transport read failed");
          }
        }
      } catch (...) {
        fail(std::current_exception()); close_fd(input);
        // Corrupt/overflowed streams cannot be resumed, but stderr is still
        // drained during physical shutdown and every pending future is settled.
        if (!stdout_done) { close_fd(output); stdout_done = true; }
      }
    }
    close_fd(input); close_fd(output); close_fd(error_output);
    { std::lock_guard lock(mutex); finished = true; begin_close_locked(); changed.notify_all(); }
  }
#endif
};

Transport::Transport(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
std::shared_ptr<Transport> Transport::launch(const LaunchOptions& options) {
  validate(options);
  if (options.backend == BrowserBackend::chromium) {
    auto impl = std::make_unique<Impl>(options);
    impl->cdp = detail::CdpBackend::launch(options);
    return std::shared_ptr<Transport>(new Transport(std::move(impl)));
  }
  if (options.backend == BrowserBackend::firefox) {
    auto impl = std::make_unique<Impl>(options);
    impl->bidi = detail::BidiBackend::launch(options);
    return std::shared_ptr<Transport>(new Transport(std::move(impl)));
  }
  if (options.backend != BrowserBackend::cef) throw failure("Unknown native browser backend");
#ifdef _WIN32
  throw failure("Native process transport currently requires POSIX");
#else
  auto transport = std::shared_ptr<Transport>(new Transport(std::make_unique<Impl>(options)));
  transport->impl_->spawn_child();
  try {
    auto* impl = transport->impl_.get();
    impl->io = std::thread([impl] { impl->run(); });
    std::unique_lock lock(impl->mutex);
    const bool settled = impl->changed.wait_for(lock, options.startup_timeout,
      [impl] { return impl->ready_seen || impl->terminal || impl->finished; });
    if (!settled || impl->terminal || !impl->ready_seen) {
      const auto error = impl->terminal;
      const auto diagnostics = impl->stderr_tail;
      lock.unlock(); transport->close();
      if (error) {
        try { std::rethrow_exception(error); }
        catch (const std::exception& cause) { throw failure(std::string(cause.what()) + (diagnostics.empty() ? "" : ": " + diagnostics)); }
      }
      throw failure("Native engine startup timed out" + (diagnostics.empty() ? std::string{} : ": " + diagnostics));
    }
  } catch (...) { transport->close(); throw; }
  return transport;
#endif
}
Transport::~Transport() { try { close(); } catch (...) {} }

Request Transport::submit(std::string fields_json, std::chrono::milliseconds timeout) {
  return submit_json(Json::parse(fields_json), timeout);
}
Request Transport::submit_json(Json message, std::chrono::milliseconds timeout) {
  duration_valid(timeout, "Request timeout");
  if (!message.is_object() || !message.contains("command") || !message.at("command").is_string() ||
      message.at("command").as_string().empty()) throw failure("Native command must be a JSON object with command");
  const auto page = message.contains("pageId") ? message.at("pageId").as_int() : 1;
  if (page < 1 || page > INT_MAX) throw failure("Native pageId must be positive");
  if (impl_->cdp) return impl_->cdp->submit(std::move(message), timeout);
  if (impl_->bidi) return impl_->bidi->submit(std::move(message), timeout);
  auto& state = *impl_;
  std::lock_guard lock(state.mutex);
  if (state.terminal) std::rethrow_exception(state.terminal);
  if (state.closing) throw failure("Native transport is closed");
  if (state.options.max_pending && state.pending.size() >= state.options.max_pending)
    throw failure("Native pending request limit reached");
  const int id = state.allocate_id();
  message["version"] = 1; message["id"] = id; message["pageId"] = static_cast<int>(page);
  if (!message.contains("timeoutMs")) message["timeoutMs"] = static_cast<std::int64_t>(timeout.count());
  Impl::Pending pending;
  pending.deadline = Clock::now() + timeout; pending.page = static_cast<int>(page);
  Request request{id, pending.promise.get_future()};
  auto line = message.dump() + "\n";
  state.pending.emplace(id, std::move(pending));
  try { state.enqueue_locked(id, std::move(line)); }
  catch (...) { state.pending.erase(id); throw; }
  state.counters.pending = state.pending.size();
  ++state.counters.commands; state.wake(); return request;
}
Packet Transport::call(std::string fields_json, std::chrono::milliseconds timeout) {
  return submit(std::move(fields_json), timeout).result.get();
}
Packet Transport::call_json(Json fields, std::chrono::milliseconds timeout) {
  return submit_json(std::move(fields), timeout).result.get();
}
void Transport::cancel(int request_id) {
  if (impl_->cdp) { impl_->cdp->cancel(request_id); return; }
  if (impl_->bidi) { impl_->bidi->cancel(request_id); return; }
  auto& state = *impl_;
  try {
    std::lock_guard lock(state.mutex);
    if (state.closing) return;
    state.cancel_locked(request_id, "Native request canceled"); state.wake();
  } catch (...) { state.fail(std::current_exception()); throw; }
}
std::optional<Packet> Transport::poll(std::chrono::milliseconds timeout) {
  duration_valid(timeout, "Event timeout", true);
  if (impl_->cdp) return impl_->cdp->poll(timeout);
  if (impl_->bidi) return impl_->bidi->poll(timeout);
  auto& state = *impl_;
  std::unique_lock lock(state.mutex);
  state.changed.wait_for(lock, timeout, [&state] { return !state.events.empty() || state.closing || state.terminal; });
  if (!state.events.empty()) {
    Packet result = std::move(state.events.front().packet); state.events.pop_front();
    state.counters.queued_event_bytes -= result.header.size() + result.payload.size(); return result;
  }
  if (state.terminal) std::rethrow_exception(state.terminal);
  return std::nullopt;
}
Packet Transport::ready() const {
  if (impl_->cdp) return impl_->cdp->ready();
  if (impl_->bidi) return impl_->bidi->ready();
  std::lock_guard lock(impl_->mutex);
  if (!impl_->ready_seen) throw failure("Native engine is not ready");
  return impl_->ready_packet;
}
Packet Transport::wait_ready(int page_id, std::chrono::milliseconds timeout) {
  duration_valid(timeout, "Page ready timeout");
  if (page_id < 1) throw failure("Ready page ID must be positive");
  if (impl_->cdp) return impl_->cdp->wait_ready(page_id, timeout);
  if (impl_->bidi) return impl_->bidi->wait_ready(page_id, timeout);
  auto& state = *impl_;
  std::unique_lock lock(state.mutex);
  if (!state.changed.wait_for(lock, timeout, [&state, page_id] {
        return state.page_ready.contains(page_id) || state.closing || state.terminal;
      })) throw failure("Native page readiness timed out");
  if (const auto found = state.page_ready.find(page_id); found != state.page_ready.end()) return found->second;
  if (state.terminal) std::rethrow_exception(state.terminal);
  throw failure("Native transport closed before page readiness");
}
std::optional<int> Transport::process_id() const {
  if (impl_->cdp || impl_->bidi) return std::nullopt;
  // Set once at launch, before the I/O thread starts, and never changed.
  if (impl_->pid < 1) return std::nullopt;
  return static_cast<int>(impl_->pid);
}
ClientStats Transport::stats() const { if (impl_->cdp) return impl_->cdp->stats(); if (impl_->bidi) return impl_->bidi->stats(); std::lock_guard lock(impl_->mutex); return impl_->counters; }
void Transport::close() {
  if (!impl_) return;
  if (impl_->cdp) { impl_->cdp->close(); return; }
  if (impl_->bidi) { impl_->bidi->close(); return; }
  auto& state = *impl_;
  std::lock_guard close_lock(state.close_mutex);
  { std::lock_guard lock(state.mutex); state.begin_close_locked(); state.wake(); }
  // No user callbacks run on io; no application-owned action can invoke close
  // on that thread. Physical process retirement precedes profile removal.
  if (state.io.joinable()) state.io.join();
#ifndef _WIN32
  else if (state.pid > 0 && !state.child_exited) {
    // Thread creation can fail after a successful posix_spawn. This launch
    // failure path still owns and reaps the actual child before removing data.
    state.signal_group(SIGKILL);
    while (waitpid(state.pid, nullptr, 0) < 0 && errno == EINTR) {}
    state.child_exited = true;
  }
#endif
  {
    std::lock_guard lock(state.mutex);
#ifndef _WIN32
    for (int* fd : {&state.input, &state.output, &state.error_output, &state.wake_read, &state.wake_write}) close_fd(*fd);
#endif
    state.page_ready.clear();
  }
  if (state.owned_profile) {
    std::error_code error; std::filesystem::remove_all(state.profile, error);
    if (error) throw failure("Could not remove owned native profile: " + error.message());
    state.owned_profile = false;
  }
}
} // namespace suitecut
