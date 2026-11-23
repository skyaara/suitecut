// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace suitecut {
// A background native file operation. The caller retains its physical job and
// PNG byte reservations until this function returns after actual close/rollback.
// The assertion outcome is already frozen; this independent finalization clock
// cannot start another capture or extend the original assertion deadline.
struct ScreenshotArtifactPublication {
  bool committed=false,rollback=false,cleanup_failed=false;
  std::string actual_path,diff_path,error,cleanup_error;
  int error_number=0;
  uint64_t directory_device=0,directory_inode=0;
};
ScreenshotArtifactPublication WriteScreenshotArtifacts(
    const std::string& root,const std::vector<uint8_t>& actual,
    const std::vector<uint8_t>& diff,double finalization_deadline,
    const std::atomic_bool& canceled,const std::function<double()>& now);
}
