// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace suitecut {
struct RenderOptions {
  std::vector<std::string> inputs;
  std::string destination,graph,container="mp4",video_codec="libx264",audio_codec="aac",pixel_format="yuv420p";
  std::map<std::string,std::string> video_options;
  int fps=30,audio_bitrate=160000;
  double duration_ms=0;
  bool audio=false,full_range=false;
};
struct RenderStatus {uint64_t video_frames=0,audio_samples=0;};
RenderStatus RenderMedia(const RenderOptions& config,const std::function<bool()>& cancelled);
}
