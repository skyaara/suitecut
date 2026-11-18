// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
namespace suitecut {
// Chromium's macOS TimeTicks uses mach_absolute_time, unlike the per-capture
// relative timestamp attached to compositor video frames.
double MacNativeTimeTicksMs();
bool CopyMacSurface(void* surface,int x,int y,int width,int height,std::vector<uint8_t>& bgra);
class MacCapturePool;
void PrepareMacCapture();
class MacCaptureFrame {
 public:
  ~MacCaptureFrame();
  int width=0,height=0;
  bool nv12=false;
  bool Read(std::vector<uint8_t>& bgra) const;
  void* PixelBuffer() const;
 private:
  struct Impl; std::unique_ptr<Impl> impl_;
  MacCaptureFrame(); friend class MacCapturePool;
};
// Caches at most |max_bytes| (at least 64 MiB) of idle reusable surfaces; frames
// in use are not limited by it. Copy
// completes on the GPU before CEF's borrowed texture expires. Readback runs on
// the existing media worker.
class MacCapturePool {
 public:
  explicit MacCapturePool(uint64_t max_bytes=64ULL*1024*1024); ~MacCapturePool();
  std::shared_ptr<MacCaptureFrame> Copy(void* surface,int x,int y,int width,int height,bool nv12=false);
 private:
  struct Impl; std::shared_ptr<Impl> impl_;
};
}
