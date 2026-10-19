// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "libyuv/scale_argb.h"

struct SuiteCutCaptureRect {
  int x = 0, y = 0, width = 0, height = 0;
};

// The published rectangle and actual fitted pixels share exactly this rounding.
inline bool SuiteCutCaptureContentRect(int source_width, int source_height,
                                      int canvas_width, int canvas_height,
                                      SuiteCutCaptureRect& rect) {
  const auto valid = [](int width, int height) { return width >= 1 && height >= 1 && width <= 3840 && height <= 2160; };
  if (!valid(source_width, source_height) || !valid(canvas_width, canvas_height)) return false;
  const double fit = std::min(static_cast<double>(canvas_width) / source_width,
                              static_cast<double>(canvas_height) / source_height);
  const int width = std::clamp(static_cast<int>(std::round(source_width * fit)), 1, canvas_width);
  const int height = std::clamp(static_cast<int>(std::round(source_height * fit)), 1, canvas_height);
  rect = {(canvas_width - width) / 2, (canvas_height - height) / 2, width, height};
  return true;
}

// Keep recording dimensions stable when the CSS viewport changes. Bounds are
// checked before any multiplication; source viewports may be odd or one pixel.
inline bool SuiteCutFitCapture(const std::vector<uint8_t>& pixels, int source_width, int source_height,
                              int canvas_width, int canvas_height, std::vector<uint8_t>& canvas,
                              SuiteCutCaptureRect* content_rect = nullptr) {
  SuiteCutCaptureRect rect;
  if (&pixels == &canvas ||
      !SuiteCutCaptureContentRect(source_width, source_height, canvas_width, canvas_height, rect) ||
      pixels.size() != static_cast<size_t>(source_width) * source_height * 4) return false;
  if (source_width == canvas_width && source_height == canvas_height) {
    canvas = pixels;
    if (content_rect) *content_rect = rect;
    return true;
  }
  canvas.assign(static_cast<size_t>(canvas_width) * canvas_height * 4, 0);
  // Scaling overwrites every content pixel, including alpha. Initialize opaque
  // black only in the letterbox, rather than walking the entire image again.
  for (int y = 0; y < canvas_height; ++y) {
    const bool content_row = y >= rect.y && y < rect.y + rect.height;
    const int left_end = content_row ? rect.x : canvas_width;
    const int right_start = content_row ? rect.x + rect.width : canvas_width;
    const size_t row = static_cast<size_t>(y) * canvas_width * 4;
    for (int x = 0; x < left_end; ++x) canvas[row + static_cast<size_t>(x) * 4 + 3] = 255;
    for (int x = right_start; x < canvas_width; ++x) canvas[row + static_cast<size_t>(x) * 4 + 3] = 255;
  }
  if (libyuv::ARGBScale(pixels.data(), source_width * 4, source_width, source_height,
      canvas.data() + (static_cast<size_t>(rect.y) * canvas_width + rect.x) * 4, canvas_width * 4,
      rect.width, rect.height, libyuv::kFilterBilinear) != 0) return false;
  if (content_rect) *content_rect = rect;
  return true;
}

inline bool SuiteCutFitCapture(std::vector<uint8_t>& pixels, int source_width, int source_height,
                              int canvas_width, int canvas_height, SuiteCutCaptureRect* content_rect = nullptr) {
  // Preserve the zero-allocation identity path used by existing callers.
  if (source_width == canvas_width && source_height == canvas_height) {
    SuiteCutCaptureRect rect;
    if (!SuiteCutCaptureContentRect(source_width, source_height, canvas_width, canvas_height, rect) ||
        pixels.size() != static_cast<size_t>(source_width) * source_height * 4) return false;
    if (content_rect) *content_rect = rect;
    return true;
  }
  std::vector<uint8_t> canvas;
  if (!SuiteCutFitCapture(pixels, source_width, source_height, canvas_width, canvas_height, canvas, content_rect)) return false;
  pixels.swap(canvas);
  return true;
}
