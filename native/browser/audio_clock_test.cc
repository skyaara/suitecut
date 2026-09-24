// SPDX-License-Identifier: MIT
#include "audio_clock.h"
#include <cmath>
#include <cstdio>

int main() {
  for (const double base : {386208653.0, 1790058418577.0}) {
    SuiteCutAudioClock clock;
    if (std::abs(clock.Map(base, 1000) - 1000) > 0.001) return 1;
    // Callback jitter must not disturb the sample timeline.
    if (std::abs(clock.Map(base + 20, 1025) - 1020) > 0.001) return 2;
    if (std::abs(clock.Map(base + 40, 1038) - 1040) > 0.001) return 3;
    // Source discontinuity and explicit restart get new anchors.
    if (std::abs(clock.Map(0, 1100) - 1100) > 0.001) return 4;
    clock.Reset();
    if (std::abs(clock.Map(base, 1200) - 1200) > 0.001) return 5;
  }
  std::puts("Native audio clock tests passed");
  return 0;
}
