// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <string>

namespace suitecut {
// Watches the compositor frames macOS shows in a windowed browser's view and
// reports what arrived as one JSON object. Frames are only visible in-process
// when Chromium hands IOSurfaces to the browser process, i.e. with
// --disable-features=RemoteCoreAnimationAPI; otherwise the view holds a remote
// CALayerHost and the report says so. Writes the last frame to |png_path|.
void ProbeWindowFrames(void* view_or_window, int duration_ms, const std::string& png_path,
                       std::function<void(std::string)> done);
}  // namespace suitecut
