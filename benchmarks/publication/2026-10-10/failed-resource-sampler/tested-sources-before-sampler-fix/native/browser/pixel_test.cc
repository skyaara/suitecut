// SPDX-License-Identifier: MIT
#include <array>
#include <cmath>
#include <cstdio>
#include "libyuv/convert_from_argb.h"
#include "libyuv/convert.h"

int main() {
  // Opaque BGRA patches: black, white, red, green, blue. Check BT.709 limited
  // luma and chroma ordering, catching RGB/BGR and 601/709 regressions.
  constexpr std::array<std::array<int, 6>, 5> colors{{
    {0, 0, 0, 16, 128, 128}, {255, 255, 255, 235, 128, 128},
    {0, 0, 255, 63, 102, 240}, {0, 255, 0, 173, 42, 26},
    {255, 0, 0, 32, 240, 118}
  }};
  for (const auto& color : colors) {
    std::array<uint8_t, 16> bgra{};
    for (int i = 0; i < 4; ++i) {
      for (int c = 0; c < 3; ++c) bgra[i * 4 + c] = static_cast<uint8_t>(color[c]);
      bgra[i * 4 + 3] = 255;
    }
    std::array<uint8_t, 6> planes{};
    if (libyuv::ARGBToI420Matrix(bgra.data(), 8, planes.data(), 2, planes.data() + 4, 1,
        planes.data() + 5, 1, &libyuv::kArgbH709Constants, 2, 2)) return 1;
    for (int i = 0; i < 4; ++i) if (std::abs(int(planes[i]) - color[3]) > 1) return 2;
    if (std::abs(int(planes[4]) - color[4]) > 1 || std::abs(int(planes[5]) - color[5]) > 1) return 3;
  }
  std::puts("Native pixel conversion tests passed");
  return 0;
}
