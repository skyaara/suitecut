// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
#include "include/cef_api_hash.h"
#include "http_trace_controls.h"
#include "trace_action_metadata.h"
#include "trace_context_actions.h"
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Trace=suitecut::HTTPTraceControls;using Dict=Trace::Dict;
  auto command=CefDictionaryValue::Create();command->SetString("command","evaluate");command->SetInt("id",7);auto action=suitecut::TraceActionMetadata(command,4,10);assert(action&&action->GetString("apiName")=="Evaluate"&&!action->HasKey("title")&&action->GetString("callId")=="call@7");
  command->SetString("command","waitEvent");command->SetString("event","console");action=suitecut::TraceActionMetadata(command,4,10);assert(action->GetString("apiName")=="Wait for event \"console\""&&action->GetString("title")=="Wait for event \"console\"");
  auto frame_element=CefDictionaryValue::Create();frame_element->SetString("command","handle");frame_element->SetString("action","frameElement");frame_element->SetString("frame","actual-child-frame");frame_element->SetInt("id",8);auto frame_action=suitecut::TraceActionMetadata(frame_element,4,10);assert(frame_action&&frame_action->GetString("class")=="Frame"&&frame_action->GetString("method")=="frameElement"&&frame_action->GetString("apiName")=="Get frame element"&&frame_action->GetDictionary("params")->GetString("frame")=="actual-child-frame");
  auto close=suitecut::TracePageCloseMetadata(command,4,10);assert(close->GetString("apiName")=="Close page"&&!close->HasKey("title"));
  const auto directory=std::filesystem::temp_directory_path()/("suitecut-http-trace-"+std::to_string(getpid()));std::filesystem::create_directories(directory);const auto caller=directory/"actual-caller.cc";{std::ofstream file(caller);file<<"actual native caller bytes";}
  double now=10;std::vector<std::function<void()>> background;Trace::Hooks hooks;hooks.now=[&]{return now;};hooks.context_exists=[](int context){return context==1||context==2;};hooks.background=[&](auto work){background.push_back(std::move(work));return true;};hooks.owner=[](auto){return false;};hooks.wake=[]{};
  Trace trace(hooks);int owner=0,replies=0;auto call=[&](const char* action,Dict value=nullptr){auto params=value?value:CefDictionaryValue::Create();params->SetString("action",action);trace.Handle(++owner,1,params,100,[&](bool ok,Dict,const std::string& error){assert(ok&&error.empty());++replies;});};
  auto start=CefDictionaryValue::Create();start->SetBool("snapshots",true);start->SetBool("sources",true);call("start",start);
  auto params=CefDictionaryValue::Create();params->SetString("url","http://native.test:8080/actual");params->SetString("method","POST");auto stack=CefListValue::Create();auto frame=CefDictionaryValue::Create();frame->SetString("file",caller.string());frame->SetInt("line",1);frame->SetInt("column",1);frame->SetString("function","actual caller");stack->SetDictionary(0,frame);params->SetList("traceStack",stack);
  trace.Before(101,1,params);assert(background.size()==1&&!trace.ContextReady(1)&&trace.ContextReady(2));auto headers=CefDictionaryValue::Create();headers->SetString("Content-Type","application/octet-stream");std::vector<uint8_t> all(256);for(size_t i=0;i<all.size();++i)all[i]=uint8_t(i);trace.Request(101,55,"http://native.test:8080/actual","POST",headers,all);
  auto response=CefDictionaryValue::Create();response->SetInt("status",201);response->SetString("statusText","Created");response->SetDictionary("headers",headers->Copy(false));now=20;trace.Response(101,55,response,all,"");trace.After(101,true,CefDictionaryValue::Create(),"");
  // A genuine pending caller read must retain the same stop deadline and body
  // ownership until its native background callback physically retires.
  auto save=CefDictionaryValue::Create();save->SetString("path",(directory/"first.zip").string());call("stopChunk",save);assert(replies==1&&trace.Physical()==1);background[0]();trace.Tick(now);
  auto drain=[&]{for(int i=0;i<10000&&trace.NeedsTick();++i){trace.Tick(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!trace.NeedsTick()&&!trace.Physical());};drain();assert(replies==2&&std::filesystem::exists(directory/"first.zip")&&trace.ContextReady(1));
  call("startChunk");params->Remove("traceStack");trace.Before(102,1,params);trace.Request(102,56,"http://native.test:8080/unfinished","POST",headers,{});
  save=CefDictionaryValue::Create();save->SetString("path",(directory/"second.zip").string());call("stopChunk",save);drain();call("startChunk");
  // Real completion from a former chunk cannot produce an after without a new
  // before, or publish its bytes into a replacement chunk/session.
  trace.Response(102,56,response,all,"");trace.After(102,true,CefDictionaryValue::Create(),"");save=CefDictionaryValue::Create();save->SetString("path",(directory/"third.zip").string());call("stop",save);drain();
  auto diagnostics=CefDictionaryValue::Create();trace.Diagnostics(diagnostics);for(const char* key:{"apiTraceContexts","apiTracePhysical","apiTraceWaitingStop","apiTracePendingActions","apiTraceSourcesPhysical","apiTraceTransfers"})assert(diagnostics->GetInt(key)==0);
  {Trace active(hooks);auto config=CefDictionaryValue::Create();config->SetString("action","start");config->SetBool("snapshots",true);active.Handle(999,1,config,100,[](bool ok,Dict,const std::string& error){assert(ok&&error.empty());});}
  // API disposal must retain actual canceled live writers until the accepted
  // native closure retires; another API context remains independently ready.
  {
    auto live_hooks=hooks;live_hooks.raw_directory=(directory/"live").string();std::vector<std::function<void()>> queued;live_hooks.background=[&](auto task){queued.push_back(std::move(task));return true;};Trace live(live_hooks);
    auto start=CefDictionaryValue::Create();start->SetString("action","start");start->SetString("name","actual-api");live.Handle(3000,1,start,30000,[](bool ok,Dict,const std::string&){assert(ok);});now+=100;live.Tick(now);assert(queued.size()==1&&live.Physical()==1&&!live.ContextReady(1)&&live.ContextReady(2));
    live.Closed(1);assert(live.Physical()==1&&!live.ContextReady(1));queued.front()();live.Tick(now);assert(!live.Physical()&&live.ContextReady(1)&&!std::filesystem::exists(directory/"live/actual-api.trace"));
  }
  trace.Clear();std::cout<<directory.string()<<'\n';
}
