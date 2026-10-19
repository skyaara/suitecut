// SPDX-License-Identifier: MIT
#include "suitecut/browser.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using suitecut::Json;
const char* usage =
  "Usage: suitecut-native-record --url URL --output FILE [--duration-ms 5000]\n"
  "  [--fps 30] [--width 1280] [--height 720] [--audio] [--headed]\n"
  "  [--backend cef|chromium] [--browser /absolute/browser]\n"
  "  [--ffmpeg /absolute/ffmpeg] [--ffprobe /absolute/ffprobe]\n"
  "  [--browser-arg ARG]...\n"
  "CEF records WebM, including optional page audio; Chromium records silent MP4/WebM.\n"
  "Bundled engine/media tools are found relative to this executable. No Node.js.\n";
double number(const std::string& input,double low,double high,bool whole) {
  const auto value=Json::parse(input,{128,1,1,128});
  if(!value.is_number()||!std::isfinite(value.as_double())||value.as_double()<low||
     value.as_double()>high||(whole&&std::floor(value.as_double())!=value.as_double()))
    throw std::invalid_argument("Numeric option is outside its supported range");
  return value.as_double();
}
std::filesystem::path executable_directory(const char* argument) {
  std::error_code error;
#ifdef __linux__
  const auto executable=std::filesystem::canonical("/proc/self/exe",error);
  if(!error)return executable.parent_path();
#endif
  return std::filesystem::canonical(argument).parent_path();
}
std::string tool(const std::filesystem::path& bin,const char* name) {
  const auto bundled=bin/name;
  return std::filesystem::is_regular_file(bundled)?bundled.string():std::string("/usr/bin/")+name;
}
}

int main(int argc,char** argv) {
  try {
    const auto bin=executable_directory(argv[0]);
    suitecut::LaunchOptions options;
    options.executable=(bin/"../libexec/suitecut/cef/suitecut-browser").lexically_normal().string();
    std::string url,output,ffmpeg=tool(bin,"ffmpeg"),ffprobe=tool(bin,"ffprobe");
    double fps=30;
    int duration=5000;
    bool audio=false,browser_explicit=false;
    for(int i=1;i<argc;++i) {
      const std::string key=argv[i];
      if(key=="--help"||key=="-h"){std::cout<<usage;return 0;}
      if(key=="--audio"){audio=true;continue;}
      if(key=="--headed"){options.headless=false;continue;}
      if(i+1==argc)throw std::invalid_argument("Missing value for "+key);
      const std::string value=argv[++i];
      if(key=="--url")url=value;
      else if(key=="--output")output=value;
      else if(key=="--browser"){options.executable=value;browser_explicit=true;}
      else if(key=="--ffmpeg")ffmpeg=value;
      else if(key=="--ffprobe")ffprobe=value;
      else if(key=="--fps")fps=number(value,1,120,false);
      else if(key=="--width")options.width=static_cast<int>(number(value,2,3840,true));
      else if(key=="--height")options.height=static_cast<int>(number(value,2,2160,true));
      else if(key=="--duration-ms")duration=static_cast<int>(number(value,1,600000,true));
      else if(key=="--browser-arg")options.arguments.push_back(value);
      else if(key=="--backend") {
        if(value!="cef"&&value!="chromium")throw std::invalid_argument("Recording backend must be cef or chromium");
        options.backend=value=="cef"?suitecut::BrowserBackend::cef:suitecut::BrowserBackend::chromium;
      } else throw std::invalid_argument("Unknown option: "+key);
    }
    if(url.empty()||output.empty())throw std::invalid_argument(usage);
    output=std::filesystem::absolute(output).lexically_normal().string();
    const bool cef=options.backend==suitecut::BrowserBackend::cef;
    const auto extension=std::filesystem::path(output).extension();
    if(cef&&extension!=".webm")throw std::invalid_argument("CEF recording output must be .webm");
    if(!cef&&(extension!=".webm"&&extension!=".mp4"))throw std::invalid_argument("Chromium recording output must be .webm or .mp4");
    if(!cef&&!browser_explicit)throw std::invalid_argument("Chromium recording requires --browser /absolute/browser");
    if(!cef&&audio)throw std::invalid_argument("Page audio recording is currently supported by the CEF backend");
    if(options.width%2||options.height%2)throw std::invalid_argument("Recording dimensions must be even");
    // Capture cadence is integral; encoding resamples onto the requested fractional timeline.
    options.frames_per_second=std::ceil(fps);
    auto browser=suitecut::Browser::launch(options);
    auto page=browser.main_page();
    page.set_timeout(std::chrono::milliseconds(60000));
    page.navigate(url);
    Json started;
    if(cef)started=page.command("recording",Json{{"action","start"},{"ffmpegPath",ffmpeg},
      {"outputPath",output},{"framesPerSecond",fps},{"width",options.width},{"height",options.height},{"audio",audio}});
    else started=page.start_recording(output,fps,Json{{"ffmpegPath",ffmpeg},{"ffprobePath",ffprobe},
      {"maxWidth",options.width},{"maxHeight",options.height}});
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    auto result=cef?page.command("recording",Json{{"action","stop"},{"recordingId",started.at("id")}}):page.stop_recording();
    browser.close();
    std::cout<<Json{{"ok",true},{"backend",cef?"cef":"chromium"},{"ffmpegPath",ffmpeg},
      {"ffprobePath",ffprobe},{"recording",std::move(result)}}.dump()<<'\n';
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<suitecut::Json{{"ok",false},{"error",error.what()}}.dump()<<'\n';
    return 1;
  }
}
