// SPDX-License-Identifier: MIT
#include "frame_rate.h"
#include <cstdio>
#include <string>

int main() {
  for (int fps = 1; fps <= 120; ++fps) {
    const auto parsed = ParseSuiteCutFrameRate(std::to_string(fps));
    if (!parsed || *parsed != fps) return 1;
  }
  for (const auto text : {"", "0", "-1", "121", "1.5", "60fps", "120.0", " 60", "60 ", "999999999999999999999"}) {
    if (ParseSuiteCutFrameRate(text)) return 2;
  }
  std::puts("Native frame rate parsing tests passed");
  return 0;
}
