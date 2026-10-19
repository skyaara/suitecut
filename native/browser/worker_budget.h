// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include "worker_windows_resources.h"
#endif
#if !defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#if defined(__linux__)
#include <cerrno>
#include <sched.h>
#endif

namespace suitecut {
struct WorkerPolicy {
  size_t workers;
  size_t capacity;
  double cpu_capacity;
  uint64_t effective_memory_bytes;  // Zero also represents an unknown memory limit.
  size_t thread_stack_bytes;
};

namespace worker_budget_internal {
inline std::optional<uint64_t> PositiveInteger(std::string_view text) {
  if (text.empty()) return std::nullopt;
  uint64_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !value)
    return std::nullopt;
  return value;
}

// An absent limit covers both unlimited and unreadable/malformed settings.
inline std::optional<double> ParseCpuQuota(const std::string& quota,
                                          const std::string& period) {
  const auto q = PositiveInteger(quota), p = PositiveInteger(period);
  if (!q || !p) return std::nullopt;
  return static_cast<double>(*q) / static_cast<double>(*p);
}

inline std::optional<double> ParseCpuMax(const std::string& contents) {
  std::istringstream input(contents);
  std::string quota, period, extra;
  if (!(input >> quota >> period) || input >> extra) return std::nullopt;
  return ParseCpuQuota(quota, period);
}

inline void Tighten(std::optional<double>& limit, std::optional<double> candidate) {
  if (candidate && std::isfinite(*candidate) && *candidate > 0)
    limit = limit ? std::min(*limit, *candidate) : candidate;
}

inline bool HasController(const std::string& controllers, const std::string& name) {
  std::istringstream input(controllers);
  std::string controller;
  while (std::getline(input, controller, ',')) if (controller == name) return true;
  return false;
}

inline std::string UnescapeMountPath(const std::string& path) {
  std::string result;
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '\\' && i + 3 < path.size() &&
        path[i + 1] >= '0' && path[i + 1] <= '7' &&
        path[i + 2] >= '0' && path[i + 2] <= '7' &&
        path[i + 3] >= '0' && path[i + 3] <= '7') {
      result += static_cast<char>((path[i + 1] - '0') * 64 +
                                  (path[i + 2] - '0') * 8 + path[i + 3] - '0');
      i += 3;
    } else result += path[i];
  }
  return result;
}

inline bool SafeAbsolutePath(const std::string& path) {
  if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos)
    return false;
  std::istringstream input(path);
  std::string component;
  while (std::getline(input, component, '/'))
    if (component == ".." || component == ".") return false;
  return true;
}

// Translate cgroup membership through a mount's root (which may be a subtree).
inline std::optional<std::string> ResolveCgroupPath(const std::string& membership,
                                                   const std::string& root,
                                                   const std::string& mount) {
  if (!SafeAbsolutePath(membership) || !SafeAbsolutePath(root) ||
      !SafeAbsolutePath(mount)) return std::nullopt;
  if (root != "/" && membership != root &&
      (membership.size() <= root.size() || membership.compare(0, root.size(), root) ||
       membership[root.size()] != '/')) return std::nullopt;
  const std::string suffix = root == "/" ? (membership == "/" ? "" : membership)
                                         : membership.substr(root.size());
  if (suffix.empty()) return mount;
  return (mount == "/" ? std::string{} : mount) + suffix;
}

// Share membership/mount translation and ancestor traversal across controllers.
template <typename Visitor>
inline void VisitCgroupAncestors(const std::string& memberships,
                                const std::string& mounts,
                                const std::string& controller, Visitor visit) {
  std::string v2_path, v1_path, line;
  std::istringstream membership_input(memberships);
  while (std::getline(membership_input, line)) {
    const auto first = line.find(':'), second = line.find(':', first == std::string::npos ? 0 : first + 1);
    if (first == std::string::npos || second == std::string::npos) continue;
    const auto controllers = line.substr(first + 1, second - first - 1);
    if (controllers.empty()) v2_path = line.substr(second + 1);
    if (HasController(controllers, controller)) v1_path = line.substr(second + 1);
  }
  std::istringstream mount_input(mounts);
  while (std::getline(mount_input, line)) {
    const auto separator = line.find(" - ");
    if (separator == std::string::npos) continue;
    std::istringstream left(line.substr(0, separator)), right(line.substr(separator + 3));
    std::string id, parent, device, root, mount, options, type, source, super_options;
    if (!(left >> id >> parent >> device >> root >> mount >> options) ||
        !(right >> type >> source >> super_options)) continue;
    const bool v2 = type == "cgroup2";
    if (!v2 && (type != "cgroup" || !HasController(super_options, controller))) continue;
    root = UnescapeMountPath(root);
    mount = UnescapeMountPath(mount);
    auto path = ResolveCgroupPath(v2 ? v2_path : v1_path, root, mount);
    if (!path) continue;
    // Every ancestor can impose a stricter limit than the process's own group.
    for (;;) {
      visit(v2, *path == "/" ? std::string{} : *path);
      if (*path == mount || path->empty() || *path == "/") break;
      const auto slash = path->rfind('/');
      if (slash == std::string::npos) break;
      path->resize(slash ? slash : 1);
    }
  }
}

// The reader makes hierarchy detection testable without changing real cgroups.
template <typename Reader>
inline std::optional<double> CgroupCpuLimit(const std::string& memberships,
                                           const std::string& mounts,
                                           Reader read) {
  std::optional<double> limit;
  Tighten(limit, ParseCpuMax(read("/sys/fs/cgroup/cpu.max")));
  VisitCgroupAncestors(memberships, mounts, "cpu", [&](bool v2, const std::string& path) {
    if (v2) Tighten(limit, ParseCpuMax(read(path + "/cpu.max")));
    else {
      std::istringstream quota_input(read(path + "/cpu.cfs_quota_us"));
      std::istringstream period_input(read(path + "/cpu.cfs_period_us"));
      std::string quota, period, extra;
      if ((quota_input >> quota) && !(quota_input >> extra) &&
          (period_input >> period) && !(period_input >> extra))
        Tighten(limit, ParseCpuQuota(quota, period));
    }
  });
  return limit;
}

inline std::optional<uint64_t> ParseMemoryLimit(const std::string& contents) {
  std::istringstream input(contents);
  std::string token, extra;
  if (!(input >> token) || input >> extra) return std::nullopt;
  uint64_t bytes = 0;
  const auto parsed = std::from_chars(token.data(), token.data() + token.size(), bytes);
  if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
    return std::nullopt;
  return bytes;
}

inline void TightenMemory(std::optional<uint64_t>& limit,
                          std::optional<uint64_t> candidate) {
  if (candidate) limit = limit ? std::min(*limit, *candidate) : candidate;
}

template <typename Reader>
inline std::optional<uint64_t> CgroupMemoryLimit(const std::string& memberships,
                                               const std::string& mounts, Reader read) {
  std::optional<uint64_t> limit;
  TightenMemory(limit, ParseMemoryLimit(read("/sys/fs/cgroup/memory.max")));
  VisitCgroupAncestors(memberships, mounts, "memory", [&](bool v2, const std::string& path) {
    TightenMemory(limit, ParseMemoryLimit(read(path + (v2 ? "/memory.max" : "/memory.limit_in_bytes"))));
  });
  return limit;
}

inline double CpuCapacity(unsigned fallback, std::optional<double> affinity,
                          std::optional<double> quota) {
  double available = affinity && std::isfinite(*affinity) && *affinity > 0
                         ? *affinity : std::max(1u, fallback);
  if (quota && std::isfinite(*quota) && *quota > 0) available = std::min(available, *quota);
  return available;
}

inline size_t WorkerCount(double available_cpus) {
  // Leave half the usable CPU capacity for the browser. Keep 4*workers safe.
  if (!std::isfinite(available_cpus) || available_cpus < 4) return 1;
  const size_t maximum = std::numeric_limits<size_t>::max() / 4;
  const double half = available_cpus / 2;
  return half >= static_cast<double>(maximum) ? maximum : static_cast<size_t>(half);
}

inline WorkerPolicy ComputeWorkerPolicy(double available_cpus,
                                        std::optional<uint64_t> memory_bytes,
                                        size_t stack_bytes) {
  size_t workers = WorkerCount(available_cpus);
  // Reserve at most 1/64 of the effective memory limit for native pool stacks,
  // except that one worker is always available even on extremely tight limits.
  // This budgets stack reservations only: tasks, allocations and CEF have their
  // own memory behavior and are not bounded by this policy.
  if (memory_bytes && stack_bytes) {
    const uint64_t by_memory = *memory_bytes / 64 / stack_bytes;
    if (by_memory < workers) workers = static_cast<size_t>(std::max<uint64_t>(1, by_memory));
  }
  return {workers, workers * 4, available_cpus, memory_bytes.value_or(0), stack_bytes};
}

inline std::optional<uint64_t> PhysicalMemoryBytes() {
#if defined(__APPLE__)
  uint64_t bytes = 0;
  size_t size = sizeof(bytes);
  if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 && bytes)
    return bytes;
#elif defined(_WIN32)
  return WindowsMemoryBytes();
#endif
#if !defined(_WIN32) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
  const long pages = sysconf(_SC_PHYS_PAGES), page_bytes = sysconf(_SC_PAGESIZE);
  if (pages > 0 && page_bytes > 0 &&
      static_cast<uint64_t>(pages) <= std::numeric_limits<uint64_t>::max() /
                                          static_cast<uint64_t>(page_bytes))
    return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_bytes);
#endif
  return std::nullopt;
}

inline size_t DefaultThreadStackBytes() {
#if defined(_WIN32)
  if (const size_t bytes = WindowsThreadStackBytes()) return bytes;
#else
  pthread_attr_t attributes;
  if (pthread_attr_init(&attributes) == 0) {
    size_t bytes = 0;
    const int result = pthread_attr_getstacksize(&attributes, &bytes);
    pthread_attr_destroy(&attributes);
    if (result == 0 && bytes) return bytes;
  }
#endif
  // Conservative reservation estimate when the platform cannot report defaults.
  // The Windows value is only a policy fallback; it adds no execution backend.
  return 8 * 1024 * 1024;
}

inline std::string ReadFile(const std::string& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

inline std::optional<double> AffinityCpuCount() {
#if defined(__linux__)
  // A fixed cpu_set_t fails on hosts with a kernel CPU mask larger than 1024.
  for (size_t bytes = 128; bytes <= 65536; bytes *= 2) {
    std::vector<unsigned long> mask(bytes / sizeof(unsigned long), 0);
    if (sched_getaffinity(0, bytes, reinterpret_cast<cpu_set_t*>(mask.data())) == 0) {
      size_t count = 0;
      for (auto word : mask) while (word) { word &= word - 1; ++count; }
      if (count) return static_cast<double>(count);
      return std::nullopt;
    }
    if (errno != EINVAL) break;
  }
#elif defined(__APPLE__)
  uint32_t cpus = 0;
  size_t size = sizeof(cpus);
  if (sysctlbyname("hw.activecpu", &cpus, &size, nullptr, 0) == 0 && cpus)
    return static_cast<double>(cpus);
#elif defined(_WIN32)
  return WindowsCpuCapacity();
#endif
  return std::nullopt;
}
}  // namespace worker_budget_internal

inline WorkerPolicy AutomaticWorkerPolicy() {
  using namespace worker_budget_internal;
  std::optional<double> quota;
  auto memory = PhysicalMemoryBytes();
#if defined(__linux__)
  const auto memberships = ReadFile("/proc/self/cgroup");
  const auto mounts = ReadFile("/proc/self/mountinfo");
  quota = CgroupCpuLimit(memberships, mounts, ReadFile);
  // Stable configured limits, not memory.current/available or cache fluctuations.
  TightenMemory(memory, CgroupMemoryLimit(memberships, mounts, ReadFile));
#endif
  return ComputeWorkerPolicy(
      CpuCapacity(std::thread::hardware_concurrency(), AffinityCpuCount(), quota),
      memory, DefaultThreadStackBytes());
}

inline size_t AutomaticWorkerCount() { return AutomaticWorkerPolicy().workers; }
}  // namespace suitecut
