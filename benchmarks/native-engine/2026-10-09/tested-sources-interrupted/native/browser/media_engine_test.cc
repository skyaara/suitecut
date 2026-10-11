// SPDX-License-Identifier: MIT
#include "media_engine.h"
#include "media_file.h"
#include "media_render.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
int main() {
  using namespace std::chrono;
  const auto start = steady_clock::now();
  auto now = [&] { return duration<double,std::milli>(steady_clock::now()-start).count(); };
  const auto path = "/tmp/suitecut-media-"+std::to_string(getpid())+".mp4";
  try {
    suitecut::MediaOptions config; config.destination=path;config.width=320;config.height=180;
    suitecut::MediaOutput output(config,now);
    for (int i=0;i<120;++i) {
      auto pixels=std::make_shared<std::vector<uint8_t>>(320*180*3/2,128);
      std::fill_n(pixels->begin(),320*180,static_cast<uint8_t>(16+i%200));
      output.Video(pixels,320,180,now());
      auto audio=std::make_shared<std::vector<int16_t>>(1600);
      for (size_t j=0;j<audio->size()/2;++j) (*audio)[2*j]=(*audio)[2*j+1]=static_cast<int16_t>(12000*std::sin((i*800+j)*2*3.141592653589793*440/48000));
      output.Audio(audio,now());
      std::this_thread::sleep_for(milliseconds(17));
    }
    auto status=output.Stop();
    if (!status.error.empty() || status.video_frames<55 || status.audio_frames<90000 || status.queued_audio_bytes || status.queued_video_bytes || std::filesystem::file_size(path)<10000)
      throw std::runtime_error("Native media failed: "+status.error);
    // Repeated stop must return the same counters and release queued ownership.
    if (output.Stop().video_frames!=status.video_frames) throw std::runtime_error("Stop was not idempotent");
    std::cout<<"Native output: "<<status.video_frames<<" video frames, "<<status.audio_frames<<" audio samples\n";
    const auto probe=suitecut::ProbeMedia(path);
    if(probe.width!=320 || probe.height!=180 || !probe.audio || probe.duration_ms<1900) throw std::runtime_error("Native probe is incorrect");
    config.destination=path+".webm";config.video_codec="libvpx-vp9";
    const auto clip=suitecut::CutVideo(path,config,500,1000);
    if(clip.video_frames!=30 || clip.queued_video_bytes || suitecut::ProbeMedia(config.destination).audio) throw std::runtime_error("Native checkpoint is incorrect");
    std::filesystem::remove(config.destination);
    // The end of a checkpoint pads with the final decoded frame.
    const auto tail=suitecut::CutVideo(path,config,1800,1000);
    if(tail.video_frames!=30) throw std::runtime_error("Native checkpoint tail is incorrect");
    std::filesystem::remove(config.destination);
    suitecut::RenderOptions render;
    render.inputs={path};render.destination=path+".render.mp4";render.duration_ms=1000;render.audio=true;render.video_options={{"preset","veryfast"},{"crf","22"}};
    render.graph="[0:v]trim=start=0.2:duration=1,setpts=PTS-STARTPTS,fps=30,scale=320:180,format=yuv420p[video0];[0:a]atrim=start=0.2:duration=1,asetpts=PTS-STARTPTS,aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo[audioout]";
    const auto rendered=suitecut::RenderMedia(render,[] {return false;});
    if(rendered.video_frames!=30 || rendered.audio_samples<47000 || !suitecut::ProbeMedia(render.destination).audio) throw std::runtime_error("Native render is incorrect");
    std::filesystem::remove(render.destination);
    // Multiple timeline segments using the same input require a native split.
    render.audio=false;render.graph="[0:v]trim=start=0.2:duration=0.5,setpts=PTS-STARTPTS[a];[0:v]trim=start=1:duration=0.5,setpts=PTS-STARTPTS[b];[a][b]concat=n=2:v=1:a=0,fps=30,format=yuv420p[video0]";
    const auto joined=suitecut::RenderMedia(render,[] {return false;});
    if(joined.video_frames!=30) throw std::runtime_error("Native render concat is incorrect");
    std::filesystem::remove(render.destination);
    int cancellation_checks=0;
    try {suitecut::RenderMedia(render,[&] {return ++cancellation_checks>=5;});throw std::runtime_error("Native render ignored cancellation");}
    catch(const std::runtime_error& error) {if(std::string(error.what())!="Native render cancelled") throw;}
    std::filesystem::remove(render.destination);
    std::filesystem::remove(path);
    config.destination="/nonexistent-suitecut-directory/failure.mp4";
    suitecut::MediaOutput failed(config,now);auto error=failed.Stop();
    if(error.error.empty() || !error.stopped) throw std::runtime_error("Failure lifecycle was not reported");
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';std::filesystem::remove(path);return 1;}
}
