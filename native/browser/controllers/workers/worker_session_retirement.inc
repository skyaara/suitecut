// SPDX-License-Identifier: MIT
// Actual session closure also retires flattened worker descendant channels.
// Reuse the same complete Source teardown for network/body/binding/AX owners;
// no timer, URL match or logical worker failure invokes this boundary.
void NativeTargetSessionDetached(const std::string& root){
  for(const auto& child:WorkerSessionClosures(root,native_target_parents_)){
      const auto debugger_attachment=NativeFrameDebuggerAttachment(child);
      const auto worker_debugger_attachment=NativeWorkerDebuggerChannel(child);
      if(element_states_)element_states_->Detached(child);
      NativeWebSocketPolicyDetach(child);
      WorkerConsoleSessionRemoved(child);
      WorkerDetached(child);
      native_target_parents_.erase(child);
      sessions_.erase(child); pending_child_bootstraps_.erase(child);NativeTraceSessionDetached(child);
      // Retire coverage only at this genuine target-close boundary, before
      // retained Inspector callbacks can attempt teardown on a gone session.
      NativePristineSessionDetached(child);
      NativeDebuggerDetached(child);
      NativeCoverageDetached(child);
      NativeHighlightDetached(child);
      if(snapshot_references_)snapshot_references_->ChannelDestroyed(child);
      OwnerFrameDocumentsDetached(child);
      if (bindings_) bindings_->SessionDetached(child);
      if(page_errors_)page_errors_->Detached(child);
      std::vector<suitecut::BindingControls::Result> binding_detached;
      for (auto it = binding_calls_.begin(); it != binding_calls_.end();) {
        if (it->second.session == child) { binding_detached.push_back(std::move(it->second.callback)); it = binding_calls_.erase(it); }
        else ++it;
      }
      for (auto& callback : binding_detached) callback(false,"Native binding target detached");
      std::vector<suitecut::NetworkControls::Result> detached_callbacks;
      for (auto it = network_calls_.begin(); it != network_calls_.end();) {
        if (it->second.session == child) { detached_callbacks.push_back(std::move(it->second.callback)); it = network_calls_.erase(it); }
        else ++it;
      }
      if (network_) network_->InterceptionSessionClosed(child);
      for (auto& callback : detached_callbacks) callback(false, "Target closed");
      if(sockets_)sockets_->SessionDetached(child,Now());
      GeolocationDetach(child);
      if (network_) network_->SessionDetached(child);
      for (auto it = session_trees_.begin(); it != session_trees_.end();) {
        if (it->second == child) it = session_trees_.erase(it); else ++it;
      }
      // The actual child session is gone; its retained AX/media commands no longer
      // perform physical browser work, so retire their retained ownership now.
      CompleteVisibilityInContext(child);
      std::vector<DevToolsCallback> ax_detached;
      for(auto it=devtools_.begin();it!=devtools_.end();){if(it->second.retained_control&&it->second.session==child){ax_detached.push_back(std::move(it->second.callback));it=devtools_.erase(it);}else ++it;}
      for(auto& callback:ax_detached)callback(false,"{\"message\":\"Native retained command session was detached\"}");
      NativeFrameDebuggerDetached(child,debugger_attachment);
      NativeDeveloperWorkerChannelClosed(child,worker_debugger_attachment.observer,worker_debugger_attachment.generation);
      NativeWorkerDebuggerChannelDrained(child,worker_debugger_attachment);
      // A target can disappear during a process swap while the logical frame
      // remains attached. Page.frameDetached(reason=remove) owns detachment.
      for (auto& [id, frame] : frame_states_) if (frame.session == child) frame.context = 0;
      std::vector<int> canceled;
      for (const auto& [id, current] : operation_sessions_) if (current == child && !navigations_.contains(id) && !action_navigations_.contains(id) && !url_waits_.contains(id)) canceled.push_back(id);
      for (const int id : canceled) {
        if (count_queries_.contains(id)) CompleteCountZero(id);
        else if (context_retries_.contains(id)&&!author_query_evaluations_.contains(id)) RetryContext(id);
        else Complete(id, false, "Native execution context destroyed by process swap");
      }
  }
}
