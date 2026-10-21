// SPDX-License-Identifier: MIT
#include "suitecut/browser.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using suitecut::Json;
namespace {
std::string read(const std::filesystem::path& path) {
  std::ifstream stream(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void require(bool condition,const std::string& message,Json::array& checks) {
  if(!condition)throw std::runtime_error(message);
  checks.emplace_back(message);
}
}
int main(int argc,char** argv) {
  if(argc!=3){std::cerr<<"Usage: media_workflows /absolute/chrome /absolute/output-directory\n";return 2;}
  const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
  Json report{{"scope","actual native Chromium compositor recordings, FFmpeg/ffprobe receipts, Chrome trace and CPU profile"}};
  Json::array checks,recordings;
  int result=0;
  try {
    suitecut::LaunchOptions options;options.backend=suitecut::BrowserBackend::chromium;
    options.max_event_bytes=16*1024;
    options.executable=argv[1];options.width=640;options.height=360;options.arguments={"--no-sandbox"};
    auto browser=suitecut::Browser::launch(options);auto page=browser.main_page();
    page.set_timeout(std::chrono::milliseconds(60000));
    page.navigate("data:text/html,<body style='margin:0'><canvas width=640 height=360></canvas></body>");
    page.evaluate(R"JS((()=>{const c=document.querySelector('canvas'),x=c.getContext('2d');window.mediaTicks=0;
      const frame=t=>{window.mediaTicks++;x.fillStyle=`hsl(${Math.floor(t/5)%360} 80% 40%)`;x.fillRect(0,0,640,360);
      x.fillStyle='white';x.font='32px sans-serif';x.fillText(String(window.mediaTicks),20,80);requestAnimationFrame(frame);};requestAnimationFrame(frame);})())JS");
    for(const int fps:{1,24,60,120}) {
      const auto path=output/("recording-"+std::to_string(fps)+(fps==24?".webm":".mp4"));
      page.start_recording(path.string(),fps,Json{{"maxWidth",640},{"maxHeight",360},{"maxDurationMs",10000}});
      const auto initial=page.evaluate("window.mediaTicks").as_int();
      page.wait_for_function("window.mediaTicks>="+std::to_string(initial+20));
      const auto recording=page.stop_recording();recordings.push_back(recording);
      require(recording.at("state").as_string()=="completed","recording completed at "+std::to_string(fps)+" fps",checks);
      require(std::filesystem::file_size(path)>100,"real encoded video bytes at "+std::to_string(fps)+" fps",checks);
      const auto metadata=Json::parse(read(path.string()+".json"));
      require(metadata.at("fps").as_double()==fps&&metadata.at("resampling").as_string()=="sample-and-hold",
        "declared cadence and sample-and-hold metadata at "+std::to_string(fps)+" fps",checks);
      require(recording.at("verification").at("streams").at(0).at("nb_read_frames").as_string()==
        std::to_string(recording.at("outputFrames").as_int()),"ffprobe independently decoded frame count at "+std::to_string(fps)+" fps",checks);
      require(recording.at("peakQueuedFrameBytes").as_int()<=recording.at("queueByteLimit").as_int()&&
        recording.at("peakSpoolBytes").as_int()<=recording.at("spoolByteLimit").as_int(),"bounded recording queue and actual spool at "+std::to_string(fps)+" fps",checks);
      if(fps==120)require(recording.at("duplicatedFrames").as_int()>0,
        "120 fps output reports repeated source frames instead of claiming 120 unique compositor frames",checks);
    }
    const auto counters=browser.transport()->stats();
    require(counters.dropped_events>0,"recorder-owned raw frame telemetry is shed with counted drops under a 16 KiB public event budget",checks);
    require(counters.queued_event_bytes<=options.max_event_bytes&&page.evaluate("40+2")==Json(42),"recording backpressure preserves bounded memory and ordinary browser control",checks);
    report["publicEventBudgetBytes"]=options.max_event_bytes;
    report["publicEventDropped"]=counters.dropped_events;
    report["publicEventQueuedBytes"]=counters.queued_event_bytes;
    // Critical lifecycle events are intentionally retained. Drain them between
    // scenarios; no events were polled during the four source-frame floods.
    const auto drain=[&]{while(browser.transport()->poll(std::chrono::milliseconds(0))) {}};
    drain();
    page.start_tracing("devtools.timeline,v8,blink.user_timing");page.start_profiler(1000);
    page.evaluate("(()=>{performance.mark('native-media-proof');const until=performance.now()+150;let n=0;while(performance.now()<until)n+=Math.sqrt(n+1);return n;})()");
    const auto profile=page.stop_profiler((output/"cpu.cpuprofile").string());
    const auto trace=page.stop_tracing((output/"trace.json").string());
    require(Json::parse(read(output/"cpu.cpuprofile")).at("nodes").as_array().size()>0,"usable native V8 CPU profile nodes",checks);
    require(Json::parse(read(output/"trace.json")).at("traceEvents").as_array().size()>0,"usable Chrome trace events",checks);
    report["profiler"]=profile;report["tracing"]=trace;
    bool rejected=false;try{page.start_recording((output/"invalid.mp4").string(),121);}catch(const std::exception&){rejected=true;}
    require(rejected&&!std::filesystem::exists(output/"invalid.mp4"),"invalid cadence fails before publishing an artifact",checks);
    page.start_recording((output/"canceled.mp4").string(),30);
    const auto initial=page.evaluate("window.mediaTicks").as_int();page.wait_for_function("window.mediaTicks>="+std::to_string(initial+3));
    page.cancel_recording();require(!std::filesystem::exists(output/"canceled.mp4"),"canceled recording publishes no partial video",checks);
    drain();
    for(int cycle=0;cycle<5;++cycle) {
      auto temporary=browser.new_page();
      temporary.start_recording((output/("closed-"+std::to_string(cycle)+".mp4")).string(),30);
      temporary.evaluate("new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))");
      temporary.close();
      drain();
    }
    const auto no_spools=[&] {
      for(const auto& file:std::filesystem::directory_iterator(output))
        if(file.path().filename().string().starts_with(".suitecut-media-"))return false;
      return true;
    };
    const auto cleanup_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!no_spools()&&std::chrono::steady_clock::now()<cleanup_deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(no_spools(),"five recorded page closures release per-page media slots and source spools",checks);
    for(int cycle=0;cycle<5;++cycle)
      require(!std::filesystem::exists(output/("closed-"+std::to_string(cycle)+".mp4")),
        "page closure publishes no partial recording "+std::to_string(cycle),checks);
    browser.close();
    bool clean=true;for(const auto& file:std::filesystem::directory_iterator(output))
      if(file.path().filename().string().starts_with(".suitecut-media-"))clean=false;
    require(clean,"private media spools retired after success and cancellation",checks);
    report["ok"]=true;
  }catch(const std::exception& error){report["ok"]=false;report["error"]=error.what();result=1;}
  report["checks"]=Json(std::move(checks));report["recordings"]=Json(std::move(recordings));
  std::ofstream file(output/"results.json");file<<report.dump()<<'\n';std::cout<<report.dump()<<'\n';
  return result;
}
