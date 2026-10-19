// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "developer_debugger_controls.h"
#include "developer_debugger_documents.h"
#include "developer_debugger_initial_trees.h"
#include "developer_debugger_frame_targets.h"
#include "developer_debugger_worker_targets.h"
#include "developer_debugger_frame_pause.h"
#include "coverage_script_inventory.h"
using Dict=CefRefPtr<CefDictionaryValue>;
static double clock_now=1;
static double Now(){return clock_now;}
static Dict JSON(const std::string&json){const auto value=CefParseJSON(json,JSON_PARSER_RFC);assert(value);return value->GetDictionary();}
// Controlled Source adapter host. The production includes and actual CEF value
// graph execute here; observations/sender replies are deliberate test seams,
// not a running CEF browser or a claim of real Inspector acknowledgements.
class Source{
 public:
  using Domains=suitecut::DebuggerDomains;
  struct FrameState{std::string id,session,loader,context_identity;int context=0;};
  struct Operation{uint64_t generation=0;double deadline_ms=100;};
  struct Operations{
    std::map<int,std::shared_ptr<Operation>>items;
    std::shared_ptr<Operation>Get(int id)const{const auto at=items.find(id);return at==items.end()?nullptr:at->second;}
    bool IsCurrent(int id,uint64_t generation)const{const auto op=Get(id);return op&&op->generation==generation;}
  }operations_;
  struct Result{bool ok;std::string error,json;};
  struct Call{std::string session,method;Dict params;Domains::Reply reply;};
  std::map<int,Result>results;
  std::vector<Call>calls;
  std::vector<std::string>close_requests;
  uint64_t next_operation=0,next_lease=0,native_pristine_renderer_epoch_=1;
  bool observer_=true,closing_page_=false,renderer_failed_=false,native_debugger_closing_requested_=false,transport_closed=false;
  std::string handle_namespace_="actual-controlled-source",main_frame_id_="root";
  std::map<std::string,FrameState>frame_states_;
  std::shared_ptr<Domains>native_debugger_domains_;
  suitecut::CoverageScriptInventory native_coverage_inventory_;
  void Complete(int owner,bool ok,const std::string&error,const std::string&json=""){
    assert(operations_.Get(owner));operations_.items.erase(owner);assert(!results.contains(owner));results.emplace(owner,Result{ok,error,json});
  }
  void WakeDeadlines(){}
  uint64_t NativeDebuggerLease(){return ++next_lease;}
  bool NativeDebuggerSend(const std::string&session,const std::string&method,Dict params,double,Domains::Reply reply){
    if(transport_closed)return false;
    calls.push_back({session,method,params?params->Copy(false):nullptr,std::move(reply)});return true;
  }
  std::shared_ptr<Domains>NativeDebuggerDomains(){return native_debugger_domains_;}
  void NativeDebuggerRequestClose(const std::string&error){if(native_debugger_closing_requested_)return;native_debugger_closing_requested_=true;close_requests.push_back(error);}
#ifndef SUITECUT_DEVELOPER_ROOT_SOURCE_TEST_INCLUDE
#define SUITECUT_DEVELOPER_ROOT_SOURCE_TEST_INCLUDE "developer_debugger_source.inc"
#endif
#ifndef SUITECUT_DEVELOPER_DOCUMENTS_SOURCE_TEST_INCLUDE
#define SUITECUT_DEVELOPER_DOCUMENTS_SOURCE_TEST_INCLUDE "developer_debugger_documents_source.inc"
#endif
#include SUITECUT_DEVELOPER_ROOT_SOURCE_TEST_INCLUDE
#include SUITECUT_DEVELOPER_DOCUMENTS_SOURCE_TEST_INCLUDE
#include "developer_debugger_frames_source.inc"
  std::unique_ptr<suitecut::DeveloperDebuggerWorkerTargets>native_worker_developer_debuggers_;
  void NativeDeveloperWorkerCommand(int owner,Dict){Complete(owner,false,"Controlled frame-only host has no worker target");}
  Source(){
    Domains::Hooks hooks;hooks.now=[]{return Now();};hooks.send=[this](const auto&session,const auto&method,Dict params,double deadline,auto reply){return NativeDebuggerSend(session,method,params,deadline,std::move(reply));};
    hooks.close_channel=[this](const auto&,const auto&error){NativeDebuggerRequestClose(error);};
    native_debugger_domains_=std::make_shared<Domains>(std::move(hooks));
    native_frame_debugger_registering_=true;assert(NativeFrameDebuggerObserved());NativeFrameDebuggerMainObserved();
  }
  ~Source(){transport_closed=true;native_debugger_domains_->Closed();NativeDeveloperClosed();calls.clear();}
  void Default(const std::string&frame,const std::string&session,int context,const std::string&uid){
    frame_states_[frame]={frame,session,"actual-loader",uid,context};NativeFrameDebuggerDefaultObserved(frame_states_.at(frame));
    auto script=CefDictionaryValue::Create();script->SetString("scriptId","script-"+frame);script->SetInt("executionContextId",context);script->SetString("url","http://actual/"+frame+".js");
    native_coverage_inventory_.Observe("Debugger.scriptParsed",script,session);
  }
  void Start(int owner,const std::string&action,const std::string&frame="",const std::string&token="",double deadline=100){
    operations_.items.emplace(owner,std::make_shared<Operation>(Operation{++next_operation,deadline}));
    auto params=CefDictionaryValue::Create();params->SetString("action",action);if(!frame.empty())params->SetString("frame",frame);if(!token.empty())params->SetString("session",token);
    NativeDeveloperCommand(owner,params);
  }
  bool Has(const std::string&session,const std::string&method)const{return std::any_of(calls.begin(),calls.end(),[&](const auto&call){return call.session==session&&call.method==method;});}
  void Ack(const std::string&session,const std::string&method,bool ok=true,Dict result=nullptr){const auto at=std::find_if(calls.begin(),calls.end(),[&](const auto&call){return call.session==session&&call.method==method;});assert(at!=calls.end());auto reply=std::move(at->reply);calls.erase(at);reply(ok,result?result:CefDictionaryValue::Create(),ok?"":"controlled actual sender error");}
  void Ready(const std::string&session=""){Ack(session,"Debugger.enable");Ack(session,"Debugger.setSkipAllPauses");}
  std::string Token(int owner)const{const auto&result=results.at(owner);assert(result.ok);return JSON(result.json)->GetString("session").ToString();}
  void Close(int owner,const std::string&frame,const std::string&token,const std::string&session=""){
    Start(owner,"close",frame,token,0);if(Has(session,"Debugger.resume"))Ack(session,"Debugger.resume");if(Has(session,"Debugger.disable"))Ack(session,"Debugger.disable");assert(results.at(owner).ok);
  }
  void Pause(const std::string&frame,const std::string&session=""){
    auto packet=JSON(R"({"reason":"other","callFrames":[{"callFrameId":"actual-controlled-callframe","functionName":"author","location":{"scriptId":"","lineNumber":0},"scopeChain":[]}],"hitBreakpoints":[]})");
    packet->GetList("callFrames")->GetDictionary(0)->GetDictionary("location")->SetString("scriptId","script-"+frame);
    assert(native_debugger_domains_->Event("Debugger.paused",packet,session));
  }
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
 { // Eager root adoption preserves open-before-default without coverage ever
   // starting; the original metadata waiter allocates no invented lease.
   Source s;s.Start(1,"open");assert(s.calls.empty()&&!s.next_lease);
   s.Default("root","",7,"actual-root-unique");s.NativeDeveloperTick();s.Ready();assert(s.results.at(1).ok);
   s.Close(2,"",s.Token(1));assert(s.close_requests.empty());++groups;
 }
 { // Shared root/child channel excludes legacy root through physical disable.
   Source s;s.Default("root","",7,"root-unique");s.Default("child","",8,"child-unique");
   s.Start(1,"open");s.Ready();s.Start(2,"open","child");assert(!s.results.at(2).ok&&s.calls.empty());
   s.Start(3,"close","",s.Token(1),0);s.Start(4,"open","child");assert(!s.results.at(4).ok&&s.Has("","Debugger.disable"));s.Ack("","Debugger.disable");
   s.Start(5,"open","child");s.Ready();assert(s.results.at(5).ok);s.Close(6,"child",s.Token(5));++groups;
 }
 { // A genuine root author pause is foreign to the same-process child lease.
   Source s;s.Default("root","",7,"root-unique");s.Default("child","",8,"child-unique");s.Start(1,"open","child");s.Ready();
   s.Start(2,"waitForPause","child",s.Token(1));s.Pause("root");assert(!s.results.contains(2)&&s.Has("","Debugger.resume"));s.Ack("","Debugger.resume");
   s.Pause("child");assert(s.results.at(2).ok);s.Start(3,"pause","child",s.Token(1));assert(!s.results.at(3).ok&&s.calls.empty());
   s.Close(4,"child",s.Token(1));assert(s.close_requests.empty());++groups;
 }
 { // The independent actual OOPIF channel can coexist with a root lease.
   Source s;s.Default("root","",7,"root-unique");assert(s.NativeFrameDebuggerAttached("actual-iframe-session","","remote"));s.Default("remote","actual-iframe-session",8,"remote-unique");
   s.Start(1,"open");s.Ready();s.Start(2,"open","remote");s.Ready("actual-iframe-session");s.Start(3,"waitForPause","remote",s.Token(2));
   s.Pause("remote","actual-iframe-session");assert(s.results.at(3).ok);s.Close(4,"remote",s.Token(2),"actual-iframe-session");
   assert(s.native_developer_debugger_->Retained());s.Close(5,"",s.Token(1));assert(s.close_requests.empty());++groups;
 }
 { // Navigation invalidates captured child authority, not another document.
   Source s;s.Default("root","",7,"root-unique");s.Default("child","",8,"old-child");s.Start(1,"open","child");s.Ready();const auto old=s.Token(1);
   s.NativeFrameDebuggerFrameInvalidated("child","");s.Default("child","",8,"new-child");s.Start(2,"open","child");assert(!s.results.at(2).ok&&s.Has("","Debugger.disable"));s.Ack("","Debugger.disable");
   s.Start(3,"close","child",old,0);assert(s.results.at(3).ok&&s.calls.empty());s.Start(4,"open","child");s.Ready();assert(s.results.at(4).ok);s.Close(5,"child",s.Token(4));++groups;
 }
 { // Reject malformed raw ingress without a typed CEF GetString on the
   // wrong value kind, controller allocation, native lease or protocol work.
   Source s;s.Default("root","",7,"root-unique");s.Default("child","",8,"child-unique");
   s.operations_.items.emplace(1,std::make_shared<Source::Operation>(Source::Operation{1,100}));
   auto packet=CefDictionaryValue::Create();packet->SetString("frame","child");packet->SetInt("action",5);s.NativeDeveloperCommand(1,packet);
   assert(!s.results.at(1).ok&&!s.native_frame_developer_debuggers_&&!s.next_lease&&s.calls.empty()&&s.close_requests.empty());++groups;
 }
 { // Persistent actual factory-kind domain ownership keeps Debugger enabled.
   // Scoped root cleanup reenters Changed; breakpoint-close notification can
   // synchronously retire the developer lease without any disable command.
   Source s;s.Default("root","",7,"actual-root-unique");
   const auto factory=s.NativeDebuggerLease();bool acquired=false;
   s.native_debugger_domains_->Acquire("",factory,Source::Domains::Kind::Factory,100,[&](bool ok,const auto&){assert(ok);acquired=true;});
   s.Ack("","Debugger.enable");if(s.Has("","Debugger.setSkipAllPauses"))s.Ack("","Debugger.setSkipAllPauses");assert(acquired);
   s.Start(1,"open");if(s.Has("","Debugger.setSkipAllPauses"))s.Ack("","Debugger.setSkipAllPauses");assert(s.results.at(1).ok);
   const auto token=s.Token(1);s.Start(2,"close","",token,0);
   assert(s.results.at(2).ok&&s.calls.empty()&&!s.native_developer_debugger_->Retained()&&s.native_debugger_domains_->HasLease(factory));
   s.Start(3,"close","",token,0);assert(s.results.at(3).ok&&s.results.size()==3&&s.close_requests.empty());
   bool released=false;s.native_debugger_domains_->Release(factory,100,[&](bool ok,const auto&){assert(ok);released=true;});
   s.Ack("","Debugger.disable");assert(released&&!s.native_debugger_domains_->HasLease(factory));++groups;
 }
 { // Real root and worker controllers retain separate pending source replies.
   // The worker manager must accumulate AFTER root diagnostics assigns its own
   // counts. Controlled pins/transport here do not claim a running worker.
   std::string worker_token;Source s;s.Default("root","",7,"root-unique");
   s.Start(1,"open");s.Ready();
   using Controls=suitecut::DeveloperDebuggerControls;
   using Workers=suitecut::DeveloperDebuggerWorkerTargets;
   const Workers::Pin pin{{{s.handle_namespace_,4},"controlled-worker","worker-session","",1},9,"worker-unique",1};
   Workers::Hooks worker_hooks;worker_hooks.current=[pin](const auto&actual){return actual==pin;};
   worker_hooks.create=[&s,pin](const auto&){
     Controls::Hooks h;h.now=[]{return Now();};h.domains=s.NativeDebuggerDomains();
     h.lease=[&s]{return s.NativeDebuggerLease();};h.owner_namespace=s.handle_namespace_;
     h.inspector_session=pin.attachment.session;h.agent_controls=false;
     h.current=[pin]{return Controls::Document{pin.observation,pin.context,pin.unique_context};};
     h.target_pause_eligibility=[](Dict){return Source::Domains::DeveloperPauseDisposition::Unsupported;};
     h.owns_script=[](const auto&,const auto&){return true;};
     h.send=[&s,pin](const auto&m,Dict p,double d,auto reply){return s.NativeDebuggerSend(pin.attachment.session,m,p,d,std::move(reply));};
     return std::make_unique<Controls>(std::move(h));
   };
   s.native_worker_developer_debuggers_=std::make_unique<Workers>(std::move(worker_hooks));
   s.native_worker_developer_debuggers_->Open(100,pin,JSON(R"({"action":"open"})"),100,[&worker_token](bool ok,const auto&json,const auto&){assert(ok);worker_token=JSON(json)->GetString("session").ToString();});
   s.Ready("worker-session");assert(!worker_token.empty());
   s.operations_.items.emplace(2,std::make_shared<Source::Operation>(Source::Operation{++s.next_operation,100}));
   s.NativeDeveloperCommand(2,JSON("{\"action\":\"scriptSource\",\"session\":\""+s.Token(1)+"\",\"scriptId\":\"script-root\"}"));
   auto worker_request=JSON("{\"action\":\"scriptSource\",\"session\":\""+worker_token+"\",\"scriptId\":\"controlled-worker-script\"}");
   s.native_worker_developer_debuggers_->Handle(101,pin.attachment.target,s.handle_namespace_,"1",worker_request,100,[](bool ok,const auto&,const auto&){assert(ok);});
   assert(s.Has("","Debugger.getScriptSource")&&s.Has("worker-session","Debugger.getScriptSource"));
   auto diagnostics=CefDictionaryValue::Create();s.NativeDeveloperDiagnostics(diagnostics);
   assert(diagnostics->GetInt("nativeDeveloperDebuggerSessions")==2);
   assert(diagnostics->GetInt("nativeDeveloperDebuggerPhysical")==2);
   assert(diagnostics->GetInt("nativeWorkerDeveloperDebuggerLive")==1);
   s.Ack("","Debugger.getScriptSource",true,JSON(R"({"scriptSource":"root"})"));
   s.Ack("worker-session","Debugger.getScriptSource",true,JSON(R"({"scriptSource":"worker"})"));
   diagnostics=CefDictionaryValue::Create();s.NativeDeveloperDiagnostics(diagnostics);
   assert(diagnostics->GetInt("nativeDeveloperDebuggerSessions")==2&&diagnostics->GetInt("nativeDeveloperDebuggerPhysical")==0);
   ++groups;
 }
 std::cout<<"native included frame debugger Source adapter groups "<<groups<<" passed\n";
}
