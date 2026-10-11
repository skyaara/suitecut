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
  SuiteCutAudioClock epoch;
  SuiteCutAudioClock monotonic;
  // Initial transport delay is not an A/V offset; delayed successors and real
  // missing capture intervals retain their capture timestamps.
  if (std::abs(monotonic.Map(500000,1020,0,500020)-1000)>0.001) return 11;
  if (std::abs(monotonic.Map(500020,1420,0,500420)-1020)>0.001) return 12;
  if (std::abs(monotonic.Map(500220,1440,0,500440)-1220)>0.001) return 13;
  // A host with a different TimeTicks origin still uses the existing fallback.
  monotonic.Reset();
  if (std::abs(monotonic.Map(10,1000,0,500000)-1000)>0.001) return 14;
  const double wall=1790058418577.0;
  if(std::abs(epoch.Map(wall-20,1000,wall)-980)>0.001) return 9;
  // 400 ms of Unix-clock slew over a long task must not shift its steady timeline.
  if(std::abs(epoch.Map(wall+1800000+400-20,1801000,wall+1800000+400)-1800980)>0.001) return 10;
  SuiteCutAudioClock packets;
  if(packets.NormalizePacket(1000,960)!=1000) return 15;
  // Timestamp-only jitter must not create silent holes between supplied PCM.
  if(packets.NormalizePacket(1040,960)!=1020) return 16;
  if(packets.NormalizePacket(1060,960)!=1040) return 17;
  // Corrections stay bounded even as small discontinuities accumulate.
  if(packets.NormalizePacket(1130,960)!=1130) return 18;
  if(packets.NormalizePacket(1230,960)!=1230) return 19;
  packets.Reset();
  if(packets.NormalizePacket(2000,960)!=2000) return 20;
  // OBS's 1024-frame packet size has a fractional millisecond sample clock.
  if(std::abs(packets.NormalizePacket(2021,1024)-2020)>0.001) return 21;
  if(std::abs(packets.NormalizePacket(2042,1024)-(2020+1024/48.0))>0.001) return 22;
  std::puts("Native audio clock tests passed");
  return 0;
}
