// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <optional>

// CEF versions/platforms have supplied both epoch and monotonic audio PTS.
// Anchor the first packet to the host clock and preserve subsequent PTS deltas.
// Re-anchor after a restart or source-clock discontinuity. Late callbacks must
// not move the sample timeline forward when the host is under CPU pressure.
class SuiteCutAudioClock {
 public:
  void Reset() { offset_.reset(); previous_pts_.reset(); }
  double Map(double pts_ms, double received_ms) {
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
