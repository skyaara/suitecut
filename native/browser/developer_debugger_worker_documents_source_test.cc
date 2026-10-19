// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "developer_debugger_worker_documents.h"
using Dict=CefRefPtr<CefDictionaryValue>;
static Dict JSON(const std::string&text){const auto value=CefParseJSON(text,JSON_PARSER_RFC);assert(value);return value->GetDictionary();}
// Actual production observation include, real CEF value graphs and controlled
// accepted-target maps. No running browser or physical protocol ACK claim.
class Source{
 public:
  struct WorkerTarget{std::string id,session,parent_session,kind;int context=0;uint64_t generation=0;bool published=false,bootstrap_failed=false,context_destroyed=false;};
  std::map<std::string,WorkerTarget>worker_targets_;
  std::map<std::string,std::string>worker_sessions_;
  std::string handle_namespace_="controlled-Source";
  uint64_t native_frame_debugger_observer_=4;
  bool observer_=true,native_frame_debugger_registering_=false,native_frame_debugger_registration_traffic_=false,closing_page_=false,native_debugger_closing_requested_=false;
#include "developer_debugger_worker_documents_source.inc"
  Source(){NativeWorkerDebuggerObserved();}
  void Attach(uint64_t generation=1,const std::string&session="actual-session",const std::string&kind="worker"){
    const std::string target="actual-target";worker_targets_[target]={target,session,"actual-parent",kind,0,generation,false,false,false};worker_sessions_[session]=target;
    auto data=JSON(R"({"sessionId":"actual-session","targetInfo":{"targetId":"actual-target","type":"worker"}})");data->SetString("sessionId",session);data->GetDictionary("targetInfo")->SetString("type",kind);NativeWorkerDebuggerAttached(data,generation-1);
  }
  Dict Context(const std::string&unique="actual-runtime-unique"){auto data=JSON(R"({"context":{"id":7,"uniqueId":"actual-runtime-unique"}})");data->GetDictionary("context")->SetString("uniqueId",unique);return data;}
  suitecut::DeveloperDebuggerWorkerDocuments::Pin Default(){NativeWorkerDebuggerRuntime("Runtime.executionContextCreated",Context(),"actual-session");worker_targets_.at("actual-target").context=7;worker_targets_.at("actual-target").published=true;const auto attachment=NativeWorkerDebuggerAttachment("actual-session");assert(attachment);suitecut::DeveloperDebuggerWorkerDocuments::Pin pin;assert(native_worker_debugger_documents_->Resolve(*attachment,pin));return pin;}
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));size_t groups=0;
 {Source s;s.Attach();const auto pin=s.Default();assert(s.NativeWorkerDebuggerPinCurrent(pin)&&!s.native_worker_debugger_metadata_error_);++groups;}
 {Source s;s.Attach();const auto pin=s.Default();s.worker_targets_.at("actual-target").published=false;assert(!s.NativeWorkerDebuggerPinCurrent(pin));s.worker_targets_.at("actual-target").published=true;s.worker_targets_.at("actual-target").bootstrap_failed=true;assert(!s.NativeWorkerDebuggerPinCurrent(pin));++groups;}
 {Source s;s.Attach();const auto pin=s.Default();s.NativeWorkerDebuggerRuntime("Runtime.executionContextCreated",s.Context("actual-replacement-unique"),"actual-session");assert(!s.NativeWorkerDebuggerPinCurrent(pin));++groups;}
 {Source s;s.Attach();const auto pin=s.Default();auto data=JSON(R"({"executionContextId":8})");s.NativeWorkerDebuggerRuntime("Runtime.executionContextDestroyed",data,"actual-session");assert(s.NativeWorkerDebuggerPinCurrent(pin));data->SetInt("executionContextId",7);s.NativeWorkerDebuggerRuntime("Runtime.executionContextDestroyed",data,"actual-session");assert(!s.NativeWorkerDebuggerPinCurrent(pin));++groups;}
 {Source s;s.Attach();const auto pin=s.Default();auto data=s.Context("excluded-world");data->GetDictionary("context")->SetDictionary("auxData",JSON(R"({"isDefault":false})"));s.NativeWorkerDebuggerRuntime("Runtime.executionContextCreated",data,"actual-session");assert(s.NativeWorkerDebuggerPinCurrent(pin));s.NativeWorkerDebuggerRuntime("Runtime.executionContextsCleared",CefDictionaryValue::Create(),"actual-session");assert(!s.NativeWorkerDebuggerPinCurrent(pin));++groups;}
 {Source s;s.Attach();const auto pin=s.Default();auto data=s.Context();data->GetDictionary("context")->Remove("uniqueId");s.NativeWorkerDebuggerRuntime("Runtime.executionContextCreated",data,"actual-session");assert(!s.NativeWorkerDebuggerPinCurrent(pin)&&s.native_worker_debugger_metadata_error_);++groups;}
 {Source s;s.Attach();const auto pin=s.Default();const auto old=s.NativeWorkerDebuggerChannel("actual-session");assert(old.generation==1&&old.observer==4);s.worker_sessions_.clear();s.worker_targets_.clear();s.NativeWorkerDebuggerChannelDrained("actual-session",old);assert(!s.native_worker_debugger_documents_->Records()&&!s.NativeWorkerDebuggerPinCurrent(pin));s.Attach(2);const auto replacement=s.Default();s.NativeWorkerDebuggerChannelDrained("actual-session",old);assert(s.NativeWorkerDebuggerPinCurrent(replacement));++groups;}
 {Source s;s.Attach();const auto pin=s.Default();auto wrong=s.NativeWorkerDebuggerChannel("actual-session");wrong.observer++;s.NativeWorkerDebuggerChannelDrained("actual-session",wrong);assert(s.NativeWorkerDebuggerPinCurrent(pin));s.closing_page_=true;assert(!s.NativeWorkerDebuggerPinCurrent(pin));s.NativeWorkerDebuggerClosed();assert(!s.native_worker_debugger_documents_->Available()&&!s.native_worker_debugger_documents_->Records());++groups;}
 {Source s;s.Attach(1,"actual-session","service_worker");assert(!s.NativeWorkerDebuggerAttachment("actual-session")&&!s.native_worker_debugger_documents_->Records());++groups;}
 {Source s;s.Attach();const auto pin=s.Default();auto data=JSON(R"({"sessionId":"actual-session","targetInfo":{"targetId":"actual-target","type":"worker"}})");s.NativeWorkerDebuggerAttached(data,1);assert(s.NativeWorkerDebuggerPinCurrent(pin)&&!s.native_worker_debugger_metadata_error_);++groups;}
 {Source s;s.native_worker_debugger_documents_.reset();s.native_frame_debugger_registering_=true;s.NativeWorkerDebuggerObserved();assert(!s.native_worker_debugger_documents_&&s.native_worker_debugger_metadata_error_);++groups;}
 std::cout<<"native included worker debugger Source metadata "<<groups<<" groups passed\n";
}
