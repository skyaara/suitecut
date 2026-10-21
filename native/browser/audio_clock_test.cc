// SPDX-License-Identifier: MIT
#include "audio_clock.h"
#include <cmath>
#include <cstdio>

int main() {
  {
    SuiteCutAudioClock clock;
    const double wall=1790058418577.0;
    // A first PCM callback delivered 150 ms late must not delay all audio.
    if(std::abs(clock.Map(wall-150,1000,wall)-850)>.001)return 9;
    if(std::abs(clock.Map(wall-130,1300,wall+300)-870)>.001)return 10;
    clock.Reset();
    if(std::abs(clock.Map(wall+20,1000,wall)-1020)>.001)return 11;
    // Platform variants with monotonic PTS still use the fallback anchor.
    clock.Reset();
    if(std::abs(clock.Map(1234,1000,wall)-1000)>.001)return 12;
    // The pinned CEF supplies audio TimeTicks. Its independently calibrated
    // Mach clock offset must survive a first callback that is already late.
    clock.Reset();const double native=386208653.0,offset=850-native;
    if(std::abs(clock.Map(native,1000,wall,offset)-850)>.001)return 13;
    if(std::abs(clock.Map(native+20,1500,wall+500,offset)-870)>.001)return 14;
    // A native clock learned after audio startup replaces the receipt anchor.
    clock.Reset();clock.Map(native,1000,wall);
    if(std::abs(clock.Map(native+20,1500,wall+500,offset)-870)>.001)return 15;
  }
  for (const double base : {386208653.0, 1790058418577.0}) {
    SuiteCutAudioClock clock;
    if (std::abs(clock.Map(base, 1000) - 1000) > 0.001) return 1;
    // Callback jitter must not disturb the sample timeline.
    if (std::abs(clock.Map(base + 20, 1025) - 1020) > 0.001) return 2;
    if (std::abs(clock.Map(base + 40, 1038) - 1040) > 0.001) return 3;
    // A delayed callback and its queued successors retain their original PTS.
    if (std::abs(clock.Map(base + 60, 1500) - 1060) > 0.001) return 6;
    if (std::abs(clock.Map(base + 80, 1501) - 1080) > 0.001) return 7;
    // Source discontinuity and explicit restart get new anchors.
    if (std::abs(clock.Map(0, 1100) - 1100) > 0.001) return 4;
    clock.Reset();
    if (std::abs(clock.Map(base, 1200) - 1200) > 0.001) return 5;
    // A source-clock jump into the future is a discontinuity, not host jitter.
    if (std::abs(clock.Map(base + 2000, 1220) - 1220) > 0.001) return 8;
  }
  std::puts("Native audio clock tests passed");
  return 0;
}
