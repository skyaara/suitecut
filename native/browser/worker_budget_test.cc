// SPDX-License-Identifier: MIT
#include "worker_budget.h"
#include <cassert>
#include <map>

using namespace suitecut::worker_budget_internal;

int main() {
  assert(ParseCpuMax("400000 100000\n") == 4.0);
  assert(ParseCpuMax("150000 100000") == 1.5);
  assert(ParseCpuMax("1 100000") == 0.00001);
  for (const auto* malformed : {"", "max 100000", "-1 100000", "0 100000", "4 0",
                                "4 -1", "four 100000", "4 100000 trailing",
                                "18446744073709551616 100000", "4"})
    assert(!ParseCpuMax(malformed));

  assert(WorkerCount(CpuCapacity(192, 168, 4)) == 2);
  assert(WorkerCount(CpuCapacity(192, 1, 4)) == 1);
  assert(WorkerCount(CpuCapacity(192, 168, 0.5)) == 1);
  assert(WorkerCount(CpuCapacity(8, std::nullopt, std::nullopt)) == 4);
  assert(WorkerCount(CpuCapacity(0, std::nullopt, std::nullopt)) == 1);
  for (const auto cpus : {0.25, 1.0, 2.0, 3.0, 3.99}) assert(WorkerCount(cpus) == 1);
  assert(WorkerCount(4) == 2 && WorkerCount(5.9) == 2);
  assert(WorkerCount(6) == 3 && WorkerCount(7.9) == 3);
  assert(WorkerCount(8) == 4 && WorkerCount(1024) == 512);

  constexpr uint64_t mib = 1024 * 1024;
  for (const auto cpus : {1, 4, 16, 64, 128}) {
    const auto policy = ComputeWorkerPolicy(cpus, 64 * 1024 * mib, 8 * mib);
    assert(policy.workers == static_cast<size_t>(std::max(1, cpus / 2)));
    assert(policy.capacity == policy.workers * 4);
    assert(policy.cpu_capacity == cpus);
    assert(policy.effective_memory_bytes == 64 * 1024 * mib);
    assert(policy.thread_stack_bytes == 8 * mib);
  }
  assert(ComputeWorkerPolicy(128, 1024 * mib, 8 * mib).workers == 2);
  assert(ComputeWorkerPolicy(128, 512 * mib - 1, 8 * mib).workers == 1);
  assert(ComputeWorkerPolicy(128, 0, 8 * mib).workers == 1);
  assert(ComputeWorkerPolicy(128, std::nullopt, 8 * mib).workers == 64);
  assert(ComputeWorkerPolicy(128, 1024 * mib, 0).workers == 64);
  assert(ComputeWorkerPolicy(0.25, 64 * 1024 * mib, 8 * mib).workers == 1);
  const auto maximum = ComputeWorkerPolicy(std::numeric_limits<double>::max(),
                                           std::nullopt, 8 * mib);
  assert(maximum.workers == std::numeric_limits<size_t>::max() / 4);
  assert(maximum.capacity >= maximum.workers && maximum.capacity >= 2);
  assert(WorkerCount(std::numeric_limits<double>::quiet_NaN()) == 1);

  assert(ParseMemoryLimit("0\n") == 0);
  assert(ParseMemoryLimit("536870912\n") == 512 * mib);
  for (const auto* malformed : {"", "max", "-1", "1 trailing", "+2",
                                "18446744073709551616", "broken"})
    assert(!ParseMemoryLimit(malformed));

  const std::string v2_mount = "35 24 0:29 / /sys/fs/cgroup rw - cgroup2 cgroup rw\n";
  std::map<std::string, std::string> files{
      {"/sys/fs/cgroup/cpu.max", "max 100000"},
      {"/sys/fs/cgroup/team/cpu.max", "400000 100000"},
      {"/sys/fs/cgroup/team/browser/cpu.max", "800000 100000"}};
  const auto read = [&files](const std::string& path) {
    const auto found = files.find(path);
    return found == files.end() ? std::string{} : found->second;
  };
  assert(CgroupCpuLimit("0::/team/browser\n", v2_mount, read) == 4);
  files["/sys/fs/cgroup/cpu.max"] = "200000 100000";
  assert(CgroupCpuLimit("0::/team/browser\n", v2_mount, read) == 2);
  files["/sys/fs/cgroup/cpu.max"] = "malformed";
  assert(CgroupCpuLimit("0::/team/browser\n", v2_mount, read) == 4);
  assert(!CgroupCpuLimit("invalid", "invalid", read));
  assert(!CgroupCpuLimit("0::/../team\n", v2_mount, read));

  assert(ResolveCgroupPath("/team/browser", "/team", "/mounted") == "/mounted/browser");
  assert(ResolveCgroupPath("/team", "/team", "/mounted") == "/mounted");
  assert(ResolveCgroupPath("/", "/", "/mounted") == "/mounted");
  assert(ResolveCgroupPath("/", "/", "/") == "/");
  assert(!ResolveCgroupPath("/team2/browser", "/team", "/mounted"));
  assert(!ResolveCgroupPath("/team/../browser", "/team", "/mounted"));
  files["/mounted/browser/cpu.max"] = "600000 100000";
  files["/mounted/cpu.max"] = "300000 100000";
  assert(CgroupCpuLimit("0::/team/browser\n",
      "35 24 0:29 /team /mounted rw - cgroup2 cgroup rw\n", read) == 3);

  files["/cpu/group/cpu.cfs_quota_us"] = "500000\n";
  files["/cpu/group/cpu.cfs_period_us"] = "100000\n";
  files["/cpu/cpu.cfs_quota_us"] = "200000\n";
  files["/cpu/cpu.cfs_period_us"] = "100000\n";
  const std::string v1_mount = "35 24 0:29 / /cpu rw - cgroup cgroup rw,cpu,cpuacct\n";
  assert(CgroupCpuLimit("4:cpu,cpuacct:/group\n", v1_mount, read) == 2);
  files["/cpu/cpu.cfs_quota_us"] = "-1\n";
  assert(CgroupCpuLimit("4:cpu,cpuacct:/group\n", v1_mount, read) == 5);
  files["/cpu/group/cpu.cfs_quota_us"] = "broken";
  assert(!CgroupCpuLimit("4:cpu,cpuacct:/group\n", v1_mount, read));

  files["/sys/fs/cgroup/memory.max"] = "max";
  files["/sys/fs/cgroup/team/memory.max"] = "1073741824";
  files["/sys/fs/cgroup/team/browser/memory.max"] = "4294967296";
  assert(CgroupMemoryLimit("0::/team/browser\n", v2_mount, read) == 1024 * mib);
  files["/sys/fs/cgroup/memory.max"] = "536870912";
  assert(CgroupMemoryLimit("0::/team/browser\n", v2_mount, read) == 512 * mib);
  files["/sys/fs/cgroup/memory.max"] = "malformed";
  assert(CgroupMemoryLimit("0::/team/browser\n", v2_mount, read) == 1024 * mib);
  assert(!CgroupMemoryLimit("invalid", "invalid", read));
  assert(!CgroupMemoryLimit("0::/../team\n", v2_mount, read));
  files["/mounted/browser/memory.max"] = "1073741824";
  files["/mounted/memory.max"] = "536870912";
  assert(CgroupMemoryLimit("0::/team/browser\n",
      "35 24 0:29 /team /mounted rw - cgroup2 cgroup rw\n", read) == 512 * mib);

  const std::string memory_mount =
      "35 24 0:29 / /memory rw - cgroup cgroup rw,memory\n";
  files["/memory/group/memory.limit_in_bytes"] = "2147483648";
  files["/memory/memory.limit_in_bytes"] = "1073741824";
  assert(CgroupMemoryLimit("7:memory:/group\n", memory_mount, read) == 1024 * mib);
  files["/memory/memory.limit_in_bytes"] = "9223372036854771712";
  assert(CgroupMemoryLimit("7:memory:/group\n", memory_mount, read) == 2048 * mib);
  files["/memory/group/memory.limit_in_bytes"] = "broken";
  std::optional<uint64_t> memory = 512 * mib;
  TightenMemory(memory, CgroupMemoryLimit("7:memory:/group\n", memory_mount, read));
  assert(memory == 512 * mib);  // A v1 unlimited sentinel cannot enlarge physical RAM.
  TightenMemory(memory, std::nullopt);
  assert(memory == 512 * mib);  // Malformed/unavailable limits retain the fallback.
  files["/memory/memory.limit_in_bytes"] = "broken";
  assert(!CgroupMemoryLimit("7:memory:/group\n", memory_mount, read));
  memory = 16 * 1024 * mib;
  TightenMemory(memory, CgroupMemoryLimit("0::/team/browser\n", v2_mount, read));
  assert(memory == 1024 * mib);
  assert(ComputeWorkerPolicy(128, memory, 8 * mib).workers == 2);
}
