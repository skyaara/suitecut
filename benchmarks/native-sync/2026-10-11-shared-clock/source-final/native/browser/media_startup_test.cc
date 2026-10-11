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
#include <libavcodec/avcodec.h>
}
using namespace std::chrono;
void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void Wait(const std::function<bool()>& condition,const char* label="Native audio startup") {
  const auto deadline=steady_clock::now()+seconds(5);
  while(!condition() && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(1));
  if (!condition()) throw std::runtime_error(std::string(label)+" test timed out");
}
void CheckPCM(const std::string& path,int silent_frames,int tone_frames) {
  AVFormatContext* format=nullptr;
  Require(avformat_open_input(&format,path.c_str(),nullptr,nullptr)>=0,"Open startup audio");
  Require(avformat_find_stream_info(format,nullptr)>=0,"Read startup audio streams");
  const int stream=av_find_best_stream(format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
  Require(stream>=0,"Find startup audio stream");
  auto* codec=avcodec_alloc_context3(avcodec_find_decoder(format->streams[stream]->codecpar->codec_id));
  Require(codec && avcodec_parameters_to_context(codec,format->streams[stream]->codecpar)>=0 && avcodec_open2(codec,codec->codec,nullptr)>=0,"Open startup audio decoder");
  AVPacket* packet=av_packet_alloc();AVFrame* frame=av_frame_alloc();int samples=0;bool correct=true;
  const auto receive=[&] {
    while (avcodec_receive_frame(codec,frame)>=0) {
      const bool planar=frame->format==AV_SAMPLE_FMT_S16P;
      if (frame->format!=AV_SAMPLE_FMT_S16 && !planar) {correct=false;continue;}
      for(int i=0;i<frame->nb_samples;++i) for(int c=0;c<2;++c) {
        int16_t value;std::memcpy(&value,frame->data[planar?c:0]+(planar?i:i*2+c)*2,2);
        correct &= value==(samples/2<silent_frames?0:1234);++samples;
      }
      av_frame_unref(frame);
    }
  };
  while(av_read_frame(format,packet)>=0) {
    if(packet->stream_index==stream) {Require(avcodec_send_packet(codec,packet)>=0,"Decode startup audio packet");receive();}
    av_packet_unref(packet);
  }
  avcodec_send_packet(codec,nullptr);receive();
  av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&codec);avformat_close_input(&format);
  Require(correct && samples==(silent_frames+tone_frames)*2,"Silent startup or subsequent sound changed its PCM timeline");
}

int main() {
  const auto base="/tmp/suitecut-startup-"+std::to_string(getpid());
  try {
    for(const bool sound_later:{false,true}) {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+(sound_later?"-delayed.mkv":"-silent.mkv");
      config.width=320;config.height=180;config.audio_codec="pcm_s16le";
      suitecut::MediaOutput output(config,[&] {return clock.load();});
      output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
      Wait([&] {return output.Status().ready;});
      Require(output.Status().started_ms==0,"Silent startup waited for audible content");
      if (sound_later) {
        output.Audio(std::make_shared<std::vector<int16_t>>(4800*2,0),0);
        output.Audio(std::make_shared<std::vector<int16_t>>(1920*2,1234),100);
      }
      clock=140;const auto status=output.Stop();
      Require(status.error.empty() && status.audio_frames==6720 && status.audio_late==0,"Silent-safe output lost its sample-count clock");
      Require(status.audio_missing==(sound_later?0:6720),"Coverage counter hid absent callbacks or counted provided silence as missing");
      CheckPCM(config.destination,sound_later?4800:6720,sound_later?1920:0);
      std::filesystem::remove(config.destination);
    }
    // Requested capture must wait for its first PCM block, even if that block
    // contains silence. Start both tracks at their common captured epoch.
    {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+"-requested.mkv";config.width=320;config.height=180;config.audio_codec="pcm_s16le";
      suitecut::MediaOutput output(config,[&] {return clock.load();});output.AudioState(true);
      output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
      std::this_thread::sleep_for(milliseconds(30));Require(!output.Status().ready,"Requested audio started without PCM");
      clock=100;output.Audio(std::make_shared<std::vector<int16_t>>(960*2,0),100);
      Wait([&] {return output.Status().ready;});Require(output.Status().started_ms==100,"Tracks did not use the common capture epoch");
      clock=120;const auto stopped=output.Stop();
      Require(stopped.error.empty() && stopped.audio_frames==960 && stopped.audio_missing==0,"Captured silent startup was discarded");
      CheckPCM(config.destination,960,0);std::filesystem::remove(config.destination);
    }
    // An active source may deliver old timestamps late. Wait within the native
    // budget instead of irreversibly writing silence and discarding that PCM.
    {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+"-adaptive.mkv";config.width=320;config.height=180;config.audio_codec="pcm_s16le";
      suitecut::MediaOutput output(config,[&] {return clock.load();});
      output.Audio(std::make_shared<std::vector<int16_t>>(960*2,1234),0);
      output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
      Wait([&] {return output.Status().ready;});
      clock=500;output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,500);
      Wait([&] {const auto s=output.Status();if(!s.error.empty()) throw std::runtime_error(s.error);return s.audio_frames==960 && s.buffered_delay_ms>=480;},"Adaptive initial hold");
      output.Audio(std::make_shared<std::vector<int16_t>>(960*2,1234),20);
      clock=550;output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,550);
      Wait([&] {const auto s=output.Status();if(!s.error.empty()) throw std::runtime_error(s.error);return s.audio_frames>=1920;},"Adaptive delayed input");
      Require(output.Status().audio_missing==0 && output.Status().audio_late==0,"Delayed available PCM was replaced with silence");
      Require(output.Status().buffered_delay_ms<=1000,"Adaptive buffer exceeded its bound");
      // Later timestamped input proves a real absent interval; do not retime it.
      output.Audio(std::make_shared<std::vector<int16_t>>(960*2,1234),60);
      clock=600;output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,600);
      Wait([&] {const auto s=output.Status();if(!s.error.empty()) throw std::runtime_error(s.error);return s.audio_frames>=3840;},"Adaptive genuine hole");
      Require(output.Status().audio_missing==960,"Adaptive buffer collapsed a genuine source hole");
      output.AudioState(false);
      clock=80;const auto stopped=output.Stop();
      Require(stopped.error.empty() && stopped.audio_frames==3840,"Adaptive output shutdown was incorrect");
      std::filesystem::remove(config.destination);
    }
    // Stop keeps accepting delayed source input up to its original cutoff.
    {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+"-drain.mkv";config.width=320;config.height=180;config.audio_codec="pcm_s16le";
      suitecut::MediaOutput output(config,[&] {return clock.load();});
      output.Audio(std::make_shared<std::vector<int16_t>>(960*2,1234),0);
      output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
      Wait([&] {return output.Status().ready;});clock=40;
      suitecut::MediaStatus stopped;
      std::jthread stop([&] {stopped=output.Stop();});
      Wait([&] {const auto s=output.Status();if(!s.error.empty()) throw std::runtime_error(s.error);return s.video_frames>=1;},"Stop drain video");
      output.Audio(std::make_shared<std::vector<int16_t>>(960*2,1234),20);
      stop.join();
      Require(stopped.error.empty() && stopped.audio_frames==1920 && stopped.audio_missing==0 && stopped.audio_late==0,"Stop discarded buffered final source audio");
      CheckPCM(config.destination,0,1920);std::filesystem::remove(config.destination);
    }
    // Pausing drains existing pre-pause PCM before clearing buffers. Fixed
    // codecs finish a full block so their encoder can accept resumed input.
    for (const std::string codec:{"pcm_s16le","flac"}) {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+"-pause-"+codec+".mkv";config.width=320;config.height=180;config.audio_codec=codec;
      suitecut::MediaOutput output(config,[&] {return clock.load();});
      const int before=codec=="flac"?4608:1920;
      output.Audio(std::make_shared<std::vector<int16_t>>(before*2,1234),0);
      output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
      Wait([&] {return output.Status().ready;});clock=40;output.Pause(true);
      Require(output.Status().audio_frames==before && output.Status().audio_missing==0,"Pause discarded pre-pause PCM");
      clock=1040;output.Pause(false);
      output.Audio(std::make_shared<std::vector<int16_t>>(960*2,999),500);
      output.Audio(std::make_shared<std::vector<int16_t>>(1920*2,1234),1040);
      clock=1080;const auto stopped=output.Stop();
      Require(stopped.error.empty() && stopped.audio_frames==before+1920 && stopped.audio_missing==0 && stopped.audio_late==0,"Resume included excluded source content or lost available PCM");
      CheckPCM(config.destination,0,before+1920);std::filesystem::remove(config.destination);
    }
    // Small capture jitter must not duplicate the previous frame at each tick.
    // The nearest frame is permitted only within half an output interval.
    {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+"-video-jitter.mkv";config.width=16;config.height=16;
      config.audio=false;config.fps=60;config.video_codec="ffv1";
      suitecut::MediaOutput output(config,[&] {return clock.load();});
      for(const auto [at,luma]:{std::pair<double,int>{0,16},{19,80},{36,160}}) {
        auto pixels=std::make_shared<std::vector<uint8_t>>(16*16*3/2,128);std::fill_n(pixels->begin(),16*16,luma);
        output.Video(pixels,16,16,at);
      }
      Wait([&] {return output.Status().ready;});clock=50;const auto status=output.Stop();
      Require(status.error.empty() && status.video_frames==3 && status.repeated_frames==0,"Capture jitter duplicated a video frame");
      AVFormatContext* format=nullptr;Require(avformat_open_input(&format,config.destination.c_str(),nullptr,nullptr)>=0,"Open jitter video");
      Require(avformat_find_stream_info(format,nullptr)>=0,"Probe jitter video");
      const int stream=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
      auto* decoder=avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_FFV1));
      Require(decoder && avcodec_parameters_to_context(decoder,format->streams[stream]->codecpar)>=0 && avcodec_open2(decoder,decoder->codec,nullptr)>=0,"Open jitter decoder");
      auto* packet=av_packet_alloc();auto* frame=av_frame_alloc();std::vector<int> luma;
      while(av_read_frame(format,packet)>=0) {
        if(packet->stream_index==stream) {Require(avcodec_send_packet(decoder,packet)>=0,"Decode jitter packet");while(avcodec_receive_frame(decoder,frame)>=0) {luma.push_back(frame->data[0][0]);av_frame_unref(frame);}}
        av_packet_unref(packet);
      }
      av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&decoder);avformat_close_input(&format);
      Require(luma==std::vector<int>({16,80,160}),"Nearest-frame sampling changed image order/content");std::filesystem::remove(config.destination);
    }
    // Overflow may drop old source frames, but must not display a distant future frame
    // early relative to the audio clock. Retain the last eligible captured view.
    {
      std::atomic<double> clock{0};suitecut::MediaOptions config;
      config.destination=base+"-future.mkv";config.width=320;config.height=180;config.audio=false;config.video_codec="ffv1";
      suitecut::MediaOutput output(config,[&] {return clock.load();});
      auto black=std::make_shared<std::vector<uint8_t>>(320*180*3/2,128);std::fill_n(black->begin(),320*180,16);
      output.Video(black,320,180,0);Wait([&] {return output.Status().ready;});
      auto white=std::make_shared<std::vector<uint8_t>>(320*180*3/2,128);std::fill_n(white->begin(),320*180,235);
      for(int i=0;i<70;++i) output.Video(white,320,180,1000+i*1000.0/30);
      clock=10;Require(output.Stop().error.empty(),"Future-frame overflow output failed");
      AVFormatContext* format=nullptr;Require(avformat_open_input(&format,config.destination.c_str(),nullptr,nullptr)>=0,"Open future-frame output");
      Require(avformat_find_stream_info(format,nullptr)>=0,"Probe future-frame output");
      const int stream=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
      auto* decoder=avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_FFV1));
      Require(decoder && avcodec_parameters_to_context(decoder,format->streams[stream]->codecpar)>=0 && avcodec_open2(decoder,decoder->codec,nullptr)>=0,"Open future-frame decoder");
      auto* packet=av_packet_alloc();auto* frame=av_frame_alloc();int luminance=-1;
      while(av_read_frame(format,packet)>=0) {
        if(packet->stream_index==stream && avcodec_send_packet(decoder,packet)>=0 && avcodec_receive_frame(decoder,frame)>=0) {luminance=frame->data[0][0];av_packet_unref(packet);break;}
        av_packet_unref(packet);
      }
      av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&decoder);avformat_close_input(&format);
      Require(luminance==16,"A future source frame advanced ahead of the shared media clock");
      std::filesystem::remove(config.destination);
    }
    suitecut::MediaOptions config;config.destination=base+"-timeout.mkv";
    config.width=320;config.height=180;config.audio_startup_timeout_ms=80;
    suitecut::MediaOutput timed(config,[] {return 0;});
    Wait([&] {return timed.Status().stopped;});
    Require(!timed.Status().ready && timed.Stop().error.find("output readiness timed out")!=std::string::npos,"Missing video startup timeout was hidden");
    std::filesystem::remove(config.destination);
    config.destination=base+"-audio-timeout.mkv";
    suitecut::MediaOutput audio_timed(config,[] {return 0;});audio_timed.AudioState(true);
    audio_timed.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
    Wait([&] {return audio_timed.Status().stopped;});
    Require(!audio_timed.Status().ready && audio_timed.Stop().error.find("output readiness timed out")!=std::string::npos,"Missing requested audio startup timeout was hidden");
    std::filesystem::remove(config.destination);
    config.destination=base+"-cancel.mkv";config.audio_startup_timeout_ms=5000;
    suitecut::MediaOutput cancelled(config,[] {return 0;});
    const auto began=steady_clock::now();const auto stopped=cancelled.Stop();
    Require(!stopped.ready && stopped.stopped && steady_clock::now()-began<seconds(1),"Stop did not interrupt engine readiness");
    std::filesystem::remove(config.destination);
    for (const bool offline:{false,true}) {
      config.audio=offline;config.offline=offline;
      config.destination=base+(offline?"-offline.mkv":"-video-only.mkv");
      suitecut::MediaOutput output(config,[] {return 0;});
      output.Video(std::make_shared<std::vector<uint8_t>>(320*180*3/2,128),320,180,0);
      Wait([&] {return output.Status().ready;});
      Require(output.Stop().error.empty(),"Video-only/offline output waited for sound");
      std::filesystem::remove(config.destination);
    }
    std::cout<<"Native silent startup, later PCM, sample clock, timeout and cancellation passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
