// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "har_recording.h"
#include "har_replay.h"
using Dict=CefRefPtr<CefDictionaryValue>;
Dict Parse(const char*text){return CefParseJSON(text,JSON_PARSER_RFC)->GetDictionary();}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));double now=20;std::vector<std::function<void()>>background,owner;std::vector<int>settled;
  suitecut::NativeNetworkArchives archives([&]{return now;},[](const std::string&){assert(false);});
  suitecut::NativeHARRecording::Hooks hooks;
  hooks.now=[&]{return now;};hooks.background=[&](std::function<void()>work){background.push_back(std::move(work));return true;};hooks.owner=[&](std::function<void()>work){owner.push_back(std::move(work));return true;};hooks.settled=[&](int context){settled.push_back(context);};
  auto recorder=std::make_shared<suitecut::NativeHARRecording>(archives,std::move(hooks));const auto directory=std::filesystem::temp_directory_path()/std::filesystem::path("suitecut-har-recorder-"+std::to_string(getpid()));std::filesystem::create_directories(directory);const auto path=(directory/"record.har").string();
  auto options=CefDictionaryValue::Create();options->SetString("path",path);std::string error;assert(recorder->Configure(1,options,error));
  suitecut::NativeHARCapture::BodyReply body;assert(archives.Attach(1,"actual-page:1",[&](const std::string&session,const std::string&request,double deadline,suitecut::NativeHARCapture::BodyReply reply){assert(session=="real-session"&&request=="real-request"&&deadline==30020);body=std::move(reply);}));
  auto event=Parse(R"({"event":"request","request":{"id":"canonical1","requestId":"real-request","session":"real-session","frameId":"actual-frame","url":"https://native.test/data","method":"GET","resourceType":"Fetch","headers":{},"timing":{"startTime":20,"responseEnd":2}}})");assert(archives.Observe(1,"actual-page:1",event,1000));
  auto response=Parse(R"({"status":200,"statusText":"OK","headers":{"Content-Type":"application/octet-stream"},"url":"https://native.test/data","session":"real-session","requestId":"real-request"})");event->SetDictionary("response",response->Copy(false));event->SetString("event","requestfinished");assert(archives.Observe(1,"actual-page:1",event,1000));assert(body&&archives.recording_pending(1)==1);
  int closes=0;recorder->BeforeClose(1,50,[&]{++closes;archives.Detach("actual-page:1");});recorder->BeforeClose(1,60,[&]{++closes;});assert(closes==0&&!recorder->ContextReady(1));
  auto late=event->Copy(false);late->SetString("event","request");late->GetDictionary("request")->SetString("id","after-seal");assert(archives.Observe(1,"actual-page:1",late,1000));
  body(true,{0,255,13,10},"");assert(archives.recording_pending(1)==0);recorder->Tick();assert(closes==2);assert(!recorder->ContextReady(1));assert(recorder->Physical()==1&&!std::filesystem::exists(path));
  background[0]();assert(recorder->Physical()==1&&!recorder->ContextReady(1));assert(std::filesystem::exists(path));owner[0]();assert(recorder->Physical()==0&&recorder->ContextReady(1)&&recorder->Error(1).empty());
  std::atomic_bool canceled=false;suitecut::NativeHARFiles::Action action{canceled,50,[&]{return now;}};Dict read;assert(suitecut::NativeHARFiles::Read(path,action,read,error));assert(read->GetDictionary("log")->GetList("entries")->GetSize()==1);suitecut::NativeHARReplay replay;assert(replay.Load(read,error));auto match=replay.Lookup(Parse(R"({"url":"https://native.test/data","method":"GET","headers":{}})"));assert(match&&match->body==std::vector<uint8_t>({0,255,13,10}));
  // Removal cancels a queued file operation but retains its physical job. The
  // old completion cannot stop or settle a later recorder with the same ID.
  recorder->Removed(1);assert(recorder->Configure(1,options,error));recorder->BeforeClose(1,50,[]{});assert(!recorder->ContextReady(1)&&recorder->Physical()==1);recorder->Removed(1);assert(recorder->Physical()==1);assert(recorder->Configure(1,options,error));background[1]();owner[1]();assert(recorder->Physical()==0&&!recorder->ContextReady(1));assert(settled.size()==1);
  // The canceled old task leaves the previously committed archive intact.
  error.clear();assert(suitecut::NativeHARFiles::Read(path,action,read,error));assert(read->GetDictionary("log")->GetList("entries")->GetSize()==1);
  // Page close waits only the body reservations actually captured at admission.
  // A later unrelated response cannot extend that original native close scope.
  recorder->Removed(1);assert(recorder->Configure(2,options,error));
  std::vector<suitecut::NativeHARCapture::BodyReply> page_bodies;
  assert(archives.Attach(2,"actual-page:2",[&](const std::string&,const std::string&,double,suitecut::NativeHARCapture::BodyReply reply){page_bodies.push_back(std::move(reply));}));
  auto page_event=event->Copy(false);page_event->SetString("event","request");page_event->GetDictionary("request")->SetString("id","scope-first");assert(archives.Observe(2,"actual-page:2",page_event,1000));page_event->SetString("event","requestfinished");assert(archives.Observe(2,"actual-page:2",page_event,1000));
  int page_closed=0;recorder->BeforePageClose(2,"actual-page:2",50,[&]{++page_closed;});assert(page_closed==0);
  page_event->SetString("event","request");page_event->GetDictionary("request")->SetString("id","scope-later");assert(archives.Observe(2,"actual-page:2",page_event,1000));page_event->SetString("event","requestfinished");assert(archives.Observe(2,"actual-page:2",page_event,1000));assert(page_bodies.size()==2);
  page_bodies[0](true,{1},"");recorder->Tick();assert(page_closed==1&&archives.recording_pending(2)==1);page_bodies[1](true,{2},"");assert(archives.recording_pending(2)==0&&archives.physical(2)==0);
  // A committed file is a physical fact, but its delayed owner-thread ACK
  // cannot turn an expired original close into a logical success.
  recorder->Removed(2);assert(recorder->Configure(3,options,error));recorder->BeforeClose(3,50,[]{});assert(!recorder->ContextReady(3));
  assert(recorder->Physical()==1);background.back()();assert(std::filesystem::exists(path));now=51;owner.back()();
  assert(recorder->Physical()==0&&recorder->ContextReady(3));assert(recorder->Error(3)=="Native HAR original context close file deadline exceeded");
  // Removal/Clear release original page/context close continuations exactly
  // once while genuine body callbacks remain owned until their real replies.
  now=20;assert(recorder->Configure(4,options,error));suitecut::NativeHARCapture::BodyReply retained;
  assert(archives.Attach(4,"actual-page:4",[&](const std::string&,const std::string&,double,suitecut::NativeHARCapture::BodyReply reply){retained=std::move(reply);}));
  auto pending=event->Copy(false);pending->SetString("event","request");pending->GetDictionary("request")->SetString("id","clear-pending");assert(archives.Observe(4,"actual-page:4",pending,1000));pending->SetString("event","requestfinished");assert(archives.Observe(4,"actual-page:4",pending,1000));
  int released=0;recorder->BeforePageClose(4,"actual-page:4",50,[&]{++released;recorder->Removed(4);});recorder->BeforeClose(4,50,[&]{++released;});assert(released==0&&archives.physical(4)==1);
  recorder->Clear();assert(released==2&&archives.physical(4)==1);recorder->Clear();assert(released==2);
  retained(false,{},"actual channel closed");assert(archives.physical(4)==0&&recorder->Physical()==0);std::filesystem::remove_all(directory);
}
