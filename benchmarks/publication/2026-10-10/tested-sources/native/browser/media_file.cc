// SPDX-License-Identifier: MIT
#include "media_file.h"
#include "memory_bounds.h"
#include <limits>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <stdexcept>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
}
namespace suitecut {
namespace {
void Check(int code,const char* operation) {if(code<0) {char detail[AV_ERROR_MAX_STRING_SIZE];av_strerror(code,detail,sizeof(detail));throw std::runtime_error(std::string(operation)+": "+detail);}}
struct Input {
  AVFormatContext* format=nullptr;
  AVCodecContext* decoder=nullptr;
  AVPacket* packet=nullptr;
  AVFrame* frame=nullptr;
  SwsContext* scale=nullptr;
  ~Input() {sws_freeContext(scale);av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&decoder);avformat_close_input(&format);}
  void Open(const std::string& path) {
    if(path.empty() || path.front()!='/' || path.find('\0')!=std::string::npos) throw std::runtime_error("Native file jobs require an absolute local path");
    Check(avformat_open_input(&format,path.c_str(),nullptr,nullptr),"Open native input");
    Check(avformat_find_stream_info(format,nullptr),"Read native input streams");
  }
};
}
ProbeResult ProbeMedia(const std::string& path) {
  Input input;input.Open(path);ProbeResult result;
  if(input.format->duration!=AV_NOPTS_VALUE) result.duration_ms=std::max(0.0,input.format->duration/1000.0);
  result.format=input.format->iformat->name;
  for(unsigned i=0;i<input.format->nb_streams;++i) {auto* params=input.format->streams[i]->codecpar;
    if(params->codec_type==AVMEDIA_TYPE_VIDEO && !result.width) {result.width=params->width;result.height=params->height;}
    if(params->codec_type==AVMEDIA_TYPE_AUDIO) result.audio=true;
    if(params->codec_type!=AVMEDIA_TYPE_VIDEO && params->codec_type!=AVMEDIA_TYPE_AUDIO) continue;
    const auto* stream=input.format->streams[i];ProbeStream item;item.codec=avcodec_get_name(params->codec_id);
    item.duration_ms=stream->duration==AV_NOPTS_VALUE ? result.duration_ms : stream->duration*av_q2d(stream->time_base)*1000;
    if(params->codec_type==AVMEDIA_TYPE_AUDIO) {item.kind="audio";item.channels=params->ch_layout.nb_channels;item.sample_rate=params->sample_rate;}
    else {item.kind="video";item.width=params->width;item.height=params->height;
      auto rate=stream->avg_frame_rate.num ? stream->avg_frame_rate : stream->r_frame_rate;item.frame_rate=av_q2d(rate);item.variable=!stream->avg_frame_rate.num || av_cmp_q(stream->avg_frame_rate,stream->r_frame_rate)!=0;
      item.time_base=std::to_string(stream->time_base.num)+"/"+std::to_string(stream->time_base.den);
      if(const auto* name=av_get_pix_fmt_name(static_cast<AVPixelFormat>(params->format))) item.pixel_format=name;
      if(params->color_range!=AVCOL_RANGE_UNSPECIFIED) item.color_range=params->color_range==AVCOL_RANGE_JPEG ? "full" : "limited";
      if(params->color_space!=AVCOL_SPC_UNSPECIFIED) {const auto* name=av_color_space_name(params->color_space);if(name)item.color_space=name;}
      if(params->color_trc!=AVCOL_TRC_UNSPECIFIED) {const auto* name=av_color_transfer_name(params->color_trc);if(name)item.color_transfer=name;}
      if(params->color_primaries!=AVCOL_PRI_UNSPECIFIED) {const auto* name=av_color_primaries_name(params->color_primaries);if(name)item.color_primaries=name;}
    }
    result.streams.push_back(item);
  }
  return result;
}
MediaStatus CutVideo(const std::string& path,MediaOptions options,double start,double duration) {
  if(!MediaTimestamp(start) || !std::isfinite(duration) || start<0 || duration<=0 || duration>3600000) throw std::runtime_error("Invalid native clip interval");
  if(std::filesystem::weakly_canonical(path)==std::filesystem::weakly_canonical(options.destination)) throw std::runtime_error("Native checkpoint would overwrite its input");
  Input input;input.Open(path);const AVCodec* codec=nullptr;
  const int video=av_find_best_stream(input.format,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);Check(video,"Find checkpoint video");
  input.decoder=avcodec_alloc_context3(codec);if(!input.decoder) throw std::runtime_error("Allocate checkpoint decoder");
  Check(avcodec_parameters_to_context(input.decoder,input.format->streams[video]->codecpar),"Configure checkpoint decoder");
  input.decoder->thread_count=2;Check(avcodec_open2(input.decoder,codec,nullptr),"Open checkpoint decoder");
  input.packet=av_packet_alloc();input.frame=av_frame_alloc();if(!input.packet || !input.frame) throw std::runtime_error("Allocate checkpoint input buffers");
  const auto timebase=input.format->streams[video]->time_base;
  const int64_t stream_start=input.format->streams[video]->start_time==AV_NOPTS_VALUE ? 0 : input.format->streams[video]->start_time;
  if(timebase.num<=0 || timebase.den<=0) throw std::runtime_error("Invalid checkpoint timebase");
  if(start>0) {
    const auto offset=av_rescale_q(static_cast<int64_t>(start*1000),AVRational{1,1000000},timebase);
    if(offset<0 || stream_start>std::numeric_limits<int64_t>::max()-offset) throw std::runtime_error("Checkpoint seek timestamp overflow");
    Check(av_seek_frame(input.format,video,stream_start+offset,AVSEEK_FLAG_BACKWARD),"Seek checkpoint input");avcodec_flush_buffers(input.decoder);
  }
  options.audio=false;options.offline=true;
  std::atomic<double> timeline{0};MediaOutput output(options,[&] {return timeline.load();});
  bool done=false,captured=false;
  auto drain=[&] {
    for(;;) {
      int code=avcodec_receive_frame(input.decoder,input.frame);if(code==AVERROR(EAGAIN) || code==AVERROR_EOF) return;Check(code,"Decode checkpoint frame");
      const auto pts=input.frame->best_effort_timestamp;
      if(pts==AV_NOPTS_VALUE) throw std::runtime_error("Checkpoint video has no frame timestamp");
      const double at=(static_cast<double>(pts)-static_cast<double>(stream_start))*av_q2d(timebase)*1000-start;
      if(at<-.5) {av_frame_unref(input.frame);continue;}
      if(at>=duration) {done=true;av_frame_unref(input.frame);return;}
      input.scale=sws_getCachedContext(input.scale,input.frame->width,input.frame->height,static_cast<AVPixelFormat>(input.frame->format),options.width,options.height,AV_PIX_FMT_YUV420P,SWS_BILINEAR,nullptr,nullptr,nullptr);
      if(!input.scale) throw std::runtime_error("Allocate checkpoint scaler");
      const size_t y=static_cast<size_t>(options.width)*options.height;
      auto pixels=std::make_shared<std::vector<uint8_t>>(y*3/2);
      uint8_t* planes[]={pixels->data(),pixels->data()+y,pixels->data()+y*5/4};const int strides[]={options.width,options.width/2,options.width/2};
      sws_scale(input.scale,input.frame->data,input.frame->linesize,0,input.frame->height,planes,strides);
      timeline=std::max(0.0,at);output.Video(pixels,options.width,options.height,captured ? timeline.load() : 0.0);captured=true;
      av_frame_unref(input.frame);
      const auto status=output.Status();if(!status.error.empty()) throw std::runtime_error(status.error);
    }
  };
  while(!done) {
    const int code=av_read_frame(input.format,input.packet);
    if(code==AVERROR_EOF) {Check(avcodec_send_packet(input.decoder,nullptr),"Flush checkpoint decoder");drain();break;}
    Check(code,"Read checkpoint packet");
    if(input.packet->stream_index==video) {Check(avcodec_send_packet(input.decoder,input.packet),"Send checkpoint packet");drain();}
    av_packet_unref(input.packet);
  }
  if(!captured) throw std::runtime_error("Checkpoint interval contains no video frames");
  timeline=duration;auto result=output.Stop();if(!result.error.empty()) throw std::runtime_error(result.error);return result;
}
}
