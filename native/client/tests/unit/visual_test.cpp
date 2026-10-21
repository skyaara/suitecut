// SPDX-License-Identifier: MIT
#include "suitecut/visual.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
template<class F> void rejects(F work) {
  bool rejected = false;
  try { work(); } catch (const std::exception&) { rejected = true; }
  check(rejected, "Expected visual validation failure");
}
std::vector<std::uint8_t> header(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> result{137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R'};
  for (auto dimension : {width, height})
    for (int shift : {24,16,8,0}) result.push_back(static_cast<std::uint8_t>(dimension >> shift));
  return result;
}
} // namespace
int main() {
  try {
    using namespace suitecut;
    check(visual_png_dimensions(header(2048, 1000)) == std::pair{2048U, 1000U},
          "PNG encoded dimensions were not retained");
    rejects([] { visual_png_dimensions(header(0, 1)); });
    rejects([] { visual_png_dimensions(header(32769, 1)); });
    rejects([] { visual_png_dimensions(header(16384, 16384)); });
    auto bytes = header(1, 1); bytes.resize(23);
    rejects([&] { visual_png_dimensions(bytes); });
    bytes = header(1, 1); bytes[12] = 'J';
    rejects([&] { visual_png_dimensions(bytes); });
    bytes = header(1, 1); bytes[11] = 12;
    rejects([&] { visual_png_dimensions(bytes); });
    bytes = header(1, 1); bytes[0] = 0;
    rejects([&] { visual_png_dimensions(bytes); });

    check(visual_clip_to_viewport(800, 600) == VisualRect{0, 0, 800, 600},
          "Default clip was not the viewport");
    const auto crop = visual_clip_to_viewport(800, 600, VisualRect{-20, 30, 120, 700});
    check(crop == VisualRect{0, 30, 100, 570}, "Partially outside clip was not intersected");
    check(visual_clip_to_viewport(800, 600, VisualRect{750, 550, 100, 100}) ==
              VisualRect{750, 550, 50, 50}, "Bottom/right crop intersection failed");
    rejects([] { visual_clip_to_viewport(800, 600, VisualRect{800, 0, 1, 1}); });
    rejects([] { visual_clip_to_viewport(800, 600, VisualRect{-100, 0, 10, 1}); });
    rejects([] { visual_clip_to_viewport(800, 600, VisualRect{0, 0, -1, 1}); });
    rejects([] { visual_clip_to_viewport(0, 600); });
    rejects([] { visual_clip_to_viewport(std::numeric_limits<double>::infinity(), 600); });
    rejects([] { visual_clip_to_viewport(800, 600,
        VisualRect{0, 0, std::numeric_limits<double>::quiet_NaN(), 1}); });

    // Fractional crop, HiDPI and rounded independent axes: using an assumed
    // DPR or one shared factor would select a different CSS point.
    VisualCaptureMetadata capture{"test", 201, 101, {25.5, 40.25, 100.25, 50.75},
                                   100.25 / 201, 50.75 / 101};
    const auto point = visual_pixel_to_css(capture, 100.5, 50.5);
    check(std::abs(point.x - 75.625) < 1e-12 && std::abs(point.y - 65.625) < 1e-12,
          "Crop/HiDPI mapping lost an axis or offset");
    check(visual_pixel_to_css(capture, 0, 0).x == 25.5, "Image origin lost clip offset");
    (void)visual_pixel_to_css(capture, 200.999, 100.999);
    rejects([&] { visual_pixel_to_css(capture, 201, 0); });
    rejects([&] { visual_pixel_to_css(capture, 0, 101); });
    rejects([&] { visual_pixel_to_css(capture, -0.001, 0); });
    rejects([&] { visual_pixel_to_css(capture, 0, std::numeric_limits<double>::infinity()); });
    rejects([&] { visual_pixel_to_css(capture, std::numeric_limits<double>::quiet_NaN(), 0); });
    capture.pixel_to_css_scale_x = 0;
    rejects([&] { visual_pixel_to_css(capture, 0, 0); });
    std::cout << "visual mapping tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
