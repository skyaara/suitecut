// SPDX-License-Identifier: MIT
#pragma once
#include "media_engine.h"
namespace suitecut {
struct ProbeStream {
  std::string kind,codec,pixel_format,color_range,color_space,color_transfer,color_primaries,time_base;
  double duration_ms=0,frame_rate=0;int width=0,height=0,channels=0,sample_rate=0;bool variable=false;
};
struct ProbeResult {double duration_ms=0;int width=0,height=0;bool audio=false;std::string format;std::vector<ProbeStream> streams;};
ProbeResult ProbeMedia(const std::string& input);
// Decodes, crops in time, scales and encodes a video-only checkpoint natively.
MediaStatus CutVideo(const std::string& input, MediaOptions output, double start_ms, double duration_ms);
}
