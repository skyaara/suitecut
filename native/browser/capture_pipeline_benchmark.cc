// SPDX-License-Identifier: MIT
// Standalone component benchmark; intentionally separate from correctness tests.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "capture_buffer.h"
#include "capture_canvas.h"
#include "libyuv/convert.h"
#include "libyuv/convert_from_argb.h"

namespace {
using Clock = std::chrono::steady_clock;
uint64_t checksum = 0;
void Consume(const std::vector<uint8_t>& bytes) { checksum += bytes.front() + bytes.back(); }
template <class Work> double MedianMicros(int iterations, Work work) {
  std::vector<double> samples;
  for (int warmup = 0; warmup < 4; ++warmup) work();
  for (int repeat = 0; repeat < 7; ++repeat) {
    const auto started = Clock::now();
    for (int index = 0; index < iterations; ++index) work();
    samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - started).count() / iterations);
  }
  std::sort(samples.begin(), samples.end()); return samples[samples.size() / 2];
}
void Report(const char* name, double baseline, double optimized) {
  std::printf("{\"case\":\"%s\",\"baseline_us\":%.3f,\"optimized_us\":%.3f,\"speedup\":%.3f}\n",
      name, baseline, optimized, baseline / optimized);
}
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
std::vector<uint8_t> ToI420(const std::vector<uint8_t>& bgra, int width, int height) {
  const size_t y = static_cast<size_t>(width) * height;
  std::vector<uint8_t> planes(y * 3 / 2);
  if (libyuv::ARGBToI420Matrix(bgra.data(), width * 4, planes.data(), width,
      planes.data() + y, width / 2, planes.data() + y * 5 / 4, width / 2,
      &libyuv::kArgbH709Constants, width, height)) std::abort();
  return planes;
}
}

int main(int argc, char** argv) {
  const int iterations = argc > 1 ? std::atoi(argv[1]) : 80;
  if (iterations < 1 || iterations > 10000) return 1;
  struct Rect { int x, y, width, height; };
  for (const auto [width, height] : {std::pair{1920, 1080}, std::pair{3840, 2160}}) {
    std::vector<uint8_t> paint(static_cast<size_t>(width) * height * 4);
    for (size_t index = 0; index < paint.size(); ++index) paint[index] = index * 31 + index / 29;
    std::vector<uint8_t> retained = paint;
    const std::vector<Rect> dirty{{width / 2, height / 2, 128, 64}};
    const auto baseline = MedianMicros(iterations, [&] { retained.assign(paint.begin(), paint.end()); Consume(retained); });
    const auto optimized = MedianMicros(iterations, [&] {
      if (!SuiteCutUpdateCapture(retained, paint.data(), width, height, width, height, dirty)) std::abort();
      Consume(retained);
    });
    Report(width == 1920 ? "damage_1080p_128x64" : "damage_4k_128x64", baseline, optimized);
    const std::vector<Rect> full{{0, 0, width, height}};
    Report(width == 1920 ? "full_paint_1080p_control" : "full_paint_4k_control", baseline,
        MedianMicros(iterations, [&] {
          if (!SuiteCutUpdateCapture(retained, paint.data(), width, height, width, height, full)) std::abort();
          Consume(retained);
        }));
    const auto copied = paint;
    if (ToI420(copied, width, height) != ToI420(paint, width, height)) return 2;
    Report(width == 1920 ? "i420_1080p_no_popup" : "i420_4k_no_popup",
        MedianMicros(iterations, [&] { const auto copy = paint; Consume(ToI420(copy, width, height)); }),
        MedianMicros(iterations, [&] { Consume(ToI420(paint, width, height)); }));
  }
  struct Fit { int sw, sh, cw, ch; const char* name; };
  for (const auto test : {Fit{1920, 1080, 3840, 2160, "fit_1080p_to_4k"},
      Fit{390, 844, 1920, 1080, "fit_portrait_to_1080p"},
      Fit{3840, 2160, 1280, 720, "fit_4k_to_720p"}}) {
    std::vector<uint8_t> paint(static_cast<size_t>(test.sw) * test.sh * 4);
    for (size_t index = 0; index < paint.size(); ++index) paint[index] = index * 31 + index / 29;
    auto expected = paint;
    std::vector<uint8_t> actual;
    if (!LegacyFit(expected, test.sw, test.sh, test.cw, test.ch) ||
        !SuiteCutFitCapture(paint, test.sw, test.sh, test.cw, test.ch, actual) || actual != expected) return 3;
    Report(test.name,
        MedianMicros(iterations, [&] {
          auto copy = paint;
          if (!LegacyFit(copy, test.sw, test.sh, test.cw, test.ch)) std::abort(); Consume(copy);
        }),
        MedianMicros(iterations, [&] {
          std::vector<uint8_t> fitted;
          if (!SuiteCutFitCapture(paint, test.sw, test.sh, test.cw, test.ch, fitted)) std::abort(); Consume(fitted);
        }));
  }
  std::fprintf(stderr, "checksum=%llu; %d iterations x 7 repeats per case\n",
      static_cast<unsigned long long>(checksum), iterations);
}
