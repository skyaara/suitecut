// SPDX-License-Identifier: MIT
#include "media_engine.h"
#include "audio_trace.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace suitecut {
bool EncoderAvailable(const std::string& name) { return avcodec_find_encoder_by_name(name.c_str()) != nullptr; }
namespace {
struct EncoderChoice { const char *name, *kind, *codec; bool hardware, lossless; };
constexpr EncoderChoice choices[] = {
  {"libx264", "video", "h264", false, false},
  {"libx265", "video", "hevc", false, false},
  {"libvpx", "video", "vp8", false, false},
  {"libvpx-vp9", "video", "vp9", false, false},
  {"libsvtav1", "video", "av1", false, false},
  {"libaom-av1", "video", "av1", false, false},
  {"ffv1", "video", "ffv1", false, true},
  {"h264_nvenc", "video", "h264", true, false},
  {"hevc_nvenc", "video", "hevc", true, false},
  {"av1_nvenc", "video", "av1", true, false},
  {"aac", "audio", "aac", false, false},
  {"libopus", "audio", "opus", false, false},
  {"flac", "audio", "flac", false, true},
  {"pcm_s16le", "audio", "pcm_s16le", false, true},
};
const EncoderChoice& Choice(const std::string& name, const char* kind) {
  for (const auto& item : choices) if (name == item.name && std::string(kind) == item.kind) return item;
  throw std::runtime_error("Unsupported native " + std::string(kind) + " encoder: " + name);
}
std::string Container(const MediaOptions& options) {
  if (options.streaming) {
    if (!options.container.empty()) throw std::runtime_error("RTMP container is fixed to FLV");
    return "flv";
  }
  const auto explicit_name = options.container == "mkv" ? "matroska" : options.container;
  if (!explicit_name.empty() && explicit_name != "mp4" && explicit_name != "webm" && explicit_name != "matroska" && explicit_name != "mov")
    throw std::runtime_error("Unsupported native recording container: " + explicit_name);
  const auto* format = av_guess_format(explicit_name.empty() ? nullptr : explicit_name.c_str(), options.destination.c_str(), nullptr);
  if (!format) throw std::runtime_error("Cannot infer native recording container; use mp4, webm, mkv or mov");
  const std::string name = format->name;
  if (name != "mp4" && name != "webm" && name != "matroska" && name != "mov")
    throw std::runtime_error("Unsupported native recording container: " + name);
  return name;
}
void ValidateCodecs(const MediaOptions& options) {
  const auto& video = Choice(options.video_codec, "video");
  if (!std::isfinite(options.crf) || (options.crf != -1 && (options.crf < 0 || options.crf >
      (options.video_codec == "libx264" || options.video_codec == "libx265" ? 51 : 63))))
    throw std::runtime_error("Invalid native encoder CRF");
  if (options.crf != -1 && (video.lossless || video.hardware || options.streaming))
    throw std::runtime_error("CRF is for software lossy recording; omit it for lossless, hardware or RTMP output");
  if (options.audio_bitrate_kbps < 16 || options.audio_bitrate_kbps > 512)
    throw std::runtime_error("Invalid native audio bitrate");
  if (options.streaming && (std::string(video.codec) != "h264" || options.audio && options.audio_codec != "aac"))
    throw std::runtime_error("Native RTMP requires H.264 video and AAC audio");
  const auto container = Container(options);
  const auto* format = av_guess_format(container.c_str(), nullptr, nullptr);
  auto validate = [&](const std::string& name, const char* kind) {
    Choice(name, kind);
    const auto* codec = avcodec_find_encoder_by_name(name.c_str());
    if (!codec) throw std::runtime_error("Requested native encoder is unavailable: " + name);
    if (avformat_query_codec(format, codec->id, FF_COMPLIANCE_NORMAL) <= 0)
      throw std::runtime_error("Encoder " + name + " is incompatible with " + container + " container");
  };
  validate(options.video_codec, "video");
  if (options.audio) validate(options.audio_codec, "audio");
}
void Check(int result, const char* operation) {
  if (result >= 0) return;
  char detail[AV_ERROR_MAX_STRING_SIZE]; av_strerror(result, detail, sizeof(detail));
  throw std::runtime_error(std::string(operation) + ": " + detail);
}
}

std::vector<EncoderInfo> ListMediaEncoders() {
  std::vector<EncoderInfo> result;
  for (const auto& item : choices) {
    EncoderInfo info{item.name, item.kind, item.codec, EncoderAvailable(item.name), item.hardware, item.lossless, {}};
    const auto* encoder = avcodec_find_encoder_by_name(item.name);
    for (const auto& name : {"mp4", "webm", "matroska", "mov", "flv"}) {
      const auto* format = av_guess_format(name, nullptr, nullptr);
      // Conventional RTMP only; HEVC/AV1 enhanced RTMP is not implemented.
      if (std::string(name) == "flv" && std::string(item.codec) != "h264" && std::string(item.codec) != "aac") continue;
      if (encoder && format && avformat_query_codec(format, encoder->id, FF_COMPLIANCE_NORMAL) > 0)
        info.containers.push_back(std::string(name) == "matroska" ? "mkv" : name);
    }
    result.push_back(std::move(info));
  }
  return result;
}
namespace {
template<class T> T* Require(T* value, const char* operation) {
  if (!value) throw std::runtime_error(operation);
  return value;
}
}

struct MediaOutput::Impl {
  struct VideoPacket { Pixels pixels; int width, height; double at; };
  struct AudioPacket { Samples samples; double at; };
  MediaOptions options;
  std::function<double()> clock;
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::deque<VideoPacket> video_queue;
  std::deque<AudioPacket> audio_queue;
  VideoPacket latest{};
  MediaStatus status;
  std::atomic<bool> stopping{false};
  std::atomic<int64_t> io_deadline{0};
  bool paused = false;
  double pause_at = 0, excluded = 0, origin = 0, stop_at = 0, previous_video_at = -1;
  std::thread worker;
  std::mutex stop_mutex;
  AVFormatContext* format = nullptr;
  AVCodecContext *video_codec = nullptr, *audio_codec = nullptr;
  AVStream *video_stream = nullptr, *audio_stream = nullptr;
  AVFrame *video_frame = nullptr, *audio_frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* scale = nullptr;
  SwrContext* resample = nullptr;
  int64_t video_index = 0, audio_index = 0;
  double audio_encoded_until = -1;
  bool header_written = false;
  SuiteCutAudioTrace audio_trace;
  static int64_t Tick() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  static int Interrupt(void* opaque) {
    auto& self = *static_cast<Impl*>(opaque);
    return self.io_deadline.load() && Tick() > self.io_deadline.load();
  }
  Impl(MediaOptions config, std::function<double()> now) : options(std::move(config)), clock(std::move(now)) {
    if (options.width < 2 || options.height < 2 || options.width > 3840 || options.height > 2160 ||
        options.width % 2 || options.height % 2 || options.bitrate_kbps < 100 || options.bitrate_kbps > 50000 || (options.fps != 30 && options.fps != 60))
      throw std::runtime_error("Invalid native output dimensions or frame rate");
    if (options.streaming && !options.destination.starts_with("rtmp://") && !options.destination.starts_with("rtmps://"))
      throw std::runtime_error("Native streaming requires RTMP(S)");
    if (!options.streaming && (options.destination.empty() || options.destination.front() != '/'))
      throw std::runtime_error("Native recording requires an absolute output path");
    ValidateCodecs(options);
    if (options.audio_startup != "immediate" && options.audio_startup != "wait-for-packet" && options.audio_startup != "wait-for-signal")
      throw std::runtime_error("Invalid native audio startup mode");
    if (options.audio_startup_timeout_ms < 1 || options.audio_startup_timeout_ms > 60000)
      throw std::runtime_error("Invalid native audio startup timeout");
    if (options.audio_startup != "immediate" && (!options.audio || options.offline))
      throw std::runtime_error("Native audio startup waiting requires realtime audio capture");
    audio_trace.Open(std::filesystem::path(options.destination).filename().string(),
                    "at_ms,encoded_ms,sample_index,frames,missing_frames,queued_chunks,oldest_ms,newest_ms", true);
    worker = std::thread([this] { Run(); });
  }
  ~Impl() { Stop(); Cleanup(); }
  void Cleanup() {
    sws_freeContext(scale); scale = nullptr;
    swr_free(&resample);
    av_frame_free(&video_frame); av_frame_free(&audio_frame); av_packet_free(&packet);
    avcodec_free_context(&video_codec); avcodec_free_context(&audio_codec);
    if (format) { if (format->pb) avio_closep(&format->pb); avformat_free_context(format); format = nullptr; }
  }
  AVCodecContext* Codec(const std::string& name, bool audio) {
    const auto* codec = Require(avcodec_find_encoder_by_name(name.c_str()), "Requested native encoder is unavailable");
    auto* context = Require(avcodec_alloc_context3(codec), "Allocate native encoder");
    // Store before any fallible configuration so failure cleanup always owns it.
    (audio ? audio_codec : video_codec) = context;
    context->thread_count = 2;
    if (format->oformat->flags & AVFMT_GLOBALHEADER) context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (audio) {
      context->sample_rate = 48000; context->time_base = {1, 48000}; context->bit_rate = options.audio_bitrate_kbps * 1000LL;
      av_channel_layout_default(&context->ch_layout, 2);
      const void* supported=nullptr;int formats=0;
      Check(avcodec_get_supported_config(context,codec,AV_CODEC_CONFIG_SAMPLE_FORMAT,0,&supported,&formats),"Get encoder sample formats");
      context->sample_fmt=formats ? static_cast<const AVSampleFormat*>(supported)[0] : AV_SAMPLE_FMT_FLTP;
    } else {
      context->width = options.width; context->height = options.height;
      context->pix_fmt = AV_PIX_FMT_YUV420P; context->time_base = {1, options.fps};
      context->framerate = {options.fps, 1}; context->gop_size = options.fps * 2; context->max_b_frames = 0;
      context->color_range = AVCOL_RANGE_MPEG; context->colorspace = AVCOL_SPC_BT709;
      context->color_primaries = AVCOL_PRI_BT709; context->color_trc = AVCOL_TRC_IEC61966_2_1;
      if (options.streaming) { context->bit_rate = options.bitrate_kbps * 1000LL; context->rc_max_rate = context->bit_rate; context->rc_buffer_size = context->bit_rate * 2; }
      if (name == "libx264") {
        Check(av_opt_set(context->priv_data, "preset", "veryfast", 0), "Set x264 preset");
        Check(av_opt_set(context->priv_data, "tune", "zerolatency", 0), "Set x264 latency");
        if (!options.streaming) Check(av_opt_set_double(context->priv_data, "crf", options.crf < 0 ? 18 : options.crf, 0), "Set recording quality");
      } else if (name == "libx265") {
        Check(av_opt_set(context->priv_data, "preset", "ultrafast", 0), "Set HEVC preset");
        Check(av_opt_set(context->priv_data, "tune", "zerolatency", 0), "Set HEVC latency");
        Check(av_opt_set(context->priv_data, "x265-params", "pools=none:frame-threads=2:log-level=error", 0), "Bound HEVC threads");
        Check(av_opt_set_double(context->priv_data, "crf", options.crf < 0 ? 23 : options.crf, 0), "Set HEVC quality");
      } else if (name == "libvpx-vp9" || name == "libvpx") {
        context->bit_rate = name == "libvpx" ? options.bitrate_kbps * 1000LL : 0;
        Check(av_opt_set(context->priv_data, "deadline", "realtime", 0), "Set VP9 deadline");
        Check(av_opt_set(context->priv_data, "cpu-used", name == "libvpx" ? "8" : "5", 0), "Set VPx speed");
        if (name == "libvpx-vp9") Check(av_opt_set(context->priv_data, "row-mt", "1", 0), "Set VP9 threads");
        Check(av_opt_set_double(context->priv_data, "crf", options.crf < 0 ? 18 : options.crf, 0), "Set VPx quality");
      } else if (name == "libsvtav1") {
        Check(av_opt_set(context->priv_data, "preset", "12", 0), "Set AV1 speed");
        Check(av_opt_set_double(context->priv_data, "crf", options.crf < 0 ? 32 : options.crf, 0), "Set AV1 quality");
        Check(av_opt_set(context->priv_data, "svtav1-params", "lp=2", 0), "Bound AV1 parallelism");
      } else if (name == "libaom-av1") {
        context->bit_rate = 0;
        Check(av_opt_set(context->priv_data, "usage", "realtime", 0), "Set AOM realtime mode");
        Check(av_opt_set(context->priv_data, "cpu-used", "8", 0), "Set AOM speed");
        Check(av_opt_set(context->priv_data, "lag-in-frames", "0", 0), "Set AOM latency");
        Check(av_opt_set(context->priv_data, "row-mt", "1", 0), "Set AOM threads");
        Check(av_opt_set_double(context->priv_data, "crf", options.crf < 0 ? 32 : options.crf, 0), "Set AOM quality");
      } else if (name == "ffv1") {
        context->bit_rate = 0;
        context->level = 3;
        Check(av_opt_set(context->priv_data, "slicecrc", "1", 0), "Set FFV1 slice checksums");
      } else if (Choice(name, "video").hardware) {
        context->bit_rate = options.bitrate_kbps * 1000LL;
        Check(av_opt_set(context->priv_data, "preset", "p4", 0), "Set NVENC preset");
        Check(av_opt_set(context->priv_data, "tune", "ull", 0), "Set NVENC latency");
      }
    }
    Check(avcodec_open2(context, codec, nullptr), ("Open native " + name + " encoder").c_str());
    return context;
  }
  void Open() {
    static std::once_flag network;std::call_once(network,[] {avformat_network_init();});
    const auto container = Container(options);
    Check(avformat_alloc_output_context2(&format, nullptr, container.c_str(), options.destination.c_str()), "Create native muxer");
    Require(format, "Allocate native muxer"); format->interrupt_callback = {Interrupt, this};
    // Flush completed fragments even for nearly static video/silent audio, whose
    // compressed data may otherwise stay in AVIO's buffer until normal shutdown.
    format->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    format->flush_packets = 1;
    video_codec = Codec(options.video_codec, false);
    video_stream = Require(avformat_new_stream(format, nullptr), "Create video stream"); video_stream->time_base = video_codec->time_base;
    video_stream->avg_frame_rate = {options.fps, 1};
    Check(avcodec_parameters_from_context(video_stream->codecpar, video_codec), "Copy video parameters");
    if (video_codec->codec_id == AV_CODEC_ID_HEVC && (container == "mp4" || container == "mov"))
      video_stream->codecpar->codec_tag = MKTAG('h','v','c','1');
    video_frame = Require(av_frame_alloc(), "Allocate video frame");
    video_frame->format = video_codec->pix_fmt; video_frame->width = options.width; video_frame->height = options.height;
    video_frame->colorspace = video_codec->colorspace; video_frame->color_range = video_codec->color_range;
    Check(av_frame_get_buffer(video_frame, 32), "Allocate video planes");
    if (options.audio) {
      audio_codec = Codec(options.audio_codec, true);
      audio_stream = Require(avformat_new_stream(format, nullptr), "Create audio stream"); audio_stream->time_base = audio_codec->time_base;
      Check(avcodec_parameters_from_context(audio_stream->codecpar, audio_codec), "Copy audio parameters");
      audio_frame = Require(av_frame_alloc(), "Allocate audio frame"); audio_frame->format = audio_codec->sample_fmt;
      audio_frame->sample_rate = 48000; audio_frame->nb_samples = audio_codec->frame_size ? audio_codec->frame_size : 960;
      Check(av_channel_layout_copy(&audio_frame->ch_layout, &audio_codec->ch_layout), "Set audio channels");
      Check(av_frame_get_buffer(audio_frame, 0), "Allocate audio planes");
      AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
      Check(swr_alloc_set_opts2(&resample, &stereo, audio_codec->sample_fmt, 48000, &stereo, AV_SAMPLE_FMT_S16, 48000, 0, nullptr), "Allocate PCM converter");
      Check(swr_init(resample), "Initialize PCM converter");
    }
    packet = Require(av_packet_alloc(), "Allocate encoded packet");
    io_deadline = Tick() + 10000;
    if (!(format->oformat->flags & AVFMT_NOFILE)) {
      AVDictionary* config = nullptr; av_dict_set(&config, "rw_timeout", "5000000", 0);
      const int result = avio_open2(&format->pb, options.destination.c_str(), AVIO_FLAG_WRITE, &format->interrupt_callback, &config);
      av_dict_free(&config); Check(result, "Open native output");
    }
    AVDictionary* config = nullptr;
    if (!options.streaming && std::string(format->oformat->name) == "mp4") av_dict_set(&config, "movflags", "+frag_keyframe+empty_moov+default_base_moof", 0);
    const int result = avformat_write_header(format, &config); av_dict_free(&config); Check(result, "Write native media header");
    if(format->pb) {avio_flush(format->pb); Check(format->pb->error, "Flush native media header");}
    header_written = true; io_deadline = 0;
    std::lock_guard lock(mutex); cv.notify_all();
  }
  void Encode(AVCodecContext* codec, AVFrame* frame, AVStream* stream) {
    Check(avcodec_send_frame(codec, frame), "Send native media frame");
    for (;;) {
      const int result = avcodec_receive_packet(codec, packet);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
      Check(result, "Receive native media packet");
      if(stream==video_stream && packet->duration<=0) packet->duration=1;
      av_packet_rescale_ts(packet, codec->time_base, stream->time_base); packet->stream_index = stream->index;
      io_deadline = Tick() + 5000;
      const int written = av_interleaved_write_frame(format, packet); av_packet_unref(packet); io_deadline = 0;
      Check(written, "Mux native media packet");
    }
  }
  void EncodeVideo(double at) {
    VideoPacket selected;
    {
      std::lock_guard lock(mutex);
      while (video_queue.size() > 1 && video_queue[1].at <= at) {
        status.queued_video_bytes -= video_queue.front().pixels->size(); video_queue.pop_front();
      }
      cv.notify_all();
      selected = video_queue.empty() ? latest : video_queue.front();
      if (selected.at == previous_video_at) ++status.repeated_frames;
    }
    if (!selected.pixels) return;
    Check(av_frame_make_writable(video_frame), "Reuse native video planes");
    const size_t y = static_cast<size_t>(selected.width) * selected.height;
    const uint8_t* planes[] = {selected.pixels->data(), selected.pixels->data()+y, selected.pixels->data()+y*5/4};
    const int strides[] = {selected.width, selected.width/2, selected.width/2};
    scale = Require(sws_getCachedContext(scale, selected.width, selected.height, AV_PIX_FMT_YUV420P, options.width, options.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), "Create native scaler");
    sws_scale(scale, planes, strides, 0, selected.height, video_frame->data, video_frame->linesize);
    video_frame->pts = video_index++; Encode(video_codec, video_frame, video_stream); previous_video_at = selected.at;
    std::lock_guard lock(mutex); status.video_frames = video_index;
  }
  void EncodeAudio(double at, int remaining = 0) {
    const int capacity = audio_codec->frame_size ? audio_codec->frame_size : 960;
    // Lossless encoders accept a partial final block. Preserve the exact PCM
    // sample count rather than extending a stopped output with silent padding.
    const bool lossless = audio_codec->codec_id == AV_CODEC_ID_FLAC || audio_codec->codec_id == AV_CODEC_ID_PCM_S16LE;
    const int count = lossless && remaining > 0 ? std::min(capacity, remaining) : capacity;
    audio_frame->nb_samples = count;
    std::vector<int16_t> pcm(static_cast<size_t>(count) * 2, 0);
    std::vector<bool> covered(count, false);
    {
      std::lock_guard lock(mutex);
      for (const auto& chunk : audio_queue) {
        const auto offset = static_cast<int64_t>(std::llround((chunk.at - at) * 48));
        const int64_t samples = chunk.samples->size()/2;
        const auto start = std::max<int64_t>(0, offset), end = std::min<int64_t>(count, offset + samples);
        if (end > start) {
          std::copy_n(chunk.samples->data()+(start-offset)*2, (end-start)*2, pcm.data()+start*2);
          std::fill(covered.begin()+start, covered.begin()+end, true);
        }
      }
      status.audio_missing += std::count(covered.begin(),covered.end(),false);
      audio_encoded_until = at + count/48.0;
      audio_trace.Row("%.6f,%.6f,%lld,%d,%d,%zu,%.6f,%.6f\n", at, clock(),
                      static_cast<long long>(audio_index), count, static_cast<int>(std::count(covered.begin(),covered.end(),false)),
                      audio_queue.size(), audio_queue.empty() ? -1 : audio_queue.front().at,
                      audio_queue.empty() ? -1 : audio_queue.back().at);
      const double end = at + count/48.0;
      while (!audio_queue.empty() && audio_queue.front().at + audio_queue.front().samples->size()/96.0 <= end) {
        status.queued_audio_bytes -= audio_queue.front().samples->size()*2; audio_queue.pop_front();
      }
    }
    audio_trace.PCM(pcm.data(), pcm.size()*sizeof(int16_t));
    Check(av_frame_make_writable(audio_frame), "Reuse native audio planes");
    const uint8_t* input[] = {reinterpret_cast<const uint8_t*>(pcm.data())};
    const int samples = swr_convert(resample, audio_frame->data, count, input, count); Check(samples, "Convert native PCM");
    if (samples != count) throw std::runtime_error("Unexpected native PCM conversion delay");
    audio_frame->pts = audio_index; audio_index += count; Encode(audio_codec, audio_frame, audio_stream);
    std::lock_guard lock(mutex); status.audio_frames = audio_index;
  }
  void Run() {
    try {
      Open();
      {
        std::unique_lock lock(mutex);
        std::optional<double> audio_origin;
        const auto can_start = [&] {
          if (stopping) return true;
          if (!latest.pixels) return false;
          if (options.audio_startup == "immediate") return true;
          if (video_queue.empty()) return false;
          // Opening a slow output can outlive the bounded video queue. Do not
          // start audio before the earliest video still available to encode.
          const double video_begin = video_queue.front().at;
          for (const auto& chunk : audio_queue) {
            const auto first = static_cast<size_t>(std::clamp<double>(std::ceil((video_begin-chunk.at)*48),0,chunk.samples->size()/2));
            if (first == chunk.samples->size()/2) continue;
            if (options.audio_startup == "wait-for-packet") { audio_origin = chunk.at+first/48.0; return true; }
            for (size_t frame=first;frame<chunk.samples->size()/2;++frame)
              if (std::abs(static_cast<int>((*chunk.samples)[frame*2])) > 32 ||
                  std::abs(static_cast<int>((*chunk.samples)[frame*2+1])) > 32) {
                audio_origin = chunk.at + frame/48.0; return true;
              }
          }
          return false;
        };
        if (options.audio_startup == "immediate") cv.wait(lock,can_start);
        else if (!cv.wait_for(lock,std::chrono::milliseconds(options.audio_startup_timeout_ms),can_start))
          throw std::runtime_error("Native audio readiness timed out ("+options.audio_startup+")");
        if (stopping && options.audio_startup != "immediate")
          throw std::runtime_error("Native output stopped before audio readiness");
        if (!latest.pixels) throw std::runtime_error("Native output stopped before its first captured frame");
        origin = options.offline ? video_queue.front().at : audio_origin.value_or(clock());
        status.started_ms = origin;status.ready=true;cv.notify_all();
      }
      // Keep a short native jitter buffer, independent of the JS event loop.
      constexpr double delay_ms = 150;
      for (;;) {
        double video_at, audio_at, target, paused_total, end_at;
        int final_audio_samples = 0;
        {
          std::unique_lock lock(mutex);
          if (paused && !stopping) { cv.wait(lock, [this] { return !paused || stopping; }); continue; }
          paused_total = excluded;
          video_at = video_index * 1000.0/options.fps;
          audio_at = options.audio ? audio_index/48.0 : 1e30;
          target = origin + std::min(video_at, audio_at) + paused_total;
          end_at = stop_at;
          if (stopping && target >= end_at) break;
          if (stopping && options.audio)
            final_audio_samples = static_cast<int>(std::min<double>(audio_codec->frame_size ? audio_codec->frame_size : 960,
                std::max(0.0,std::ceil((end_at-origin-paused_total)*48-audio_index))));
          if(options.offline && !stopping && latest.at < target) {cv.wait(lock);continue;}
          const double wait = options.offline ? 0 : target + delay_ms - clock();
          if (!stopping && wait > 0) { cv.wait_for(lock, std::chrono::duration<double,std::milli>(wait)); continue; }
        }
        const double began = clock();
        if (audio_at <= video_at) EncodeAudio(origin + audio_at + paused_total, final_audio_samples);
        else EncodeVideo(origin + video_at + paused_total);
        const double elapsed = clock() - began;
        std::lock_guard lock(mutex); status.maximum_encode_ms = std::max(status.maximum_encode_ms, elapsed);
        status.duration_ms = video_index * 1000.0/options.fps;
      }
      Encode(video_codec, nullptr, video_stream);
      if (options.audio) Encode(audio_codec, nullptr, audio_stream);
      io_deadline = Tick() + 5000; Check(av_write_trailer(format), "Finalize native media"); io_deadline = 0;
    } catch (const std::exception& error) {
      std::lock_guard lock(mutex); status.error = error.what();
    }
    Cleanup();
    std::lock_guard lock(mutex); video_queue.clear(); audio_queue.clear(); latest = {};
    status.queued_video_bytes = status.queued_audio_bytes = 0; status.stopped = true; cv.notify_all();
  }
  MediaStatus Stop() {
    std::lock_guard stopping_lock(stop_mutex);
    {
      std::lock_guard lock(mutex);
      if (!stopping) { stop_at = paused ? pause_at : clock(); stopping = true; }
      cv.notify_all();
    }
    if (worker.joinable()) worker.join();
    std::lock_guard lock(mutex); return status;
  }
};
MediaOutput::MediaOutput(MediaOptions config, std::function<double()> clock) : impl_(std::make_unique<Impl>(std::move(config),std::move(clock))) {}
MediaOutput::~MediaOutput() = default;
void MediaOutput::Video(Pixels pixels, int width, int height, double at) {
  if (!pixels || width < 2 || height < 2 || width > 3840 || height > 2160 || width%2 || height%2 || pixels->size()!=static_cast<size_t>(width)*height*3/2) return;
  std::unique_lock lock(impl_->mutex);
  if(impl_->options.offline) impl_->cv.wait(lock,[this,&pixels] {return impl_->stopping || impl_->status.stopped || impl_->video_queue.size()<20 && (impl_->video_queue.empty() || impl_->status.queued_video_bytes+pixels->size()<=64*1024*1024);});
  if (impl_->stopping || impl_->status.stopped || impl_->paused) return;
  impl_->latest = {std::move(pixels),width,height,at}; impl_->video_queue.push_back(impl_->latest);
  impl_->status.queued_video_bytes += impl_->latest.pixels->size();
  while (impl_->video_queue.size()>1 && (impl_->video_queue.size()>20 || impl_->status.queued_video_bytes>64*1024*1024)) {
    impl_->status.queued_video_bytes -= impl_->video_queue.front().pixels->size(); impl_->video_queue.pop_front();
  }
  impl_->cv.notify_all();
}
void MediaOutput::Audio(Samples samples, double at) {
  if (!samples || !impl_->options.audio || samples->empty() || samples->size()%2) return;
  std::lock_guard lock(impl_->mutex);
  if (impl_->stopping || impl_->status.stopped || impl_->paused) return;
  if (impl_->audio_encoded_until >= 0) {
    const auto late = static_cast<int64_t>(std::llround((impl_->audio_encoded_until-at)*48));
    impl_->status.audio_late += std::clamp<int64_t>(late,0,samples->size()/2);
  }
  impl_->status.queued_audio_bytes += samples->size()*2; impl_->audio_queue.push_back({std::move(samples),at});
  while (impl_->audio_queue.size()>500 || impl_->status.queued_audio_bytes>1920000) {
    impl_->status.queued_audio_bytes -= impl_->audio_queue.front().samples->size()*2;
    impl_->status.audio_dropped += impl_->audio_queue.front().samples->size()/2;
    impl_->audio_queue.pop_front();
  }
  impl_->cv.notify_all();
}
void MediaOutput::Pause(bool paused) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->paused==paused || impl_->stopping) return;
  if (paused) impl_->pause_at=impl_->clock(); else impl_->excluded += impl_->clock()-impl_->pause_at;
  impl_->paused=paused;impl_->status.paused=paused;impl_->audio_queue.clear();impl_->video_queue.clear();
  impl_->status.queued_audio_bytes=impl_->status.queued_video_bytes=0;impl_->cv.notify_all();
}
void MediaOutput::Fail(const std::string& error) {
  std::lock_guard lock(impl_->mutex);if(impl_->stopping || impl_->status.stopped) return;
  impl_->status.error=error;impl_->stop_at=impl_->paused ? impl_->pause_at : impl_->clock();impl_->stopping=true;impl_->cv.notify_all();
}
MediaStatus MediaOutput::Status() const { std::lock_guard lock(impl_->mutex); return impl_->status; }
MediaStatus MediaOutput::Stop() { return impl_->Stop(); }
}
