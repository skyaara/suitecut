// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// CEF supplies the complete BGRA surface and the rectangles changed since its
// previous paint. Retain unchanged pixels without reading the whole surface.
// A new surface or an unusable damage list always takes a complete snapshot.
template <class Rectangles>
inline bool SuiteCutUpdateCapture(std::vector<uint8_t>& pixels, const void* buffer,
                                 int width, int height, int previous_width,
                                 int previous_height, const Rectangles& dirty) {
  if (!buffer || width < 1 || height < 1 || width > 3840 || height > 2160) return false;
  const auto* source = static_cast<const uint8_t*>(buffer);
  const size_t stride = static_cast<size_t>(width) * 4;
  const size_t bytes = stride * height;
  bool full = width != previous_width || height != previous_height || pixels.size() != bytes || dirty.empty();
  size_t area = 0;
  if (!full) for (const auto& rect : dirty) {
    if (rect.x < 0 || rect.y < 0 || rect.width < 1 || rect.height < 1 ||
        rect.x > width || rect.y > height || rect.width > width - rect.x || rect.height > height - rect.y) {
      full = true; break;
    }
    area += static_cast<size_t>(rect.width) * rect.height;
    if (area >= static_cast<size_t>(width) * height) { full = true; break; }
  }
  if (full) {
    pixels.assign(source, source + bytes);
    return true;
  }
  for (const auto& rect : dirty) for (int y = rect.y; y < rect.y + rect.height; ++y) {
    const size_t offset = static_cast<size_t>(y) * stride + static_cast<size_t>(rect.x) * 4;
    std::memcpy(pixels.data() + offset, source + offset, static_cast<size_t>(rect.width) * 4);
  }
  return true;
}
