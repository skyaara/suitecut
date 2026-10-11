// SPDX-License-Identifier: MIT
#include "media_engine.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
extern "C" {
#include <libswscale/swscale.h>
SwsContext* __real_sws_getCachedContext(SwsContext*,int,int,AVPixelFormat,int,int,AVPixelFormat,int,SwsFilter*,SwsFilter*,const double*);
SwsContext* __wrap_sws_getCachedContext(SwsContext* context,int w,int h,AVPixelFormat format,int outw,int outh,AVPixelFormat outformat,int flags,SwsFilter* source,SwsFilter* destination,const double* parameters) {
  if(context && w==32) {
    // Match FFmpeg's documented ownership: changing scaler parameters may
    // free the old context even if replacement allocation fails.
    sws_freeContext(context);return nullptr;
  }
  return __real_sws_getCachedContext(context,w,h,format,outw,outh,outformat,flags,source,destination,parameters);
}
}
int main() {
  using namespace std::chrono;
  const auto path="/tmp/suitecut-scaler-failure-"+std::to_string(getpid())+".mkv";
  try {
    suitecut::MediaOptions config;config.destination=path;config.width=16;config.height=16;
    config.video_codec="ffv1";config.audio=false;config.offline=true;
    std::atomic<double> clock{0};suitecut::MediaOutput output(config,[&]{return clock.load();});
    output.Video(std::make_shared<const std::vector<uint8_t>>(16*16*3/2,128),16,16,0);
    const auto deadline=steady_clock::now()+seconds(5);
    while(output.Status().video_frames<1 && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(1));
    if(output.Status().video_frames<1) throw std::runtime_error("First scaler was not allocated");
    output.Video(std::make_shared<const std::vector<uint8_t>>(32*16*3/2,128),32,16,1000.0/30);
    clock=67;const auto status=output.Stop();
    if(status.error!="Create native scaler" || !status.stopped || status.queued_video_bytes)
      throw std::runtime_error("Scaler failure did not release ownership safely");
    std::filesystem::remove(path);std::cout<<"Scaler replacement allocation failure released safely\n";
  } catch(const std::exception& error) {std::filesystem::remove(path);std::cerr<<error.what()<<'\n';return 1;}
}
