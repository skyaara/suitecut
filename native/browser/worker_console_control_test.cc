// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "operation_registry.h"
#include "devtools_utf16.h"
#include "console_journal.h"

// Controlled command/scheduling seams exercise the real owned native module.
// Actual worker author execution, contexts and browser lifecycle need live proof.
namespace {
double clock_value = 100;
double Now() { return clock_value; }
std::deque<CefRefPtr<CefTask>> scheduled;
class Task : public CefTask {
 public:
  explicit Task(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override { fn_(); }
 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(Task);
};
bool Post(cef_thread_id_t thread,CefRefPtr<CefTask> task) { assert(thread==TID_UI);scheduled.push_back(task);return true; }
bool PostDelayed(cef_thread_id_t thread,CefRefPtr<CefTask> task,int64_t delay) { assert(delay==10);return Post(thread,task); }
void RunOne() { assert(!scheduled.empty());auto task=scheduled.front();scheduled.pop_front();task->Execute(); }
}
#define CefPostTask Post
#define CefPostDelayedTask PostDelayed
class Source : public CefBaseRefCounted {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using DevToolsCallback=std::function<void(bool,const std::string&)>;
  struct DevToolsCall {int owner;DevToolsCallback callback;std::string session,method;Dict params;bool retain_after_cancel;};
  struct Raw {int id;std::string method,session;Dict params;};
  struct Completion {int id;bool ok;std::string error,json;};
  struct Network {
    std::vector<std::function<void(bool,const std::string&)>> ready;
    std::set<std::string> sessions;
    std::vector<std::vector<std::string>> script_handoffs;
    void SessionAttached(const std::string& session,std::function<void(bool,const std::string&)> callback,bool fetch_available=true) {assert(!fetch_available);sessions.insert(session);ready.push_back(std::move(callback));}
    bool WorkerScriptAttached(const std::string& target,const std::string& parent,const std::string& child,const std::string& url) {script_handoffs.push_back({target,parent,child,url});return false;}
    void SessionDetached(const std::string& session) {sessions.erase(session);}
  };
  SuiteCutOperationRegistry operations_;
  bool browser_=true,closing_page_=false;
  std::string handle_namespace_="controlled-worker-source";
  uint64_t event_sequence_=0;
  std::map<int,std::string> event_waiters_,operation_sessions_;
  std::unique_ptr<Network> network_=std::make_unique<Network>();
  std::vector<Raw> raw;
  std::vector<Dict> events;
  std::vector<Completion> completions;
  std::vector<std::string> errors;
  std::set<std::string> remote_groups;
  int next_call=0,next_handle_=0;
  std::vector<std::vector<std::string>> legacy_handoffs;
  void NativeWorkerInitialRequestSession(const std::string& target,const std::string& parent,const std::string& child,const std::string& url) {legacy_handoffs.push_back({target,parent,child,url});}
  std::map<int,int> handles_,pending_handle_groups_;
  size_t console_argument_bytes_=0;
  SuiteCutConsoleJournal console_journal_;
  std::string ConsolePreview(Dict value) { return value->GetString("description"); }
  static std::string Json(Dict dict) {auto value=CefValue::Create();value->SetDictionary(dict);return CefWriteJSON(value,JSON_WRITER_DEFAULT);}
  void Admit(int id,double timeout=1000) {assert(operations_.Begin(id,Now(),timeout));}
  int AllocateDevToolsId() {return ++next_call;}
  bool SubmitDevTools(int id,const std::string& method,Dict params,const std::string& session) {raw.push_back({id,method,session,params?params->Copy(false):nullptr});return true;}
  int SendDevTools(const std::string& method,Dict params=nullptr,const std::string& session="") {
    if(method=="Runtime.releaseObjectGroup")remote_groups.erase(params->GetString("objectGroup"));
    return SubmitDevTools(++next_call,method,params,session)?next_call:0;
  }
  void DevTools(int owner,const std::string& method,Dict params,DevToolsCallback callback,std::optional<std::string> session=std::nullopt,bool retained=false,size_t limit=16*1048576,bool lossless_scalars=false) {
    assert(operations_.Contains(owner)&&limit<=16*1048576);
    assert(!lossless_scalars||method=="Runtime.getProperties");
    devtools_.emplace(++next_call,DevToolsCall{owner,std::move(callback),session.value_or(operation_sessions_[owner]),method,params,retained});
  }
  void WakeDeadlines() {}
  void AutoAttach(const std::string& session) {SendDevTools("Target.setAutoAttach",nullptr,session);}
  void Error(const char* error) {errors.push_back(error);}
  Dict Header(const char* type) {auto header=CefDictionaryValue::Create();header->SetString("type",type);return header;}
  void JournalEvent(uint64_t,const std::string&,const std::string&) {}
  void SendPacket(Dict header,const std::vector<uint8_t>& payload) {
    assert(header->GetString("type")=="worker");auto value=CefParseJSON(std::string(payload.begin(),payload.end()),JSON_PARSER_RFC);events.push_back(value->GetDictionary());
  }
  void Complete(int id,bool ok,const std::string& error="",const std::string& json="") {
    if(!operations_.Finish(id))return;
    WorkerCancel(id);completions.push_back({id,ok,error,json});event_waiters_.erase(id);operation_sessions_.erase(id);
    for(auto it=devtools_.begin();it!=devtools_.end();)if(it->second.owner==id&&!it->second.retain_after_cancel)it=devtools_.erase(it);else++it;
  }
  void AckBoot(const std::string& method,const std::string& session,const std::string& json="{}",bool ok=true) {
    auto found=std::find_if(raw.begin(),raw.end(),[&](const Raw& entry){return entry.method==method&&entry.session==session&&worker_calls_.contains(entry.id);});
    assert(found!=raw.end());const int id=found->id;assert(WorkerToolResult(id,ok,json.data(),json.size(),session));
  }
  void Ack(const std::string& method,const std::string& json) {
    auto found=std::find_if(devtools_.begin(),devtools_.end(),[&](const auto& entry){return entry.second.method==method;});assert(found!=devtools_.end());
    auto call=std::move(found->second);devtools_.erase(found);
    if(call.params->HasKey("objectGroup")){if(call.method=="Runtime.releaseObjectGroup")remote_groups.erase(call.params->GetString("objectGroup"));else remote_groups.insert(call.params->GetString("objectGroup"));}
    call.callback(true,json);
  }
  struct LifetimeCall {std::string method;Dict params;double deadline;std::function<void(bool,const std::string&)> done;};
  std::vector<LifetimeCall> lifetime_calls;
  int lifetime_ready=0;
  void NativeNetworkSend(const std::string& session,const std::string& method,Dict params,std::function<void(bool,const std::string&)> done,double deadline){assert(session.empty());lifetime_calls.push_back({method,params->Copy(false),deadline,std::move(done)});}
  void AckLifetime(bool ok=true){assert(lifetime_calls.size()==1);auto call=std::move(lifetime_calls.front());lifetime_calls.clear();call.done(ok,ok?"{}":"Actual controlled discovery rejection");}
  void NetworkFatal(const std::string& error){Error(error.c_str());}
  void MaybeReady(){++lifetime_ready;}
  void NativeTargetSessionDetached(const std::string& session){WorkerDetached(session);}
  #include "controllers/workers/worker_controller.inc"
  #include "controllers/workers/worker_target_lifetime.inc"
#ifdef SUITECUT_CONTEXT_WORKER_CONSOLE_REFERENCE_TEST
  #include "controllers/workers/context_worker_console_refs.inc"
#else
  #include "controllers/workers/worker_console.inc"
#endif
  // Declared after physical latch state so callbacks die before that state.
  std::map<int,DevToolsCall> devtools_;
 private:
  IMPLEMENT_REFCOUNTING(Source);
};
#undef CefPostTask
#undef CefPostDelayedTask
namespace {
using Dict=Source::Dict;
Dict Parse(const std::string& json) {return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
Dict Request(const char* action,const char* worker="worker-A") {auto params=CefDictionaryValue::Create();params->SetString("action",action);params->SetString("worker",worker);return params;}
void Attach(CefRefPtr<Source> source,const char* kind="worker",const char* id="worker-A",const char* session="worker-session-A") {
  auto data=CefDictionaryValue::Create(),info=CefDictionaryValue::Create();info->SetString("type",kind);info->SetString("targetId",id);info->SetString("url","http://fixture.test/worker.js");data->SetDictionary("targetInfo",info);data->SetString("sessionId",session);
  assert(source->WorkerAttached(data,"actual-oopif-parent"));
  const std::vector<std::string> actual_metadata{id,"actual-oopif-parent",session,"http://fixture.test/worker.js"};
  assert(source->legacy_handoffs.size()==1&&source->legacy_handoffs.back()==actual_metadata);
  assert(source->network_->script_handoffs.size()==1&&source->network_->script_handoffs.back()==actual_metadata);
  RunOne();
  source->AckBoot("Runtime.enable",session);source->AckBoot("Network.enable",session);
  assert(source->network_->ready.size()==1&&!source->worker_targets_.at(id).resumed);
  auto ready=std::move(source->network_->ready.front());source->network_->ready.clear();ready(true,"");
  // Resume must not depend on a context that only author startup can create.
  assert(source->worker_targets_.at(id).resumed&&source->worker_targets_.at(id).context==0);
  source->AckBoot("Runtime.runIfWaitingForDebugger",session);assert(source->events.empty());
  assert(source->WorkerRuntimeEvent("Runtime.executionContextCreated",Parse("{\"context\":{\"id\":17}}"),session));
  assert(source->events.empty());assert(source->WorkerRuntimeEvent("Inspector.workerScriptLoaded",CefDictionaryValue::Create(),session));
  assert(source->events.back()->GetString("event")=="worker");
}
void CheckConsoleGenerationOwnership() {
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);
    const auto metadata=source->WorkerConsoleRetain(Parse("{\"type\":\"object\",\"objectId\":\"genuine-worker-global\",\"description\":\"DedicatedWorkerGlobalScope\"}"),"worker-session-A",17);
    assert(metadata&&metadata->GetString("worker")=="worker-A");const int handle=metadata->GetInt("handle");
    auto control=metadata->GetDictionary("workerControl");assert(control&&control->GetString("owner")==source->handle_namespace_&&control->GetString("generation")==std::to_string(source->worker_targets_.at("worker-A").generation));

    assert(source->worker_handles_.size()==1&&source->worker_console_arguments_.size()==1);
    // A genuine handle index in this registry must not authorize a caller's
    // other physical Inspector owner, even when target IDs/generations match.
    auto wrong=Request("dispose");wrong->SetInt("handle",handle);
    auto expected=source->WorkerControlInfo(source->worker_targets_.at("worker-A"));expected->SetString("owner","other-physical-owner");wrong->SetDictionary("workerControl",expected);
    const auto calls=source->raw.size(),pending=source->devtools_.size();source->Admit(777);source->WorkerDispatch(777,wrong);
    assert(!source->completions.back().ok&&source->worker_handles_.size()==1&&source->worker_console_arguments_.size()==1&&source->raw.size()==calls&&source->devtools_.size()==pending);
    auto old=Request("property");old->SetInt("handle",handle);old->SetString("name","origin");
    expected=source->WorkerControlInfo(source->worker_targets_.at("worker-A"));expected->SetString("generation","18446744073709551615");old->SetDictionary("workerControl",expected);
    source->Admit(778);source->WorkerDispatch(778,old);assert(!source->completions.back().ok&&source->worker_console_arguments_.size()==1&&source->raw.size()==calls&&source->devtools_.size()==pending);
    auto malformed=Request("dispose");malformed->SetInt("handle",handle);malformed->SetString("workerControl","not-a-control-object");
    source->Admit(779);source->WorkerDispatch(779,malformed);assert(!source->completions.back().ok&&source->worker_handles_.size()==1&&source->worker_console_arguments_.size()==1&&source->raw.size()==calls);
    source->WorkerConsoleRuntimeEvent("Runtime.executionContextDestroyed",Parse("{\"executionContextId\":999}"),"worker-session-A");assert(source->worker_console_arguments_.size()==1);
    source->Admit(100,35);auto request=Request("property");request->SetInt("handle",handle);request->SetString("name","origin");request->SetDictionary("workerControl",source->WorkerControlInfo(source->worker_targets_.at("worker-A")));source->WorkerDispatch(100,request);
    assert(source->operations_.Get(100)->deadline_ms==135&&source->worker_physical_==1);
    source->Complete(100,false,"actual cancel");source->Admit(100,35);source->WorkerDispatch(100,request);
    assert(source->worker_physical_==2);
    source->Ack("Runtime.callFunctionOn","{\"result\":{\"type\":\"object\",\"objectId\":\"late-wrapper\"}}");
    assert(source->worker_console_arguments_.size()==1&&source->worker_handles_.at(handle).object.empty()&&source->worker_physical_==1&&source->operations_.Contains(100));
    source->Ack("Runtime.callFunctionOn","{\"result\":{\"type\":\"object\",\"objectId\":\"live-wrapper\"}}");
    assert(source->worker_console_arguments_.empty()&&source->worker_handles_.at(handle).object=="live-wrapper"&&source->worker_physical_==1);
    source->Ack("Runtime.callFunctionOn","{\"result\":{\"type\":\"object\",\"objectId\":\"property-wrapper\"}}");
    source->Ack("Runtime.getProperties","{\"result\":[{\"name\":\"value\",\"value\":{\"type\":\"string\",\"value\":\"null\"}}]}");
    assert(source->worker_physical_==0&&source->worker_handles_.size()==2&&source->completions.back().ok);
    source->WorkerConsoleClear(true);source->WorkerClear();assert(source->worker_handles_.empty());
  }
}
void CheckConsoleObservedRetirement() {
  auto source=CefRefPtr<Source>(new Source());source->worker_context_channel_=true;
  assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"shared_worker","targetId":"console-pre-ready","url":"http://fixture.test/shared.js"},"sessionId":"console-pre-ready-channel"})"),""));RunOne();
  assert(!source->worker_targets_.at("console-pre-ready").published&&source->events.empty());
  auto metadata=CefDictionaryValue::Create();source->WorkerConsoleMetadata(metadata,"console-pre-ready-channel");
  assert(metadata->GetString("worker")=="console-pre-ready"&&metadata->GetDictionary("workerInfo")->GetDictionary("control"));
  assert(source->worker_targets_.at("console-pre-ready").observed&&source->events.empty());
  const auto generation=source->worker_targets_.at("console-pre-ready").generation;
  source->WorkerBootstrapFailed("console-pre-ready",generation,"Actual controlled setup rejection");assert(source->events.empty()&&source->worker_targets_.size()==1);
  source->WorkerConsoleSessionRemoved("console-pre-ready-channel");assert(source->WorkerDetached("console-pre-ready-channel"));
  assert(source->events.size()==1&&source->events.back()->GetString("event")=="channelclosed");
  assert(!source->WorkerDetached("console-pre-ready-channel")&&source->events.size()==1);
  source->WorkerClear();assert(source->worker_targets_.empty()&&source->worker_calls_.empty());
}
}
int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  CheckConsoleObservedRetirement();
#ifdef SUITECUT_CONTEXT_WORKER_CONSOLE_REFERENCE_TEST
  CheckConsoleGenerationOwnership();
#else
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->Admit(1);
    auto request=Request("create");request->SetString("expression","({actual:42})");source->WorkerRun(1,request);
    assert(source->worker_physical_==1&&source->worker_handles_.empty());
    source->Ack("Runtime.evaluate","{\"result\":{\"type\":\"object\",\"objectId\":\"actual-wrapper\"}}");
    assert(source->worker_physical_==1);source->Ack("Runtime.getProperties","{\"result\":[{\"name\":\"value\",\"value\":{\"type\":\"object\",\"description\":\"Object\",\"objectId\":\"actual-value\"}}]}");
    assert(source->worker_physical_==0&&source->worker_handles_.size()==1&&source->remote_groups.size()==1&&source->completions.back().ok);
    source->Admit(2);auto dispose=Request("dispose");dispose->SetInt("handle",source->worker_handles_.begin()->first);source->WorkerRun(2,dispose);
    source->Ack("Runtime.releaseObjectGroup","{}");assert(source->worker_handles_.empty()&&source->remote_groups.empty());source->WorkerClear();
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->Admit(1,35);const auto original=source->operations_.Get(1);
    auto request=Request("create");request->SetString("expression","new Promise(()=>{})");source->WorkerRun(1,request);
    source->Complete(1,false,"cancel");assert(source->worker_physical_==1&&source->worker_work_.empty()&&source->worker_handles_.empty());
    source->Admit(1,35);assert(source->operations_.Get(1)->generation!=original->generation);
    auto current=Request("evaluate");current->SetString("expression","42");source->WorkerRun(1,current);assert(source->worker_physical_==2);
    source->Ack("Runtime.evaluate","{\"result\":{\"type\":\"object\",\"objectId\":\"late-wrapper\"}}");
    assert(source->worker_physical_==1&&source->worker_handles_.empty()&&source->remote_groups.empty()&&source->operations_.Contains(1));
    assert(source->completions.size()==1&&source->worker_work_.at(1).generation==source->operations_.Get(1)->generation);
    source->Ack("Runtime.evaluate","{\"result\":{\"value\":42}}");assert(source->worker_physical_==0&&source->completions.size()==2&&source->completions.back().ok);
    source->WorkerClear();
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);
    for(int i=1;i<=64;++i){source->Admit(i);auto request=Request("evaluate");request->SetString("expression","new Promise(()=>{})");source->WorkerRun(i,request);source->Complete(i,false,"cancel");}
    assert(source->worker_physical_==64);
    source->Admit(65);auto request=Request("evaluate");request->SetString("expression","1");source->WorkerRun(65,request);
    assert(!source->completions.back().ok&&source->worker_physical_==64);
    assert(source->WorkerDetached("worker-session-A"));assert(source->worker_physical_==0&&source->devtools_.empty()&&source->worker_handles_.empty());
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->Admit(1,35);
    auto request=Request("waitClose");source->WorkerRun(1,request);assert(source->operations_.Get(1)->deadline_ms==135);
    source->NativeWorkerTargetDiscoveryStart();source->AckLifetime();
    assert(source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"worker-A"})")));assert(source->completions.back().ok&&source->events.back()->GetString("event")=="close");
    assert(!source->WorkerDetached("worker-session-A"));
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source,"shared_worker");source->Admit(1);
    source->WorkerRun(1,Request("waitClose"));source->WorkerDetached("worker-session-A");
    assert(!source->completions.back().ok&&source->events.back()->GetString("event")=="channelclosed");
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->Admit(1);
    auto request=Request("evaluate");request->SetString("expression","new Promise(()=>{})");source->WorkerRun(1,request);
    assert(source->WorkerRuntimeEvent("Runtime.executionContextsCleared",CefDictionaryValue::Create(),"worker-session-A"));
    assert(!source->completions.back().ok&&source->worker_work_.empty()&&source->worker_handles_.empty());
    source->WorkerClear();assert(source->worker_physical_==0);
  }
  {
    auto source=CefRefPtr<Source>(new Source());
    auto data=Parse("{\"targetInfo\":{\"type\":\"worker\",\"targetId\":\"unready\",\"url\":\"\"},\"sessionId\":\"unready-session\"}");source->WorkerAttached(data,"");RunOne();clock_value=30100;source->WorkerTick();
    assert(source->worker_targets_.size()==1&&source->worker_targets_.at("unready").bootstrap_failed&&!source->errors.empty());
    source->WorkerClear();assert(source->worker_targets_.empty());
    assert(std::any_of(source->raw.begin(),source->raw.end(),[](const Source::Raw& raw){return raw.method=="Runtime.runIfWaitingForDebugger";}));
    clock_value=100;
  }
  CheckConsoleGenerationOwnership();
  {
    auto source=CefRefPtr<Source>(new Source());auto invalid=Parse("{\"targetInfo\":{\"type\":\"worker\",\"targetId\":\"\",\"url\":\"http://fixture.test/worker.js\"},\"sessionId\":\"invalid-session\"}");
    assert(source->WorkerAttached(invalid,"actual-parent"));assert(source->legacy_handoffs.empty()&&source->network_->script_handoffs.empty()&&source->worker_targets_.empty());
  }

  {
    auto source=CefRefPtr<Source>(new Source());
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"service_worker","targetId":"console-observed","url":"http://fixture.test/sw.js"},"sessionId":"console-early-session"})"),""));RunOne();
    auto info=CefDictionaryValue::Create();source->WorkerConsoleMetadata(info,"console-early-session");
    assert(info->GetString("worker")=="console-observed"&&source->events.empty());
    const auto generation=source->worker_targets_.at("console-observed").generation;
    source->WorkerBootstrapFailed("console-observed",generation,"actual console setup failure");
    assert(source->events.empty()&&source->worker_targets_.contains("console-observed"));
    source->WorkerClear();assert(source->worker_targets_.empty()&&source->worker_calls_.empty());
    assert(source->events.size()==1&&source->events.back()->GetString("event")=="channelclosed");
  }
#endif
  assert(scheduled.empty());
}
