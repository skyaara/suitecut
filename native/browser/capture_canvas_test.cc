// SPDX-License-Identifier: MIT
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "capture_buffer.h"
#include "capture_canvas.h"

bool DamageTests() {
  struct Rect { int x, y, width, height; };
  std::vector<uint8_t> current, complete(64 * 64 * 4, 17);
  std::vector<Rect> dirty{{3, 7, 8, 9}};
  // A first paint is complete even if CEF supplies only partial damage.
  if (!SuiteCutUpdateCapture(current, complete.data(), 64, 64, 0, 0, dirty) || current != complete) return false;
  for (int index = 0; index < 128; ++index) {
    dirty = {{index % 40, (index * 7) % 40, 8, 9}, {40, 40, 7, 13}};
    for (const auto& rect : dirty) for (int y = rect.y; y < rect.y + rect.height; ++y)
      std::memset(complete.data() + (y * 64 + rect.x) * 4, index, rect.width * 4);
    if (!SuiteCutUpdateCapture(current, complete.data(), 64, 64, 64, 64, dirty) || current != complete) return false;
  }
  // A screenshot lease can consume paints without updating the video snapshot.
  // CEF's next damage list describes only its immediately preceding paint,
  // so reusing that damage would miss changes from the withheld paint.
  std::memset(complete.data(), 201, 64 * 4);
  dirty = {{10, 10, 2, 2}};
  for (int y = 10; y < 12; ++y) std::memset(complete.data() + (y * 64 + 10) * 4, 202, 2 * 4);
  auto stale = current;
  if (!SuiteCutUpdateCapture(stale, complete.data(), 64, 64, 64, 64, dirty) || stale == complete) return false;
  // Source invalidates the retained surface's damage provenance on every skip.
  if (!SuiteCutUpdateCapture(current, complete.data(), 64, 64, 0, 64, dirty) || current != complete) return false;
  // Reallocation cannot be inferred from byte count: an equal-size surface can
  // have a different stride. Invalid/empty damage falls back before any copy.
  std::fill(complete.begin(), complete.end(), 92);
  if (!SuiteCutUpdateCapture(current, complete.data(), 32, 128, 64, 64, dirty) || current != complete) return false;
  for (const auto& invalid : std::vector<Rect>{{-1, 0, 1, 1}, {0, 0, INT_MAX, 1},
      {0, 128, 1, 1}, {0, 0, 0, 1}, {0, 0, 1, INT_MIN}}) {
    std::fill(complete.begin(), complete.end(), complete.front() + 1);
    dirty = {{1, 1, 2, 2}, invalid};
    if (!SuiteCutUpdateCapture(current, complete.data(), 32, 128, 32, 128, dirty) || current != complete) return false;
  }
  dirty.clear(); std::fill(complete.begin(), complete.end(), 131);
  if (!SuiteCutUpdateCapture(current, complete.data(), 32, 128, 32, 128, dirty) || current != complete) return false;
  const auto retained = current;
  return !SuiteCutUpdateCapture(current, nullptr, 32, 128, 32, 128, dirty) && current == retained &&
      !SuiteCutUpdateCapture(current, complete.data(), INT_MAX, 128, 32, 128, dirty) && current == retained;
}

// Freeze the previous fitting algorithm so changed alpha/letterbox pixels are
// checked byte for byte rather than against a second copy of the new code.
bool LegacyFit(std::vector<uint8_t>& pixels, int sw, int sh, int cw, int ch) {
  if (sw == cw && sh == ch) return true;
  SuiteCutCaptureRect rect;
  if (!SuiteCutCaptureContentRect(sw, sh, cw, ch, rect)) return false;
  std::vector<uint8_t> canvas(static_cast<size_t>(cw) * ch * 4, 0);
  for (size_t i = 3; i < canvas.size(); i += 4) canvas[i] = 255;
  if (libyuv::ARGBScale(pixels.data(), sw * 4, sw, sh,
      canvas.data() + (static_cast<size_t>(rect.y) * cw + rect.x) * 4,
      cw * 4, rect.width, rect.height, libyuv::kFilterBilinear)) return false;
  pixels.swap(canvas); return true;
}

int main(int argc, char** argv) {
  const auto iterations = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 10000;
  if (!iterations || iterations > 10000000) return 1;
  if (!DamageTests()) return 11;
  std::vector<uint8_t> invalid(4, 255);
  for (int dimension : {INT_MIN, -1, 0, INT_MAX}) {
    if (SuiteCutFitCapture(invalid, dimension, 1, 1, 1) ||
        SuiteCutFitCapture(invalid, 1, dimension, 1, 1) ||
        SuiteCutFitCapture(invalid, 1, 1, dimension, 1) ||
        SuiteCutFitCapture(invalid, 1, 1, 1, dimension)) return 2;
  }
  if (SuiteCutFitCapture(invalid, 2, 2, 1, 1) || invalid.size() != 4) return 3;
  struct Case { int sw, sh, cw, ch, x, y, w, h; };
  const Case cases[] = {
    {1280, 720, 1280, 720, 0, 0, 1280, 720},
    {390, 844, 1280, 720, 473, 0, 333, 720},
    {641, 479, 1280, 720, 158, 0, 964, 720},
    {1280, 720, 390, 844, 0, 312, 390, 219},
    {1, 64, 64, 64, 31, 0, 1, 64},
    {64, 1, 64, 64, 0, 31, 64, 1},
  };
  for (const auto& test : cases) {
    SuiteCutCaptureRect rect;
    if (!SuiteCutCaptureContentRect(test.sw, test.sh, test.cw, test.ch, rect) ||
        rect.x != test.x || rect.y != test.y || rect.width != test.w || rect.height != test.h) return 7;
    std::vector<uint8_t> pixels(static_cast<size_t>(test.sw) * test.sh * 4, 255);
    SuiteCutCaptureRect fitted;
    if (!SuiteCutFitCapture(pixels, test.sw, test.sh, test.cw, test.ch, &fitted) ||
        fitted.x != rect.x || fitted.y != rect.y || fitted.width != rect.width || fitted.height != rect.height) return 8;
    for (int y = 0; y < test.ch; ++y) for (int x = 0; x < test.cw; ++x) {
      const bool inside = x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
      const size_t offset = (static_cast<size_t>(y) * test.cw + x) * 4;
      for (int channel = 0; channel < 3; ++channel)
        if (inside ? pixels[offset + channel] < 250 : pixels[offset + channel] != 0) return 9;
      if (pixels[offset + 3] < 250) return 10;
    }
  }
  uint64_t state = 0x85c927dea73;
  const auto random = [&state] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
  for (uint64_t iteration = 0; iteration < iterations; ++iteration) {
    const int source_width = 1 + random() % 64, source_height = 1 + random() % 64;
    const int canvas_width = 1 + random() % 64, canvas_height = 1 + random() % 64;
    std::vector<uint8_t> pixels(static_cast<size_t>(source_width) * source_height * 4, 255);
    // Vary every channel, including alpha, to catch partially initialized
    // content and letterbox errors in both source-preserving and in-place APIs.
    std::vector<uint8_t> gradient(pixels.size());
    for (auto& value : gradient) value = random();
    auto expected = gradient;
    std::vector<uint8_t> actual;
    if (!LegacyFit(expected, source_width, source_height, canvas_width, canvas_height) ||
        !SuiteCutFitCapture(gradient, source_width, source_height, canvas_width, canvas_height, actual) ||
        actual != expected ||
        !SuiteCutFitCapture(gradient, source_width, source_height, canvas_width, canvas_height) ||
        gradient != expected ||
        SuiteCutFitCapture(actual, canvas_width, canvas_height, canvas_width, canvas_height, actual)) return 12;
    SuiteCutCaptureRect rect;
    if (!SuiteCutFitCapture(pixels, source_width, source_height, canvas_width, canvas_height, &rect) ||
        pixels.size() != static_cast<size_t>(canvas_width) * canvas_height * 4 ||
        rect.x < 0 || rect.y < 0 || rect.width < 1 || rect.height < 1 ||
        rect.x + rect.width > canvas_width || rect.y + rect.height > canvas_height) return 4;
    const size_t center = (static_cast<size_t>((canvas_height - 1) / 2) * canvas_width + (canvas_width - 1) / 2) * 4;
    if (pixels[center] < 250 || pixels[center + 1] < 250 || pixels[center + 2] < 250) return 5;
    for (size_t index = 3; index < pixels.size(); index += 4) if (pixels[index] < 250) return 6;
  }
  std::printf("Native capture canvas: %llu deterministic cases passed\n", static_cast<unsigned long long>(iterations));
  return 0;
}
