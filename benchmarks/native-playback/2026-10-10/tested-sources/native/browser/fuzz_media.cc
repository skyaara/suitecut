// SPDX-License-Identifier: MIT
#include "media_engine.h"
#include "memory_bounds.h"
#include <atomic>
#include <cstring>
#include <filesystem>
#include <unistd.h>
// Real encoder/queue/lifecycle operations, bounded to a 40 ms output per input.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size) {
  if(size<9) return 0;
  const auto path="/tmp/suitecut-fuzz-"+std::to_string(getpid())+".mkv";
  double origin;std::memcpy(&origin,data+1,8);
  if(!suitecut::MediaTimestamp(origin) || origin<0 || origin>10000) origin=0;
  std::atomic<double> clock{origin};
  suitecut::MediaOptions config;config.destination=path;config.width=16;config.height=16;
  config.video_codec="ffv1";config.audio_codec="pcm_s16le";config.audio=data[0]&1;
  config.offline=data[0]&2;config.fps=data[0]&4 ? 60 : 30;
  try {
    suitecut::MediaOutput output(config,[&]{return clock.load();});
    output.Video(std::make_shared<const std::vector<uint8_t>>(16*16*3/2,128),16,16,origin);
    for(size_t i=1;i+8<=size && i<257;i+=8) {
      double at;std::memcpy(&at,data+i,8);
      auto samples=std::make_shared<const std::vector<int16_t>>((data[i]%64)*2,data[i]);
      output.Audio(samples,at);
      if(data[i]&2) output.AudioState(data[i]&4);
      if(data[i]&8) {output.Pause(true);output.Pause(false);}
      if(data[i]&16) output.Fail("fuzzed failure");
    }
    clock=origin+40;output.Stop();output.Stop();
  } catch(const std::exception&) {}
  std::filesystem::remove(path);return 0;
}
