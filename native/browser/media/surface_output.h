// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace suitecut {
// Hands a page's painted view to another process without copying pixels. Each
// frame is converted on the GPU into an NV12 IOSurface ('420f', BT.601 full
// range) and sent as one Mach message to a receiver that checked a port in to
// the bootstrap namespace under |name|. The message is 64 bytes plus the
// kernel's trailer:
//
//   header  msgh_id 'GBS1', complex, no reply port
//   body    one port descriptor: a send right to the frame's IOSurface
//   inline  u32 magic 'GBS1', u32 width, u32 height, u32 reserved (0),
//           u64 timestamp (microseconds, mach_absolute_time)
//
// A send never waits: a receiver whose queue is full misses that frame. A
// surface is drawn into again only once no process holds it
// (IOSurfaceIsInUse), and never right after it was sent, so a slow receiver
// runs the ring dry and frames are dropped, never torn.
class SurfaceOutput {
 public:
  enum class Sent { delivered, dropped, closed };
  struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
  };
  // Null with |error| set when the name is malformed or nobody checked it in.
  static std::unique_ptr<SurfaceOutput> Open(const std::string& name, std::string& error);
  ~SurfaceOutput();
  SurfaceOutput(const SurfaceOutput&) = delete;
  SurfaceOutput& operator=(const SurfaceOutput&) = delete;
  // Keeps a copy of a borrowed popup surface (a <select> list, say) to draw
  // over the next views at |at|, in the view's pixels. Null clears it.
  void SetPopup(void* surface, Rect visible, Rect at);
  // Converts |visible| of the borrowed BGRA |surface| and sends it. Only
  // `closed` is final: the receiver's port is gone.
  Sent Send(void* surface, Rect visible);
  static bool ValidName(const std::string& name);

 private:
  struct Impl;
  explicit SurfaceOutput(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}  // namespace suitecut
