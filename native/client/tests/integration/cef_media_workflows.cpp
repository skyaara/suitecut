// SPDX-License-Identifier: MIT
// Actual CEF video/audio playback, recorder pause/resume and cancellation proof.
#include "suitecut/browser.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using suitecut::Json;
using namespace std::chrono_literals;
int main(int argc,char** argv) {
  if(argc!=5){std::cerr<<"Usage: cef_media_workflows ENGINE FFMPEG FIXTURE_URL OUTPUT_DIRECTORY\n";return 2;}
  Json report{{"checks",Json::array{}},{"ok",false}};
  const auto output=std::filesystem::absolute(argv[4]);std::filesystem::create_directories(output);
  const auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);report["checks"].as_array().emplace_back(name);};
  try {
    suitecut::LaunchOptions options;options.executable=argv[1];options.width=1280;options.height=720;options.frames_per_second=60;
    auto browser=suitecut::Browser::launch(options);auto page=browser.main_page();page.set_timeout(30s);
    page.navigate(argv[3]);
    page.wait_for_function("document.querySelector('video').readyState>=2&&document.querySelector('video').currentTime>.2");
    const auto before=page.evaluate("({time:document.querySelector('video').currentTime,width:document.querySelector('video').videoWidth,height:document.querySelector('video').videoHeight,paused:document.querySelector('video').paused})");
    check(before.at("width").as_int()==640&&before.at("height").as_int()==360&&!before.at("paused").as_bool(),"CEF decodes and plays actual VP9/Opus media");
    auto image=page.screenshot();std::ofstream png(output/"playback.png",std::ios::binary);png.write(reinterpret_cast<const char*>(image.data()),image.size());png.close();
    const auto start_requested=std::chrono::steady_clock::now();
    auto start=page.command("recording",Json{{"action","start"},{"ffmpegPath",argv[2]},
      {"outputPath",(output/"playback.webm").string()},{"framesPerSecond",60},{"width",1280},{"height",720},{"audio",true}});
    const auto start_acknowledged=std::chrono::steady_clock::now();
    const auto command=[&](const char* action){return page.command("recording",Json{{"action",action},{"recordingId",start.at("id")}});};
    std::this_thread::sleep_for(2s);const auto pause_requested=std::chrono::steady_clock::now();command("pause");
    const auto pause_acknowledged=std::chrono::steady_clock::now();
    const auto paused=command("status");std::this_thread::sleep_for(1s);
    const auto resume_requested=std::chrono::steady_clock::now();command("resume");
    const auto resume_acknowledged=std::chrono::steady_clock::now();std::this_thread::sleep_for(2s);
    const auto after=page.evaluate("document.querySelector('video').currentTime");
    const auto stop_requested=std::chrono::steady_clock::now();
    auto recording=command("stop");const auto stop_acknowledged=std::chrono::steady_clock::now();report["recording"]=recording;report["pausedStatus"]=paused;
    check(recording.at("failure").as_string().empty()&&std::filesystem::file_size(output/"playback.webm")>1000,"native recorder publishes actual playback video");
    check(recording.at("audioInputFrames").as_double()>48000,"native recorder captures actual browser page audio");
    const auto milliseconds=[](auto interval){return std::chrono::duration<double,std::milli>(interval).count();};
    // Bracket actual command processing. A loaded host can delay start/pause/
    // resume receipts; four seconds of requested sleeps is not a media clock.
    const double active_min=milliseconds(stop_requested-start_acknowledged)-milliseconds(resume_acknowledged-pause_requested);
    const double active_max=milliseconds(stop_acknowledged-start_requested)-milliseconds(resume_requested-pause_acknowledged);
    report["hostActiveBoundsMs"]=Json{{"min",active_min},{"max",active_max}};
    check(recording.at("durationMs").as_double()>=active_min-250&&recording.at("durationMs").as_double()<=active_max+250,"pause/resume media time lies within measured active command intervals");
    check(after.as_double()>before.at("time").as_double()+4,"browser video playback advances while recording is paused");
    auto canceled=page.command("recording",Json{{"action","start"},{"ffmpegPath",argv[2]},
      {"outputPath",(output/"canceled.webm").string()},{"framesPerSecond",30},{"width",1280},{"height",720},{"audio",false}});
    std::this_thread::sleep_for(300ms);
    page.command("recording",Json{{"action","cancel"},{"recordingId",canceled.at("id")}});
    check(!std::filesystem::exists(output/"canceled.webm"),"cancel leaves no public recording output");
#ifdef __APPLE__
    // Shared-texture Invalidate may not repaint an unchanged compositor image.
    // Both ordinary static capture and the silent-page audio readiness timeout
    // must start and physically finish without JavaScript animation.
    page.navigate("data:text/html,<body style='background:rgb(205,32,64)'>Static capture</body>");
    for(bool audio:{false,true}) {
      const auto path=output/(audio?"static-audio.mp4":"static.mp4");
      auto fixed=page.command("recording",Json{{"action","start"},{"ffmpegPath",argv[2]},
        {"outputPath",path.string()},{"framesPerSecond",120},{"width",1280},{"height",720},{"audio",audio}});
      std::this_thread::sleep_for(1s);
      auto done=page.command("recording",Json{{"action","stop"},{"recordingId",fixed.at("id")}});
      check(done.at("failure").as_string().empty()&&done.at("outputFrames").as_double()>=120&&
        done.at("heldFrames").as_double()>0&&std::filesystem::file_size(path)>1000,
        audio?"silent static page starts and finishes with audio enabled":"static page starts and finishes without new animation paints");
    }
#endif
    browser.close();report["ok"]=true;
  }catch(const std::exception& error){report["error"]=error.what();}
  std::ofstream(output/"results.json")<<report.dump()<<'\n';std::cout<<report.dump()<<'\n';
  return report.at("ok").as_bool()?0:1;
}
