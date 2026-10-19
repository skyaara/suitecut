// SPDX-License-Identifier: MIT
#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace suitecut::worker_budget_internal {

inline std::optional<double> WindowsCpuCapacity() {
  const DWORD active = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  std::optional<double> capacity;
  if (active) capacity = static_cast<double>(active);

  // A process affinity mask describes one group, not the entire machine.
  // Bound storage independently of OS results and avoid allocating in policy setup.
  std::array<USHORT, 64> groups{};
  const WORD active_groups = GetActiveProcessorGroupCount();
  USHORT count = active_groups;
  bool single_group = active_groups == 1;
  if (count && count <= groups.size() &&
      GetProcessGroupAffinity(GetCurrentProcess(), &count, groups.data()) &&
      count && count <= active_groups) {
    DWORD assigned = 0;
    bool complete = true;
    for (USHORT i = 0; i < count; ++i) {
      const DWORD processors = GetActiveProcessorCount(groups[i]);
      if (!processors || groups[i] >= active_groups) { complete = false; break; }
      assigned += processors;
    }
    if (complete && assigned)
      capacity = capacity ? (std::min)(*capacity, static_cast<double>(assigned))
                          : static_cast<double>(assigned);
    single_group = count == 1;
  }
  DWORD_PTR process_mask = 0, system_mask = 0;
  if (single_group && GetProcessAffinityMask(GetCurrentProcess(), &process_mask,
                                            &system_mask) && process_mask) {
    unsigned processors = 0;
    for (; process_mask; process_mask &= process_mask - 1) ++processors;
    capacity = capacity ? (std::min)(*capacity, static_cast<double>(processors))
                        : static_cast<double>(processors);
  }

#if defined(JOB_OBJECT_CPU_RATE_CONTROL_ENABLE)
  // A null handle queries the current job; unavailable restrictions remain unknown.
  JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
  if (active && QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation,
                                          &rate, sizeof(rate), nullptr) &&
      (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE)) {
    DWORD maximum = 0;
    if (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP)
      maximum = rate.CpuRate;
    else if (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE)
      // MinRate WORD then MaxRate WORD overlay CpuRate, including in older SDKs.
      maximum = HIWORD(rate.CpuRate);
    if (maximum && maximum <= 10000) {
      const double quota = static_cast<double>(active) * maximum / 10000.0;
      capacity = capacity ? (std::min)(*capacity, quota) : quota;
    }
  }
#endif
  return capacity;
}

inline std::optional<std::uint64_t> WindowsMemoryBytes() {
  std::optional<std::uint64_t> bytes;
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys)
    bytes = static_cast<std::uint64_t>(memory.ullTotalPhys);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION job{};
  if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation,
                               &job, sizeof(job), nullptr)) {
    const auto tighten = [&](SIZE_T limit) {
      if (limit) bytes = bytes ? (std::min)(*bytes, static_cast<std::uint64_t>(limit))
                               : static_cast<std::uint64_t>(limit);
    };
    if (job.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY)
      tighten(job.ProcessMemoryLimit);
    if (job.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_JOB_MEMORY)
      tighten(job.JobMemoryLimit);
  }
  return bytes;
}

inline std::size_t WindowsThreadStackBytes() {
  // Default Windows thread creation uses the executable's PE stack reservation.
  const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
  if (!base) return 0;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
      dos->e_lfanew < static_cast<LONG>(sizeof(*dos)) ||
      dos->e_lfanew > static_cast<LONG>(4096 - sizeof(IMAGE_NT_HEADERS))) return 0;
  const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (image->Signature != IMAGE_NT_SIGNATURE ||
      image->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) ||
      image->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) return 0;
  const std::uint64_t reserve = image->OptionalHeader.SizeOfStackReserve;
  if (reserve < 64 * 1024 || reserve > (std::uint64_t{1} << 40) ||
      reserve > (std::numeric_limits<std::size_t>::max)()) return 0;
  return static_cast<std::size_t>(reserve);
}

}  // namespace suitecut::worker_budget_internal
#endif  // defined(_WIN32)
