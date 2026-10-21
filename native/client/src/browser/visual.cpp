// SPDX-License-Identifier: MIT
#include "suitecut/visual.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace suitecut {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t max_axis = 32768;
constexpr std::uint64_t max_pixels = 64 * 1024 * 1024;
void require(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
bool positive(double value) { return std::isfinite(value) && value > 0; }
void image_bounds(std::uint32_t width, std::uint32_t height) {
  require(width && height && width <= max_axis && height <= max_axis &&
              std::uint64_t{width} * height <= max_pixels,
          "Visual capture exceeds native image dimension bounds");
}
void rectangle(const VisualRect& value) {
  require(std::isfinite(value.x) && std::isfinite(value.y) && positive(value.width) &&
              positive(value.height) && std::isfinite(value.x + value.width) &&
              std::isfinite(value.y + value.height),
          "Visual clip must have finite coordinates and positive dimensions");
}
std::uint64_t next_controller() {
  static std::atomic<std::uint64_t> sequence{0};
  auto current = sequence.load(std::memory_order_relaxed);
  for (;;) {
    require(current != std::numeric_limits<std::uint64_t>::max(),
            "Visual controller identity space exhausted");
    if (sequence.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
      return current + 1;
  }
}
class Deadline {
 public:
  explicit Deadline(Timeout timeout) {
    const auto now = Clock::now();
    require(timeout.count() > 0 && timeout <
                std::chrono::duration_cast<Timeout>(Clock::time_point::max() - now),
            "Visual operation timeout is out of range");
    end_ = now + timeout;
  }
  Page view(const Page& page) const {
    const auto remaining = std::chrono::duration_cast<Timeout>(end_ - Clock::now());
    require(remaining.count() > 0, "Visual operation timed out");
    return page.with_timeout(remaining);
  }
 private:
  Clock::time_point end_;
};
struct Document {
  std::string frame, loader;
  bool operator==(const Document&) const = default;
};
struct Geometry {
  double width, height, scroll_x, scroll_y, device_scale, visual_scale;
  bool operator==(const Geometry&) const = default;
};
struct State {
  Document document;
  Geometry geometry;
  bool operator==(const State&) const = default;
};
Document document(const Page& page, const Deadline& deadline) {
  const auto tree = deadline.view(page).devtools("Page.getFrameTree");
  const auto& frame = tree.at("frameTree").at("frame");
  Document result{frame.at("id").as_string(), frame.at("loaderId").as_string()};
  require(!result.frame.empty() && !result.loader.empty(),
          "Visual capture requires a live document identity");
  return result;
}
State state(const Page& page, const Deadline& deadline) {
  auto before = document(page, deadline);
  const auto value = deadline.view(page).evaluate(
      "(()=>({width:innerWidth,height:innerHeight,scrollX,scrollY,"
      "deviceScale:devicePixelRatio,visualScale:visualViewport?visualViewport.scale:1}))()");
  const auto number = [&](const char* field) {
    const auto& item = value.at(field);
    require(item.is_number() && std::isfinite(item.as_double()),
            "Visual viewport returned invalid geometry");
    return item.as_double();
  };
  Geometry geometry{number("width"), number("height"), number("scrollX"),
                    number("scrollY"), number("deviceScale"), number("visualScale")};
  require(positive(geometry.width) && positive(geometry.height) && positive(geometry.device_scale),
          "Visual viewport dimensions and device scale must be positive");
  require(geometry.visual_scale == 1, "Visual capture does not support pinch zoom");
  require(before == document(page, deadline), "Visual document changed while reading geometry");
  return {std::move(before), geometry};
}
} // namespace

std::pair<std::uint32_t, std::uint32_t> visual_png_dimensions(
    std::span<const std::uint8_t> png) {
  constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
  require(png.size() >= 24 && std::equal(signature.begin(), signature.end(), png.begin()) &&
              png[8] == 0 && png[9] == 0 && png[10] == 0 && png[11] == 13 &&
              png[12] == 'I' && png[13] == 'H' && png[14] == 'D' && png[15] == 'R',
          "Visual capture has an invalid PNG header");
  const auto read = [&](std::size_t offset) {
    return (std::uint32_t{png[offset]} << 24) | (std::uint32_t{png[offset + 1]} << 16) |
           (std::uint32_t{png[offset + 2]} << 8) | std::uint32_t{png[offset + 3]};
  };
  const auto width = read(16), height = read(20);
  image_bounds(width, height);
  return {width, height};
}

VisualRect visual_clip_to_viewport(double width, double height,
                                 std::optional<VisualRect> requested) {
  require(positive(width) && positive(height), "Visual viewport dimensions must be positive");
  if (!requested) return {0, 0, width, height};
  rectangle(*requested);
  const auto left = std::max(0.0, requested->x), top = std::max(0.0, requested->y);
  const auto right = std::min(width, requested->x + requested->width);
  const auto bottom = std::min(height, requested->y + requested->height);
  require(right > left && bottom > top, "Visual clip is outside the viewport");
  return {left, top, right - left, bottom - top};
}

VisualPoint visual_pixel_to_css(const VisualCaptureMetadata& capture, double x, double y) {
  image_bounds(capture.pixel_width, capture.pixel_height);
  rectangle(capture.css_clip);
  require(positive(capture.pixel_to_css_scale_x) && positive(capture.pixel_to_css_scale_y),
          "Visual capture has invalid pixel scales");
  require(std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 &&
              x < capture.pixel_width && y < capture.pixel_height,
          "Visual point must be finite and inside the captured image");
  VisualPoint result{capture.css_clip.x + x * capture.pixel_to_css_scale_x,
                     capture.css_clip.y + y * capture.pixel_to_css_scale_y};
  require(std::isfinite(result.x) && std::isfinite(result.y), "Visual mapped point is invalid");
  return result;
}

struct VisualController::Impl {
  explicit Impl(const Page& source)
      : page(source.with_timeout(source.timeout())), controller(next_controller()) {}
  struct Active {
    VisualCaptureMetadata metadata;
    State state;
    Clock::time_point expires;
  };
  Page page;
  std::uint64_t controller, sequence = 0;
  std::optional<Active> active;
};
VisualController::VisualController(const Page& page) : impl_(std::make_unique<Impl>(page)) {}
VisualController::~VisualController() = default;

VisualCapture VisualController::capture(VisualCaptureOptions options) {
  impl_->active.reset();
  require(options.scale == "css" || options.scale == "device",
          "Visual capture scale must be css or device");
  Deadline deadline(impl_->page.timeout());
  const auto before = state(impl_->page, deadline);
  const auto clip = visual_clip_to_viewport(before.geometry.width, before.geometry.height,
                                           options.clip);
  Json native{{"format", "png"}, {"scale", options.scale}, {"fullPage", false},
              {"clip", Json{{"x", clip.x}, {"y", clip.y},
                             {"width", clip.width}, {"height", clip.height}}}};
  const auto started = Clock::now();
  auto png = deadline.view(impl_->page).screenshot(std::move(native));
  const auto [width, height] = visual_png_dimensions(png);
  require(before == state(impl_->page, deadline),
          "Visual document or viewport changed during capture");
  require(Clock::now() < started + std::chrono::seconds(30), "Visual capture expired");
  require(impl_->sequence != std::numeric_limits<std::uint64_t>::max(),
          "Visual capture identity space exhausted");
  VisualCapture result;
  result.id = "visual:" + std::to_string(impl_->controller) + ":" +
              std::to_string(++impl_->sequence);
  result.pixel_width = width;
  result.pixel_height = height;
  result.css_clip = clip;
  result.pixel_to_css_scale_x = clip.width / width;
  result.pixel_to_css_scale_y = clip.height / height;
  result.png = std::move(png);
  impl_->active = Impl::Active{static_cast<const VisualCaptureMetadata&>(result), before,
                                started + std::chrono::seconds(30)};
  return result;
}

void VisualController::click(const std::string& capture_id, double x, double y) {
  require(impl_->active && impl_->active->metadata.id == capture_id,
          "Visual capture token is unknown or already consumed");
  auto active = std::move(*impl_->active);
  impl_->active.reset();
  require(Clock::now() < active.expires, "Visual capture expired");
  const auto point = visual_pixel_to_css(active.metadata, x, y);
  Deadline deadline(impl_->page.timeout());
  require(active.state == state(impl_->page, deadline),
          "Visual document or viewport changed since capture");
  require(Clock::now() < active.expires, "Visual capture expired");
  deadline.view(impl_->page).mouse_click(point.x, point.y);
}
} // namespace suitecut
