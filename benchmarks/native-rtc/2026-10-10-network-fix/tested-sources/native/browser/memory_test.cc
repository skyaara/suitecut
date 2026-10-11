// SPDX-License-Identifier: MIT
#include "media_engine.h"
#include "media_render.h"
#include "memory_bounds.h"
#include "audio_trace.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace std::chrono;
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main() {
  try {
    Require(suitecut::NextCommandID(0)==1 && suitecut::NextCommandID(-1)==1 &&
      suitecut::NextCommandID(std::numeric_limits<int>::max()-1)==std::numeric_limits<int>::max() &&
      suitecut::NextCommandID(std::numeric_limits<int>::max())==1,"Protocol counter overflow was not bounded");
    std::vector<int16_t> source{1,2,3,4,5,6},out(6,-1);
    std::vector<uint8_t> covered(3);
    Require(suitecut::CopyAudioChunk(source,0,out,covered,1.0/48),"Valid audio overlap rejected");
    Require(out==std::vector<int16_t>({3,4,5,6,-1,-1}) && covered==std::vector<uint8_t>({1,1,0}),"Audio overlap crossed its buffer");
    for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1e300,-1e300})
      Require(!suitecut::CopyAudioChunk(source,invalid,out,covered,0),"Invalid audio timestamp accepted");
    std::vector<uint8_t> view(4*4*4,0),popup(2*2*4,255);
    Require(suitecut::CompositeBGRA(view,4,4,popup,2,2,-1,-1),"Clipped popup rejected");
    Require(std::count(view.begin(),view.end(),255)==4,"Popup clipping copied outside the intersection");
    Require(suitecut::CompositeBGRA(view,4,4,popup,2,2,-2147483648.0,2147483647.0),"Offscreen popup rejected");
    auto base="/tmp/suitecut-memory-"+std::to_string(getpid());
    suitecut::MediaOptions config;config.width=16;config.height=16;
    config.video_codec="ffv1";config.audio_codec="pcm_s16le";config.destination=base+".mkv";
    // Overlap Open/Cleanup with pause, status, producers and concurrent Stop.
    // Each iteration releases weak owners after all retained packets are gone.
    for(int i=0;i<160;++i) {
      std::atomic<double> clock{0};suitecut::MediaOutput output(config,[&]{return clock.load();});
      auto pixels=std::make_shared<const std::vector<uint8_t>>(16*16*3/2,128);
      auto samples=std::make_shared<const std::vector<int16_t>>(960*2,1234);
      std::weak_ptr<const std::vector<uint8_t>> weak_pixels=pixels;
      std::weak_ptr<const std::vector<int16_t>> weak_samples=samples;
      output.Video(pixels,16,16,0);output.Audio(samples,0);
      std::jthread pause([&]{output.Pause(true);output.Pause(false);});
      std::jthread producer([&]{for(int j=0;j<20;++j){output.Audio(samples,j);output.Video(pixels,16,16,j);output.Status();}});
      clock=20;
      std::jthread first([&]{output.Stop();});
      std::jthread second([&]{output.Stop();});
      pause.join();producer.join();first.join();second.join();
      const auto status=output.Stop();
      Require(status.stopped && !status.queued_audio_bytes && !status.queued_video_bytes,"Shutdown retained queued buffers");
      pixels.reset();samples.reset();Require(weak_pixels.expired() && weak_samples.expired(),"Output retained media after Stop");
      std::filesystem::remove(config.destination);
    }
    // AAC pause alignment at nonzero clock origins must not wait forever on
    // sub-sample floating point cancellation at the completed boundary.
    config.video_codec="libx264";config.audio_codec="aac";
    for(double origin:{.1,1.00012344,1234.56789,98765.4321}) {
      std::atomic<double> clock{origin};suitecut::MediaOutput output(config,[&]{return clock.load();});
      output.Audio(std::make_shared<const std::vector<int16_t>>(4800*2,1234),origin);
      output.Video(std::make_shared<const std::vector<uint8_t>>(16*16*3/2,128),16,16,origin);
      const auto deadline=steady_clock::now()+seconds(5);
      while(!output.Status().ready && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(1));
      Require(output.Status().ready,"Fractional-origin output not ready");
      clock=origin+40;std::atomic<bool> finished{false};
      std::jthread pause([&]{output.Pause(true);finished=true;});
      const auto pause_deadline=steady_clock::now()+seconds(2);
      while(!finished && steady_clock::now()<pause_deadline) std::this_thread::sleep_for(milliseconds(1));
      const bool completed=finished.load();output.Stop();pause.join();
      Require(completed,"Pause deadlocked at its aligned sample endpoint");
      std::filesystem::remove(config.destination);
    }
    config.video_codec="ffv1";config.audio_codec="pcm_s16le";
    // Reject malformed timestamps before queueing or integer conversion.
    std::atomic<double> clock{0};suitecut::MediaOutput bounded(config,[&]{return clock.load();});
    auto samples=std::make_shared<const std::vector<int16_t>>(960*2,1);
    auto pixels=std::make_shared<const std::vector<uint8_t>>(16*16*3/2,128);
    bounded.Video(pixels,16,16,std::numeric_limits<double>::quiet_NaN());
    bounded.Audio(samples,std::numeric_limits<double>::infinity());
    bounded.Audio(std::make_shared<const std::vector<int16_t>>(960002),0);
    Require(!bounded.Status().queued_video_bytes && !bounded.Status().queued_audio_bytes,"Malformed input was retained");
    bounded.Video(pixels,16,16,0);clock=std::numeric_limits<double>::quiet_NaN();
    Require(bounded.Stop().error=="Invalid native output clock","Invalid clock did not fail safely");
    std::filesystem::remove(config.destination);
    suitecut::RenderOptions render;render.inputs={"/nonexistent"};render.destination=base+".mp4";
    render.duration_ms=std::numeric_limits<double>::quiet_NaN();
    try {suitecut::RenderMedia(render,{});throw std::runtime_error("NaN render duration accepted");}
    catch(const std::runtime_error& error){Require(std::string(error.what())=="Invalid native render plan","NaN reached native file processing");}
    std::cout<<"160 concurrent native lifecycle cycles; bounds and ownership regressions passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
