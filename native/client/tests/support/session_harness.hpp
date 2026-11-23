// SPDX-License-Identifier: MIT
// Starts a session service against the session fake engine and talks to its
// socket the way an embedding app does: a little-endian u32 length, then
// that many bytes of JSON, any number of requests per connection.
#pragma once
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "suitecut/json.hpp"

extern char** environ;

namespace session_test {
using suitecut::Json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

inline int failures = 0;
inline std::string last_check;  // reported when a test throws
inline void check(bool condition, const std::string& description) {
  last_check = description;
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << description << '\n';
}
inline void require(bool condition, const std::string& description) {
  if (!condition) throw std::runtime_error("required: " + description);
}

// Runs |argv| to completion and returns its exit status and stdout.
inline std::pair<int, std::string> run(const std::vector<std::string>& args) {
  int out[2];
  require(::pipe(out) == 0, "pipe");
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, out[0]);
  // Only the dup on stdout stays: a forked worker must not keep the pipe open.
  posix_spawn_file_actions_addclose(&actions, out[1]);
  std::vector<char*> argv;
  std::vector<std::string> copy = args;
  for (auto& arg : copy) argv.push_back(arg.data());
  argv.push_back(nullptr);
  pid_t pid = -1;
  const int error = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(out[1]);
  require(error == 0, "spawn " + args[0]);
  // The service's first process exits once it has answered; the worker it
  // forks keeps no copy of this pipe open past its own startup.
  std::string text;
  char buffer[4096];
  for (;;) {
    const auto n = ::read(out[0], buffer, sizeof buffer);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    text.append(buffer, static_cast<std::size_t>(n));
  }
  ::close(out[0]);
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, text};
}

class Connection {
 public:
  explicit Connection(const std::string& path) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    require(fd_ >= 0, "socket");
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    require(path.size() < sizeof address.sun_path, "socket path fits");
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    require(::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0,
            "connect " + path);
  }
  ~Connection() {
    if (fd_ >= 0) ::close(fd_);
  }
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  void send_bytes(const std::string& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const auto n = ::send(fd_, bytes.data() + offset, bytes.size() - offset, 0);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) throw std::runtime_error("send failed");
      offset += static_cast<std::size_t>(n);
    }
  }
  static std::string frame(const std::string& body) {
    const auto size = static_cast<std::uint32_t>(body.size());
    std::string out{static_cast<char>(size), static_cast<char>(size >> 8),
                    static_cast<char>(size >> 16), static_cast<char>(size >> 24)};
    return out + body;
  }
  void send(const Json& value) { send_bytes(frame(value.dump())); }

  // False when the service closed the connection before |count| bytes.
  bool read_exact(char* data, std::size_t count, Clock::duration timeout) {
    const auto deadline = Clock::now() + timeout;
    std::size_t offset = 0;
    while (offset < count) {
      const auto left =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
      if (left <= 0) throw std::runtime_error("no reply in time");
      pollfd p{fd_, POLLIN, 0};
      const auto rc = ::poll(&p, 1, static_cast<int>(left));
      if (rc < 0 && errno == EINTR) continue;
      if (rc <= 0) continue;
      const auto n = ::read(fd_, data + offset, count - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      offset += static_cast<std::size_t>(n);
    }
    return true;
  }
  // Nullopt-like: returns a null Json when the connection closed.
  Json receive(Clock::duration timeout = 30s) {
    unsigned char prefix[4];
    if (!read_exact(reinterpret_cast<char*>(prefix), 4, timeout)) return Json{};
    const std::size_t size = prefix[0] | (std::size_t(prefix[1]) << 8) |
                             (std::size_t(prefix[2]) << 16) | (std::size_t(prefix[3]) << 24);
    std::string body(size, '\0');
    if (!read_exact(body.data(), size, timeout)) return Json{};
    return Json::parse(body);
  }
  Json call(const Json& value, Clock::duration timeout = 30s) {
    send(value);
    return receive(timeout);
  }
  // True when the service has closed this connection.
  bool closed_by_service(Clock::duration timeout) {
    char byte;
    try {
      return !read_exact(&byte, 1, timeout);
    } catch (...) {
      return false;
    }
  }

 private:
  int fd_ = -1;
};

class Service {
 public:
  Service(std::string binary, std::string engine, std::string name,
          std::vector<std::string> extra = {})
      : binary_(std::move(binary)),
        engine_(std::move(engine)),
        name_(std::move(name)),
        extra_(std::move(extra)) {
    // Socket paths are short (sun_path is 104 bytes on macOS).
    char root[] = "/tmp/sc-session-XXXXXX";
    require(::mkdtemp(root) != nullptr, "temporary directory");
    root_ = root;
    dir_ = root_ + "/run";
    profile_ = root_ + "/profile";
    require(::mkdir(dir_.c_str(), 0700) == 0, "session directory");
    require(::mkdir(profile_.c_str(), 0700) == 0, "profile directory");
  }
  ~Service() {
    try {
      stop();
    } catch (...) {
    }
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }
  Json start() {
    auto args = command();
    args.insert(args.end(), {"--browser", engine_, "--profile", profile_});
    args.insert(args.end(), extra_.begin(), extra_.end());
    args.push_back("start");
    const auto [status, text] = run(args);
    started_ = status == 0;
    try {
      return Json::parse(text);
    } catch (...) {
      return Json{{"unparsed", text}, {"status", status}};
    }
  }
  Json control(const std::string& verb) {
    auto args = command();
    args.push_back(verb);
    const auto [status, text] = run(args);
    (void)status;
    return Json::parse(text);
  }
  void stop() {
    if (!started_) return;
    started_ = false;
    control("close");
    // The worker withdraws its socket before it answers; wait for its exit.
    for (int i = 0; i < 100 && std::filesystem::exists(socket()); ++i)
      std::this_thread::sleep_for(50ms);
  }
  std::string socket() const { return dir_ + "/" + name_ + ".sock"; }
  const std::string& profile() const { return profile_; }
  std::vector<Json> commands() const {
    std::vector<Json> out;
    std::ifstream in(profile_ + "/commands.log");
    std::string line;
    while (std::getline(in, line))
      if (!line.empty()) out.push_back(Json::parse(line));
    return out;
  }
  void mark_stopped() { started_ = false; }

 private:
  std::vector<std::string> command() const {
    return {binary_, "--session", name_, "--session-dir", dir_};
  }
  std::string binary_, engine_, name_, root_, dir_, profile_;
  std::vector<std::string> extra_;
  bool started_ = false;
};

inline bool ok(const Json& reply) {
  return reply.is_object() && reply.contains("ok") && reply.at("ok") == Json(true);
}
inline std::string code(const Json& reply) {
  if (!reply.is_object() || !reply.contains("error") || !reply.at("error").is_object() ||
      !reply.at("error").contains("code"))
    return "";
  return reply.at("error").at("code").as_string();
}
inline const Json& result(const Json& reply) {
  require(ok(reply), "ok reply: " + reply.dump());
  return reply.at("result");
}

// Waits until |condition| holds, or |timeout| passes.
inline bool eventually(const std::function<bool()>& condition, Clock::duration timeout = 5s) {
  const auto deadline = Clock::now() + timeout;
  while (Clock::now() < deadline) {
    if (condition()) return true;
    std::this_thread::sleep_for(20ms);
  }
  return condition();
}
}  // namespace session_test
