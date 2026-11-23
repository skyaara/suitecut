// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace suitecut {
class MacCaptureFrame;
// A hardware-only H.264 session. Four in-flight frames bound GPU ownership;
// callbacks write Annex B into the caller's already-private file descriptor.
class MacNativeEncoder {
 public:
  MacNativeEncoder(int output,int width,int height,double fps,uint64_t max_bytes,std::function<void()> check);
  ~MacNativeEncoder();
  void Write(const std::shared_ptr<MacCaptureFrame>& surface,const std::vector<uint8_t>& nv12,bool preparation=false);
  void Drain();
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
