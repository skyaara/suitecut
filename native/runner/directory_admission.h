// SPDX-License-Identifier: MIT
#pragma once
#include "suite_file_owner.h"
#include "directory_actor_timing.h"
#include "repeat_execution.h"
#include "repeat_group_partition.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace suitecut::runner {
// Native-only immutable admission. Not serialized by the author host.
struct DirectoryAdmission {
  SuiteFileToken file;std::string path;std::shared_ptr<const std::string> protocol;
  std::optional<size_t> project;std::optional<NativeRepeatRole> project_role;std::vector<uint64_t> tests;
  bool collection=false,defer_publication=false;
  std::shared_ptr<const NativeRepeatSelection> repeat_selection;std::optional<size_t> repeat_slice;
  std::shared_ptr<const NativeRepeatGroupPartition> repeat_groups;std::optional<size_t> repeat_group;
  std::shared_ptr<DirectoryActorTiming> timing;
  std::function<void(int64_t)> actual_host_started;
};
}
