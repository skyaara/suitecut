// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <optional>

// CEF versions/platforms have supplied both epoch and monotonic audio PTS.
// Map a known native source clock directly through its calibrated host offset.
// Map legacy epoch PTS through the host wall/monotonic clock pair. The first
// callback may already be late; anchoring it to receipt shifts the whole track.
// Unknown monotonic PTS retain the legacy receipt anchor and subsequent deltas.
// Re-anchor after a restart or source-clock discontinuity. Late callbacks must
// not move the sample timeline forward when the host is under CPU pressure.
class SuiteCutAudioClock {
 public:
  void Reset() { offset_.reset(); previous_pts_.reset(); }
  double Map(double pts_ms, double received_ms, std::optional<double> epoch_ms = std::nullopt,
             std::optional<double> source_to_host_offset = std::nullopt) {
    if(source_to_host_offset&&std::abs(pts_ms+*source_to_host_offset-received_ms)<10000){
      offset_=source_to_host_offset;previous_pts_=pts_ms;return pts_ms+*offset_;
    }
    if (!offset_ || (previous_pts_ && pts_ms < *previous_pts_) ||
        pts_ms + *offset_ > received_ms + 250.0)
      offset_ = epoch_ms && std::abs(pts_ms-*epoch_ms)<10000 ?
          received_ms-*epoch_ms : received_ms-pts_ms;
    previous_pts_ = pts_ms;
    return pts_ms + *offset_;
  }
 private:
  std::optional<double> offset_;
  std::optional<double> previous_pts_;
};
