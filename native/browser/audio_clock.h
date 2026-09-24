// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <optional>

// CEF versions/platforms have supplied both epoch and monotonic audio PTS.
// Anchor the first packet to the host clock and preserve subsequent PTS deltas.
// Re-anchor after a restart or large discontinuity; never silently clamp to zero.
class SuiteCutAudioClock {
 public:
  void Reset() { offset_.reset(); }
  double Map(double pts_ms, double received_ms) {
    if (!offset_ || std::abs(pts_ms + *offset_ - received_ms) > 250.0)
      offset_ = received_ms - pts_ms;
    return pts_ms + *offset_;
  }
 private:
  std::optional<double> offset_;
};
