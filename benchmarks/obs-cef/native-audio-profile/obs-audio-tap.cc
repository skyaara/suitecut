// Benchmark-only passive input/mixer taps. ABI declarations are verified against
// OBS 32.2.0 libobs/obs.h and media-io/audio-io.h, saved with the evidence.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <time.h>
#include <vector>
struct source_audio {
  const uint8_t* data[8];
  uint32_t frames;
  int speakers, format;
  uint32_t rate;
  uint64_t timestamp;
};
struct audio_data { uint8_t* data[8]; uint32_t frames; uint64_t timestamp; };
struct conversion { uint32_t rate; int format, speakers; bool clipping; };
static_assert(sizeof(source_audio)==88 && offsetof(source_audio,timestamp)==80);
static_assert(sizeof(audio_data)==80 && offsetof(audio_data,timestamp)==72);
static_assert(sizeof(conversion)==16);
static double Now() {
  timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t);
  return t.tv_sec*1000.0+t.tv_nsec/1e6;
}
struct Trace {
  FILE *csv=nullptr,*pcm=nullptr;
  size_t packets=0;
  std::mutex mutex;
  void Write(const char* name,const float* left,const float* right,uint32_t frames,
             uint64_t pts,double received) {
    const char* root=std::getenv("OBS_AUDIO_TRACE_DIRECTORY");
    if(!root || frames>4096 || !left || !right)return;
    std::lock_guard lock(mutex);
    if(!csv) {
      const std::string path=std::string(root)+"/"+name;
      csv=std::fopen((path+".csv").c_str(),"w");
      pcm=std::fopen((path+".s16").c_str(),"wb");
      if(csv)std::fprintf(csv,"received_ms,pts_ms,mapped_ms,frames,rms,steady_ticks_ms\n");
    }
    if(!csv || !pcm)return;
    std::vector<int16_t> samples(frames*2);
    double energy=0;
    for(uint32_t i=0;i<frames;i++) {
      for(int ch=0;ch<2;ch++) {
        float value=ch?right[i]:left[i];
        if(!std::isfinite(value))value=0;
        const auto s=static_cast<int16_t>(std::clamp(value,-1.0f,1.0f)*32767);
        samples[i*2+ch]=s; energy+=double(s)*s;
      }
    }
    const double mapped=pts/1e6;
    std::fprintf(csv,"%.6f,%.6f,%.6f,%u,%.9f,%.6f\n",received,mapped,mapped,frames,
                 std::sqrt(energy/samples.size())/32768,received);
    std::fwrite(samples.data(),sizeof(int16_t),samples.size(),pcm);
    if(++packets%50==0){std::fflush(csv);std::fflush(pcm);}
  }
  ~Trace(){if(csv)std::fclose(csv);if(pcm)std::fclose(pcm);}
};
static Trace input_trace,mix_trace;
extern "C" void obs_source_output_audio(void* source,const source_audio* data) {
  using Function=void(*)(void*,const source_audio*);
  static auto next=reinterpret_cast<Function>(dlsym(RTLD_NEXT,"obs_source_output_audio"));
  static auto id=reinterpret_cast<const char*(*)(const void*)>(dlsym(RTLD_NEXT,"obs_source_get_id"));
  const double received=Now();
  if(data && id && std::strcmp(id(source),"browser_source")==0 &&
     data->rate==48000 && data->speakers==2 && data->format==8)
    input_trace.Write("obs-cef-input",reinterpret_cast<const float*>(data->data[0]),
                      reinterpret_cast<const float*>(data->data[1]),data->frames,data->timestamp,received);
  if(next)next(source,data);
}
using Callback=void(*)(void*,size_t,audio_data*);
static void Mixed(void*,size_t,audio_data* data) {
  mix_trace.Write("obs-mixed-output",reinterpret_cast<const float*>(data->data[0]),
                  reinterpret_cast<const float*>(data->data[1]),data->frames,data->timestamp,Now());
}
using Connect=bool(*)(void*,size_t,const conversion*,Callback,void*);
static std::atomic<bool> observer_attached{false};
static void AttachMixed(void* audio) {
  static auto next=reinterpret_cast<Connect>(dlsym(RTLD_NEXT,"audio_output_connect"));
  if(!audio || !next || !std::getenv("OBS_AUDIO_TRACE_DIRECTORY") || observer_attached.exchange(true))return;
  conversion trace_format{48000,8,2,false};
  const bool connected=next(audio,0,&trace_format,Mixed,nullptr);
  std::fprintf(stderr,"[SuiteCut benchmark tap] mixer observer attached: %d\n",connected);
}
extern "C" bool obs_output_start(void* output) {
  static auto next=reinterpret_cast<bool(*)(void*)>(dlsym(RTLD_NEXT,"obs_output_start"));
  static auto get_audio=reinterpret_cast<void*(*)()>(dlsym(RTLD_NEXT,"obs_get_audio"));
  const bool result=next && next(output);
  if(result && get_audio)AttachMixed(get_audio());
  return result;
}
extern "C" bool audio_output_connect(void* audio,size_t mix,const conversion* conv,
                                      Callback callback,void* param) {
  using Function=bool(*)(void*,size_t,const conversion*,Callback,void*);
  static auto next=reinterpret_cast<Function>(dlsym(RTLD_NEXT,"audio_output_connect"));
  if(!next)return false;
  const bool result=next(audio,mix,conv,callback,param);
  if(result && mix==0)AttachMixed(audio);
  return result;
}
