// SPDX-License-Identifier: MIT
#include "media_render.h"
#include <algorithm>
#include <memory>
#include <filesystem>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}
namespace suitecut {
namespace {
void Check(int result,const char* action) {if(result<0) {char text[AV_ERROR_MAX_STRING_SIZE];av_strerror(result,text,sizeof(text));throw std::runtime_error(std::string(action)+": "+text);}}
struct Decoder {
  AVCodecContext* codec=nullptr;
  AVFilterContext* source=nullptr;
  int stream=-1;
  bool closed=false;
  ~Decoder() {avcodec_free_context(&codec);}
};
struct Input {
  AVFormatContext* format=nullptr;
  std::vector<std::unique_ptr<Decoder>> decoders;
  bool ended=false;
  ~Input(){avformat_close_input(&format);}
};
struct Render {
  const RenderOptions& config;
  std::function<bool()> cancelled;
  std::vector<std::unique_ptr<Input>> inputs;
  AVFilterGraph* graph=nullptr;
  AVFilterInOut *links_in=nullptr,*links_out=nullptr;
  AVFilterContext *video_sink=nullptr,*audio_sink=nullptr;
  AVCodecContext *video=nullptr,*audio=nullptr;
  AVFormatContext* output=nullptr;
  AVStream *video_stream=nullptr,*audio_stream=nullptr;
  AVFrame *decoded=nullptr,*filtered=nullptr;
  AVPacket *input_packet=nullptr,*encoded=nullptr;
  bool video_end=false,audio_end=false;
  RenderStatus status;
  Render(const RenderOptions& options,std::function<bool()> stop):config(options),cancelled(std::move(stop)){}
  ~Render() {
    avfilter_inout_free(&links_in);avfilter_inout_free(&links_out);avfilter_graph_free(&graph);
    av_frame_free(&decoded);av_frame_free(&filtered);av_packet_free(&input_packet);av_packet_free(&encoded);
    avcodec_free_context(&video);avcodec_free_context(&audio);
    if(output) {if(output->pb) avio_closep(&output->pb);avformat_free_context(output);}
  }
  void Cancel() {if(cancelled && cancelled()) throw std::runtime_error("Native render cancelled");}
  static void Local(const std::string& path) {if(path.empty() || path.front()!='/' || path.find('\0')!=std::string::npos) throw std::runtime_error("Native render requires absolute local paths");}
  Decoder& OpenDecoder(size_t index,AVMediaType type) {
    auto& input=*inputs[index];auto decoder=std::make_unique<Decoder>();const AVCodec* codec=nullptr;
    decoder->stream=av_find_best_stream(input.format,type,-1,-1,&codec,0);Check(decoder->stream,"Find render input stream");
    decoder->codec=avcodec_alloc_context3(codec);if(!decoder->codec) throw std::runtime_error("Allocate render decoder");
    Check(avcodec_parameters_to_context(decoder->codec,input.format->streams[decoder->stream]->codecpar),"Configure render decoder");decoder->codec->thread_count=2;
    Check(avcodec_open2(decoder->codec,codec,nullptr),"Open render decoder");
    input.decoders.push_back(std::move(decoder));return *input.decoders.back();
  }
  void Sources() {
    std::map<std::string,std::vector<AVFilterInOut*>> by_name;
    for(auto* link=links_in;link;link=link->next) {if(!link->name) throw std::runtime_error("Unnamed render input");by_name[link->name].push_back(link);}
    for(auto& [name,links]:by_name) {
      const auto separator=name.find(':');if(separator==std::string::npos) throw std::runtime_error("Invalid render input label");
      const auto index=static_cast<size_t>(std::stoul(name.substr(0,separator)));
      const bool is_audio=name.substr(separator+1)=="a";
      if(index>=inputs.size() || (!is_audio && name.substr(separator+1)!="v")) throw std::runtime_error("Unknown render input label");
      auto& decoder=OpenDecoder(index,is_audio ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO);
      const auto* stream=inputs[index]->format->streams[decoder.stream];const auto base=stream->time_base;
      const auto source_name="input-"+name;
      if(is_audio) {
        char layout[128];Check(av_channel_layout_describe(&decoder.codec->ch_layout,layout,sizeof(layout)),"Describe render audio layout");
        const std::string args="time_base="+std::to_string(base.num)+"/"+std::to_string(base.den)+":sample_rate="+std::to_string(decoder.codec->sample_rate)+":sample_fmt="+av_get_sample_fmt_name(decoder.codec->sample_fmt)+":channel_layout="+layout;
        Check(avfilter_graph_create_filter(&decoder.source,avfilter_get_by_name("abuffer"),source_name.c_str(),args.c_str(),nullptr,graph),"Create render audio source");
      } else {
        const auto ratio=decoder.codec->sample_aspect_ratio.num ? decoder.codec->sample_aspect_ratio : AVRational{1,1};
        const auto rate=av_guess_frame_rate(inputs[index]->format,const_cast<AVStream*>(stream),nullptr);
        const std::string args="video_size="+std::to_string(decoder.codec->width)+"x"+std::to_string(decoder.codec->height)+":pix_fmt="+std::to_string(decoder.codec->pix_fmt)+":time_base="+std::to_string(base.num)+"/"+std::to_string(base.den)+":pixel_aspect="+std::to_string(ratio.num)+"/"+std::to_string(ratio.den)+":frame_rate="+std::to_string(rate.num)+"/"+std::to_string(rate.den ? rate.den : 1);
        Check(avfilter_graph_create_filter(&decoder.source,avfilter_get_by_name("buffer"),source_name.c_str(),args.c_str(),nullptr,graph),"Create render video source");
      }
      AVFilterContext* connect=decoder.source;
      if(links.size()>1) {
        Check(avfilter_graph_create_filter(&connect,avfilter_get_by_name(is_audio ? "asplit" : "split"),(source_name+"-split").c_str(),std::to_string(links.size()).c_str(),nullptr,graph),"Create render input split");
        Check(avfilter_link(decoder.source,0,connect,0),"Link render split");
      }
      for(size_t i=0;i<links.size();++i) Check(avfilter_link(connect,static_cast<unsigned>(links.size()>1 ? i : 0),links[i]->filter_ctx,links[i]->pad_idx),"Link render graph input");
    }
  }
  void Sinks() {
    for(auto* link=links_out;link;link=link->next) {
      const std::string name=link->name ? link->name : "";
      if(name=="video0") {
        Check(avfilter_graph_create_filter(&video_sink,avfilter_get_by_name("buffersink"),"video-output",nullptr,nullptr,graph),"Create render video sink");
        const AVPixelFormat formats[]={av_get_pix_fmt(config.pixel_format.c_str()),AV_PIX_FMT_NONE};if(formats[0]==AV_PIX_FMT_NONE) throw std::runtime_error("Unknown render pixel format");
        Check(av_opt_set_int_list(video_sink,"pix_fmts",formats,AV_PIX_FMT_NONE,AV_OPT_SEARCH_CHILDREN),"Set render video format");
        AVFilterContext* cadence=nullptr;
        const auto fps="fps="+std::to_string(config.fps)+":start_time=0:round=near";
        Check(avfilter_graph_create_filter(&cadence,avfilter_get_by_name("fps"),"video-cadence",fps.c_str(),nullptr,graph),"Create render cadence");
        Check(avfilter_link(link->filter_ctx,link->pad_idx,cadence,0),"Link render cadence");
        Check(avfilter_link(cadence,0,video_sink,0),"Link render video sink");
      } else if(name=="audioout" && config.audio) {
        Check(avfilter_graph_create_filter(&audio_sink,avfilter_get_by_name("abuffersink"),"audio-output",nullptr,nullptr,graph),"Create render audio sink");
        const auto* codec=avcodec_find_encoder_by_name(config.audio_codec.c_str());if(!codec) throw std::runtime_error("Native audio encoder unavailable");
        const void* formats=nullptr;int count=0;Check(avcodec_get_supported_config(nullptr,codec,AV_CODEC_CONFIG_SAMPLE_FORMAT,0,&formats,&count),"Get render audio formats");
        if(count) Check(av_opt_set_bin(audio_sink,"sample_fmts",static_cast<const uint8_t*>(formats),count*sizeof(AVSampleFormat),AV_OPT_SEARCH_CHILDREN),"Set render audio format");
        const int rates[]={48000,-1};Check(av_opt_set_int_list(audio_sink,"sample_rates",rates,-1,AV_OPT_SEARCH_CHILDREN),"Set render sample rate");
        Check(av_opt_set(audio_sink,"ch_layouts","stereo",AV_OPT_SEARCH_CHILDREN),"Set render channels");
        Check(avfilter_link(link->filter_ctx,link->pad_idx,audio_sink,0),"Link render audio sink");
      } else throw std::runtime_error("Unknown render output label");
    }
    if(!video_sink || (config.audio && !audio_sink)) throw std::runtime_error("Render graph is missing its output");
    avfilter_inout_free(&links_in);avfilter_inout_free(&links_out);
    Check(avfilter_graph_config(graph,nullptr),"Configure native render graph");
  }
  AVCodecContext* Encoder(const std::string& name,bool is_audio) {
    auto* codec=avcodec_find_encoder_by_name(name.c_str());if(!codec) throw std::runtime_error("Native render encoder unavailable: "+name);
    auto* context=avcodec_alloc_context3(codec);if(!context) throw std::runtime_error("Allocate render encoder");
    (is_audio ? audio : video)=context;context->thread_count=2;
    if(output->oformat->flags & AVFMT_GLOBALHEADER) context->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options=nullptr;
    if(is_audio) {
      context->sample_fmt=static_cast<AVSampleFormat>(av_buffersink_get_format(audio_sink));context->sample_rate=av_buffersink_get_sample_rate(audio_sink);context->time_base={1,context->sample_rate};context->bit_rate=config.audio_bitrate;
      Check(av_buffersink_get_ch_layout(audio_sink,&context->ch_layout),"Get render encoder audio layout");
    } else {
      context->width=av_buffersink_get_w(video_sink);context->height=av_buffersink_get_h(video_sink);context->pix_fmt=static_cast<AVPixelFormat>(av_buffersink_get_format(video_sink));
      context->time_base={1,config.fps};context->framerate={config.fps,1};context->sample_aspect_ratio={1,1};
      context->color_range=config.full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
      context->color_primaries=AVCOL_PRI_BT709;context->color_trc=AVCOL_TRC_BT709;
      context->colorspace=(av_pix_fmt_desc_get(context->pix_fmt)->flags&AV_PIX_FMT_FLAG_RGB) ? AVCOL_SPC_RGB : AVCOL_SPC_BT709;
      for(const auto& [key,value]:config.video_options) av_dict_set(&options,key.c_str(),value.c_str(),0);
      if(name=="libvpx-vp9" || name=="libvpx") context->bit_rate=0;
    }
    const int code=avcodec_open2(context,codec,&options);av_dict_free(&options);Check(code,"Open native render encoder");
    return context;
  }
  void Open() {
    if(config.inputs.empty() || config.inputs.size()>32 || config.graph.size()>4*1024*1024 || config.duration_ms<=0 || config.duration_ms>24*3600000.0 || (config.fps!=30 && config.fps!=60)) throw std::runtime_error("Invalid native render plan");
    Local(config.destination);
    for(const auto& path:config.inputs) {
      Local(path);if(std::filesystem::weakly_canonical(path)==std::filesystem::weakly_canonical(config.destination)) throw std::runtime_error("Native render would overwrite its input");
      auto input=std::make_unique<Input>();Check(avformat_open_input(&input->format,path.c_str(),nullptr,nullptr),"Open render input");Check(avformat_find_stream_info(input->format,nullptr),"Read render input");inputs.push_back(std::move(input));
    }
    graph=avfilter_graph_alloc();if(!graph) throw std::runtime_error("Allocate render graph");graph->nb_threads=2;
    Check(avfilter_graph_parse2(graph,config.graph.c_str(),&links_in,&links_out),"Parse native render graph");Sources();Sinks();
    Check(avformat_alloc_output_context2(&output,nullptr,config.container.c_str(),config.destination.c_str()),"Create render muxer");if(!output) throw std::runtime_error("Allocate render muxer");
    video=Encoder(config.video_codec,false);video_stream=avformat_new_stream(output,nullptr);if(!video_stream) throw std::runtime_error("Allocate render video stream");video_stream->time_base=video->time_base;video_stream->avg_frame_rate={config.fps,1};Check(avcodec_parameters_from_context(video_stream->codecpar,video),"Set render video stream");
    if(config.audio) {audio=Encoder(config.audio_codec,true);audio_stream=avformat_new_stream(output,nullptr);if(!audio_stream) throw std::runtime_error("Allocate render audio stream");audio_stream->time_base=audio->time_base;Check(avcodec_parameters_from_context(audio_stream->codecpar,audio),"Set render audio stream");if(audio->frame_size) av_buffersink_set_frame_size(audio_sink,audio->frame_size);}
    decoded=av_frame_alloc();filtered=av_frame_alloc();input_packet=av_packet_alloc();encoded=av_packet_alloc();if(!decoded || !filtered || !input_packet || !encoded) throw std::runtime_error("Allocate render buffers");
    Check(avio_open(&output->pb,config.destination.c_str(),AVIO_FLAG_WRITE),"Open render output");
    AVDictionary* options=nullptr;if(config.container=="mp4" || config.container=="mov") av_dict_set(&options,"movflags","+faststart",0);
    const int code=avformat_write_header(output,&options);av_dict_free(&options);Check(code,"Write render header");
    audio_end=!config.audio;
  }
  void Encode(AVCodecContext* codec,AVStream* stream,AVFrame* frame) {
    Check(avcodec_send_frame(codec,frame),"Send rendered frame");
    for(;;) {const int code=avcodec_receive_packet(codec,encoded);if(code==AVERROR(EAGAIN) || code==AVERROR_EOF) break;Check(code,"Receive rendered packet");if(stream==video_stream && encoded->duration<=0) encoded->duration=1;av_packet_rescale_ts(encoded,codec->time_base,stream->time_base);encoded->stream_index=stream->index;const int written=av_interleaved_write_frame(output,encoded);av_packet_unref(encoded);Check(written,"Mux rendered packet");}
  }
  void Drain(AVFilterContext* sink,bool is_audio) {
    auto& ended=is_audio ? audio_end : video_end;if(ended) return;
    for(;;) {
      Cancel();const int code=av_buffersink_get_frame(sink,filtered);if(code==AVERROR(EAGAIN)) return;if(code==AVERROR_EOF) {ended=true;return;}Check(code,"Read rendered frame");
      auto* codec=is_audio ? audio : video;
      filtered->pts=av_rescale_q(filtered->pts,av_buffersink_get_time_base(sink),codec->time_base);
      if(filtered->pts*av_q2d(codec->time_base)*1000>=config.duration_ms) {ended=true;av_frame_unref(filtered);return;}
      if(is_audio) status.audio_samples+=filtered->nb_samples;else ++status.video_frames;
      if(!is_audio) filtered->pict_type=AV_PICTURE_TYPE_NONE;
      Encode(codec,is_audio ? audio_stream : video_stream,filtered);av_frame_unref(filtered);
    }
  }
  void Decode(Input& input,Decoder& decoder) {
    for(;;) {
      Cancel();
      const int code=avcodec_receive_frame(decoder.codec,decoded);if(code==AVERROR(EAGAIN) || code==AVERROR_EOF) return;Check(code,"Decode render input");
      decoded->pts=decoded->best_effort_timestamp;
      const auto start=input.format->streams[decoder.stream]->start_time;if(decoded->pts!=AV_NOPTS_VALUE && start!=AV_NOPTS_VALUE) decoded->pts-=start;
      if(!decoder.closed) {
        const int fed=av_buffersrc_add_frame_flags(decoder.source,decoded,AV_BUFFERSRC_FLAG_KEEP_REF);
        if(fed==AVERROR_EOF) decoder.closed=true;else Check(fed,"Feed native render graph");
      }
      av_frame_unref(decoded);
    }
  }
  void Read(Input& input) {
    const int code=av_read_frame(input.format,input_packet);
    if(code==AVERROR_EOF) {for(auto& decoder:input.decoders) {if(decoder->closed) continue;Check(avcodec_send_packet(decoder->codec,nullptr),"Flush render decoder");Decode(input,*decoder);const int ended=av_buffersrc_add_frame_flags(decoder->source,nullptr,0);if(ended!=AVERROR_EOF) Check(ended,"Finish render graph input");decoder->closed=true;}input.ended=true;return;}
    Check(code,"Read render input packet");for(auto& decoder:input.decoders) if(!decoder->closed && decoder->stream==input_packet->stream_index) {Check(avcodec_send_packet(decoder->codec,input_packet),"Send render input packet");Decode(input,*decoder);break;}av_packet_unref(input_packet);
    if(std::all_of(input.decoders.begin(),input.decoders.end(),[](const auto& decoder){return decoder->closed;})) input.ended=true;
  }
  RenderStatus Run() {
    Open();size_t cursor=0;
    while(!video_end || !audio_end) {
      Cancel();Drain(video_sink,false);if(config.audio) Drain(audio_sink,true);if(video_end && audio_end) break;
      Input* selected=nullptr;unsigned requests=0;
      for(auto& input:inputs) if(!input->ended) {unsigned total=0;for(auto& decoder:input->decoders) total+=av_buffersrc_get_nb_failed_requests(decoder->source);if(!selected || total>requests) {selected=input.get();requests=total;}}
      if(!selected) throw std::runtime_error("Native render graph stalled after input EOF");
      if(!requests) {for(size_t i=0;i<inputs.size();++i) {auto& input=inputs[(cursor++)%inputs.size()];if(!input->ended) {selected=input.get();break;}}}
      Read(*selected);
    }
    Encode(video,video_stream,nullptr);if(config.audio) Encode(audio,audio_stream,nullptr);Check(av_write_trailer(output),"Finalize native render");return status;
  }
};
}
RenderStatus RenderMedia(const RenderOptions& config,const std::function<bool()>& cancelled) {return Render(config,cancelled).Run();}
}
