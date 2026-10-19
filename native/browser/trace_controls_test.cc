// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include "include/cef_api_hash.h"
#include "trace_controls.h"
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::TraceControls;using Dict=Controls::Dict;
  double now=10;Controls::Hooks hooks;hooks.now=[&]{return now;};hooks.context_options=[](int context){return context==1?CefDictionaryValue::Create():nullptr;};hooks.request_capture=[](int){};
  const auto controls=std::make_shared<Controls>(hooks);int owner=0;auto request=[&](const char* action,Dict extra=nullptr){auto config=extra?extra:CefDictionaryValue::Create();config->SetString("action",action);bool success=false;controls->Handle(++owner,1,config,100,[&](bool ok,Dict,const std::string&){success=ok;});return success;};
  assert(request("groupEnd"));assert(request("stop"));auto save=CefDictionaryValue::Create();save->SetString("path","unused.zip");assert(!request("stop",save));
  assert(request("start"));const auto session=controls->CaptureLease(1);assert(session.context==1);
  assert(controls->SourceResource(session,"src/actual.cpp",{1,2,3}));
  assert(request("startChunk"));const auto chunk=controls->CaptureLease(1);assert(chunk.chunk!=session.chunk&&chunk.session==session.session);assert(!controls->SourceResource(session,"src/late.cpp",{4}));assert(controls->SourceResource(chunk,"src/current.cpp",{5}));
  auto discard=CefDictionaryValue::Create();discard->SetInt("session",session.session);discard->SetInt("chunk",session.chunk);assert(request("discardChunk",discard));assert(controls->CaptureLease(1).chunk==chunk.chunk);
  discard=CefDictionaryValue::Create();discard->SetInt("session",session.session);discard->SetInt("chunk",session.chunk);assert(request("discard",discard));assert(!controls->Configuration(1));
  assert(request("start"));const auto next=controls->CaptureLease(1);assert(next.session!=session.session);
  discard=CefDictionaryValue::Create();discard->SetInt("session",session.session);discard->SetInt("chunk",session.chunk);assert(request("discard",discard));assert(controls->CaptureLease(1).session==next.session);
  assert(request("stopChunk"));assert(controls->Configuration(1));discard=CefDictionaryValue::Create();discard->SetDouble("session",next.session);discard->SetDouble("chunk",next.chunk);assert(request("discard",discard));assert(!controls->Configuration(1));
  controls->Clear();assert(!controls->PhysicalCount()&&controls->ContextReady(1));
  bool capture_busy=true;hooks.pending_capture=[&](int){return capture_busy;};const auto flushing=std::make_shared<Controls>(hooks);int flushed_replies=0;
  auto invoke_flush=[&](const char* action){auto config=CefDictionaryValue::Create();config->SetString("action",action);const int current=++owner;flushing->Handle(current,1,config,100,[&](bool ok,Dict,const std::string&){assert(ok);++flushed_replies;});return current;};
  invoke_flush("start");const auto owned_capture=flushing->CaptureLease(1);invoke_flush("stop");assert(flushed_replies==1&&!flushing->CaptureLease(1).context&&!flushing->ContextReady(1)&&flushing->NeedsTick());
  auto event=CefDictionaryValue::Create();event->SetString("type","frame-snapshot");flushing->Captured(owned_capture,event,"",{});capture_busy=false;flushing->Tick(now);assert(flushed_replies==2&&flushing->ContextReady(1)&&!flushing->NeedsTick());
  capture_busy=true;invoke_flush("start");auto config=CefDictionaryValue::Create();config->SetString("action","stopChunk");const int cancel_flush=++owner;int canceled_replies=0;flushing->Handle(cancel_flush,1,config,100,[&](bool ok,Dict,const std::string&){assert(!ok);++canceled_replies;});flushing->Cancel(cancel_flush);assert(canceled_replies==1&&!flushing->CaptureLease(1).context&&!flushing->NeedsTick());capture_busy=false;flushing->Tick(now);assert(canceled_replies==1);flushing->Clear();hooks.pending_capture={};

  // Real file writes run on the native worker thread. Exercise failed task posts:
  // original native ticks must release the physical reservation and reply once.
  hooks.post=[](std::function<void()>){return false;};
  const auto writer=std::make_shared<Controls>(hooks);
  const auto directory=std::filesystem::temp_directory_path()/std::string("suitecut-trace-controls-"+std::to_string(getpid()));std::filesystem::create_directories(directory);
  auto start=[&]{auto config=CefDictionaryValue::Create();config->SetString("action","start");bool ok=false;writer->Handle(++owner,1,config,100,[&](bool success,Dict,const std::string&){ok=success;});assert(ok);};
  int replies=0;bool success=false;auto stop=[&](const char* filename,double deadline=100){auto config=CefDictionaryValue::Create();config->SetString("action","stop");config->SetString("path",(directory/filename).string());int current=++owner;writer->Handle(current,1,config,deadline,[&](bool ok,Dict,const std::string&){++replies;success=ok;});return current;};
  auto drain=[&]{for(int i=0;i<10000&&writer->PhysicalCount();++i){writer->Tick(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!writer->PhysicalCount());};
  // A genuine filesystem ENOTDIR retires its trace session before replying;
  // the same context must accept another recording and native ZIP write.
  {std::ofstream blocker(directory/"blocker");blocker<<"actual file";}
  start();auto blocked=CefDictionaryValue::Create();blocked->SetString("action","stop");blocked->SetString("path",(directory/"blocker"/"trace.zip").string());int rejected=0;writer->Handle(++owner,1,blocked,100,[&](bool ok,Dict,const std::string& error){assert(!ok&&error.find("ENOTDIR")!=std::string::npos);++rejected;});drain();assert(rejected==1&&!writer->Configuration(1)&&writer->ContextReady(1)&&!writer->PhysicalCount());std::filesystem::remove(directory/"blocker");
  start();stop("actual.zip");assert(writer->PhysicalCount()==1&&!writer->ContextReady(1));drain();assert(replies==1&&success&&writer->ContextReady(1));
  {std::ifstream actual(directory/"actual.zip",std::ios::binary);char signature[4]={};actual.read(signature,4);assert(std::string(signature,4)=="PK\003\004");}
  start();assert(writer->Resource(1,"resources/actual.bin",std::vector<uint8_t>(24*1024*1024,42)));const auto canceled=stop("canceled.zip");assert(writer->PhysicalCount()==1);writer->Cancel(canceled);assert(replies==2&&!success&&writer->PhysicalCount()==1);drain();assert(replies==2&&!std::filesystem::exists(directory/"canceled.zip"));
  start();assert(writer->Resource(1,"resources/actual.bin",std::vector<uint8_t>(24*1024*1024,17)));stop("expired.zip",11);now=12;writer->Tick(now);assert(replies==3&&!success);drain();assert(replies==3&&!std::filesystem::exists(directory/"expired.zip"));now=10;
  start();assert(writer->Resource(1,"resources/actual.bin",std::vector<uint8_t>(24*1024*1024,7)));stop("closed.zip");writer->Closed(1);assert(replies==4&&!success&&writer->PhysicalCount()==1);drain();assert(replies==4&&!std::filesystem::exists(directory/"closed.zip"));
  for(const auto& entry:std::filesystem::directory_iterator(directory)){assert(entry.path().filename()=="actual.zip");}std::filesystem::remove_all(directory);

  // A capture consumer is sealed before flushing and keeps its original native
  // session across chunks. Stale scopes and failed starts cannot retire others.
  {
    std::vector<std::pair<uint64_t,bool>> begun;std::vector<uint64_t> sealed,ended;
    bool held=false,admit=true;int completed=0,failed=0;std::string actual_error;
    Controls::Hooks capture;capture.now=[&]{return now;};capture.context_options=[](int){return CefDictionaryValue::Create();};capture.request_capture=[](int){};
    capture.begin_session=[&](auto lease,bool initial,std::string& error){if(!admit){error="actual native consumer capacity rejection";return false;}begun.emplace_back(lease.session,initial);return true;};
    capture.seal_session=[&](auto lease){sealed.push_back(lease.session);};capture.end_session=[&](auto lease){ended.push_back(lease.session);};capture.pending_capture=[&](int){return held;};
    const auto scoped=std::make_shared<Controls>(capture);
    auto call=[&](const char* action,int context=1,Dict value=nullptr,double deadline=100){auto params=value?value:CefDictionaryValue::Create();params->SetString("action",action);const int id=++owner;scoped->Handle(id,context,params,deadline,[&](bool ok,Dict,const std::string& error){if(ok)++completed;else{++failed;actual_error=error;}});return id;};
    call("start");const auto first=scoped->CaptureLease(1);assert(begun.size()==1&&begun.back().second);
    call("start");assert(failed==1&&begun.size()==1&&ended.empty());
    auto stale=CefDictionaryValue::Create();stale->SetDouble("session",first.session+1);stale->SetDouble("chunk",first.chunk);call("discard",1,stale);assert(sealed.empty()&&ended.empty());
    held=true;const int stop=call("stopChunk");assert(completed==2&&sealed.size()==1&&sealed.back()==first.session&&ended.empty());
    scoped->Tick(now);assert(completed==2);held=false;scoped->Tick(now);assert(completed==3&&ended.empty());
    call("startChunk");assert(begun.size()==2&&!begun.back().second&&begun.back().first==first.session);
    held=true;const int cancel=call("stop");scoped->Cancel(cancel);assert(failed==2&&ended.size()==1&&ended.back()==first.session);scoped->Cancel(cancel);assert(ended.size()==1);
    held=false;call("start");const auto replacement=scoped->CaptureLease(1);assert(replacement.session!=first.session);call("start",2);const auto second=scoped->CaptureLease(2);scoped->Clear();assert(ended.size()==3&&ended[1]==replacement.session&&ended[2]==second.session);
    admit=false;call("start");assert(failed==3&&actual_error=="actual native consumer capacity rejection"&&!scoped->Configuration(1));
    (void)stop;
  }


  // Native raw-only exports own the same physical writer/deadline as ZIPs.
  {
    const auto raw_directory=std::filesystem::temp_directory_path()/("suitecut-trace-raw-controls-"+std::to_string(getpid()));std::filesystem::remove_all(raw_directory);
    Controls::Hooks raw_hooks;raw_hooks.now=[&]{return now;};raw_hooks.context_options=[](int){return CefDictionaryValue::Create();};raw_hooks.request_capture=[](int){};raw_hooks.post=[](std::function<void()>){return false;};raw_hooks.raw_directory=(raw_directory/"journals").string();
    const auto raw=std::make_shared<Controls>(raw_hooks);int count=0;bool result=false;
    auto call=[&](const char* action,const char* name=nullptr,const char* zip=nullptr){auto params=CefDictionaryValue::Create();params->SetString("action",action);if(name)params->SetString("name",name);if(zip)params->SetString("path",(raw_directory/zip).string());const int id=++owner;raw->Handle(id,1,params,100,[&](bool ok,Dict,const std::string&){++count;result=ok;});return id;};
    auto drain=[&]{for(int i=0;i<10000&&raw->PhysicalCount();++i){raw->Tick(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!raw->PhysicalCount());};
    call("start","first");assert(result&&count==1);call("stopChunk");assert(raw->PhysicalCount()==1&&count==1);drain();assert(result&&count==2&&std::filesystem::exists(raw_directory/"journals/first.trace"));
    call("startChunk","second");assert(result);call("stopChunk",nullptr,"actual.zip");drain();assert(result&&std::filesystem::exists(raw_directory/"actual.zip")&&std::filesystem::exists(raw_directory/"journals/second.trace"));
    call("startChunk","discarded");const auto scope=raw->CaptureLease(1);auto discard=CefDictionaryValue::Create();discard->SetString("action","discardChunk");discard->SetDouble("session",scope.session);discard->SetDouble("chunk",scope.chunk);raw->Handle(++owner,1,discard,100,[&](bool ok,Dict,const std::string&){assert(ok);});assert(!std::filesystem::exists(raw_directory/"journals/discarded.trace")&&!raw->PhysicalCount());
    call("startChunk","../invalid");assert(!result&&!raw->CaptureLease(1).context);call("startChunk","canceled");assert(result);assert(raw->Resource(1,"resources/actual.bin",std::vector<uint8_t>(24*1024*1024,7)));const int canceled=call("stopChunk");assert(raw->PhysicalCount()==1);raw->Cancel(canceled);assert(!result&&raw->PhysicalCount()==1);const int replies=count;drain();assert(count==replies&&!std::filesystem::exists(raw_directory/"journals/canceled.trace"));
    call("startChunk","final");assert(result);call("stop");drain();assert(result&&std::filesystem::exists(raw_directory/"journals/final.trace")&&!raw->Configuration(1));
    raw->Clear();std::filesystem::remove_all(raw_directory);
  }
}
