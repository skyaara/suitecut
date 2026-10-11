// SPDX-License-Identifier: MIT
#include "media_engine.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
extern "C" {
#include <libavformat/avformat.h>
}
using namespace std::chrono;
void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void Wait(const std::function<bool()>& condition) {
  const auto deadline=steady_clock::now()+seconds(5);
  while(!condition() && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(1));
  Require(condition(),"Native audio startup test timed out");
}
void CheckPCM(const std::string& path,int16_t sample,int frames) {
  AVFormatContext* format=nullptr;
  Require(avformat_open_input(&format,path.c_str(),nullptr,nullptr)>=0,"Open startup PCM");
  AVPacket* packet=av_packet_alloc();int bytes=0;bool correct=true;
  while(av_read_frame(format,packet)>=0) {
    if(format->streams[packet->stream_index]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO) {
      bytes+=packet->size;
      for(int i=0;i+1<packet->size;i+=2) {
        int16_t value;std::memcpy(&value,packet->data+i,2);correct &= value==sample;
      }
    }
    av_packet_unref(packet);
  }
  av_packet_free(&packet);avformat_close_input(&format);
  Require(correct && bytes==frames*4,"Startup PCM contains missing or leading silent samples");
}
int main() {
  const auto base="/tmp/suitecut-startup-"+std::to_string(getpid());
  try {
    for(const bool delayed_video:{false,true}) {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+"-signal"+(delayed_video?"-video-late":"")+".mkv";config.width=320;config.height=180;
      config.audio_codec="pcm_s16le";
      suitecut::MediaOutput output(config,[&] {return clock.load();});
      output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,delayed_video?130:0);
      std::this_thread::sleep_for(milliseconds(40));Require(!output.Status().ready,"Video started audio-gated output");
      output.Audio(std::make_shared<std::vector<int16_t>>(960*2,0),100);
      {
        std::this_thread::sleep_for(milliseconds(40));Require(!output.Status().ready,"Silent PCM satisfied signal readiness");
        auto samples=std::make_shared<std::vector<int16_t>>(960*2,1234);
        std::fill_n(samples->begin(),240*2,0);output.Audio(samples,120);
      }
      Wait([&] {return output.Status().ready;});
      const double expected=delayed_video?130:125;
      Require(output.Status().started_ms==expected,"Startup timeline does not match common audio/video readiness");
      clock=expected+10;const auto status=output.Stop();
      Require(status.error.empty() && status.audio_frames==480 && status.audio_missing==0,"Startup output lost available PCM");
      CheckPCM(config.destination,1234,480);std::filesystem::remove(config.destination);
    }
    suitecut::MediaOptions config;config.destination=base+"-timeout.mkv";
    config.width=320;config.height=180;config.audio_startup_timeout_ms=80;
    suitecut::MediaOutput timed(config,[] {return 0;});
    timed.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
    timed.Audio(std::make_shared<std::vector<int16_t>>(960*2,0),0);
    Wait([&] {return timed.Status().stopped;});
    Require(!timed.Status().ready && timed.Stop().error.find("audio readiness timed out")!=std::string::npos,"Silent startup timeout was hidden");
    std::filesystem::remove(config.destination);
    config.destination=base+"-cancel.mkv";config.audio_startup_timeout_ms=5000;
    suitecut::MediaOutput cancelled(config,[] {return 0;});
    cancelled.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
    const auto began=steady_clock::now();const auto stopped=cancelled.Stop();
    Require(!stopped.ready && stopped.stopped && steady_clock::now()-began<seconds(1),"Stop did not interrupt audio readiness");
    std::filesystem::remove(config.destination);
    config.audio=false;
    config.destination=base+"-video-only.mkv";
    suitecut::MediaOutput silent(config,[] {return 0;});
    silent.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
    Wait([&] {return silent.Status().ready;});
    Require(silent.Stop().error.empty(),"Video-only output waited for sound");
    std::filesystem::remove(config.destination);
    config.audio=true;config.offline=true;config.destination=base+"-offline.mkv";
    suitecut::MediaOutput offline(config,[] {return 0;});
    offline.Audio(std::make_shared<std::vector<int16_t>>(960*2,0),0);
    offline.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
    Wait([&] {return offline.Status().ready;});
    Require(offline.Stop().error.empty(),"Offline input waited for live CEF sound");
    std::filesystem::remove(config.destination);
    std::cout<<"Native audio readiness, PCM start, intentional silence, timeout and cancellation passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
