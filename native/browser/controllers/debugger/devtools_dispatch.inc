// SPDX-License-Identifier: MIT
// Included inside Source. CEF observers must return before another submission.
struct DevToolsObserverScope {
  int& depth;
  explicit DevToolsObserverScope(int& value) : depth(value) { ++depth; }
  ~DevToolsObserverScope() { --depth; }
};
struct QueuedDevToolsMessage { int id; std::string json; bool operation_owned; bool final_release;bool trace_owned=false;std::string session;bool initial_tree=false;uint64_t tree_attachment=0; };
int devtools_observer_depth_ = 0;
bool devtools_dispatch_task_ = false;
size_t devtools_dispatch_bytes_ = 0;
std::deque<QueuedDevToolsMessage> devtools_dispatch_;
void DeferredDevToolsFailure(int id) {
  if (NativeDropDispatchRefused(id,"Native queued drag submission was refused")) return;
  if(native_trace_protocol_.contains(id)){NativeTraceDispatchFailed(id);return;}
  const auto tool = devtools_.find(id);
  if (tool != devtools_.end()) {
    auto call = std::move(tool->second); devtools_.erase(tool);
    if (artifacts_ && call.retain_after_cancel && !call.retained_control) artifacts_->PhysicalDispatchFailed(call.owner);
    call.callback(false,"Native deferred protocol dispatch failed"); return;
  }
  const auto network = network_calls_.find(id);
  if (network != network_calls_.end()) {
    auto callback = std::move(network->second.callback); network_calls_.erase(network);
    callback(false,"Native deferred network dispatch failed"); return;
  }
  const auto binding = binding_calls_.find(id);
  if (binding != binding_calls_.end()) {
    auto callback = std::move(binding->second.callback); binding_calls_.erase(binding);
    callback(false,"Native deferred binding dispatch failed"); return;
  }
  utility_worlds_.erase(id); session_trees_.erase(id);
  if (browser_ && !closing_page_) {
    browser_->GetHost()->CloseBrowser(true);
    Error("Native deferred protocol dispatch failed");
  }
}
void WakeDeferredDevTools() {
  if (devtools_dispatch_task_ || devtools_dispatch_.empty() || closing_page_) return;
  devtools_dispatch_task_ = true;
  CefPostTask(TID_UI,new Task([self = CefRefPtr<Source>(this)] {
    // Bound each UI turn so timers, input and cancellation continue to run.
    for (int count = 0; count < 64 && !self->devtools_dispatch_.empty(); ++count) {
      auto message = std::move(self->devtools_dispatch_.front()); self->devtools_dispatch_.pop_front();
      self->devtools_dispatch_bytes_ -= message.json.size()+message.session.size();
      if (!self->browser_ || self->closing_page_) break;
      // A canceled logical command has not executed yet. Physical readback
      // callbacks retained by Source remain registered and settle normally.
      if(message.trace_owned&&!self->NativeTraceDispatchReady(message.id)){self->NativeInspectorNeverDispatched(message.id);continue;}
      if (message.operation_owned && !self->devtools_.contains(message.id)){self->NativeInspectorNeverDispatched(message.id);continue;}
      const auto tool = self->devtools_.find(message.id);
      if (tool != self->devtools_.end() && self->SendKeysPhysicalPending(tool->second.owner) &&
          !self->operations_.Contains(tool->second.owner) && !self->SendKeysCleanupCommand(message.id) &&
          !tool->second.selector_group_release) {
        self->NativeInspectorNeverDispatched(message.id);self->DeferredDevToolsFailure(message.id); continue;
      }
      if (!self->NativeIdleDispatchCurrent(message.id)){self->NativeInspectorNeverDispatched(message.id);self->DeferredDevToolsFailure(message.id);continue;}
      if (!self->NativeDropDispatchReady(message.id)){self->NativeInspectorNeverDispatched(message.id);continue;}
      if (message.final_release) self->MarkFinalReleaseSubmitted(message.id);
      if(!self->NativeFrameDebuggerBeforeSend(message.id,message.session,message.initial_tree,message.tree_attachment)){self->NativeInspectorNeverDispatched(message.id);continue;}
      const auto actual_browser=self->browser_;
      if(!actual_browser){self->NativeInspectorNeverDispatched(message.id);break;}
      if (!self->NativeInspectorCall(message.id,message.session,[actual_browser,&message]{return actual_browser->GetHost()->SendDevToolsMessage(message.json.data(),message.json.size());}))
        self->DeferredDevToolsFailure(message.id);
    }
    if (!self->browser_ || self->closing_page_) {
      self->devtools_dispatch_.clear(); self->devtools_dispatch_bytes_ = 0;
    }
    self->devtools_dispatch_task_ = false; self->WakeDeferredDevTools();
  }));
}
bool QueueDeferredDevTools(int id, std::string json, bool final_release,const std::string& session,bool initial_tree,uint64_t tree_attachment) {
  if (closing_page_ || devtools_dispatch_.size() >= 1024 || json.size() > 8 * 1048576 || session.size()>128 ||
      devtools_dispatch_bytes_ + json.size()+session.size() > 8 * 1048576) return false;
  const auto charge=json.size()+session.size();
  devtools_dispatch_.push_back({id,std::move(json),devtools_.contains(id),final_release,native_trace_protocol_.contains(id),session,initial_tree,tree_attachment});
  devtools_dispatch_bytes_ += charge;
  WakeDeferredDevTools(); return true;
}
