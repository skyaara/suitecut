// SPDX-License-Identifier: MIT
#pragma once
#include "host_process.h"
#include "owned_process_inventory.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <set>
#include <sstream>
#include <sys/syscall.h>

namespace suitecut::runner {
// Linux serial runner ownership. Construct before the worker is forked. A worker
// exit alone is insufficient: detached helpers are adopted by this subreaper.
class DescendantOwner {
 public:
  struct Exit { pid_t pid; bool signaled; int code; bool kill_requested; };
  DescendantOwner() {
    if (syscall(SYS_gettid) != getpid()) throw std::runtime_error("Descendant owner requires the native main thread");
    size_t threads = 0;
    for (const auto& task : std::filesystem::directory_iterator("/proc/self/task")) { (void)task; ++threads; }
    if (threads != 1) throw std::runtime_error("Descendant owner requires a single native thread");
    if (occupied_.exchange(true)) throw std::runtime_error("Only one serial descendant owner may be active");
    try {
      if (!Children().empty()) throw std::runtime_error("Descendant owner requires no preexisting children");
      if (prctl(PR_GET_CHILD_SUBREAPER, &prior_) || prctl(PR_SET_CHILD_SUBREAPER, 1))
        throw std::runtime_error("Native child subreaper setup failed");
      enabled_ = true;
    } catch (...) { occupied_ = false; throw; }
  }
  ~DescendantOwner() noexcept {
    // The explicit retirement fence must complete before destruction. Preserve
    // subreaper adoption if physical descendants remain; never invent their exit.
    if (enabled_ && !active_) prctl(PR_SET_CHILD_SUBREAPER, prior_);
    if (!active_) occupied_ = false;
  }
  DescendantOwner(const DescendantOwner&) = delete;
  DescendantOwner& operator=(const DescendantOwner&) = delete;
  template<class Host> void Begin(Host& worker) {
    if (active_) throw std::runtime_error("Invalid native descendant worker admission");
    const auto children = Children();
    worker_ = worker.Pid(); active_ = true; retiring_ = false; exits_.clear(); killed_.clear();
    // A fast worker can already be a zombie with adopted grandchildren by this
    // point. The native serial caller alone may fork between reservation/Begin.
    if (std::find(children.begin(), children.end(), worker.Pid()) == children.end())
      throw std::runtime_error("Native serial worker is not an owned child");
  }
  template<class Host> void BeginRetirement(Host& worker, double now, double grace_ms) {
    if (!active_ || retiring_ || worker.Pid() != worker_ || !worker.Exited())
      throw std::runtime_error("Descendant retirement requires confirmed original worker waitpid");
    if (!std::isfinite(now) || !std::isfinite(grace_ms) || grace_ms < 0 || grace_ms > 120000 || !std::isfinite(now + grace_ms))
      throw std::runtime_error("Invalid native descendant retirement deadline");
    deadline_ = now + grace_ms; retiring_ = true;
  }
  bool Tick(double now) { return Tick(now, [](pid_t) {}); }
  template<class Observer> bool Tick(double now, Observer&& before_wait) {
    if (!retiring_ || !std::isfinite(now)) throw std::runtime_error("Invalid native descendant retirement tick");
    // The primary worker's owner has already reaped its exact PID.
    // Every PID below is a current child and remains reserved until exact waitpid.
    for (const auto pid : Children()) {
      before_wait(pid); // Observation failure retains the actual child and owner.
      int status = 0;
      pid_t result;
      do { result = waitpid(pid, &status, WNOHANG); } while (result < 0 && errno == EINTR);
      if (result == pid) {
        if (!WIFEXITED(status) && !WIFSIGNALED(status)) throw std::runtime_error("Unexpected native descendant status");
        if (exits_.size() >= 4096) throw std::runtime_error("Native descendant exit capacity4096 exceeded");
        exits_.push_back({pid, WIFSIGNALED(status), WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status), killed_.contains(pid)});
      } else if (result < 0) throw std::runtime_error("Native descendant waitpid failed");
      else if (now >= deadline_ && !killed_.contains(pid)) {
        if (killed_.size() >= 4096) throw std::runtime_error("Native descendant kill capacity4096 exceeded");
        if (kill(pid, SIGKILL)) throw std::runtime_error("Native descendant SIGKILL failed");
        killed_.insert(pid);
      }
    }
    // Reaping an intermediate process can adopt its children; observe again.
    if (!Children().empty()) return false;
    active_ = false; retiring_ = false; worker_ = -1;
    return true;
  }
  bool Active() const { return active_; }
  const std::vector<Exit>& Exits() const { return exits_; }
 private:
  static std::vector<pid_t> Children() {
    return ReadOwnedChildren();
  }
  inline static std::atomic<bool> occupied_{false};
  int prior_ = 0; bool enabled_ = false, active_ = false, retiring_ = false;
  pid_t worker_ = -1; double deadline_ = 0;
  std::vector<Exit> exits_; std::set<pid_t> killed_;
};
}
