// SPDX-License-Identifier: MIT
#include "memory_bounds.h"
#include "audio_clock.h"
#include <array>
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size) {
  if(size<40) return 0;
  double source_at,destination_at,x,y;int frames;
  std::memcpy(&source_at,data,8);std::memcpy(&destination_at,data+8,8);
  std::memcpy(&x,data+16,8);std::memcpy(&y,data+24,8);std::memcpy(&frames,data+32,4);
  if(suitecut::NextCommandID(frames)<1) std::abort();
  std::array<int16_t,512> source{},out{};std::array<uint8_t,256> covered{};
  suitecut::CopyAudioChunk(std::span(source).first(data[36]*2),source_at,out,covered,destination_at);
  std::array<uint8_t,16*16*4> view{},popup{};
  suitecut::CompositeBGRA(view,16,16,popup,16,16,x,y);
  SuiteCutAudioClock clock;clock.Map(source_at,destination_at,x,y);clock.NormalizePacket(source_at,frames);
  return 0;
}
