// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "suitecut/browser.hpp"

namespace suitecut {
struct VisualRect {
  double x = 0, y = 0, width = 0, height = 0;
  bool operator==(const VisualRect&) const = default;
};
struct VisualPoint { double x = 0, y = 0; };
struct VisualCaptureOptions {
  std::string scale = "css"; // "css" or "device"; viewport captures only.
  std::optional<VisualRect> clip;
};
struct VisualCaptureMetadata {
  std::string id;
  std::uint32_t pixel_width = 0, pixel_height = 0;
  VisualRect css_clip; // Effective viewport-relative CSS rectangle.
  double pixel_to_css_scale_x = 0, pixel_to_css_scale_y = 0;
};
struct VisualCapture : VisualCaptureMetadata {
  std::vector<std::uint8_t> png;
};

// Pure validation/mapping helpers. Metadata and calculated points grant no
// permission to act. PNG inspection checks its first 24 bytes, not its codec,
// CRCs or remaining image data; capture() obtains bytes from the native engine.
std::pair<std::uint32_t, std::uint32_t> visual_png_dimensions(
    std::span<const std::uint8_t> png);
VisualRect visual_clip_to_viewport(double width, double height,
                                 std::optional<VisualRect> requested = std::nullopt);
VisualPoint visual_pixel_to_css(const VisualCaptureMetadata& capture, double x, double y);

// One non-thread-safe controller per page. Holds a non-owning Page view and
// only one capture's private metadata, never a second PNG copy. Each capture
// attempt invalidates its predecessor. A matching click attempt consumes the
// token, including attempts that fail validation or native delivery.
//
// Tokens expire after 30 seconds. Document/viewport/scroll checks are client
// freshness checks, not atomic renderer authorization: layout shifts and a
// navigation between the final check and input can still occur. Prefer native
// locators when available. Keep other page mutation serialized with this owner.
class VisualController {
 public:
  explicit VisualController(const Page& page);
  ~VisualController();
  VisualController(const VisualController&) = delete;
  VisualController& operator=(const VisualController&) = delete;
  VisualCapture capture(VisualCaptureOptions options = {});
  void click(const std::string& capture_id, double x, double y);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace suitecut
