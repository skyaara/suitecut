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
#include "context_worker_cursor.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "operation_registry.h"
#include "devtools_utf16.h"

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
    std::map<std::string,bool> fetch_support;
    std::vector<std::vector<std::string>> script_handoffs;
    void SessionAttached(const std::string& session,std::function<void(bool,const std::string&)> callback,bool fetch_available=true) {fetch_support[session]=fetch_available;sessions.insert(session);ready.push_back(std::move(callback));}
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
  size_t wake_receipts=0;
  std::vector<std::vector<std::string>> legacy_handoffs;
  void NativeWorkerInitialRequestSession(const std::string& target,const std::string& parent,const std::string& child,const std::string& url) {legacy_handoffs.push_back({target,parent,child,url});}
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
  void WakeDeadlines() {++wake_receipts;}
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
  #include "worker_controller.inc"
  #include "worker_target_lifetime.inc"
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
}
int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  { // Only the actual context broker service-worker channel enables Fetch.
    for(bool broker:{false,true})for(const char*kind:{"worker","shared_worker","service_worker"}){
      auto source=CefRefPtr<Source>(new Source());source->worker_context_channel_=broker;source->worker_service_fetch_=broker;
      auto data=CefDictionaryValue::Create(),info=CefDictionaryValue::Create();info->SetString("type",kind);info->SetString("targetId","fetch-policy-target");info->SetString("url","http://fixture.test/service.js");data->SetDictionary("targetInfo",info);data->SetString("sessionId","fetch-policy-session");
      assert(source->WorkerAttached(data,"actual-parent"));RunOne();source->AckBoot("Network.enable","fetch-policy-session");
      assert(source->network_->fetch_support.at("fetch-policy-session")== (broker&&std::string(kind)=="service_worker"));
      assert(!source->worker_targets_.at("fetch-policy-target").network_ready&&source->events.empty());
      auto ready=std::move(source->network_->ready.front());source->network_->ready.clear();ready(true,"");
      assert(source->worker_targets_.at("fetch-policy-target").network_ready&&source->events.empty());
      source->WorkerClear();while(!scheduled.empty())RunOne();
    }
  }
  { // Actual native uint64 generation remains exact on the JSON wire.
    auto source=CefRefPtr<Source>(new Source());source->worker_generation_=std::numeric_limits<uint64_t>::max()-1;
    auto data=Parse(R"({"targetInfo":{"type":"worker","targetId":"uint64-target","url":"http://fixture.test/worker.js"},"sessionId":"uint64-session"})");
    assert(source->WorkerAttached(data,""));assert(source->worker_targets_.size()==1);
    const auto info=source->WorkerInfo(source->worker_targets_.at("uint64-target"));
    auto wire=Parse(Source::Json(info->Copy(false)));auto control=wire->GetDictionary("control");
    assert(control&&control->GetString("owner")==source->handle_namespace_&&control->GetType("generation")==VTYPE_STRING);
    assert(control->GetString("generation")=="18446744073709551615");
    auto next=Parse(R"({"targetInfo":{"type":"worker","targetId":"overflow-target","url":"http://fixture.test/worker.js"},"sessionId":"overflow-session"})");
    assert(source->WorkerAttached(next,""));assert(source->worker_targets_.size()==1&&!source->errors.empty());
    assert(source->worker_generation_==std::numeric_limits<uint64_t>::max());source->WorkerClear();scheduled.clear();
  }

  { // Genuine reattach replaces only the exact older channel snapshot.
    auto source=CefRefPtr<Source>(new Source());source->worker_context_channel_=true;
    auto first=Parse(R"({"targetInfo":{"type":"service_worker","targetId":"reattach-target","url":"http://fixture.test/service.js"},"sessionId":"old-channel"})");
    assert(source->WorkerAttached(first,""));source->worker_targets_.at("reattach-target").observed=true;
    std::map<std::string,Source::WorkerTarget> detached;
    detached.emplace("reattach-target",source->worker_targets_.at("reattach-target"));
    detached.emplace("unrelated",source->worker_targets_.at("reattach-target"));
    assert(source->WorkerDetached("old-channel"));assert(source->events.back()->GetString("event")=="channelclosed");
    auto replacement=Parse(R"({"targetInfo":{"type":"service_worker","targetId":"reattach-target","url":"http://fixture.test/service.js"},"sessionId":"new-channel"})");
    const auto before=source->worker_generation_;assert(source->WorkerAttached(replacement,""));
    assert(!source->WorkerRetireReattachedSnapshot(first,before,detached));
    assert(detached.size()==2&&source->WorkerRetireReattachedSnapshot(replacement,before,detached));
    assert(detached.size()==1&&detached.contains("unrelated"));
    detached.emplace("reattach-target",source->worker_targets_.at("reattach-target"));
    const auto duplicate=source->worker_generation_;assert(source->WorkerAttached(replacement,""));
    assert(!source->WorkerRetireReattachedSnapshot(replacement,duplicate,detached));
    assert(detached.contains("reattach-target"));
    source->WorkerClear();while(!scheduled.empty())RunOne();
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->Admit(1);
    auto request=Request("create");request->SetString("expression","({actual:42})");source->WorkerRun(1,request);
    assert(source->worker_physical_==1&&source->worker_handles_.empty());
    source->Ack("Runtime.evaluate","{\"result\":{\"type\":\"object\",\"objectId\":\"actual-wrapper\"}}");
    assert(source->worker_physical_==1);source->Ack("Runtime.getProperties","{\"result\":[{\"name\":\"value\",\"value\":{\"type\":\"object\",\"description\":\"Object\",\"objectId\":\"actual-value\"}}]}");
    assert(source->worker_physical_==0&&source->worker_handles_.size()==1&&source->remote_groups.size()==1&&source->completions.back().ok);
    auto metadata=Parse(source->completions.back().json);auto control=metadata->GetDictionary("workerControl");
    assert(control&&control->GetString("owner")==source->handle_namespace_&&control->GetString("generation")==std::to_string(source->worker_targets_.at("worker-A").generation));

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
    const auto pending=source->worker_calls_.size();assert(pending==2&&!source->WorkerNeedsTick());
    const auto wakes=source->wake_receipts;
    source->AckBoot("Runtime.enable","unready-session");assert(source->worker_calls_.size()==pending-1&&!source->WorkerNeedsTick()&&source->wake_receipts>wakes);
    assert(source->worker_targets_.at("unready").bootstrap_failed&&!source->worker_targets_.at("unready").published);
    source->WorkerClear();assert(source->worker_targets_.empty());
    assert(std::any_of(source->raw.begin(),source->raw.end(),[](const Source::Raw& raw){return raw.method=="Runtime.runIfWaitingForDebugger";}));
    clock_value=100;
  }
  {
    auto source=CefRefPtr<Source>(new Source());
    auto data=Parse("{\"targetInfo\":{\"type\":\"service_worker\",\"targetId\":\"service-A\",\"url\":\"http://fixture.test/service.js\"},\"sessionId\":\"service-session\",\"waitingForDebugger\":true}");
    assert(source->WorkerAttached(data,""));RunOne();
    assert(source->worker_targets_.at("service-A").resumed&&!source->worker_targets_.at("service-A").runtime_ready&&!source->worker_targets_.at("service-A").network_ready);
    assert(source->worker_targets_.at("service-A").deadline==30100);
    source->AckBoot("Runtime.runIfWaitingForDebugger","service-session");
    source->WorkerRuntimeEvent("Runtime.executionContextCreated",Parse("{\"context\":{\"id\":17}}"),"service-session");
    source->WorkerRuntimeEvent("Inspector.workerScriptLoaded",CefDictionaryValue::Create(),"service-session");assert(source->events.empty());
    source->AckBoot("Runtime.enable","service-session");source->AckBoot("Network.enable","service-session");assert(source->events.empty());
    auto ready=std::move(source->network_->ready.front());source->network_->ready.clear();ready(true,"");
    assert(source->events.size()==1&&source->events.back()->GetDictionary("worker")->GetString("type")=="service_worker");
    source->WorkerClear();assert(source->events.back()->GetString("event")=="channelclosed");
  }
  {
    auto source=CefRefPtr<Source>(new Source());auto invalid=Parse("{\"targetInfo\":{\"type\":\"worker\",\"targetId\":\"\",\"url\":\"http://fixture.test/worker.js\"},\"sessionId\":\"invalid-session\"}");
    assert(source->WorkerAttached(invalid,"actual-parent"));assert(source->legacy_handoffs.empty()&&source->network_->script_handoffs.empty()&&source->worker_targets_.empty());
  }
  {
    auto source=CefRefPtr<Source>(new Source());Source::WorkerCoverageReady coverage_ready;int attached=0;
    source->worker_coverage_attach_=[&](const std::string& session,const std::string& id,const std::string& kind,double deadline,Source::WorkerCoverageReady ready){assert(session=="coverage-session"&&id=="coverage-worker"&&kind=="worker"&&deadline==30100);++attached;coverage_ready=std::move(ready);};
    auto data=Parse(R"({"targetInfo":{"type":"worker","targetId":"coverage-worker","url":"http://fixture.test/worker.js"},"sessionId":"coverage-session"})");
    assert(source->WorkerAttached(data,""));RunOne();assert(attached==1&&coverage_ready);
    source->AckBoot("Runtime.enable","coverage-session");source->AckBoot("Network.enable","coverage-session");auto ready=std::move(source->network_->ready.front());source->network_->ready.clear();ready(true,"");
    assert(!source->worker_targets_.at("coverage-worker").resumed);source->WorkerCoverageBootstrap("coverage-worker",source->worker_targets_.at("coverage-worker").generation);assert(attached==1);
    // The controller's real startup completion is the final author-resume gate.
    coverage_ready(true,"");assert(source->worker_targets_.at("coverage-worker").resumed);source->AckBoot("Runtime.runIfWaitingForDebugger","coverage-session");
    int lost=0;source->worker_coverage_lost_=[&](const auto& session,const auto& error){assert(session=="coverage-session"&&!error.empty());++lost;};
    source->WorkerRuntimeEvent("Runtime.executionContextCreated",Parse(R"({"context":{"id":17}})"),"coverage-session");source->WorkerRuntimeEvent("Runtime.executionContextsCleared",CefDictionaryValue::Create(),"coverage-session");assert(lost==1);
    source->WorkerClear();assert(source->worker_physical_==0);
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);const auto generation=source->worker_targets_.at("worker-A").generation;
    const int retained=source->AllocateDevToolsId();int replies=0,lost=0;
    source->worker_coverage_retains_=[retained](int id){return id==retained;};source->worker_coverage_lost_=[&](const auto&,const auto&){++lost;};
    source->devtools_.emplace(retained,Source::DevToolsCall{0,[&](bool ok,const std::string&){assert(ok);++replies;},"worker-session-A","Debugger.enable",CefDictionaryValue::Create(),true});
    // Bootstrap failure is logical failure, not a transport-close receipt.
    source->WorkerBootstrapFailed("worker-A",generation,"controlled Runtime failure");assert(lost==1&&source->worker_targets_.at("worker-A").bootstrap_failed&&source->devtools_.contains(retained)&&replies==0);
    source->Ack("Debugger.enable","{}");assert(replies==1&&source->devtools_.empty());source->WorkerClear();
  }
  {
    auto source=CefRefPtr<Source>(new Source());
    auto attach=[&](const std::string& id,const std::string& session,const std::string& parent){auto data=Parse(R"({"targetInfo":{"type":"worker","url":"http://fixture.test/actual.js"}})");data->GetDictionary("targetInfo")->SetString("targetId",id);data->SetString("sessionId",session);assert(source->WorkerAttached(data,parent));RunOne();source->AckBoot("Runtime.enable",session);source->AckBoot("Network.enable",session);auto ready=std::move(source->network_->ready.front());source->network_->ready.clear();ready(true,"");source->AckBoot("Runtime.runIfWaitingForDebugger",session);};
    attach("actual-worker-A","actual-session-A","actual-iframe-session");attach("actual-worker-B","actual-session-B","actual-session-A");attach("actual-worker-C","actual-session-C","other-actual-iframe");
    // These exact parent edges are supplied by Source's actual iframe attach
    // receipts; the worker metadata alone cannot bridge intermediate OOPIFs.
    const std::map<std::string,std::string> iframe_parents{{"actual-iframe-session","actual-top-iframe"},{"actual-top-iframe","actual-root-session"},{"other-actual-iframe","unrelated-root-session"}};
    assert((source->WorkerSessionClosures("actual-root-session",iframe_parents)==std::vector<std::string>{"actual-session-B","actual-session-A","actual-iframe-session","actual-top-iframe","actual-root-session"}));
    const std::map<std::string,std::string> malformed_cycle{{"cycle-A","cycle-B"},{"cycle-B","cycle-A"},{"","cycle-A"}};
    assert((source->WorkerSessionClosures("cycle-A",malformed_cycle)==std::vector<std::string>{"cycle-B","cycle-A"}));
    assert(source->WorkerSessionClosures("",iframe_parents).empty());
    const auto closed=source->WorkerSessionClosures("actual-iframe-session");assert((closed==std::vector<std::string>{"actual-session-B","actual-session-A","actual-iframe-session"}));assert(source->WorkerSessionClosures("").empty());
    // The browser's parent-close receipt retires only genuine descendants.
    // Unrelated live worker state remains usable, and the snapshot is stable.
    for(const auto& session:closed)source->WorkerDetached(session);
    assert(source->worker_targets_.size()==1&&source->worker_targets_.contains("actual-worker-C"));assert((source->WorkerSessionClosures("actual-iframe-session")==std::vector<std::string>{"actual-iframe-session"}));
    source->WorkerClear();assert(source->worker_targets_.empty()&&source->worker_calls_.empty()&&source->worker_physical_==0);
  }

  {
    // A request may disclose genuine worker metadata before its author-ready
    // barrier. Real detach retires that observation without inventing discovery.
    auto source=CefRefPtr<Source>(new Source());
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"worker","targetId":"observed-early","url":"http://fixture.test/early.js"},"sessionId":"observed-session"})"),"actual-parent"));RunOne();
    auto request=CefDictionaryValue::Create();source->WorkerAttributeNetwork("Network.requestWillBeSent",request,"observed-session");
    assert(request->GetDictionary("suitecutWorkerInfo")->GetString("id")=="observed-early");assert(source->events.empty());
    assert(source->WorkerDetached("observed-session"));assert(source->events.size()==1&&source->events.back()->GetString("event")=="channelclosed");
    assert(!source->WorkerDetached("observed-session"));assert(source->events.size()==1&&source->worker_calls_.empty());
  }
  {
    auto source=CefRefPtr<Source>(new Source());
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"service_worker","targetId":"failed-observed","url":"http://fixture.test/sw.js"},"sessionId":"failed-session"})"),""));RunOne();
    auto request=CefDictionaryValue::Create();source->WorkerAttributeNetwork("Network.responseReceived",request,"failed-session");
    const auto generation=source->worker_targets_.at("failed-observed").generation;
    source->WorkerBootstrapFailed("failed-observed",generation,"actual startup rejection");
    assert(source->events.empty()&&source->worker_targets_.size()==1&&source->worker_sessions_.contains("failed-session"));
    assert(!source->worker_calls_.empty()); // Pending real ACKs remain owned.
    source->WorkerBootstrapFailed("failed-observed",generation,"duplicate failure");assert(source->errors.size()==1);
    source->Admit(1);source->WorkerRun(1,Request("evaluate","failed-observed"));assert(!source->completions.back().ok&&source->completions.back().error.find("bootstrap failed")!=std::string::npos);
    source->AckBoot("Runtime.enable","failed-session");source->AckBoot("Runtime.runIfWaitingForDebugger","failed-session");
    source->WorkerRuntimeEvent("Runtime.executionContextCreated",Parse(R"({"context":{"id":31}})"),"failed-session");source->WorkerRuntimeEvent("Inspector.workerScriptLoaded",CefDictionaryValue::Create(),"failed-session");
    assert(source->events.empty()&&source->worker_targets_.at("failed-observed").bootstrap_failed);
    assert(source->WorkerDetached("failed-session"));assert(source->events.size()==1&&source->events.back()->GetString("event")=="channelclosed");
  }
  {
    auto source=CefRefPtr<Source>(new Source());
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"shared_worker","targetId":"observed-destroyed","url":"http://fixture.test/shared.js"},"sessionId":"destroyed-session"})"),""));RunOne();
    auto request=CefDictionaryValue::Create();source->WorkerAttributeNetwork("Fetch.requestPaused",request,"destroyed-session");
    source->WorkerRuntimeEvent("Runtime.executionContextsCleared",CefDictionaryValue::Create(),"destroyed-session");
    assert(source->WorkerDetached("destroyed-session"));assert(source->events.size()==1&&source->events.back()->GetString("event")=="close");
  }
  {
    auto source=CefRefPtr<Source>(new Source());
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"worker","targetId":"never-observed","url":"http://fixture.test/no.js"},"sessionId":"unobserved-session"})"),""));RunOne();
    assert(source->WorkerDetached("unobserved-session"));assert(source->events.empty());
  }

  {
    auto source=CefRefPtr<Source>(new Source());source->worker_context_channel_=true;
    for(int i=0;i<1024;++i){
      const auto id="retained-observed-"+std::to_string(i),session="retained-session-"+std::to_string(i);
      auto data=Parse(R"({"targetInfo":{"type":"worker","url":"http://fixture.test/observed.js"}})");data->GetDictionary("targetInfo")->SetString("targetId",id);data->SetString("sessionId",session);
      assert(source->WorkerAttached(data,"actual-context-parent"));RunOne();auto request=CefDictionaryValue::Create();source->WorkerAttributeNetwork("Network.requestWillBeSent",request,session);
      source->WorkerBootstrapFailed(id,source->worker_targets_.at(id).generation,"controlled logical setup failure");
    }
    assert(source->worker_targets_.size()==1024&&source->events.empty()&&source->worker_calls_.size()==2048);
    auto extra=Parse(R"({"targetInfo":{"type":"worker","targetId":"over-capacity","url":"http://fixture.test/extra.js"},"sessionId":"over-capacity-session"})");assert(source->WorkerAttached(extra,"actual-context-parent"));
    assert(source->worker_targets_.size()==1024&&!source->worker_targets_.contains("over-capacity")&&scheduled.empty());
    source->WorkerClear();assert(source->worker_targets_.empty()&&source->worker_calls_.empty());
    assert(source->events.size()==1024);for(const auto& event:source->events)assert(event->GetString("event")=="channelclosed");
  }

  {
    auto source=CefRefPtr<Source>(new Source());
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"worker","targetId":"late-real-ack","url":"http://fixture.test/late.js"},"sessionId":"late-real-session"})"),""));RunOne();
    assert(source->worker_calls_.size()==2);clock_value=30101;
    source->AckBoot("Runtime.enable","late-real-session"); // Actual ACK before Tick.
    assert(source->worker_calls_.size()==1&&source->worker_targets_.at("late-real-ack").bootstrap_failed&&!source->worker_targets_.at("late-real-ack").runtime_ready);
    assert(source->events.empty());source->WorkerClear();assert(source->worker_calls_.empty());clock_value=100;
  }

  {
    auto source=CefRefPtr<Source>(new Source());
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"worker","targetId":"owner-page-closes","url":"http://fixture.test/early.js"},"sessionId":"owner-page-session"})"),""));RunOne();
    auto request=CefDictionaryValue::Create();source->WorkerAttributeNetwork("Network.requestWillBeSent",request,"owner-page-session");
    source->WorkerUpdated(Parse(R"({"targetInfo":{"targetId":"owner-page-closes","url":"http://fixture.test/actual.js"}})"));
    assert(source->events.size()==1&&source->events.back()->GetString("event")=="updated");assert(!source->worker_targets_.at("owner-page-closes").published);
    source->WorkerClear(); // Actual owning-page/renderer teardown seam.
    assert(source->events.size()==2&&source->events.back()->GetString("event")=="close");assert(source->worker_calls_.empty()&&source->worker_targets_.empty());
  }

  {
    auto source=CefRefPtr<Source>(new Source());source->worker_context_channel_=true;
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"shared_worker","targetId":"known-destroyed-at-close","url":"http://fixture.test/shared.js"},"sessionId":"known-destroyed-session"})"),""));RunOne();
    auto request=CefDictionaryValue::Create();source->WorkerAttributeNetwork("Network.requestWillBeSent",request,"known-destroyed-session");
    source->WorkerRuntimeEvent("Runtime.executionContextsCleared",CefDictionaryValue::Create(),"known-destroyed-session");
    source->WorkerClear();assert(source->events.size()==1&&source->events.back()->GetString("event")=="close");assert(source->worker_calls_.empty()&&source->worker_targets_.empty());
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);
    source->NativeWorkerTargetDiscoveryStart();assert(!source->native_worker_discovery_ready_&&source->lifetime_ready==0&&source->lifetime_calls.size()==1);
    assert(source->lifetime_calls.front().method=="Target.setDiscoverTargets"&&source->lifetime_calls.front().params->GetBool("discover")&&source->lifetime_calls.front().deadline==Now()+30000);
    source->NativeWorkerTargetDiscoveryStart();assert(source->lifetime_calls.size()==1);source->AckLifetime();assert(source->native_worker_discovery_ready_&&source->lifetime_ready==1);
    source->Admit(901,35);source->WorkerRun(901,Request("waitClose"));const auto owner=source->operations_.Get(901);assert(owner&&owner->deadline_ms==Now()+35);
    assert(source->WorkerDetached("worker-session-A"));assert(source->events.back()->GetString("event")=="channelclosed");
    const auto channel_reason=source->events.back()->GetString("reason").ToString();assert(channel_reason.find("Inspector control channel detached")!=std::string::npos&&channel_reason.find("control channel unavailable")!=std::string::npos&&channel_reason.find("author execution termination was not proven")!=std::string::npos);
    assert(source->operations_.IsCurrent(901,owner->generation)&&source->operations_.Get(901)->deadline_ms==owner->deadline_ms&&source->worker_work_.contains(901)&&!source->operation_sessions_.contains(901));
    assert(source->native_detached_workers_.size()==1&&!source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"other-actual-target"})")));
    assert(source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"worker-A"})")));assert(source->native_detached_workers_.empty()&&!source->operations_.Contains(901)&&source->completions.back().ok&&source->events.back()->GetString("event")=="close");
    assert(!source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"worker-A"})")));source->WorkerClear();
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->NativeWorkerTargetDiscoveryStart();source->AckLifetime();
    source->Admit(902,35);source->WorkerRun(902,Request("waitClose"));assert(source->WorkerDetached("worker-session-A"));
    const auto before=source->worker_generation_;auto attached=Parse(R"({"targetInfo":{"type":"worker","targetId":"worker-A","url":"http://fixture.test/worker.js"},"sessionId":"real-reattach-session"})");
    assert(source->WorkerAttached(attached,""));source->NativeWorkerReattached(attached,before);assert(source->native_detached_workers_.empty()&&source->worker_targets_.at("worker-A").observed&&source->worker_work_.contains(902));
    RunOne();assert(source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"worker-A"})")));assert(!source->operations_.Contains(902)&&source->completions.back().ok&&source->events.back()->GetString("event")=="close");source->WorkerClear();
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->NativeWorkerTargetDiscoveryStart();source->AckLifetime();
    source->Admit(903,35);source->WorkerRun(903,Request("waitClose"));assert(source->WorkerDetached("worker-session-A"));source->closing_page_=true;
    source->NativeWorkerTargetOwnerClosed();assert(source->native_detached_workers_.empty()&&!source->operations_.Contains(903)&&source->completions.back().ok&&source->events.back()->GetString("event")=="close");source->WorkerClear();
  }
  {
    auto source=CefRefPtr<Source>(new Source());source->NativeWorkerTargetDiscoveryStart();source->AckLifetime(false);assert(!source->native_worker_discovery_ready_&&source->lifetime_ready==0&&!source->errors.empty());
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->NativeWorkerTargetDiscoveryStart();source->AckLifetime();
    source->Admit(904,35);source->WorkerRun(904,Request("waitClose"));assert(source->WorkerDetached("worker-session-A"));
    source->Complete(904,false,"Actual controlled cancellation");source->Admit(904,100);const auto replacement=source->operations_.Get(904)->generation;
    assert(source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"worker-A"})")));assert(source->operations_.IsCurrent(904,replacement));source->Complete(904,true);source->WorkerClear();
  }
  {
    auto source=CefRefPtr<Source>(new Source());Attach(source);source->NativeWorkerTargetDiscoveryStart();source->AckLifetime();
    source->Admit(905,35);source->WorkerRun(905,Request("waitClose"));assert(source->WorkerDetached("worker-session-A"));
    clock_value+=36;assert(source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"worker-A"})")));assert(!source->completions.back().ok&&source->completions.back().error.find("deadline")!=std::string::npos);source->WorkerClear();clock_value=100;
  }
  {
    auto source=CefRefPtr<Source>(new Source());source->NativeWorkerTargetDiscoveryStart();assert(source->lifetime_calls.size()==1);
    source->closing_page_=true;source->NativeWorkerTargetOwnerClosed();source->AckLifetime();assert(!source->native_worker_discovery_ready_&&source->lifetime_ready==0&&!source->worker_channel_detached_);
  }
  {
    auto source=CefRefPtr<Source>(new Source());source->NativeWorkerTargetDiscoveryStart();source->AckLifetime();
    for(int index=0;index<1024;++index){
      const auto id="controlled-lifetime-"+std::to_string(index),session="controlled-lifetime-session-"+std::to_string(index);
      source->legacy_handoffs.clear();source->network_->script_handoffs.clear();source->events.clear();Attach(source,"worker",id.c_str(),session.c_str());assert(source->WorkerDetached(session));assert(source->native_detached_workers_.size()==size_t(index+1));
    }
    source->legacy_handoffs.clear();source->network_->script_handoffs.clear();source->events.clear();Attach(source,"worker","controlled-over-capacity","controlled-over-capacity-session");assert(source->WorkerDetached("controlled-over-capacity-session"));
    assert(source->native_detached_workers_.size()==1024&&!source->errors.empty()&&source->errors.back().find("capacity")!=std::string::npos);
    source->closing_page_=true;source->NativeWorkerTargetOwnerClosed();source->WorkerClear();assert(source->native_detached_workers_.empty()&&source->worker_targets_.empty()&&source->worker_calls_.empty());
  }
  {
    using Cursor=suitecut::NativeContextWorkerCursors;
    int failures=0;auto source=CefRefPtr<Source>(new Source());Attach(source);
    source->NativeWorkerTargetDiscoveryStart();source->AckLifetime();
    auto* actual=source.get();source->worker_event_available_=[actual](bool retirement){return Cursor::CanNext(actual->event_sequence_,retirement);};source->worker_event_failure_=[&failures]{++failures;};
    source->event_sequence_=Cursor::kAdmissionSequence-1;
    source->WorkerUpdated(Parse(R"({"targetInfo":{"targetId":"worker-A","url":"http://fixture.test/updated-worker.js"}})"));
    assert(source->event_sequence_==Cursor::kAdmissionSequence&&source->worker_targets_.at("worker-A").url=="http://fixture.test/updated-worker.js");
    const auto event_count=source->events.size();source->closing_page_=true;
    source->WorkerUpdated(Parse(R"({"targetInfo":{"targetId":"worker-A","url":"http://fixture.test/not-admitted.js"}})"));
    assert(failures==1&&source->events.size()==event_count&&source->worker_targets_.at("worker-A").url=="http://fixture.test/updated-worker.js");
    const auto generation=source->worker_generation_;
    assert(source->WorkerAttached(Parse(R"({"targetInfo":{"type":"worker","targetId":"not-admitted-target","url":"http://fixture.test/not-admitted.js"},"sessionId":"not-admitted-session"})"),""));
    assert(failures==2&&source->worker_generation_==generation&&source->worker_targets_.size()==1&&!source->worker_sessions_.contains("not-admitted-session"));
    assert(source->WorkerDetached("worker-session-A"));assert(source->events.back()->GetString("event")=="channelclosed"&&source->event_sequence_==Cursor::kAdmissionSequence+1);
    assert(source->NativeWorkerTargetDestroyed(Parse(R"({"targetId":"worker-A"})")));assert(source->events.back()->GetString("event")=="close"&&source->event_sequence_==Cursor::kAdmissionSequence+2&&failures==2);
    source->NativeWorkerTargetOwnerClosed();source->WorkerClear();
  }
  assert(scheduled.empty());
  // The actual worker module must preserve a shared Debugger command until its
  // own reply/channel drain. The target-map removal is only logical here.
  {
    CefRefPtr<Source> source=new Source;
    Attach(source);
    size_t replies=0;
    const int held=++source->next_call,ordinary=++source->next_call;
    source->devtools_.emplace(held,Source::DevToolsCall{0,[&](bool ok,const auto&){assert(!ok);++replies;},"worker-session-A","Debugger.getScriptSource",nullptr,true});
    source->devtools_.emplace(ordinary,Source::DevToolsCall{0,[](bool,const auto&){assert(false);},"worker-session-A","ordinary",nullptr,false});
    source->worker_debugger_retains_=[held](int actual){return actual==held;};
    assert(source->WorkerDetached("worker-session-A"));
    assert(!source->devtools_.contains(ordinary)&&source->devtools_.contains(held)&&replies==0);
    auto callback=std::move(source->devtools_.at(held).callback);
    source->devtools_.erase(held);
    callback(false,"Actual controlled Inspector channel detach receipt");
    assert(replies==1&&source->devtools_.empty());
  }

}
