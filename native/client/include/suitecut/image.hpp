// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace suitecut {
struct ImageLimits {
  std::uint32_t max_dimension = 16384;
  std::size_t max_pixels = 16 * 1024 * 1024;
  std::size_t max_encoded_bytes = 64 * 1024 * 1024;
  // Each decoded RGBA image and inflated scanline stream is bounded separately.
  std::size_t max_decoded_bytes = 128 * 1024 * 1024;
};
struct PngImage {
  std::uint32_t width = 0, height = 0;
  // Straight-alpha, row-major RGBA8; 16-bit PNG samples retain their high byte.
  std::vector<std::uint8_t> rgba;
};
// Static PNG: grayscale, RGB, palette, grayscale-alpha and RGBA, all standard
// bit depths and Adam7. Chunk CRCs, order and complete zlib stream are checked.
// APNG is rejected. Color-profile/gamma conversion is not performed.
PngImage decode_png(const std::vector<std::uint8_t>& bytes, ImageLimits limits = {});
std::vector<std::uint8_t> encode_png(const PngImage& image, ImageLimits limits = {});
struct ImageCompareOptions {
  std::uint8_t channel_tolerance = 0;
  bool include_alpha = true;
  bool make_diff = false;
  ImageLimits limits;
};
struct ImageComparison {
  bool equal = false, dimensions_match = false;
  std::uint32_t expected_width = 0, expected_height = 0;
  std::uint32_t actual_width = 0, actual_height = 0;
  std::uint64_t compared_pixels = 0, changed_pixels = 0;
  double changed_ratio = 0;
  // Optional max-width/max-height canvas: magenta changed/missing pixels,
  // transparent unchanged pixels. No color-space or perceptual comparison.
  std::vector<std::uint8_t> diff_png;
};
// Compare on the bounding canvas. Pixels absent from either image count as
// changed; dimension mismatch is always unequal, even at tolerance 255.
ImageComparison compare_png(const std::vector<std::uint8_t>& expected,
                            const std::vector<std::uint8_t>& actual,
                            ImageCompareOptions options = {});
} // namespace suitecut
