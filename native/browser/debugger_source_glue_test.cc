// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <string>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "coverage_controls.h"
#include "debugger_domains.h"
#include "network_fatal_teardown.h"
#include <deque>
namespace {constexpr size_t kMaxResponseBytes=192*1048576;double clock_value=1;double Now(){return clock_value;}}
class Source {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using DevToolsCallback=std::function<void(bool,const std::string&)>;
  struct DevToolsCall {int owner;DevToolsCallback callback;std::string session;bool retain_after_cancel;size_t response_limit;bool retained_control,lossless_scalars;};
  struct Host {size_t closed=0;void CloseBrowser(bool force){assert(force);++closed;}};
  struct Browser {Host host;Host* GetHost(){return &host;}};
  struct Command {int id;std::string session,method;Dict params;};
  std::shared_ptr<Browser> browser_=std::make_shared<Browser>();bool closing_page_=false,accept=true;std::string closed_reason_;size_t wakes=0;int next=0;
  std::map<int,DevToolsCall> devtools_;std::vector<Command> wire;
  int AllocateDevToolsId(){return ++next;}
  bool SubmitDevTools(int id,const std::string& method,Dict params,const std::string& session){if(!accept)return false;wire.push_back({id,session,method,params?params->Copy(false):nullptr});return true;}
  std::deque<std::function<void()>> posted;
  std::unique_ptr<suitecut::NativeNetworkFatalTeardown> fatal;
  void NetworkFatal(const std::string& error){
    if(!fatal)fatal=std::make_unique<suitecut::NativeNetworkFatalTeardown>(
      [this](std::function<void()> task){posted.push_back(std::move(task));return true;},
      [this](const std::string& error){if(closed_reason_.empty())closed_reason_=error;},
      [this](){browser_->GetHost()->CloseBrowser(true);});
    fatal->Fail(error);WakeDeadlines();
  }
  void DrainPosted(){while(!posted.empty()){auto task=std::move(posted.front());posted.pop_front();task();}}
  void WakeDeadlines(){++wakes;}
  void NativeTargetSessionDetached(const std::string& session){NativeDebuggerDetached(session);}
#include "native_debugger_controller.inc"
  void Ack(const std::string& method,bool ok=true,const std::string& result="{}"){
    assert(!wire.empty()&&wire.front().method==method);auto command=wire.front();wire.erase(wire.begin());auto found=devtools_.find(command.id);assert(found!=devtools_.end());auto callback=std::move(found->second.callback);devtools_.erase(found);callback(ok,result);
  }
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  {Source source;size_t ready=0,released=0;assert(source.NativeDebuggerCoverageAcquire("actual",100,[&](bool ok,auto,const auto&){assert(ok);++ready;}));assert(ready==0&&source.native_debugger_calls_.size()==1);source.Ack("Debugger.enable");source.Ack("Debugger.setSkipAllPauses");assert(ready==1&&source.native_debugger_calls_.empty());assert(source.NativeDebuggerCoverageRelease("actual",200,[&](bool ok,auto,const auto&){assert(ok);++released;}));assert(released==0);source.Ack("Debugger.disable");assert(released==1&&source.native_coverage_debugger_leases_.empty());source.NativeDebuggerClosed();}
  {Source source;source.accept=false;size_t callbacks=0;assert(source.NativeDebuggerSend("actual","Debugger.enable",nullptr,100,[&](bool ok,auto,const auto&){assert(!ok);++callbacks;}));assert(callbacks==1&&source.native_debugger_calls_.empty()&&source.devtools_.empty());}
  {Source source;size_t failed=0;source.NativeDebuggerCoverageAcquire("actual",100,[&](bool ok,auto,const auto&){assert(!ok);++failed;});source.Ack("Debugger.enable",false,"{\"message\":\"Session with given id not found.\"}");assert(failed==1&&source.native_debugger_domains_->PhysicalCount()==0&&source.native_debugger_calls_.empty()&&source.devtools_.empty());}
  {Source source;size_t callbacks=0;assert(source.NativeDebuggerSend("actual","Runtime.evaluate",nullptr,100,[&](bool ok,auto,const auto&){assert(ok);++callbacks;}));assert(source.native_debugger_calls_.size()==1);source.Ack("Runtime.evaluate");assert(callbacks==1&&source.native_debugger_calls_.empty());clock_value=101;assert(!source.NativeDebuggerSend("actual","Runtime.evaluate",nullptr,100,[](bool,auto,const auto&){assert(false);}));clock_value=1;}
  {Source source;size_t callbacks=0;assert(source.NativeDebuggerSend("actual","Debugger.getScriptSource",nullptr,10,[&](bool ok,auto,const auto&){assert(ok);++callbacks;}));assert(source.NativeDebuggerNeedsTick());clock_value=11;source.NativeDebuggerTick();assert(callbacks==0&&source.browser_->host.closed==0&&source.native_debugger_calls_.size()==1);clock_value=30010;source.NativeDebuggerTick();assert(callbacks==0&&source.browser_->host.closed==0&&source.posted.size()==1&&source.native_debugger_calls_.size()==1&&source.devtools_.size()==1&&!source.NativeDebuggerNeedsTick());source.NativeDebuggerTick();assert(source.posted.size()==1);source.DrainPosted();assert(source.browser_->host.closed==1&&source.native_debugger_calls_.size()==1&&source.devtools_.size()==1);source.Ack("Debugger.getScriptSource");assert(callbacks==1&&source.native_debugger_calls_.empty());clock_value=1;}
  {Source source;size_t callbacks=0;assert(source.NativeDebuggerSend("actual","Runtime.evaluate",nullptr,10,[&](bool ok,auto,const auto&){assert(!ok);++callbacks;}));clock_value=30010;source.NativeDebuggerTick();assert(source.native_debugger_calls_.size()==1);source.NativeDebuggerDetached("actual");assert(callbacks==1&&source.native_debugger_calls_.empty()&&source.devtools_.empty());clock_value=1;}

  {Source source;source.accept=false;auto domains=source.NativeDebuggerDomains();auto raw=domains->BeginRaw("actual","Debugger.enable",100);assert(raw);size_t callbacks=0;source.NativeDebuggerRawSend(42,raw,"actual","Debugger.enable",nullptr,100,[&](bool ok,const std::string&){assert(!ok);domains->EndRaw(raw,ok);++callbacks;});assert(callbacks==1&&source.devtools_.empty()&&domains->PhysicalCount()==0&&!domains->NeedsTick());size_t acquired=0;source.accept=true;domains->Acquire("actual",1,suitecut::DebuggerDomains::Kind::Coverage,100,[&](bool ok,const std::string&){assert(ok);++acquired;});source.Ack("Debugger.enable");source.Ack("Debugger.setSkipAllPauses");assert(acquired==1);source.NativeDebuggerClosed();}
  {Source source;auto domains=source.NativeDebuggerDomains();auto raw=domains->BeginRaw("actual","Debugger.enable",10);assert(raw);clock_value=11;size_t callbacks=0;source.NativeDebuggerRawSend(42,raw,"actual","Debugger.enable",nullptr,10,[&](bool ok,const std::string&){assert(!ok);domains->EndRaw(raw,ok);++callbacks;});assert(callbacks==1&&source.wire.empty()&&domains->PhysicalCount()==0);clock_value=1;}

  {Source source;auto domains=source.NativeDebuggerDomains();auto raw=domains->BeginRaw("actual","Debugger.enable",100);assert(raw);size_t callbacks=0;source.NativeDebuggerRawSend(42,raw,"actual","Debugger.enable",nullptr,100,[&](bool ok,const std::string&){assert(!ok);++callbacks;});source.Ack("Debugger.enable",false,"{\"message\":\"actual backend error\"}");assert(callbacks==1&&domains->PhysicalCount()==0);size_t failures=0;domains->Acquire("actual",1,suitecut::DebuggerDomains::Kind::Coverage,100,[&](bool ok,const std::string&){assert(!ok);++failures;});assert(failures==1);source.NativeDebuggerClosed();}

  {Source source;size_t failed=0,retained=0;source.NativeDebuggerSend("actual","Runtime.evaluate",nullptr,100,[&](bool ok,auto,const auto&){assert(!ok);++failed;source.NativeDebuggerRequestClose("actual callback failure");});source.NativeDebuggerSend("actual","Debugger.getScriptSource",nullptr,100,[&](bool ok,auto,const auto&){assert(ok);++retained;});source.Ack("Runtime.evaluate",false,"{\"message\":\"actual backend failure\"}");assert(failed==1&&retained==0&&source.browser_->host.closed==0&&source.posted.size()==1&&source.native_debugger_calls_.size()==1&&source.devtools_.size()==1);source.DrainPosted();assert(source.browser_->host.closed==1&&source.native_debugger_calls_.size()==1);source.Ack("Debugger.getScriptSource");assert(retained==1&&source.native_debugger_calls_.empty());source.NativeDebuggerClosed();}

}
