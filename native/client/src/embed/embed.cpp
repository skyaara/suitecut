// SPDX-License-Identifier: MIT
// embed.h: protocol 1 of suitecut-session, for apps that show its tabs.
#include "embed_internal.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

extern char** environ;

using suitecut::Json;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
namespace embed = suitecut::embed;

namespace suitecut::embed {

// ---- Wire ----------------------------------------------------------------

Wire::Wire(const std::string& socket_path) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (socket_path.size() >= sizeof address.sun_path)
    throw std::invalid_argument("The session's socket path is too long for AF_UNIX: " + socket_path);
  std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
  ::fcntl(fd_, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
  int on = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
  if (::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
    const int error = errno;
    ::close(fd_);
    fd_ = -1;
    if (error == ENOENT || error == ECONNREFUSED)
      throw Closed("No session is running at " + socket_path);
    throw std::runtime_error("connect " + socket_path + ": " + std::strerror(error));
  }
  ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK);
}

Wire::~Wire() {
  if (fd_ >= 0) ::close(fd_);
}

namespace {
int remaining_ms(Clock::time_point deadline) {
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
  return left <= 0 ? 0 : static_cast<int>(std::min<long long>(left, 1 << 30));
}
}  // namespace

void Wire::Send(const std::string& bytes, Clock::time_point deadline) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
#ifdef MSG_NOSIGNAL
    const auto n = ::send(fd_, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
#else
    const auto n = ::send(fd_, bytes.data() + offset, bytes.size() - offset, 0);
#endif
    if (n > 0) {
      offset += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd p{fd_, POLLOUT, 0};
      const int wait = remaining_ms(deadline);
      if (!wait || ::poll(&p, 1, wait) == 0) throw TimedOut("The session did not take the request in time");
      continue;
    }
    throw Closed("The session closed the connection");
  }
}

void Wire::Receive(char* data, std::size_t size, Clock::time_point deadline) {
  std::size_t offset = 0;
  while (offset < size) {
    const auto n = ::recv(fd_, data + offset, size - offset, 0);
    if (n > 0) {
      offset += static_cast<std::size_t>(n);
      continue;
    }
    if (n == 0) throw Closed("The session closed the connection");
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      pollfd p{fd_, POLLIN, 0};
      const int wait = remaining_ms(deadline);
      if (!wait || ::poll(&p, 1, wait) == 0) throw TimedOut("The session did not answer in time");
      continue;
    }
    throw Closed(std::string("The session connection failed: ") + std::strerror(errno));
  }
}

Json Wire::Call(const Json& message, std::chrono::milliseconds timeout) {
  if (fd_ < 0) throw Closed("The connection is closed");
  const auto deadline = Clock::now() + timeout;
  const auto body = message.dump();
  if (body.size() > UINT32_MAX) throw std::invalid_argument("The request does not fit a frame");
  const auto length = static_cast<std::uint32_t>(body.size());
  std::string frame{static_cast<char>(length), static_cast<char>(length >> 8), static_cast<char>(length >> 16),
                    static_cast<char>(length >> 24)};
  frame += body;
  try {
    Send(frame, deadline);
    unsigned char head[4];
    Receive(reinterpret_cast<char*>(head), 4, deadline);
    const std::uint32_t size = head[0] | head[1] << 8 | head[2] << 16 | static_cast<std::uint32_t>(head[3]) << 24;
    std::string reply(size, '\0');
    Receive(reply.data(), size, deadline);
    return Json::parse(reply);
  } catch (...) {
    ::close(fd_);
    fd_ = -1;
    throw;
  }
}

// ---- Programs ------------------------------------------------------------

Program Run(const std::vector<std::string>& argv, std::chrono::milliseconds timeout) {
  int out[2] = {-1, -1}, err[2] = {-1, -1};
  if (::pipe(out) || ::pipe(err)) throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&actions, out[1], 1);
  posix_spawn_file_actions_adddup2(&actions, err[1], 2);
  for (int fd : {out[0], out[1], err[0], err[1]}) posix_spawn_file_actions_addclose(&actions, fd);
  // The service's worker outlives this call; it keeps none of the app's
  // other descriptors.
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
  posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#endif
  std::vector<char*> args;
  for (const auto& argument : argv) args.push_back(const_cast<char*>(argument.c_str()));
  args.push_back(nullptr);
  pid_t pid = -1;
  const int spawned = ::posix_spawn(&pid, argv.front().c_str(), &actions, &attributes, args.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  ::close(out[1]);
  ::close(err[1]);
  if (spawned) {
    ::close(out[0]);
    ::close(err[0]);
    throw std::runtime_error("Cannot run " + argv.front() + ": " + std::strerror(spawned));
  }
  Program program;
  const auto deadline = Clock::now() + timeout;
  bool open_out = true, open_err = true, exited = false;
  int status = 0;
  // The service's worker keeps running after `start` returns, with its own
  // stdio, so the program's exit (not the pipes' end) finishes this.
  while (!exited) {
    const auto waited = ::waitpid(pid, &status, WNOHANG);
    if (waited == pid) exited = true;
    pollfd fds[2] = {{open_out ? out[0] : -1, POLLIN, 0}, {open_err ? err[0] : -1, POLLIN, 0}};
    const int wait = exited ? 0 : std::min(remaining_ms(deadline), 50);
    if (!exited && !wait) {
      ::kill(pid, SIGKILL);
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      ::close(out[0]);
      ::close(err[0]);
      throw TimedOut(argv.front() + " did not finish in time");
    }
    if (::poll(fds, 2, wait) > 0 || exited) {
      for (int i = 0; i < 2; ++i) {
        bool& open = i == 0 ? open_out : open_err;
        if (!open || (!(fds[i].revents & (POLLIN | POLLHUP)) && !exited)) continue;
        char buffer[4096];
        ::fcntl(fds[i].fd, F_SETFL, O_NONBLOCK);
        for (;;) {
          const auto n = ::read(fds[i].fd, buffer, sizeof buffer);
          if (n > 0) {
            (i == 0 ? program.out : program.err).append(buffer, static_cast<std::size_t>(n));
            continue;
          }
          if (n == 0) open = false;
          break;
        }
      }
    }
  }
  ::close(out[0]);
  ::close(err[0]);
  program.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
  return program;
}

}  // namespace suitecut::embed

// ---- Handles ---------------------------------------------------------------

namespace {
bool safe(const char* text, const char* extra) {
  if (!text || !*text) return false;
  for (const char* c = text; *c; ++c)
    if (!std::isalnum(static_cast<unsigned char>(*c)) && !std::strchr(extra, *c)) return false;
  return true;
}
sc_control control_of(const Json& name) {
  if (name == Json("agent")) return SC_CONTROL_AGENT;
  if (name == Json("human")) return SC_CONTROL_HUMAN;
  return SC_CONTROL_UNCLAIMED;
}
}  // namespace

// What a session handle and its views share, so a view outlives nothing it uses.
struct SessionCore {
  std::string socket_path;
  Json hello_params;
  Json hello_reply;
  std::chrono::milliseconds timeout{30000};
  std::atomic<bool> closed{false};
  std::mutex pool_mutex;
  std::vector<std::unique_ptr<embed::Wire>> idle;
  std::atomic<std::int64_t> next_id{1};

  void Live() const {
    if (closed) throw embed::Closed("The session handle is closed");
  }
  // A new connection, already greeted.
  std::unique_ptr<embed::Wire> Connect() {
    auto wire = std::make_unique<embed::Wire>(socket_path);
    const auto reply = Exchange(*wire, "hello", hello_params, timeout);
    if (hello_reply.is_null()) hello_reply = reply;
    return wire;
  }
  Json Exchange(embed::Wire& wire, const std::string& op, Json params, std::chrono::milliseconds wait) {
    const auto id = next_id++;
    const auto reply = wire.Call(Json{{"v", 1}, {"id", id}, {"op", op}, {"params", std::move(params)}}, wait);
    if (!reply.is_object() || !reply.contains("v"))
      throw embed::Unsupported("The session did not answer in protocol 1");
    if (reply.contains("ok") && reply.at("ok") == Json(true))
      return reply.contains("result") ? reply.at("result") : Json(Json::object{});
    const auto& error = reply.contains("error") ? reply.at("error") : Json(Json::object{});
    const auto code = error.contains("code") && error.at("code").is_string() ? error.at("code").as_string() : "failed";
    const auto message = error.contains("message") && error.at("message").is_string() ? error.at("message").as_string() : "";
    if (code == "unsupported" || code == "protocol_unsupported") throw embed::Unsupported(code + ": " + message);
    throw embed::Refusal(code, message);
  }
  // One request on a pooled connection; another thread's slow call never
  // holds this one up.
  Json Request(const std::string& op, Json params, std::optional<std::chrono::milliseconds> wait = {}) {
    Live();
    std::unique_ptr<embed::Wire> wire;
    {
      std::lock_guard lock(pool_mutex);
      if (!idle.empty()) {
        wire = std::move(idle.back());
        idle.pop_back();
      }
    }
    if (!wire) wire = Connect();
    const auto keep = [&] {
      std::lock_guard lock(pool_mutex);
      if (!closed) idle.push_back(std::move(wire));
    };
    try {
      auto result = Exchange(*wire, op, std::move(params), wait.value_or(timeout));
      keep();
      return result;
    } catch (const embed::Refusal&) {
      keep();  // a refusal is a whole reply; the connection is still good
      throw;
    } catch (const embed::Unsupported&) {
      keep();
      throw;
    }
  }
  // Refuses (unknown_page) unless |page| is one of this owner's tabs.
  void TabOf(int32_t page) {
    const auto listed = Request("tabs.list", Json::object{});
    for (const auto& tab : listed.at("tabs").as_array())
      if (tab.at("page").as_int() == page) return;
    throw embed::Refusal("unknown_page", "No tab " + std::to_string(page) + " is this owner's");
  }
  void CloseConnections() {
    closed = true;
    std::lock_guard lock(pool_mutex);
    idle.clear();
  }
};

struct sc_session {
  std::shared_ptr<SessionCore> core;
  std::mutex events_mutex;
  std::unique_ptr<embed::Wire> events_wire;
  std::uint64_t cursor = 0;
  std::deque<Json> pending;
};
struct sc_json { std::string text; };
struct sc_event {
  Json event;
  std::string text;
};
struct sc_view {
  std::shared_ptr<SessionCore> core;
  int32_t page = 0;
  std::unique_ptr<embed::FrameReceiver> receiver;
  std::mutex mutex;
  std::condition_variable idle;
  bool busy = false;
  std::optional<Json> pending_move;
  std::unique_ptr<embed::Wire> input;
  bool detached = false;
  bool detached_sink = false;  // an input-only view has nothing to detach
};

namespace {
sc_status result(sc_error* error, sc_status code, const std::string& message = "") noexcept {
  if (error) {
    error->code = code;
    const auto length = std::min(message.size(), sizeof(error->message) - 1);
    std::memcpy(error->message, message.data(), length);
    error->message[length] = 0;
  }
  return code;
}
template <class Fn>
sc_status boundary(sc_error* error, Fn fn) noexcept {
  try {
    fn();
    return result(error, SC_OK);
  } catch (const embed::Refusal& e) {
    return result(error, SC_REFUSED, e.code + ": " + e.what());
  } catch (const embed::Unsupported& e) {
    return result(error, SC_UNSUPPORTED, e.what());
  } catch (const embed::Closed& e) {
    return result(error, SC_CLOSED, e.what());
  } catch (const embed::TimedOut& e) {
    return result(error, SC_TIMEOUT, e.what());
  } catch (const std::invalid_argument& e) {
    return result(error, SC_INVALID_ARGUMENT, e.what());
  } catch (const std::exception& e) {
    return result(error, SC_ERROR, e.what());
  } catch (...) {
    return result(error, SC_ERROR, "Unknown embedding failure");
  }
}
template <class T>
void check_struct(const T* value, const char* what) {
  if (!value || value->struct_size != sizeof(T) || value->abi_version != SC_EMBED_ABI_VERSION)
    throw std::invalid_argument(std::string("Invalid ") + what + " or ABI version");
}
SessionCore& core_of(sc_session* session) {
  if (!session || !session->core) throw std::invalid_argument("Session is null");
  return *session->core;
}
void fill_tab(const Json& tab, sc_tab* out) {
  if (!out) return;
  check_struct(out, "tab");
  out->page = static_cast<int32_t>(tab.at("page").as_int());
  out->control = control_of(tab.at("control"));
  out->generation = static_cast<std::uint64_t>(tab.at("generation").as_int());
}
void copy_text(char* to, std::size_t size, const std::string& text) {
  const auto length = std::min(text.size(), size - 1);
  std::memcpy(to, text.data(), length);
  to[length] = 0;
}
sc_json* owned_json(const Json& value) { return new sc_json{value.dump()}; }
std::filesystem::path program_dir(const std::filesystem::path& root) {
  if (root.extension() == ".app" || std::filesystem::is_directory(root / "Contents" / "MacOS"))
    return root / "Contents" / "MacOS";
  return root;
}
// Sends one input request on the view's own connection.
void send_input(sc_view& view, const std::string& op, Json params) {
  if (!view.input) view.input = view.core->Connect();
  params["view"] = view.page;
  try {
    view.core->Exchange(*view.input, op, std::move(params), view.core->timeout);
  } catch (const embed::Refusal&) {
    throw;  // the connection stays usable after a refusal
  } catch (...) {
    view.input.reset();
    throw;
  }
}
// Input in order on one connection. A move that arrives while another input
// is on its way waits as the newest move, and replaces any older one.
void input(sc_view* view, const std::string& op, Json params, bool is_move) {
  if (!view || !view->core) throw std::invalid_argument("View is null");
  view->core->Live();
  std::unique_lock lock(view->mutex);
  if (view->detached) throw embed::Closed("The view is detached");
  if (is_move && view->busy) {
    view->pending_move = std::move(params);
    return;
  }
  view->idle.wait(lock, [&] { return !view->busy; });
  view->busy = true;
  std::optional<Json> first = std::move(view->pending_move);
  view->pending_move.reset();
  lock.unlock();
  struct Done {
    sc_view* view;
    ~Done() {
      std::lock_guard relock(view->mutex);
      view->busy = false;
      view->idle.notify_all();
    }
  } done{view};
  if (first) send_input(*view, "input.pointer", std::move(*first));
  send_input(*view, op, std::move(params));
  for (;;) {
    std::optional<Json> next;
    {
      std::lock_guard relock(view->mutex);
      next = std::move(view->pending_move);
      view->pending_move.reset();
    }
    if (!next) break;
    send_input(*view, "input.pointer", std::move(*next));
  }
}

Json ime_params(const sc_ime_composition* composition, bool selection) {
  check_struct(composition, "IME composition");
  if (!composition->text && composition->size) throw std::invalid_argument("The composition's text is null");
  Json params{{"text", std::string(composition->text ? composition->text : "", composition->size)}};
  auto range = [](int32_t start, int32_t end, const char* what) -> std::optional<Json> {
    if (start < 0 && end < 0) return std::nullopt;
    if (start < 0 || end < start) throw std::invalid_argument(std::string(what) + " is start <= end, or -1 for none");
    return Json::array{Json(start), Json(end)};
  };
  if (auto replace = range(composition->replace_start, composition->replace_end, "The replaced range"))
    params["replacement"] = std::move(*replace);
  if (selection)
    if (auto selected = range(composition->selection_start, composition->selection_end, "The selection"))
      params["selection"] = std::move(*selected);
  return params;
}
sc_status answer_prompt(sc_session* session, const char* prompt, Json params, sc_error* error) {
  return boundary(error, [&] {
    if (!prompt || !*prompt) throw std::invalid_argument("Name the prompt");
    params["prompt"] = prompt;
    core_of(session).Request("prompt.answer", std::move(params));
  });
}
}  // namespace

extern "C" {

uint32_t sc_embed_abi_version(void) { return SC_EMBED_ABI_VERSION; }

size_t sc_error_refusal(const sc_error* error, char* code, size_t code_size) {
  if (code && code_size) code[0] = 0;
  if (!error || error->code != SC_REFUSED) return 0;
  const char* colon = std::strchr(error->message, ':');
  if (!colon) return 0;
  const auto length = static_cast<std::size_t>(colon - error->message);
  if (code && code_size) {
    const auto copied = std::min(length, code_size - 1);
    std::memcpy(code, error->message, copied);
    code[copied] = 0;
  }
  return length;
}

const char* sc_json_data(const sc_json* json, size_t* size) {
  if (size) *size = json ? json->text.size() : 0;
  return json ? json->text.c_str() : nullptr;
}
void sc_json_destroy(sc_json* json) { delete json; }

// ---- Runtime ----

void sc_runtime_options_init(sc_runtime_options* options) {
  if (!options) return;
  *options = {};
  options->struct_size = sizeof *options;
  options->abi_version = SC_EMBED_ABI_VERSION;
  options->protocol_min = 1;
  options->protocol_max = 1;
}
void sc_runtime_info_init(sc_runtime_info* info) {
  if (!info) return;
  *info = {};
  info->struct_size = sizeof *info;
  info->abi_version = SC_EMBED_ABI_VERSION;
}

sc_status sc_runtime_inspect(const sc_runtime_options* options, sc_runtime_info* out, sc_error* error) {
  return boundary(error, [&] {
    check_struct(options, "runtime options");
    check_struct(out, "runtime info");
    if (!options->path || !std::filesystem::path(options->path).is_absolute())
      throw std::invalid_argument("The runtime path is absolute");
    if (options->protocol_min > options->protocol_max) throw std::invalid_argument("protocol_min is above protocol_max");
    const std::filesystem::path root(options->path);
    const auto programs = program_dir(root);
    // An embed kit (a folder, not a bundle) keeps the engine in
    // suitecut-browser.app beside the session (scripts/package-embed-kit-macos.py).
    const auto kit_engine = root / "suitecut-browser.app" / "Contents" / "MacOS" / "suitecut-browser";
    const auto engine = programs == root && ::access(kit_engine.c_str(), X_OK) == 0 ? kit_engine
                                                                                   : programs / "suitecut-browser";
    const auto service = programs / "suitecut-session";
    for (const auto& program : {engine, service})
      if (::access(program.c_str(), X_OK) != 0)
        throw std::runtime_error("The runtime has no " + program.filename().string() + " at " + program.string());
    sc_runtime_info info;
    sc_runtime_info_init(&info);
    copy_text(info.engine_path, sizeof info.engine_path, engine.string());
    copy_text(info.service_path, sizeof info.service_path, service.string());
    if (options->signing_requirement) {
      embed::CheckSignature(root.string(), {engine.string(), service.string()}, options->signing_requirement);
      info.signature_verified = 1;
    }
    // Only now, with the signature checked, does anything in the runtime run.
    const auto version = embed::Run({service.string(), "--version", "--json"}, 10s);
    std::uint32_t low = 0, high = 0;
    try {
      const auto described = Json::parse(version.out);
      if (version.exit_code == 0 && described.contains("protocols")) {
        bool any = false;
        for (const auto& protocol : described.at("protocols").at("session").as_array()) {
          const auto value = static_cast<std::uint32_t>(protocol.as_int());
          low = any ? std::min(low, value) : value;
          high = any ? std::max(high, value) : value;
          any = true;
        }
        if (described.contains("version")) copy_text(info.service_version, sizeof info.service_version, described.at("version").as_string());
      }
    } catch (...) {
      // A service from before protocol 1 does not know --version: protocol 0.
      low = high = 0;
    }
    info.protocol_min = low;
    info.protocol_max = high;
    const auto top = std::min(high, options->protocol_max), bottom = std::max(low, options->protocol_min);
    info.protocol = top >= bottom ? top : 0;
    *out = info;
    if (top < bottom)
      throw embed::Unsupported("The runtime speaks protocols " + std::to_string(low) + " to " + std::to_string(high) +
                               "; this app speaks " + std::to_string(options->protocol_min) + " to " +
                               std::to_string(options->protocol_max));
  });
}

// ---- Session ----

void sc_session_options_init(sc_session_options* options) {
  if (!options) return;
  *options = {};
  options->struct_size = sizeof *options;
  options->abi_version = SC_EMBED_ABI_VERSION;
  options->role = SC_ROLE_HOST;
}

sc_status sc_session_open(const sc_session_options* options, sc_session** out, sc_error* error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    check_struct(options, "session options");
    if (!out) throw std::invalid_argument("Nowhere to put the session");
    if (!safe(options->name, "._-")) throw std::invalid_argument("The session name is letters, digits, '.', '_' and '-'");
    if (!safe(options->owner, "._:-")) throw std::invalid_argument("The owner is letters, digits, '.', '_', ':' and '-'");
    if (!options->directory || !std::filesystem::path(options->directory).is_absolute())
      throw std::invalid_argument("The session directory is absolute");
    if (options->role != SC_ROLE_HOST && options->role != SC_ROLE_AGENT) throw std::invalid_argument("Unknown role");
    if (options->flags & ~(SC_SESSION_START | SC_SESSION_PROMPTS)) throw std::invalid_argument("Unknown session flags");
    auto core = std::make_shared<SessionCore>();
    core->socket_path = std::string(options->directory) + "/" + options->name + ".sock";
    // AF_UNIX paths are short (sun_path is 104 bytes on macOS, 108 on Linux,
    // with the terminating NUL). Refuse here rather than when the service starts.
    constexpr auto kSocketPathMax = sizeof(sockaddr_un{}.sun_path) - 1;
    if (core->socket_path.size() > kSocketPathMax)
      throw std::invalid_argument("The session's socket path (directory + \"/\" + name + \".sock\") is " +
                                  std::to_string(core->socket_path.size()) + " bytes; AF_UNIX allows at most " +
                                  std::to_string(kSocketPathMax) + " here. Choose a shorter directory or name");
    if (options->timeout_ms) core->timeout = std::chrono::milliseconds(options->timeout_ms);
    if (options->flags & SC_SESSION_START) {
      check_struct(options->runtime, "runtime info");
      if (!options->profile || !std::filesystem::path(options->profile).is_absolute())
        throw std::invalid_argument("Starting a session needs an absolute profile");
      if (options->runtime->protocol < 1) throw embed::Unsupported("This runtime does not speak protocol 1");
      std::vector<std::string> argv{options->runtime->service_path, "--session", options->name, "--session-dir",
                                    options->directory, "--browser", options->runtime->engine_path, "--profile",
                                    options->profile};
      if (options->scale) argv.insert(argv.end(), {"--scale", std::to_string(options->scale)});
      if (options->idle_ms) argv.insert(argv.end(), {"--idle-ms", std::to_string(options->idle_ms)});
      argv.push_back("start");
      const auto started = embed::Run(argv, std::max<std::chrono::milliseconds>(core->timeout, 60s));
      if (started.exit_code != 0) {
        auto why = started.err.empty() ? started.out : started.err;
        while (!why.empty() && (why.back() == '\n' || why.back() == '\r')) why.pop_back();
        throw std::runtime_error("The session did not start: " + why);
      }
    }
    Json hello{{"protocols", Json::array{Json(1)}}, {"owner", options->owner},
               {"role", options->role == SC_ROLE_AGENT ? "agent" : "host"}};
    if (options->grant) hello["grant"] = options->grant;
    // Each of this handle's connections says so: the session holds dialogs
    // while any of them is open.
    if (options->flags & SC_SESSION_PROMPTS) hello["capabilities"] = Json::array{Json("prompts")};
    Json client{{"name", options->client_name ? options->client_name : "suitecut-embed"},
                {"version", options->client_version ? options->client_version : ""}};
    hello["client"] = std::move(client);
    core->hello_params = std::move(hello);
    // The first connection's hello checks owner, role and grant now.
    auto first = core->Connect();
    core->idle.push_back(std::move(first));
    auto session = std::make_unique<sc_session>();
    session->core = std::move(core);
    *out = session.release();
  });
}

sc_status sc_session_describe(sc_session* session, sc_json** out, sc_error* error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    auto& core = core_of(session);
    if (!out) throw std::invalid_argument("Nowhere to put the description");
    core.Live();
    *out = owned_json(core.hello_reply);
  });
}

sc_status sc_session_grant(sc_session* session, const char* tools_csv, uint32_t ttl_ms, char* token,
                           size_t token_size, sc_error* error) {
  if (token && token_size) token[0] = 0;
  return boundary(error, [&] {
    auto& core = core_of(session);
    if (!token || token_size < 2) throw std::invalid_argument("No room for the grant token");
    Json params = Json::object{};
    if (tools_csv) {
      Json::array groups;
      std::string list(tools_csv);
      for (std::size_t begin = 0; begin <= list.size();) {
        const auto end = list.find(',', begin);
        groups.emplace_back(list.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
      }
      params["tools"] = std::move(groups);
    }
    if (ttl_ms) params["ttlMs"] = static_cast<std::int64_t>(ttl_ms);
    const auto granted = core.Request("session.grant", std::move(params)).at("grant").as_string();
    if (granted.size() >= token_size) throw std::invalid_argument("The grant token needs " + std::to_string(granted.size() + 1) + " bytes");
    copy_text(token, token_size, granted);
  });
}

sc_status sc_session_revoke(sc_session* session, const char* token, sc_error* error) {
  return boundary(error, [&] {
    if (!token) throw std::invalid_argument("Name the grant");
    core_of(session).Request("session.revoke", Json{{"grant", token}});
  });
}

sc_status sc_session_close(sc_session* session, int shutdown, sc_error* error) {
  return boundary(error, [&] {
    auto& core = core_of(session);
    if (core.closed) return;
    std::exception_ptr failed;
    if (shutdown) {
      try {
        core.Request("session.close", Json::object{});
      } catch (...) {
        failed = std::current_exception();
      }
    }
    core.CloseConnections();
    {
      std::lock_guard lock(session->events_mutex);
      session->events_wire.reset();
    }
    if (failed) std::rethrow_exception(failed);
  });
}

void sc_session_destroy(sc_session* session) {
  if (!session) return;
  if (session->core) session->core->CloseConnections();
  delete session;
}

// ---- Tabs ----

void sc_tab_init(sc_tab* tab) {
  if (!tab) return;
  *tab = {};
  tab->struct_size = sizeof *tab;
  tab->abi_version = SC_EMBED_ABI_VERSION;
}

sc_status sc_tab_list(sc_session* session, sc_json** out, sc_error* error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    if (!out) throw std::invalid_argument("Nowhere to put the list");
    *out = owned_json(core_of(session).Request("tabs.list", Json::object{}));
  });
}

sc_status sc_tab_open(sc_session* session, const char* url, int for_agent, sc_tab* out, sc_error* error) {
  return boundary(error, [&] {
    if (out) check_struct(out, "tab");
    Json params = Json::object{};
    if (url) params["url"] = url;
    if (for_agent) params["by"] = "agent";
    fill_tab(core_of(session).Request("tabs.open", std::move(params)), out);
  });
}

sc_status sc_tab_close(sc_session* session, int32_t page, sc_error* error) {
  return boundary(error, [&] { core_of(session).Request("tabs.close", Json{{"page", page}}); });
}

sc_status sc_tab_navigate(sc_session* session, int32_t page, const char* url, sc_tab* out, sc_error* error) {
  return boundary(error, [&] {
    if (!url) throw std::invalid_argument("Navigate to a URL");
    if (out) check_struct(out, "tab");
    fill_tab(core_of(session).Request("tabs.navigate", Json{{"page", page}, {"url", url}}), out);
  });
}

sc_status sc_tab_history(sc_session* session, int32_t page, sc_history step, sc_tab* out, sc_error* error) {
  return boundary(error, [&] {
    const char* name = step == SC_HISTORY_BACK ? "back" : step == SC_HISTORY_FORWARD ? "forward"
                       : step == SC_HISTORY_RELOAD ? "reload" : nullptr;
    if (!name) throw std::invalid_argument("Unknown history step");
    if (out) check_struct(out, "tab");
    fill_tab(core_of(session).Request("tabs.navigate", Json{{"page", page}, {"history", name}}), out);
  });
}

sc_status sc_tab_take(sc_session* session, int32_t page, sc_tab* out, sc_error* error) {
  return boundary(error, [&] {
    if (out) check_struct(out, "tab");
    fill_tab(core_of(session).Request("tabs.take", Json{{"page", page}}), out);
  });
}

sc_status sc_tab_release(sc_session* session, int32_t page, sc_tab* out, sc_error* error) {
  return boundary(error, [&] {
    if (out) check_struct(out, "tab");
    fill_tab(core_of(session).Request("tabs.release", Json{{"page", page}}), out);
  });
}

sc_status sc_tab_stop(sc_session* session, int32_t page, sc_tab* out, sc_error* error) {
  return boundary(error, [&] {
    if (out) check_struct(out, "tab");
    fill_tab(core_of(session).Request("tabs.stop", Json{{"page", page}}), out);
  });
}

sc_status sc_tab_screenshot(sc_session* session, int32_t page, const char* path, sc_error* error) {
  return boundary(error, [&] {
    if (!path || !std::filesystem::path(path).is_absolute()) throw std::invalid_argument("The screenshot path is absolute");
    core_of(session).Request("view.screenshot", Json{{"page", page}, {"to", Json{{"file", path}}}});
  });
}

// ---- Views ----

void sc_surface_sink_init(sc_surface_sink* sink) {
  if (!sink) return;
  *sink = {};
  sink->struct_size = sizeof *sink;
  sink->abi_version = SC_EMBED_ABI_VERSION;
  sink->kind = SC_SINK_LIBRARY;
}
void sc_view_options_init(sc_view_options* options) {
  if (!options) return;
  *options = {};
  options->struct_size = sizeof *options;
  options->abi_version = SC_EMBED_ABI_VERSION;
}
void sc_frame_init(sc_frame* frame) {
  if (!frame) return;
  *frame = {};
  frame->struct_size = sizeof *frame;
  frame->abi_version = SC_EMBED_ABI_VERSION;
}

sc_status sc_view_attach(sc_session* session, const sc_view_options* options, sc_view** out, sc_error* error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    auto& core = core_of(session);
    check_struct(options, "view options");
    if (options->sink) check_struct(options->sink, "surface sink");
    if (!out) throw std::invalid_argument("Nowhere to put the view");
    if ((options->css_width == 0) != (options->css_height == 0) || options->css_width < 0 || options->css_height < 0)
      throw std::invalid_argument("Give both CSS sides, or neither");
    auto view = std::make_unique<sc_view>();
    view->core = session->core;
    view->page = options->page;
    std::string name;
    if (!options->sink) {
      // Input and resize only: nothing is attached, so no frames come.
      core.TabOf(options->page);
      if (options->css_width)
        core.Request("view.resize", Json{{"view", options->page}, {"width", options->css_width}, {"height", options->css_height}});
      view->detached_sink = true;
      *out = view.release();
      return;
    }
    if (options->sink->kind == SC_SINK_LIBRARY) {
      view->receiver = embed::FrameReceiver::Create();
      const auto& engine = core.hello_reply.contains("engine") ? core.hello_reply.at("engine") : Json{};
      if (engine.contains("pid")) view->receiver->ExpectSender(static_cast<int>(engine.at("pid").as_int()));
      name = view->receiver->Name();
    } else if (options->sink->kind == SC_SINK_MACH_BOOTSTRAP) {
      if (!options->sink->name || !*options->sink->name) throw std::invalid_argument("Name the bootstrap service");
      name = options->sink->name;
    } else {
      throw embed::Unsupported("Unknown sink kind");
    }
    if (options->css_width)
      core.Request("view.resize", Json{{"view", options->page}, {"width", options->css_width}, {"height", options->css_height}});
    Json params{{"page", options->page}, {"sink", Json{{"kind", "mach-bootstrap"}, {"name", name}}}};
    if (options->replace) params["replace"] = true;
    const auto attached = core.Request("view.attach", std::move(params));
    if (view->receiver && attached.contains("engine") && attached.at("engine").contains("pid"))
      view->receiver->ExpectSender(static_cast<int>(attached.at("engine").at("pid").as_int()));
    *out = view.release();
  });
}

sc_status sc_view_resize(sc_view* view, int css_width, int css_height, sc_error* error) {
  return boundary(error, [&] {
    if (!view || !view->core) throw std::invalid_argument("View is null");
    if (css_width < 1 || css_height < 1) throw std::invalid_argument("CSS sides are positive");
    view->core->Request("view.resize", Json{{"view", view->page}, {"width", css_width}, {"height", css_height}});
  });
}

sc_status sc_view_next_frame(sc_view* view, uint32_t wait_timeout_ms, sc_frame* out, sc_error* error) {
  return boundary(error, [&] {
    if (!view) throw std::invalid_argument("View is null");
    check_struct(out, "frame");
    if (!view->receiver) throw std::invalid_argument("Only a library sink's frames are read here");
    if (out->surface) throw std::invalid_argument("Release the frame before reading another into it");
    if (!view->receiver->Next(std::chrono::milliseconds(wait_timeout_ms), *out))
      throw embed::TimedOut("No frame arrived");
  });
}

void sc_frame_release(sc_frame* frame) {
  if (frame && frame->struct_size == sizeof *frame) embed::ReleaseFrame(*frame);
}

sc_status sc_view_detach(sc_view* view, sc_error* error) {
  return boundary(error, [&] {
    if (!view || !view->core) throw std::invalid_argument("View is null");
    {
      std::lock_guard lock(view->mutex);
      if (view->detached) return;
      view->detached = true;
    }
    if (!view->detached_sink) view->core->Request("view.detach", Json{{"view", view->page}});
  });
}

void sc_view_destroy(sc_view* view) {
  if (!view) return;
  if (!view->detached && view->core && !view->core->closed) {
    sc_error ignored;
    sc_view_detach(view, &ignored);
  }
  delete view;
}

// ---- Input ----

void sc_pointer_event_init(sc_pointer_event* event) {
  if (!event) return;
  *event = {};
  event->struct_size = sizeof *event;
  event->abi_version = SC_EMBED_ABI_VERSION;
  event->action = SC_POINTER_MOVE;
  event->button = SC_BUTTON_LEFT;
  event->click_count = 1;
}
void sc_wheel_event_init(sc_wheel_event* event) {
  if (!event) return;
  *event = {};
  event->struct_size = sizeof *event;
  event->abi_version = SC_EMBED_ABI_VERSION;
}
void sc_key_event_init(sc_key_event* event) {
  if (!event) return;
  *event = {};
  event->struct_size = sizeof *event;
  event->abi_version = SC_EMBED_ABI_VERSION;
  event->down = 1;
}

sc_status sc_view_pointer(sc_view* view, const sc_pointer_event* event, sc_error* error) {
  return boundary(error, [&] {
    check_struct(event, "pointer event");
    Json params{{"x", std::max(0.0, event->x)}, {"y", std::max(0.0, event->y)}};
    if (event->action == SC_POINTER_MOVE) {
      params["action"] = "move";
      input(view, "input.pointer", std::move(params), true);
      return;
    }
    if (event->action != SC_POINTER_DOWN && event->action != SC_POINTER_UP) throw std::invalid_argument("Unknown pointer action");
    const char* button = event->button == SC_BUTTON_RIGHT ? "right" : event->button == SC_BUTTON_MIDDLE ? "middle"
                         : event->button == SC_BUTTON_LEFT ? "left" : nullptr;
    if (!button) throw std::invalid_argument("Unknown button");
    params["action"] = event->action == SC_POINTER_DOWN ? "down" : "up";
    params["button"] = button;
    params["clickCount"] = static_cast<std::int64_t>(std::max<uint32_t>(1, event->click_count));
    input(view, "input.pointer", std::move(params), false);
  });
}

sc_status sc_view_wheel(sc_view* view, const sc_wheel_event* event, sc_error* error) {
  return boundary(error, [&] {
    check_struct(event, "wheel event");
    input(view, "input.wheel",
          Json{{"x", std::max(0.0, event->x)}, {"y", std::max(0.0, event->y)}, {"deltaX", event->delta_x},
               {"deltaY", event->delta_y}},
          false);
  });
}

sc_status sc_view_key(sc_view* view, const sc_key_event* event, sc_error* error) {
  return boundary(error, [&] {
    check_struct(event, "key event");
    if (!event->key || !*event->key) throw std::invalid_argument("A key names its key");
    input(view, "input.key", Json{{"action", event->down ? "down" : "up"}, {"key", event->key}}, false);
  });
}

sc_status sc_view_text(sc_view* view, const char* utf8, size_t size, sc_error* error) {
  return boundary(error, [&] {
    if (!utf8 && size) throw std::invalid_argument("Text is null");
    input(view, "input.text", Json{{"text", std::string(utf8 ? utf8 : "", size)}}, false);
  });
}

// ---- Text input methods and edit commands ----

void sc_ime_composition_init(sc_ime_composition* composition) {
  if (!composition) return;
  *composition = sc_ime_composition{};
  composition->struct_size = sizeof *composition;
  composition->abi_version = SC_EMBED_ABI_VERSION;
  composition->selection_start = composition->selection_end = -1;
  composition->replace_start = composition->replace_end = -1;
}


sc_status sc_view_ime_set(sc_view* view, const sc_ime_composition* composition, sc_error* error) {
  return boundary(error, [&] { input(view, "input.ime.set", ime_params(composition, true), false); });
}

sc_status sc_view_ime_commit(sc_view* view, const sc_ime_composition* composition, sc_error* error) {
  return boundary(error, [&] { input(view, "input.ime.commit", ime_params(composition, false), false); });
}

sc_status sc_view_ime_cancel(sc_view* view, sc_error* error) {
  return boundary(error, [&] { input(view, "input.ime.cancel", Json::object{}, false); });
}

sc_status sc_view_edit(sc_view* view, sc_edit_command command, sc_error* error) {
  return boundary(error, [&] {
    static const char* const names[] = {nullptr, "copy", "cut", "paste", "undo", "redo", "selectAll", "delete"};
    if (command < SC_EDIT_COPY || command > SC_EDIT_DELETE) throw std::invalid_argument("Unknown edit command");
    input(view, "input.edit", Json{{"command", names[command]}}, false);
  });
}

// ---- Prompts ----

sc_status sc_prompt_answer_dialog(sc_session* session, const char* prompt, int accept, const char* text,
                                  sc_error* error) {
  return boundary(error, [&] {
    if (!prompt || !*prompt) throw std::invalid_argument("Name the prompt");
    Json params{{"prompt", prompt}, {"accept", accept != 0}};
    if (text) params["text"] = text;
    core_of(session).Request("prompt.answer", std::move(params));
  });
}

sc_status sc_prompt_answer_files(sc_session* session, const char* prompt, const char* const* paths, size_t count,
                                 sc_error* error) {
  Json::array list;
  for (size_t i = 0; paths && i < count; ++i) {
    if (!paths[i]) return boundary(error, [] { throw std::invalid_argument("A path is NULL"); });
    list.push_back(paths[i]);
  }
  if (list.empty()) {
    return boundary(error, [] { throw std::invalid_argument("Give at least one path, or cancel the prompt"); });
  }
  return answer_prompt(session, prompt, Json{{"paths", std::move(list)}}, error);
}

sc_status sc_prompt_answer_permission(sc_session* session, const char* prompt, int allow, sc_error* error) {
  return answer_prompt(session, prompt, Json{{"accept", allow != 0}}, error);
}

sc_status sc_prompt_answer_login(sc_session* session, const char* prompt, const char* username, const char* password,
                                 sc_error* error) {
  if (!username || !password)
    return boundary(error, [] { throw std::invalid_argument("A login has a username and a password"); });
  return answer_prompt(session, prompt, Json{{"username", username}, {"password", password}}, error);
}

sc_status sc_prompt_answer_certificate(sc_session* session, const char* prompt, int32_t index, sc_error* error) {
  return answer_prompt(session, prompt, Json{{"index", index}}, error);
}

sc_status sc_prompt_answer_download(sc_session* session, const char* prompt, const char* path, sc_error* error) {
  if (!path || !*path) return boundary(error, [] { throw std::invalid_argument("Give the path to save to"); });
  return answer_prompt(session, prompt, Json{{"path", path}}, error);
}

sc_status sc_prompt_cancel(sc_session* session, const char* prompt, sc_error* error) {
  // A dialog is dismissed; every other kind takes cancel.
  if (prompt && std::string_view(prompt).starts_with("dialog:"))
    return answer_prompt(session, prompt, Json{{"accept", false}}, error);
  return answer_prompt(session, prompt, Json{{"cancel", true}}, error);
}

// ---- Agent calls ----

sc_status sc_agent_catalog(sc_session* session, sc_json** out, sc_error* error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    if (!out) throw std::invalid_argument("Nowhere to put the catalog");
    *out = owned_json(core_of(session).Request("agent.catalog", Json::object{}));
  });
}

sc_status sc_agent_call(sc_session* session, int32_t page, const char* tool, const char* arguments_json,
                        size_t arguments_size, uint64_t expect_generation, uint32_t timeout_ms, sc_json** out,
                        sc_error* error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    auto& core = core_of(session);
    if (!tool || !*tool) throw std::invalid_argument("Name the tool");
    Json params{{"page", page}, {"tool", tool}};
    if (arguments_json && arguments_size) params["arguments"] = Json::parse(std::string_view(arguments_json, arguments_size));
    if (expect_generation) params["expectGeneration"] = static_cast<std::int64_t>(expect_generation);
    const auto wait = timeout_ms ? std::chrono::milliseconds(timeout_ms) : core.timeout;
    auto reply = core.Request("agent.call", std::move(params), wait);
    if (out) *out = owned_json(reply);
  });
}

// ---- Events ----

sc_status sc_session_next_event(sc_session* session, uint32_t wait_timeout_ms, sc_event** out, sc_error* error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    auto& core = core_of(session);
    if (!out) throw std::invalid_argument("Nowhere to put the event");
    std::lock_guard lock(session->events_mutex);
    const auto deadline = Clock::now() + std::chrono::milliseconds(wait_timeout_ms);
    while (session->pending.empty()) {
      core.Live();
      if (!session->events_wire) session->events_wire = core.Connect();
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
      // The service answers a long poll within 25 s; ask again for longer waits.
      const auto ask = std::clamp<std::int64_t>(left.count(), 0, 25000);
      Json reply;
      try {
        reply = core.Exchange(*session->events_wire, "events.next",
                              Json{{"cursor", static_cast<std::int64_t>(session->cursor)}, {"timeoutMs", ask}},
                              std::chrono::milliseconds(ask) + core.timeout);
      } catch (const embed::Refusal&) {
        throw;
      } catch (...) {
        session->events_wire.reset();
        throw;
      }
      session->cursor = static_cast<std::uint64_t>(reply.at("cursor").as_int());
      for (const auto& event : reply.at("events").as_array()) session->pending.push_back(event);
      if (session->pending.empty() && Clock::now() >= deadline) throw embed::TimedOut("No event");
    }
    auto event = std::make_unique<sc_event>();
    event->event = std::move(session->pending.front());
    session->pending.pop_front();
    event->text = event->event.dump();
    *out = event.release();
  });
}

sc_event_kind sc_event_kind_of(const sc_event* event) {
  if (!event || !event->event.contains("type") || !event->event.at("type").is_string()) return SC_EVENT_OTHER;
  const auto& type = event->event.at("type").as_string();
  if (type == "tab.changed") return SC_EVENT_TAB_CHANGED;
  if (type == "tab.closed") return SC_EVENT_TAB_CLOSED;
  if (type == "resync") return SC_EVENT_RESYNC;
  if (type == "engine.exited") return SC_EVENT_ENGINE_EXITED;
  if (type == "tab.loading") return SC_EVENT_TAB_LOADING;
  if (type == "tab.title") return SC_EVENT_TAB_TITLE;
  if (type == "view.cursor") return SC_EVENT_VIEW_CURSOR;
  if (type == "view.ime") return SC_EVENT_VIEW_IME;
  if (type == "prompt.opened") return SC_EVENT_PROMPT_OPENED;
  if (type == "prompt.closed") return SC_EVENT_PROMPT_CLOSED;
  return SC_EVENT_OTHER;
}

int32_t sc_event_page(const sc_event* event) {
  if (!event) return 0;
  try {
    if (event->event.contains("page")) return static_cast<int32_t>(event->event.at("page").as_int());
    if (event->event.contains("view")) return static_cast<int32_t>(event->event.at("view").as_int());
    if (event->event.contains("tab")) return static_cast<int32_t>(event->event.at("tab").at("page").as_int());
    if (event->event.contains("prompt") && event->event.at("prompt").is_object())
      return static_cast<int32_t>(event->event.at("prompt").at("page").as_int());
  } catch (...) {
  }
  return 0;
}

const char* sc_event_json(const sc_event* event, size_t* size) {
  if (size) *size = event ? event->text.size() : 0;
  return event ? event->text.c_str() : nullptr;
}

void sc_event_destroy(sc_event* event) { delete event; }

}  // extern "C"
