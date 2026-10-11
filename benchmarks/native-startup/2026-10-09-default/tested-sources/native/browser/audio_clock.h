// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <optional>

// CEF versions/platforms have supplied both epoch and monotonic audio PTS.
// Convert epoch PTS with a current wall/steady-clock pair to avoid clock slew.
// For older monotonic PTS, preserve deltas and re-anchor on a discontinuity.
// Late callbacks must not move the sample timeline under CPU pressure.
class SuiteCutAudioClock {
 public:
  void Reset() { offset_.reset(); previous_pts_.reset(); }
  double Map(double pts_ms, double received_ms, double epoch_ms = 0) {
    // Current CEF defines PTS in Unix milliseconds. Convert against the current
    // wall/steady-clock pair, rather than preserving wall-clock slew as A/V drift.
    if(epoch_ms>1e12 && pts_ms>1e12 && std::abs(pts_ms-epoch_ms)<10000) {
      previous_pts_=pts_ms;
      return received_ms + pts_ms - epoch_ms;
    }
    if (!offset_ || (previous_pts_ && pts_ms < *previous_pts_) ||
        pts_ms + *offset_ > received_ms + 250.0)
      offset_ = received_ms - pts_ms;
    previous_pts_ = pts_ms;
    return pts_ms + *offset_;
  }
 private:
  std::optional<double> offset_;
  std::optional<double> previous_pts_;
};
