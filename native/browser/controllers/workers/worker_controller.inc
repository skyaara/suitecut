// SPDX-License-Identifier: MIT
// Included inside Source. Worker targets have their own real CDP contexts and
// remote object groups; they never enter the DOM frame or page handle tables.
struct WorkerTarget {
  std::string id, session, parent_session, parent_frame, url, kind;
  int context = 0;
  uint64_t generation = 0;
  bool runtime_ready = false, network_ready = false, inspector_ready = true, resumed = false, resume_ready = false, script_ready = false, published = false, context_destroyed = false;
  bool coverage_ready = true, coverage_bootstrap_pending = false;
  bool websocket_ready = true, websocket_bootstrap_pending = false, websocket_required = false;
  bool observed = false, bootstrap_failed = false;
  double deadline = 0;
};
struct WorkerCall { std::string session; DevToolsCallback callback; double deadline = 0; bool expired = false; };
struct WorkerHandle { std::string object, group, worker, session; int context = 0; uint64_t generation = 0; };
struct WorkerPhysical { bool settled = false; std::function<void()> release; void Settle() { if(!settled) { settled=true;release(); } } ~WorkerPhysical() { Settle(); } };
struct WorkerWork { std::string worker, group; uint64_t generation = 0, target_generation = 0; int context = 0; bool retained = false, wait_close = false; };
std::map<std::string,WorkerTarget> worker_targets_;
std::map<std::string,std::string> worker_sessions_;
std::map<int,WorkerCall> worker_calls_;
std::map<int,WorkerHandle> worker_handles_;
std::map<int,WorkerWork> worker_work_;
static constexpr size_t kWorkerTargets = 1024;
uint64_t worker_generation_ = 0;
size_t worker_physical_ = 0;
bool worker_context_channel_ = false;
// Explicit policy set only by the actual private context broker constructor.
bool worker_service_fetch_ = false;
std::function<bool(const WorkerTarget&)> worker_channel_detached_;
std::function<bool(bool)> worker_event_available_;
std::function<void()> worker_event_failure_;
// Optional document-owned JS coverage joins the real dedicated-worker startup
// barrier. The context broker leaves these hooks empty; no page scope is lent.
using WorkerCoverageReady=std::function<void(bool,const std::string&)>;
std::function<void(const std::string&,const std::string&,const std::string&,double,WorkerCoverageReady)> worker_coverage_attach_;
std::function<void(const std::string&,const std::string&)> worker_coverage_lost_;
std::function<bool(int)> worker_coverage_retains_;
// Source debugger calls retain their own real Inspector ACK/channel lease.
// The context broker leaves this empty and keeps its existing ordinary drain.
std::function<bool(int)> worker_debugger_retains_;
std::function<void(const std::string&,const std::string&,const std::string&,double,WorkerCoverageReady)>worker_websocket_attach_;
std::function<void(const std::string&)>worker_websocket_detach_;
std::function<void(const std::string&)>worker_websocket_failure_;
std::function<bool()>worker_websocket_required_;


Dict WorkerControlInfo(const WorkerTarget& target) {
  auto result=CefDictionaryValue::Create();result->SetString("owner",handle_namespace_);
  result->SetString("generation",std::to_string(target.generation));return result;
}
Dict WorkerInfo(const WorkerTarget& target) {
  auto result = CefDictionaryValue::Create(); result->SetString("id",target.id);
  result->SetString("url",target.url); result->SetString("type",target.kind);
  result->SetDictionary("control",WorkerControlInfo(target));
  return result;
}
bool WorkerEventAvailable(bool actual_retirement = false) {
  if (worker_event_available_ ? worker_event_available_(actual_retirement) : event_sequence_ < 9007199254740991ULL) return true;
  if (worker_event_failure_) worker_event_failure_();
  else Error("Native worker event cursor exceeds safe integer capacity");
  return false;
}
std::optional<uint64_t> WorkerEventSequence(bool actual_retirement = false) {
  if (!WorkerEventAvailable(actual_retirement)) return {};
  return ++event_sequence_;
}
void WorkerPublish(const WorkerTarget& target, const char* event, std::optional<uint64_t> admitted = {}) {
  if (!admitted) admitted = WorkerEventSequence(std::string(event)=="close");
  if (!admitted) return;
  auto info = CefDictionaryValue::Create(); info->SetString("event",event); info->SetDictionary("worker",WorkerInfo(target));
  const auto sequence = *admitted; info->SetDouble("sequence",static_cast<double>(sequence));
  const auto json = Json(info);
  auto header = Header("worker"); header->SetDouble("sequence",static_cast<double>(sequence));
  SendPacket(header,std::vector<uint8_t>(json.begin(),json.end()));
  // The real lifecycle event reaches the transport before a native journal wait
  // completes, preserving one worker identity for listener and wait delivery.
  JournalEvent(sequence,(worker_context_channel_||target.kind=="worker") ? (std::string(event)=="worker" ? "worker" : "workerclosed") : "contextworker",json);
  const auto waiting = event_waiters_;
  for (const auto& [id,name] : waiting) if (name == ((worker_context_channel_||target.kind=="worker") ? (std::string(event)=="worker" ? "worker" : "workerclosed") : "contextworker")) Complete(id,true,"",json);
}
bool WorkerBootstrapSend(const std::string& session, const std::string& method, Dict params, DevToolsCallback callback) {
  const int id = AllocateDevToolsId(); if (!id || !browser_) return false;
  const auto mapped=worker_sessions_.find(session);
  const double deadline=mapped==worker_sessions_.end()?Now()+30000:worker_targets_.at(mapped->second).deadline;
  worker_calls_.emplace(id,WorkerCall{session,std::move(callback),deadline});
  if (!SubmitDevTools(id,method,params,session)) { worker_calls_.erase(id); return false; }
  WakeDeadlines(); return true;
}
bool WorkerToolResult(int id, bool ok, const void* data, size_t size, const std::string& session) {
  auto found = worker_calls_.find(id); if (found == worker_calls_.end()) return false;
  if (found->second.session != session) { Error("Native worker protocol session mismatch"); return true; }
  const bool expired=found->second.expired||Now()>=found->second.deadline;
  auto callback = std::move(found->second.callback); worker_calls_.erase(found);
  if(expired)callback(false,"Native worker bootstrap original deadline expired");
  else if (size > 1048576) callback(false,"Native worker bootstrap response exceeds limit");
  else callback(ok,size ? std::string(static_cast<const char*>(data),size) : "{}");
  WakeDeadlines(); // Real replies can retire physical ownership while idle.
  return true;
}
bool WorkerWebSocketRequired(const WorkerTarget&target)const{return target.websocket_required||(worker_websocket_required_&&worker_websocket_required_());}
void WorkerBootstrapFailed(const std::string& id, uint64_t generation, const std::string& error) {
  const auto found = worker_targets_.find(id); if (found == worker_targets_.end() || found->second.generation != generation || found->second.bootstrap_failed) return;
  // Logical setup failure is not physical session/target closure. Retain this
  // bounded actual identity until a genuine detach or browser-close receipt.
  found->second.bootstrap_failed=true;
  if(WorkerWebSocketRequired(found->second)){
    if(worker_websocket_failure_)worker_websocket_failure_(error);
    else Error(("Protected native WebSocket worker bootstrap failed: "+error).c_str());
    return; // Never resume author execution after any protected setup failure.
  }
  const auto session=found->second.session;
  // A failing bootstrap must release a genuinely paused author target. Report
  // the failure rather than leave a worker silently suspended forever.
  if(worker_coverage_lost_)worker_coverage_lost_(session,"Actual native worker bootstrap failed before coverage collection: "+error);
  SendDevTools("Runtime.runIfWaitingForDebugger",nullptr,session);
  Error(("Native worker bootstrap failed: " + error).c_str());
}
void WorkerMaybePublish(const std::string& id, uint64_t generation) {
  auto found=worker_targets_.find(id);if(found==worker_targets_.end()||found->second.generation!=generation)return;
  auto& target=found->second;if(target.bootstrap_failed||target.published||!target.runtime_ready||!target.network_ready||!target.websocket_ready||!target.resume_ready||!target.context||!target.script_ready)return;
  const auto sequence=WorkerEventSequence();if(!sequence)return;
  target.published=true;WorkerPublish(target,"worker",sequence);
}
void WorkerMaybeResume(const std::string& id, uint64_t generation) {
  auto found = worker_targets_.find(id); if (found == worker_targets_.end() || found->second.generation != generation) return;
  auto& target = found->second;
  if (target.bootstrap_failed || !target.runtime_ready || !target.network_ready || !target.inspector_ready || !target.coverage_ready || !target.websocket_ready || target.resumed) return;
  target.resumed = true;
  const auto session = target.session;
  AutoAttach(session);
  if (!WorkerBootstrapSend(session,"Runtime.runIfWaitingForDebugger",nullptr,[this,id,generation](bool ok,const std::string& json) {
    auto found = worker_targets_.find(id); if (found == worker_targets_.end() || found->second.generation != generation) return;
    if (!ok) { WorkerBootstrapFailed(id,generation,json); return; }
    found->second.resume_ready = true; WorkerMaybePublish(id,generation);
  })) WorkerBootstrapFailed(id,generation,"CEF rejected worker resume");
}
void WorkerCoverageBootstrap(const std::string& id,uint64_t generation){
  const auto found=worker_targets_.find(id);if(found==worker_targets_.end()||found->second.generation!=generation||found->second.coverage_bootstrap_pending)return;
  if(!worker_coverage_attach_){found->second.coverage_ready=true;WorkerMaybeResume(id,generation);return;}
  found->second.coverage_ready=false;found->second.coverage_bootstrap_pending=true;
  const auto session=found->second.session,kind=found->second.kind;const auto deadline=found->second.deadline;
  worker_coverage_attach_(session,id,kind,deadline,[this,id,generation](bool ok,const std::string& error){
    const auto actual=worker_targets_.find(id);if(actual==worker_targets_.end()||actual->second.generation!=generation||closing_page_)return;
    actual->second.coverage_bootstrap_pending=false;if(!ok){WorkerBootstrapFailed(id,generation,error);return;}
    actual->second.coverage_ready=true;WorkerMaybeResume(id,generation);WorkerMaybePublish(id,generation);
  });
}
void WorkerWebSocketBootstrap(const std::string&id,uint64_t generation){
  const auto found=worker_targets_.find(id);if(found==worker_targets_.end()||found->second.generation!=generation||found->second.websocket_bootstrap_pending)return;
  if(!worker_websocket_attach_){found->second.websocket_ready=true;WorkerMaybeResume(id,generation);return;}
  found->second.websocket_bootstrap_pending=true;
  worker_websocket_attach_(found->second.session,id,found->second.kind,found->second.deadline,[this,id,generation](bool ok,const std::string&error){
    const auto actual=worker_targets_.find(id);if(actual==worker_targets_.end()||actual->second.generation!=generation||closing_page_)return;
    actual->second.websocket_bootstrap_pending=false;
    if(!ok){
      // Generic bootstrap failure resumes the author target. A failed inherited
      // interception policy must instead keep it paused until actual owning
      // page/profile teardown; its Inspector receipts remain owned normally.
      actual->second.bootstrap_failed=true;
      if(worker_websocket_failure_)worker_websocket_failure_(error);
      else Error(("Native WebSocket worker policy failed: "+error).c_str());
      return;
    }
    actual->second.websocket_ready=true;WorkerMaybeResume(id,generation);WorkerMaybePublish(id,generation);
  });
}
bool WorkerAttached(Dict data, const std::string& parent_session) {
  auto info = data->GetDictionary("targetInfo"); if (!info) return false;
  const auto kind = info->GetString("type").ToString();
  if (kind != "worker" && kind != "shared_worker" && kind != "service_worker") return false;
  const auto session = data->GetString("sessionId").ToString(), id = info->GetString("targetId").ToString();
  const auto url = info->GetString("url").ToString();
  if (session.empty() || session.size() > 128 || id.empty() || id.size() > 128 || url.size() > 8192 ||
      worker_targets_.size() >= kWorkerTargets || worker_targets_.contains(id) || worker_sessions_.contains(session) ||
      worker_generation_ == std::numeric_limits<uint64_t>::max()) {
    if(worker_websocket_required_&&worker_websocket_required_()){
      if(worker_websocket_failure_)worker_websocket_failure_("Native worker target identity or capacity invalid under WebSocket policy");
    }else if (!session.empty()) SendDevTools("Runtime.runIfWaitingForDebugger",nullptr,session);
    Error("Native worker target identity or capacity invalid"); return true;
  }
  if(!WorkerEventAvailable()){
    if(worker_websocket_required_&&worker_websocket_required_()){if(worker_websocket_failure_)worker_websocket_failure_("Native worker event capacity exceeded under WebSocket policy");}
    else SendDevTools("Runtime.runIfWaitingForDebugger",nullptr,session);
    return true;
  }
  const auto generation = ++worker_generation_;
  WorkerTarget target;target.id=id;target.session=session;target.parent_session=parent_session;target.url=url;target.kind=kind;target.generation=generation;target.deadline=Now()+30000;target.coverage_ready=!worker_coverage_attach_;
  if(info->GetType("parentFrameId")==VTYPE_STRING&&info->GetString("parentFrameId").length()<=128)target.parent_frame=info->GetString("parentFrameId");
  if(target.parent_frame.empty()&&worker_sessions_.contains(parent_session))target.parent_frame=worker_targets_.at(worker_sessions_.at(parent_session)).parent_frame;
  target.websocket_ready=!worker_websocket_attach_;target.websocket_required=worker_websocket_required_&&worker_websocket_required_();
  worker_targets_.emplace(id,std::move(target));
  worker_sessions_[session] = id;
  NativeWorkerInitialRequestSession(id,parent_session,session,url);
  if(network_)network_->WorkerScriptAttached(id,parent_session,session,url);
  // Runtime and Fetch observers are installed after the attach event returns.
  CefPostTask(TID_UI,new Task([self = CefRefPtr<Source>(this),id,generation,session] {
    const auto found = self->worker_targets_.find(id);
    if (found == self->worker_targets_.end() || found->second.generation != generation || self->closing_page_) return;
    // workerScriptLoaded is emitted by Chromium's target channel; workers do
    // not expose an Inspector.enable command.
    if (!self->WorkerBootstrapSend(session,"Runtime.enable",nullptr,[self,id,generation](bool ok,const std::string& json) {
      auto found = self->worker_targets_.find(id); if (found == self->worker_targets_.end() || found->second.generation != generation) return;
      if (!ok) { self->WorkerBootstrapFailed(id,generation,json); return; }
      found->second.runtime_ready = true; self->WorkerMaybeResume(id,generation); self->WorkerMaybePublish(id,generation);
    })) { self->WorkerBootstrapFailed(id,generation,"CEF rejected worker Runtime enable"); return; }
    auto ready = [self,id,generation](bool ok,const std::string& error) {
      auto found = self->worker_targets_.find(id); if (found == self->worker_targets_.end() || found->second.generation != generation) return;
      if (!ok) { self->WorkerBootstrapFailed(id,generation,error); return; }
      found->second.network_ready = true; self->WorkerMaybeResume(id,generation); self->WorkerMaybePublish(id,generation);
    };
    auto network = CefDictionaryValue::Create(); network->SetInt("maxTotalBufferSize",128*1048576); network->SetInt("maxResourceBufferSize",32*1048576);network->SetInt("maxPostDataSize",2097152);
    if (!self->WorkerBootstrapSend(session,"Network.enable",network,[self,id,generation,session,ready](bool ok,const std::string& json) {
      auto found = self->worker_targets_.find(id); if (found == self->worker_targets_.end() || found->second.generation != generation) return;
      if (!ok) { self->WorkerBootstrapFailed(id,generation,json); return; }
      // The private context broker has no Page ancestor to intercept service
      // worker fetches. Pinned CEF exposes Fetch on that actual target session.
      // Other worker kinds and public Sources retain their existing path.
      const bool fetch_available=self->worker_service_fetch_&&found->second.kind=="service_worker";
      if (self->network_) self->network_->SessionAttached(session,ready,fetch_available); else ready(true,"");
    })) self->WorkerBootstrapFailed(id,generation,"CEF rejected worker Network enable");
    self->WorkerWebSocketBootstrap(id,generation);
    self->WorkerCoverageBootstrap(id,generation);
    // Service worker startup pauses script download before its Runtime and
    // Network agents can acknowledge setup. Submitting those observers first
    // then releasing this genuine initial phase makes their ACKs possible.
    const auto service=self->worker_targets_.find(id);
    if(service!=self->worker_targets_.end()&&service->second.generation==generation&&service->second.kind=="service_worker"&&!service->second.resumed) {
      if(self->WorkerWebSocketRequired(service->second)&&(!service->second.websocket_ready||!service->second.network_ready||!service->second.runtime_ready)){
        self->WorkerBootstrapFailed(id,generation,"Backend cannot acknowledge native WebSocket policy before the initial service-worker resume");return;
      }
      service->second.resumed=true;
      if(!self->WorkerBootstrapSend(session,"Runtime.runIfWaitingForDebugger",nullptr,[self,id,generation](bool ok,const std::string& json){
        auto target=self->worker_targets_.find(id);if(target==self->worker_targets_.end()||target->second.generation!=generation)return;
        if(!ok){self->WorkerBootstrapFailed(id,generation,json);return;}
        target->second.resume_ready=true;self->WorkerMaybePublish(id,generation);
      }))self->WorkerBootstrapFailed(id,generation,"CEF rejected initial service worker resume");
    }
  }));
  WakeDeadlines(); return true;
}
void WorkerReleaseGroup(const std::string& group, const std::string& session) {
  if (!browser_ || closing_page_ || group.empty()) return;
  auto params = CefDictionaryValue::Create(); params->SetString("objectGroup",group);
  SendDevTools("Runtime.releaseObjectGroup",params,session);
}
void WorkerInvalidateHandles(const std::string& worker) {
  for (auto it = worker_handles_.begin(); it != worker_handles_.end();) {
    if (it->second.worker != worker) { ++it; continue; }
    WorkerReleaseGroup(it->second.group,it->second.session); it = worker_handles_.erase(it);
  }
}
void WorkerChannelClosed(const WorkerTarget& target) {
  if (!target.published&&!target.observed) return;
  const auto admitted=WorkerEventSequence(true);if(!admitted)return;
  auto event = CefDictionaryValue::Create(); event->SetString("event","channelclosed"); event->SetDictionary("worker",WorkerInfo(target));
  event->SetString("reason","Native worker Inspector control channel detached (control channel unavailable); author execution termination was not proven");
  const auto sequence = *admitted; event->SetDouble("sequence",static_cast<double>(sequence));
  const auto json = Json(event); JournalEvent(sequence,"workerchannelclosed",json);
  auto header = Header("worker"); header->SetDouble("sequence",static_cast<double>(sequence)); SendPacket(header,std::vector<uint8_t>(json.begin(),json.end()));
}
// A flattened child Inspector channel closes implicitly with its actual parent.
// Snapshot only real observed parent/session edges, then retire leaves first.
// Logical bootstrap failure keeps its real target/session metadata until here.
std::vector<std::string> WorkerSessionClosures(const std::string& root,const std::map<std::string,std::string>& iframe_parents={})const{
  if(root.empty())return {};
  std::map<std::string,std::vector<std::string>> children;
  for(const auto& [id,target]:worker_targets_)children[target.parent_session].push_back(target.session);
  for(const auto& [child,parent]:iframe_parents)if(!child.empty())children[parent].push_back(child);
  std::vector<std::string> closed{root};std::set<std::string> seen{root};
  for(size_t i=0;i<closed.size();++i){const auto found=children.find(closed[i]);if(found!=children.end())for(const auto& child:found->second)if(seen.insert(child).second)closed.push_back(child);}
  std::reverse(closed.begin(),closed.end());return closed;
}
bool WorkerDetached(std::string session) {
  const auto mapped = worker_sessions_.find(session); if (mapped == worker_sessions_.end()) return false;
  const auto id = mapped->second; const auto target = worker_targets_.at(id);
  if(worker_websocket_detach_)worker_websocket_detach_(session);
  if (network_) network_->SessionDetached(session);
  worker_sessions_.erase(mapped); worker_targets_.erase(id); WorkerInvalidateHandles(id);
  std::vector<std::pair<int,uint64_t>> calls; for (const auto& [owner,work] : worker_work_) if (work.worker == id) calls.emplace_back(owner,work.generation);
  // A dedicated target can remain alive after this Inspector channel detaches.
  // Only actual target/context destruction or owning-document closure proves
  // author death; the target's resource kind alone never supplies that receipt.
  const bool destroyed = target.context_destroyed;
  const bool retain_close=!destroyed&&worker_channel_detached_&&worker_channel_detached_(target);
  if ((target.published||target.observed) && destroyed) WorkerPublish(target,"close");
  else WorkerChannelClosed(target);
  for (const auto& [owner,generation] : calls) {
    const auto found = worker_work_.find(owner); if (found == worker_work_.end()||found->second.generation!=generation||!operations_.IsCurrent(owner,generation)) continue;
    if(found->second.wait_close&&retain_close){operation_sessions_.erase(owner);continue;}
    const bool closed = found->second.wait_close && destroyed;
    if(closed&&operations_.IsExpired(owner,generation,Now()))Complete(owner,false,"Native worker original close deadline expired");
    else Complete(owner,closed,closed ? "" : destroyed ? "Native worker is closed" : "Native worker control channel detached while shared/service execution may remain alive");
  }
  for (auto it = worker_calls_.begin(); it != worker_calls_.end();) {
    if (it->second.session == session) it = worker_calls_.erase(it); else ++it;
  }
  // Ordinary worker operations retire with their actual owner. Coverage domain
  // commands and shared Debugger calls also survive logical bootstrap failure:
  // Source settles them only
  // on their own real ACK or the separately observed target-close boundary.
  for (auto it = devtools_.begin(); it != devtools_.end();) {
    if (it->second.session == session&&
        (!worker_coverage_retains_||!worker_coverage_retains_(it->first))&&
        (!worker_debugger_retains_||!worker_debugger_retains_(it->first))) it = devtools_.erase(it); else ++it;
  }
  return true;
}
// Retire only an older observed-channel snapshot after a genuinely admitted
// new attachment. WorkerAttached returns handled=true even on validation error;
// its monotonic generation plus exact map/session match is the admission proof.
bool WorkerRetireReattachedSnapshot(Dict data,uint64_t before,std::map<std::string,WorkerTarget>& detached){
  auto info=data?data->GetDictionary("targetInfo"):nullptr;if(!info)return false;
  const auto id=info->GetString("targetId").ToString(),session=data->GetString("sessionId").ToString();
  const auto active=worker_targets_.find(id);if(active==worker_targets_.end()||active->second.generation<=before||active->second.session!=session)return false;
  const auto old=detached.find(id);if(old==detached.end()||old->second.generation>=active->second.generation)return false;
  detached.erase(old);return true;
}
void WorkerAttributeNetwork(const std::string& method, Dict data, const std::string& session) {
  if(method!="Network.requestWillBeSent"&&method!="Network.responseReceived"&&method!="Fetch.requestPaused")return;
  const auto mapped=worker_sessions_.find(session);if(mapped==worker_sessions_.end())return;
  auto& target=worker_targets_.at(mapped->second);
  target.observed=true;
  data->SetDictionary("suitecutWorkerInfo",WorkerInfo(target));
  const auto* actual=&target;bool context_owner=worker_context_channel_;
  for(size_t depth=0;depth<1024&&!context_owner;++depth){if(actual->kind=="service_worker"||actual->kind=="shared_worker"){context_owner=true;break;}const auto parent=worker_sessions_.find(actual->parent_session);if(parent==worker_sessions_.end())break;const auto found=worker_targets_.find(parent->second);if(found==worker_targets_.end())break;actual=&found->second;}
  data->SetBool("suitecutContextWorker",context_owner);
  if(target.kind=="service_worker")data->SetBool("suitecutServiceWorker",true);
  if(target.kind!="service_worker" && !target.parent_frame.empty()&&(data->GetType("frameId")!=VTYPE_STRING||data->GetString("frameId").empty()))data->SetString("frameId",target.parent_frame);
}
void WorkerUpdated(Dict data) {
  const auto info = data->GetDictionary("targetInfo"); if (!info) return;
  const auto found = worker_targets_.find(info->GetString("targetId")); if (found == worker_targets_.end()) return;
  const auto url = info->GetString("url").ToString(); if (url.size() > 8192 || url == found->second.url) return;
  if (!found->second.published&&!found->second.observed) {found->second.url=url;return;}
  const auto admitted=WorkerEventSequence();if(!admitted)return;
  found->second.url = url;
  auto event = CefDictionaryValue::Create(); event->SetString("event","updated"); event->SetDictionary("worker",WorkerInfo(found->second));
  const auto sequence = *admitted; event->SetDouble("sequence",static_cast<double>(sequence));
  const auto json = Json(event); JournalEvent(sequence,"workerupdated",json);
  auto header = Header("worker"); header->SetDouble("sequence",static_cast<double>(sequence)); SendPacket(header,std::vector<uint8_t>(json.begin(),json.end()));
}
bool WorkerRuntimeEvent(const std::string& method, Dict data, const std::string& session) {
  const auto mapped = worker_sessions_.find(session); if (mapped == worker_sessions_.end()) return false;
  auto& target = worker_targets_.at(mapped->second);
  if (method == "Inspector.workerScriptLoaded") { target.script_ready=true;WorkerMaybePublish(target.id,target.generation);return true; }
  if (method == "Runtime.executionContextCreated") {
    auto context = data->GetDictionary("context"); auto aux = context ? context->GetDictionary("auxData") : nullptr;
    if (context && context->GetType("id") == VTYPE_INT && (!aux || !aux->HasKey("isDefault") || aux->GetBool("isDefault"))) {
      target.context = context->GetInt("id"); target.context_destroyed=false; WorkerMaybeResume(target.id,target.generation); WorkerMaybePublish(target.id,target.generation);
    }
    return true;
  }
  if (method != "Runtime.executionContextDestroyed" && method != "Runtime.executionContextsCleared") return false;
  if (method == "Runtime.executionContextDestroyed" && data->GetInt("executionContextId") != target.context) return true;
  if(worker_coverage_lost_)worker_coverage_lost_(session,"Actual native worker execution context was destroyed before coverage collection");
  target.context = 0; target.context_destroyed=true; WorkerInvalidateHandles(target.id);
  std::vector<int> canceled;
  for (const auto& [owner,work] : worker_work_) if (work.worker == target.id && !work.wait_close) canceled.push_back(owner);
  for (const int owner : canceled) Complete(owner,false,"Native worker execution context destroyed");
  return true;
}
void WorkerCancel(int id) {
  const auto found = worker_work_.find(id); if (found == worker_work_.end()) return;
  const auto work = found->second; worker_work_.erase(found);
  const auto target = worker_targets_.find(work.worker);
  if (!work.retained && target != worker_targets_.end()) WorkerReleaseGroup(work.group,target->second.session);
}
bool WorkerCurrent(int owner) const {
  const auto work = worker_work_.find(owner); if (work == worker_work_.end() || !operations_.IsCurrent(owner,work->second.generation)) return false;
  const auto target = worker_targets_.find(work->second.worker);
  return target != worker_targets_.end() && target->second.generation == work->second.target_generation && target->second.context == work->second.context;
}
bool WorkerArguments(int id, Dict message, Dict params, const WorkerTarget& target) {
  auto ids = message->GetList("handles"); if (!ids || ids->GetSize() > 1024) { Complete(id,false,"Invalid native worker handle arguments"); return false; }
  auto arguments = CefListValue::Create();
  for (size_t i = 0; i < ids->GetSize(); ++i) {
    const auto found = ids->GetType(i) == VTYPE_INT ? worker_handles_.find(ids->GetInt(i)) : worker_handles_.end();
    if (found == worker_handles_.end() || found->second.worker != target.id || found->second.generation != target.generation || found->second.context != target.context) {
      Complete(id,false,"Native worker handle belongs to another worker or its context was destroyed"); return false;
    }
    auto argument = CefDictionaryValue::Create(); argument->SetString("objectId",found->second.object); arguments->SetDictionary(i,argument);
  }
  params->SetList("arguments",arguments); return true;
}
void WorkerRequest(int id, const std::string& method, Dict params, DevToolsCallback callback, const std::string& session, bool lossless_scalars=false) {
  if (!WorkerCurrent(id)) return;
  if (worker_physical_ >= 64) { Complete(id,false,"Native worker physical command capacity exceeded"); return; }
  const auto work = worker_work_.at(id); const auto group = work.group;
  ++worker_physical_; auto physical=std::make_shared<WorkerPhysical>(); physical->release=[this] { --worker_physical_; };
  DevTools(id,method,params,[this,id,session,group,generation=work.generation,physical,callback=std::move(callback)](bool ok,const std::string& json) mutable {
    physical->Settle();
    if (!WorkerCurrent(id) || worker_work_.at(id).generation != generation || worker_work_.at(id).group != group) { if(worker_sessions_.contains(session))WorkerReleaseGroup(group,session); return; }
    if (!ok) {
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      const auto error=result?result->GetString("message").ToString():json;
      Complete(id,false,error.empty()?"Native worker command failed":error.substr(0,4096));return;
    }
    callback(true,json);
  },session,true,16*1048576,lossless_scalars);
}
void WorkerCreateResult(int id, int handle, const std::string& group, bool ok, const std::string& json) {
  if (!WorkerCurrent(id)) return;
  auto parsed = CefParseJSON(json,JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
  auto remote = result ? result->GetDictionary("result") : nullptr;
  if (!ok || !remote || result->HasKey("exceptionDetails") || remote->GetType("objectId") != VTYPE_STRING) {
    auto details = result ? result->GetDictionary("exceptionDetails") : nullptr; auto exception = details ? details->GetDictionary("exception") : nullptr;
    const auto error = exception && exception->GetType("description") == VTYPE_STRING ? exception->GetString("description").ToString() : details ? details->GetString("text").ToString() : "No remote object";
    Complete(id,false,"Native worker handle creation failed: " + error); return;
  }
  const auto object = remote->GetString("objectId").ToString(); auto params = CefDictionaryValue::Create();
  params->SetString("objectId",object); params->SetBool("ownProperties",true);
  WorkerRequest(id,"Runtime.getProperties",params,[this,id,handle,group,object](bool ok,const std::string& json) {
    if (!WorkerCurrent(id)) return;
    auto parsed = CefParseJSON(json,JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
    auto fields = result ? result->GetList("result") : nullptr; Dict value;
    if (ok && fields) for (size_t i=0;i<fields->GetSize();++i) { auto field = fields->GetDictionary(i); if (field && field->GetString("name") == "value") { value = field->GetDictionary("value"); break; } }
    if (!value) { Complete(id,false,"Native worker handle metadata unavailable"); return; }
    auto& work = worker_work_.at(id); const auto& target = worker_targets_.at(work.worker);
    std::string preview = value->GetString("description");
    if (value->HasKey("unserializableValue")) preview = value->GetString("unserializableValue");
    else if (value->HasKey("value")){
      if(value->GetString("type")=="string"&&value->GetType("value")==VTYPE_DICTIONARY){std::u16string units;if(!suitecut::DevToolsReadTag(value->GetValue("value"),units)){Complete(id,false,"Invalid native worker string sidecar");return;}preview=suitecut::DevToolsQuoteUnits(units);}
      else preview = CefWriteJSON(value->GetValue("value"),JSON_WRITER_DEFAULT).ToString();
    }
    if (preview.empty()) preview = value->GetString("type");
    if (preview.size() > 8192) preview.resize(8192);
    worker_handles_.emplace(handle,WorkerHandle{object,group,target.id,target.session,target.context,target.generation}); work.retained = true;
    auto response = CefDictionaryValue::Create(); response->SetInt("handle",handle); response->SetString("preview",preview);
    response->SetString("guid","suitecut-worker-" + handle_namespace_ + ":" + std::to_string(handle)); response->SetBool("node",false); response->SetString("worker",target.id);
    response->SetDictionary("workerControl",WorkerControlInfo(target));
    Complete(id,true,"",Json(response));
  },worker_targets_.at(worker_work_.at(id).worker).session,true);
}
bool WorkerValidateControl(int owner,Dict message) {
  if(!message->HasKey("workerControl"))return true;
  const auto expected=message->GetType("workerControl")==VTYPE_DICTIONARY?message->GetDictionary("workerControl"):nullptr;
  const auto worker=message->GetType("worker")==VTYPE_STRING?message->GetString("worker").ToString():std::string();
  const auto target=worker_targets_.find(worker);
  if(!expected||expected->GetSize()!=2||expected->GetType("owner")!=VTYPE_STRING||expected->GetString("owner").length()>128||
      expected->GetType("generation")!=VTYPE_STRING||expected->GetString("generation").length()>20||
      target==worker_targets_.end()||expected->GetString("owner")!=handle_namespace_||
      expected->GetString("generation")!=std::to_string(target->second.generation)) {
    Complete(owner,false,"Native worker control belongs to another Inspector owner or attachment generation");return false;
  }
  return true;
}
void WorkerRun(int id, Dict message) {
  const auto operation = operations_.Get(id); if (!operation) return;
  const auto action = message->GetString("action").ToString();
  if (action == "list") {
    auto list = CefListValue::Create(); size_t i=0; for (const auto& [target_id,target] : worker_targets_) if (target.published) list->SetDictionary(i++,WorkerInfo(target));
    auto value = CefValue::Create(); value->SetList(list); Complete(id,true,"",CefWriteJSON(value,JSON_WRITER_DEFAULT)); return;
  }
  if(!WorkerValidateControl(id,message))return;
  const auto worker = message->GetString("worker").ToString(); const auto found = worker_targets_.find(worker);
  if (action == "dispose") {
    const auto handle = worker_handles_.find(message->GetInt("handle"));
    if (handle == worker_handles_.end()) { Complete(id,true); return; }
    if (handle->second.worker != worker) { Complete(id,false,"Native handle belongs to a different worker"); return; }
    const auto session = handle->second.session, group = handle->second.group; worker_handles_.erase(handle);
    auto params = CefDictionaryValue::Create(); params->SetString("objectGroup",group);
    DevTools(id,"Runtime.releaseObjectGroup",params,[this,id](bool ok,const std::string&) { Complete(id,ok,ok?"":"Native worker handle disposal failed"); },session); return;
  }
  if (found == worker_targets_.end()) { Complete(id,false,"Native worker is closed or unavailable"); return; }
  const auto target = found->second;
  if(target.bootstrap_failed){Complete(id,false,"Native worker bootstrap failed; actual session remains until physical closure");return;}
  if (!target.published || !target.context || !target.resumed) {
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation=operation->generation,message] { if(self->operations_.IsCurrent(id,generation))self->WorkerRun(id,message); }),10); return;
  }
  if (worker_work_.contains(id)) { Complete(id,false,"Native worker operation already active"); return; }
  WorkerWork work{worker,"",operation->generation,target.generation,target.context,false,action == "waitClose"};
  worker_work_[id] = work; operation_sessions_[id] = target.session;
  if (action == "waitClose") return;
  if (action != "evaluate" && action != "create" && action != "property") { Complete(id,false,"Unsupported native worker action"); return; }
  const bool create = action != "evaluate";
  if (create && (worker_handles_.size()+worker_work_.size() >= 1024 || next_handle_ == std::numeric_limits<int>::max())) { Complete(id,false,"Native worker handle capacity exceeded"); return; }
  const int handle = create ? ++next_handle_ : 0;
  const auto group = create ? "suitecut-worker-handle-"+handle_namespace_+"-"+std::to_string(handle) : "suitecut-worker-evaluate-"+handle_namespace_+"-"+std::to_string(id)+"-"+std::to_string(operation->generation);
  worker_work_.at(id).group = group;
  auto params = CefDictionaryValue::Create(); std::string method = "Runtime.evaluate";
  if (action == "property" || message->HasKey("handle")) {
    const auto receiver = worker_handles_.find(message->GetInt("handle"));
    if (receiver == worker_handles_.end() || receiver->second.worker != worker || receiver->second.context != target.context || receiver->second.generation != target.generation) { Complete(id,false,"Native worker handle is disposed or its context was destroyed"); return; }
    params->SetString("objectId",receiver->second.object); method = "Runtime.callFunctionOn";
  }
  if (action == "property") {
    if (message->GetType("name") != VTYPE_STRING || message->GetString("name").length() > 1048576) { Complete(id,false,"Invalid native worker property name"); return; }
    params->SetString("functionDeclaration","function(name){return {value:this.value[name]};}");
    auto arguments = CefListValue::Create(); auto name = CefDictionaryValue::Create(); name->SetString("value",message->GetString("name")); arguments->SetDictionary(0,name); params->SetList("arguments",arguments);
  } else if (message->GetType("function") == VTYPE_STRING) {
    if (message->GetString("function").length() > 1048576) { Complete(id,false,"Native worker function exceeds source limit"); return; }
    method = "Runtime.callFunctionOn"; params->SetString("functionDeclaration",message->GetString("function"));
    if (!params->HasKey("objectId")) params->SetInt("executionContextId",target.context);
    if (!WorkerArguments(id,message,params,target)) return;
  } else if (message->GetType("expression") == VTYPE_STRING && !params->HasKey("objectId")) {
    const auto expression = message->GetString("expression").ToString(); if (expression.size() > 1048576) { Complete(id,false,"Native worker expression exceeds source limit"); return; }
    params->SetString("expression",create ? "(async()=>({value:await("+expression+")}))()" : expression); params->SetInt("contextId",target.context);
  } else { Complete(id,false,"Worker evaluation requires a function or expression"); return; }
  params->SetBool("awaitPromise",true); params->SetBool("returnByValue",!create); params->SetString("objectGroup",group);
  WorkerRequest(id,method,params,[this,id,create,handle,group](bool ok,const std::string& json) {
    if (!WorkerCurrent(id)) return;
    if(create) WorkerCreateResult(id,handle,group,ok,json); else Complete(id,ok,ok?"":"Native worker evaluation failed",json);
  },target.session);
}
void WorkerTick() {
  std::vector<std::pair<std::string,uint64_t>> expired;
  for (const auto& [id,target] : worker_targets_) if (!target.published && !target.bootstrap_failed && Now() >= target.deadline) expired.emplace_back(id,target.generation);
  for (const auto& [id,generation] : expired) WorkerBootstrapFailed(id,generation,"Original 30000ms bootstrap deadline expired");
  // Expiry ends logical setup, not the accepted Inspector command. Keep its
  // one callback until an actual response or the genuine channel-close path.
  for(auto& [id,call]:worker_calls_)if(Now()>=call.deadline)call.expired=true;
}
bool WorkerNeedsTick() const { return std::any_of(worker_calls_.begin(),worker_calls_.end(),[](const auto& entry){return !entry.second.expired;}) || std::any_of(worker_targets_.begin(),worker_targets_.end(),[](const auto& entry) { return !entry.second.published&&!entry.second.bootstrap_failed; }); }
void WorkerClear() {
  std::vector<std::string> sessions; for (const auto& [session,id] : worker_sessions_) sessions.push_back(session);
  for (const auto& session : sessions) {
    const auto target = worker_targets_.at(worker_sessions_.at(session));
    if (target.context_destroyed||(target.kind == "worker"&&!worker_context_channel_)) {
      // WorkerClear is called only at actual owning-page/renderer closure.
      worker_targets_.at(target.id).context_destroyed=true;WorkerDetached(session);
    } else {
      // Shared/service worker execution can outlive this page. Close the actual
      // control channel explicitly, without pretending that author execution died.
      WorkerChannelClosed(target);
      WorkerInvalidateHandles(target.id); worker_targets_.erase(target.id); worker_sessions_.erase(session);
    }
  }
  worker_calls_.clear(); worker_handles_.clear(); worker_work_.clear();
}
void WorkerDiagnostics(Dict result) {
  result->SetInt("nativeWorkers",static_cast<int>(worker_targets_.size())); result->SetInt("nativeWorkerHandles",static_cast<int>(worker_handles_.size()));
  result->SetInt("nativeWorkerOperations",static_cast<int>(worker_work_.size())); result->SetInt("nativeWorkerBootstraps",static_cast<int>(worker_calls_.size()));
  result->SetInt("nativeWorkerPhysical",static_cast<int>(worker_physical_));
  size_t observed=0,failed=0,expired=0;for(const auto& [id,target]:worker_targets_){observed+=target.observed;failed+=target.bootstrap_failed;}for(const auto& [id,call]:worker_calls_)expired+=call.expired;
  result->SetInt("nativeWorkerObservedTargets",int(observed));result->SetInt("nativeWorkerFailedTargets",int(failed));result->SetInt("nativeWorkerExpiredBootstrapReceipts",int(expired));
}
