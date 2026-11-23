// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace suitecut {
// The compositor frames macOS shows in a windowed browser, delivered as the
// IOSurface Chromium hands the browser process, on the main thread, the moment
// the window's layer receives it. Frames are only in this process with
// --disable-features=RemoteCoreAnimationAPI; by default the window holds a
// remote CALayerHost instead and nothing arrives.
//
// The surface is borrowed: Chromium recycles it for later frames, so copy it
// before returning. Construction delivers the frame already on screen.
class WindowFrames {
 public:
  using Frame = std::function<void(void* io_surface, int width, int height, double timestamp_ms)>;
  WindowFrames(void* view_or_window, Frame frame);
  ~WindowFrames();
  WindowFrames(const WindowFrames&) = delete;
  WindowFrames& operator=(const WindowFrames&) = delete;
  // A CALayerHost was seen: Chromium is drawing remotely and no frames will come.
  bool Remote() const;
  // Delivers the frame on screen again, now. A static page sends no new frames.
  void Redeliver();

  struct Impl;  // public so the layer hook can reach it

 private:
  std::shared_ptr<Impl> impl_;
};

// The clock WindowFrames timestamps use, in milliseconds.
double WindowFramesNow();

// Diagnostics: watches |duration_ms| of frames and reports counts, rate, gaps,
// size and copy cost as one JSON object; writes the last frame to |png_path|.
void ProbeWindowFrames(void* view_or_window, int duration_ms, const std::string& png_path,
                       std::function<void(std::string)> done);
}  // namespace suitecut
