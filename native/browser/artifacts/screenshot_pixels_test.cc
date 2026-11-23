// SPDX-License-Identifier: MIT
#include "artifacts/screenshot_pixels.h"
#include <cassert>
#include <cmath>
using namespace suitecut;
ScreenshotRaster Solid(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  ScreenshotRaster image;
  image.width = width;
  image.height = height;
  for (int i = 0; i < width * height; ++i) image.rgba.insert(image.rgba.end(), {r, g, b, a});
  return image;
}
int main() {
  const auto red = Solid(2, 2, 255, 0, 0);
  auto actual = red;
  assert(ScreenshotPixelsEqual(red, actual));
  auto delta = CompareScreenshotPixels(red, actual, 0);
  assert(delta.dimensions_match && delta.different == 0 && delta.total == 4);
  actual.rgba[4] = 0;
  actual.rgba[6] = 255;
  assert(!ScreenshotPixelsEqual(red, actual));
  delta = CompareScreenshotPixels(red, actual, 0.2);
  assert(delta.different == 1 && delta.total == 4 && delta.dimensions_match);
  assert(CompareScreenshotPixels(red, actual, 1).different == 0);
  const auto white = Solid(1, 1, 255, 255, 255);
  const auto transparent = Solid(1, 1, 13, 71, 222, 0);
  assert(CompareScreenshotPixels(white, transparent, 0).different == 0);
  assert(!ScreenshotPixelsEqual(white, transparent));
  delta = CompareScreenshotPixels(Solid(2, 1, 255, 0, 0), Solid(1, 2, 255, 0, 0), 1);
  assert(!delta.dimensions_match && delta.different == 2 && delta.total == 3);
  // Crossing extreme valid aspect ratios must compare only the overlap,
  // without allocating or visiting a268M-pixel bounding canvas.
  delta = CompareScreenshotPixels(Solid(16384, 128, 255, 0, 0), Solid(128, 16384, 255, 0, 0), 0);
  const size_t pixels = size_t(16384) * 128;
  assert(delta.different == 2 * pixels - 2 * 128 * 128 && delta.total == 2 * pixels - 128 * 128);
  auto budget = std::make_shared<ScreenshotByteBudget>();
  std::string error;
  auto diff = ScreenshotDifferenceImage(red, actual, 0.2, budget, error);
  assert(diff && error.empty() && budget->Used() == 16);
  assert(diff->rgba[4] == 255 && diff->rgba[5] == 0 && diff->rgba[6] == 0);
  diff.reset();
  assert(budget->Used() == 0);
  const std::vector<uint8_t> png{
      137, 80,  78, 71,  13,  10, 26,  10,  0,   0,   0,   13,  73, 72,  68, 82, 0,  0,   0,
      2,   0,   0,  0,   2,   8,  6,   0,   0,   0,   114, 182, 13, 36,  0,  0,  0,  17,  73,
      68,  65,  84, 120, 156, 99, 248, 207, 192, 240, 31,  132, 25, 96,  12, 0,  71, 202, 7,
      249, 103, 89, 110, 183, 0,  0,   0,   0,   73,  69,  78,  68, 174, 66, 96, 130};
  int width = 0, height = 0;
  assert(ScreenshotPNGDimensions(png, width, height, error) && width == 2 && height == 2);
  auto lease = ReserveScreenshotPNG(png, budget, width, height, error);
  assert(lease && budget->Used() == 48);
  lease.reset();
  assert(budget->Used() == 0);
  // No dimension cap: a header past the old 16384/2M-pixel limit only books
  // ledger bytes (nothing is allocated here); CEF's decoder judges the image.
  auto wide = png;
  wide[16] = 0;
  wide[17] = 0;
  wide[18] = 0x40;
  wide[19] = 1;
  lease = ReserveScreenshotPNG(wide, budget, width, height, error);
  assert(lease && width == 16385 && height == 2 && budget->Used() == size_t(16385) * 2 * 12);
  lease.reset();
  assert(budget->Used() == 0);
  // Integer validity stays: widths past int and byte counts past size_t fail.
  auto malicious = png;
  malicious[16] = 128;
  malicious[17] = 0;
  malicious[18] = 0;
  malicious[19] = 0;
  error.clear();
  assert(!ReserveScreenshotPNG(malicious, budget, width, height, error) &&
         error.find("dimensions") != std::string::npos && budget->Used() == 0);
  auto huge = png;
  for (int i = 16; i < 24; ++i) huge[i] = (i % 4 == 0) ? 127 : 255;
  error.clear();
  assert(!ReserveScreenshotPNG(huge, budget, width, height, error) &&
         error.find("overflow") != std::string::npos && budget->Used() == 0);
  auto zero = png;
  zero[16] = zero[17] = zero[18] = zero[19] = 0;
  assert(!ReserveScreenshotPNG(zero, budget, width, height, error) && budget->Used() == 0);
  // No canvas cap: a diff canvas just past the old 2M pixels is built whole.
  error.clear();
  diff = ScreenshotDifferenceImage(Solid(1500, 1400, 255, 0, 0), Solid(1400, 1500, 255, 0, 0), 0,
                                   budget, error);
  assert(diff && error.empty() && diff->width == 1500 && diff->height == 1500 &&
         budget->Used() == size_t(1500) * 1500 * 4);
  diff.reset();
  assert(budget->Used() == 0);
  // No byte budget: reservations past the old 64 MiB limit are still admitted;
  // only an overflowing count is refused.
  auto held = budget->Reserve(64 * 1024 * 1024);
  auto more = budget->Reserve(65 * 1024 * 1024);
  assert(held && more && budget->Used() == 129 * 1024 * 1024);
  assert(!budget->Reserve(std::numeric_limits<size_t>::max()));
  more.reset();
  held.reset();
  assert(budget->Used() == 0);
}
