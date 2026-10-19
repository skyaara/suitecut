// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <fstream>
#include <thread>
#include <chrono>
#include "include/cef_api_hash.h"
#include "trace_controls.h"
namespace {std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);assert(file);return {std::istreambuf_iterator<char>(file),{}};}}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::TraceControls;using Dict=Controls::Dict;
  const auto directory=std::filesystem::temp_directory_path()/("suitecut-live-controls-"+std::to_string(getpid()));std::filesystem::remove_all(directory);
  double now=0;std::deque<std::function<void()>> workers;Controls::Hooks hooks;hooks.raw_directory=directory.string();hooks.now=[&]{return now;};hooks.background=[&](auto work){workers.push_back(std::move(work));return true;};hooks.post=[](auto){return false;};hooks.context_options=[](int){return CefDictionaryValue::Create();};hooks.request_capture=[](int){};const auto controls=std::make_shared<Controls>(hooks);int owner=0,ok=0,bad=0;
  auto call=[&](const char* action,const char* name=nullptr,const char* path=nullptr,double deadline=30000){auto config=CefDictionaryValue::Create();config->SetString("action",action);if(name)config->SetString("name",name);if(path)config->SetString("path",(directory/path).string());const int id=++owner;controls->Handle(id,1,config,deadline,[&](bool success,Dict,const std::string&){if(success)++ok;else++bad;});return id;};
  auto work=[&]{assert(!workers.empty());auto next=std::move(workers.front());workers.pop_front();next();controls->Tick(now);};
  auto drain=[&]{for(int i=0;i<10000&&controls->PhysicalCount();++i){controls->Tick(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!controls->PhysicalCount());};
  call("start","first");assert(ok==1);const auto lease=controls->CaptureLease(1);now=100;controls->Tick(now);assert(controls->PhysicalCount()==1);auto before=CefDictionaryValue::Create();before->SetString("type","before");before->SetString("callId","call@1");controls->Before(1,1,before);auto after=CefDictionaryValue::Create();after->SetString("type","after");after->SetString("callId","call@1");controls->After(1,1,after);auto event=CefDictionaryValue::Create();event->SetString("type","console");event->SetString("text","idle-console");controls->ConsoleCaptured(lease,"{\"type\":\"console\",\"text\":\"actual-idle\"}");assert(controls->Resource(lease,"resources/body.css",{0,255,1}));
  // Stop waits the held actual live writer, then writes the final snapshot and
  // ZIP under the same namespace, preserving intervening events exactly.
  call("stopChunk",nullptr,"first.zip");assert(ok==1&&!controls->ContextReady(1));now=200;controls->Tick(now);assert(workers.size()==1);work();drain();assert(ok==2&&bad==0&&controls->ContextReady(1));const auto first=Read(directory/"first.trace");assert(first.find("actual-idle")!=std::string::npos&&first.find("\"after\"")!=std::string::npos&&std::filesystem::exists(directory/"first.zip"));
  // A chunk replacement flushes the actual old active chunk before clearing
  // it, and validates the next name before altering current ownership.
  call("startChunk","second");assert(ok==3);call("startChunk","../invalid");assert(bad==1&&controls->CaptureLease(1).context);controls->ConsoleCaptured(controls->CaptureLease(1),"{\"type\":\"console\",\"text\":\"second-idle\"}");call("startChunk","third");assert(ok==3);drain();assert(ok==4&&Read(directory/"second.trace").find("second-idle")!=std::string::npos);assert(controls->CaptureLease(1).chunk==lease.chunk+2);
  // Original stop deadline includes physical live flush; canceling it cannot
  // discard a queued physical owner or create a later positive reply.
  now=300;controls->Tick(now);assert(controls->PhysicalCount()==1);call("stopChunk",nullptr,"expired.zip",310);now=311;controls->Tick(now);assert(bad==2&&controls->PhysicalCount()==1&&!controls->ContextReady(1));work();assert(controls->PhysicalCount()==0&&bad==2&&!std::filesystem::exists(directory/"expired.zip"));
  // Actual page/context close cancels busy work and waits genuine retirement.
  call("startChunk","closed");now=411;controls->Tick(now);assert(controls->PhysicalCount()==1);controls->Closed(1);assert(!controls->ContextReady(1));work();assert(controls->ContextReady(1)&&!std::filesystem::exists(directory/"closed.trace"));
  // Failed idle writes surface at genuine stop, and another session can start.
  {std::ofstream file(directory/"blocker");file<<"file";}hooks.raw_directory=(directory/"blocker/nested").string();const auto blocked=std::make_shared<Controls>(hooks);auto start=CefDictionaryValue::Create();start->SetString("action","start");blocked->Handle(++owner,1,start,30000,[&](bool success,Dict,const std::string&){assert(success);});now=511;blocked->Tick(now);assert(workers.size()==1);{auto next=std::move(workers.front());workers.pop_front();next();blocked->Tick(now);}auto stop=CefDictionaryValue::Create();stop->SetString("action","stop");int failed=0;blocked->Handle(++owner,1,stop,30000,[&](bool success,Dict,const std::string& why){assert(!success&&why.find("ENOTDIR")!=std::string::npos);++failed;});assert(failed==1&&!blocked->Configuration(1)&&!blocked->NeedsTick());
  controls->Clear();blocked->Clear();std::filesystem::remove_all(directory);
}
