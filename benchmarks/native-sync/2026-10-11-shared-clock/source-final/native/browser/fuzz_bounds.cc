// SPDX-License-Identifier: MIT
#include "memory_bounds.h"
#include "audio_clock.h"
#include <array>
#include <bit>
#include <cstdlib>
namespace { volatile uint64_t observed = 0; }
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size) {
  if(size<40) return 0;
  double source_at,destination_at,x,y;int frames;
  std::memcpy(&source_at,data,8);std::memcpy(&destination_at,data+8,8);
  std::memcpy(&x,data+16,8);std::memcpy(&y,data+24,8);std::memcpy(&frames,data+32,4);
  if(suitecut::NextCommandID(frames)<1) std::abort();
  std::array<int16_t,512> source{},out{};std::array<uint8_t,256> covered{};
  for(size_t i=0;i<source.size();++i) source[i]=data[i%size];
  uint64_t digest=suitecut::CopyAudioChunk(std::span(source).first(data[36]*2),source_at,out,covered,destination_at);
  std::array<uint8_t,16*16*4> view{},popup{};
  for(size_t i=0;i<popup.size();++i) popup[i]=data[i%size];
  digest^=suitecut::CompositeBGRA(view,16,16,popup,16,16,x,y);
  SuiteCutAudioClock clock;digest^=std::bit_cast<uint64_t>(clock.Map(source_at,destination_at,x,y));
  digest^=std::bit_cast<uint64_t>(clock.NormalizePacket(source_at,frames));
  // Make copied bytes and clock results observable so optimization cannot
  // erase the very operations this campaign is intended to exercise.
  for(auto value:out) digest=digest*33+static_cast<uint16_t>(value);
  for(auto value:covered) digest=digest*33+value;
  for(auto value:view) digest=digest*33+value;
  observed=digest;
  return 0;
}
