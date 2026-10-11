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
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
int __real_avcodec_send_frame(AVCodecContext*, const AVFrame*);
}
using namespace std::chrono;
namespace {
std::atomic<int> delayed_type{-1}, delay_ms{0};
std::atomic<bool> worker_delayed{false};
void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
template<class F> void Wait(F condition) {
  const auto end = steady_clock::now()+seconds(5);
  while (!condition() && steady_clock::now()<end) std::this_thread::sleep_for(milliseconds(1));
  Require(condition(),"Shared-clock test timed out");
}
void CheckPCM(const std::string& path) {
  AVFormatContext* format=nullptr;
  Require(avformat_open_input(&format,path.c_str(),nullptr,nullptr)>=0,"Open synchronized output");
  avformat_find_stream_info(format,nullptr);
  const int stream=av_find_best_stream(format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
  AVPacket* packet=av_packet_alloc();int count=0;bool exact=stream>=0 && packet;
  while(exact && av_read_frame(format,packet)>=0) {
    if(packet->stream_index==stream) {
      exact &= packet->size%4==0;
      for(int i=0;i+4<=packet->size;i+=4) {
        int16_t left,right;std::memcpy(&left,packet->data+i,2);std::memcpy(&right,packet->data+i+2,2);
        exact &= left==1000+count/960 && right==left; ++count;
      }
    }
    av_packet_unref(packet);
  }
  av_packet_free(&packet);avformat_close_input(&format);
  Require(exact && count==24000,"Delayed worker changed, dropped, repeated or reordered PCM");
}
}
extern "C" int __wrap_avcodec_send_frame(AVCodecContext* codec,const AVFrame* frame) {
  if(frame && codec->codec_type==delayed_type.load()) {
    const int duration=delay_ms.exchange(0);
    if(duration) {worker_delayed=true;std::this_thread::sleep_for(milliseconds(duration));}
  }
  return __real_avcodec_send_frame(codec,frame);
}
int main() {
  try {
    for(int fps:{30,60}) for(int type:{AVMEDIA_TYPE_AUDIO,AVMEDIA_TYPE_VIDEO}) {
      std::atomic<double> clock{0};suitecut::MediaOptions options;
      options.width=16;options.height=16;options.fps=fps;options.video_codec="ffv1";options.audio_codec="pcm_s16le";
      options.destination="/tmp/suitecut-sync-"+std::to_string(getpid())+"-"+std::to_string(fps)+"-"+std::to_string(type)+".mkv";
      delayed_type=type;delay_ms=250;worker_delayed=false;
      suitecut::MediaOutput output(options,[&] {return clock.load();});
      for(int i=0;i<25;++i) output.Audio(std::make_shared<const std::vector<int16_t>>(960*2,1000+i),i*20);
      for(int i=0;i<=fps/2;++i) output.Video(std::make_shared<const std::vector<uint8_t>>(16*16*3/2,128),16,16,i*1000.0/fps);
      Wait([&] {return output.Status().ready;});clock=500;
      Wait([&] {return worker_delayed.load();});std::this_thread::sleep_for(milliseconds(75));
      const auto held=output.Status();
      if(type==AVMEDIA_TYPE_AUDIO) Require(held.video_frames<=1,"Video committed ahead of delayed audio");
      else Require(held.audio_frames<=960,"Audio committed ahead of delayed video");
      const auto stopped=output.Stop();
      Require(stopped.error.empty() && stopped.video_frames==static_cast<uint64_t>(fps/2) && stopped.audio_frames==24000,"Delayed output failed to catch up and drain");
      Require(stopped.audio_missing==0 && stopped.audio_late==0 && stopped.audio_dropped==0,"Worker delay caused sample loss");
      Require(stopped.maximum_track_lead_ms<=std::max(1000.0/fps,20.0)+1e-6,"Completed track lead exceeded one block");
      CheckPCM(options.destination);std::filesystem::remove(options.destination);
      std::cout<<fps<<" FPS, delayed "<<(type==AVMEDIA_TYPE_AUDIO?"audio":"video")<<": PCM exact, maximum encoder lead "<<stopped.maximum_track_lead_ms<<" ms\n";
    }
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
