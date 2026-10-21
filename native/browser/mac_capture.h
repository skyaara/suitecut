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
// Owns at most 64 MiB of reusable surfaces. Copy completes on the GPU before
// CEF's borrowed texture expires. Readback runs on the existing media worker.
class MacCapturePool {
 public:
  MacCapturePool(); ~MacCapturePool();
  std::shared_ptr<MacCaptureFrame> Copy(void* surface,int x,int y,int width,int height,bool nv12=false);
 private:
  struct Impl; std::shared_ptr<Impl> impl_;
};
}
