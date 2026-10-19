// SPDX-License-Identifier: MIT
// Include inside Source. Genuine Chromium chooser events; native-owned handles.
void FileChooserSubscribe(int id, Dict message) {
  if (message->GetType("enabled") != VTYPE_BOOL) { Complete(id, false, "File chooser subscription requires a boolean"); return; }
  file_chooser_subscribed_ = message->GetBool("enabled");
  auto params = CefDictionaryValue::Create(); params->SetBool("enabled", file_chooser_subscribed_);
  for (const auto& [session, frame] : sessions_) { (void)frame; SendDevTools("Page.setInterceptFileChooserDialog", params, session); }
  operation_sessions_[id] = "";
  DevTools(id, "Page.setInterceptFileChooserDialog", params, [this, id](bool, const std::string&) { Complete(id, true); });
}
void FileChooserEvent(Dict data, const std::string& session) {
  if (!file_chooser_subscribed_ || !ready_) return;
  const auto frame_id = data->GetString("frameId").ToString();
  const auto frame = frame_states_.find(frame_id);
  const auto mode = data->GetString("mode").ToString();
  if (frame == frame_states_.end() || frame->second.session != session ||
      data->GetType("backendNodeId") != VTYPE_INT || data->GetInt("backendNodeId") <= 0 ||
      (mode != "selectSingle" && mode != "selectMultiple")) return;
  const auto sequence = ++event_sequence_;
  auto info = CefDictionaryValue::Create();
  info->SetString("id", handle_namespace_ + ":chooser:" + std::to_string(sequence));
  info->SetString("frame", frame_id); info->SetInt("backendNodeId", data->GetInt("backendNodeId"));
  info->SetBool("multiple", mode == "selectMultiple");
  const auto json = Json(info);
  JournalEvent(sequence, "filechooser", json);
  auto header = Header("filechooser"); header->SetDouble("sequence", static_cast<double>(sequence));
  SendPacket(header, std::vector<uint8_t>(json.begin(), json.end()));
  std::vector<int> waiters;
  for (const auto& [id, event] : event_waiters_) if (event == "filechooser") waiters.push_back(id);
  for (const int id : waiters) Complete(id, true, "", json);
}
void FileChooserElement(int id, Dict message) {
  if (message->GetType("backendNodeId") != VTYPE_INT || message->GetInt("backendNodeId") <= 0) {
    Complete(id, false, "Invalid native file chooser node"); return;
  }
  if (handles_.size() + pending_handle_groups_.size() >= 1024 || next_handle_ == std::numeric_limits<int>::max()) {
    Complete(id, false, "Native object handle limit exceeded"); return;
  }
  WithFrame(id, FrameId(message), [this, id, message](int context) {
    const auto frame_id = FrameId(message); const auto frame = frame_states_.find(frame_id);
    if (frame == frame_states_.end()) { Complete(id, false, "Native frame detached"); return; }
    const auto context_identity=frame->second.context_identity;const auto renderer_epoch=native_pristine_renderer_epoch_;
    const int handle_id = ++next_handle_;
    const auto group = "suitecut-handle-" + std::to_string(handle_id);
    pending_handle_groups_[id] = group;
    operation_frames_[id] = frame_id; operation_sessions_[id] = frame->second.session;
    auto params = CefDictionaryValue::Create(); params->SetInt("backendNodeId", message->GetInt("backendNodeId"));
    params->SetInt("executionContextId", context); params->SetString("objectGroup", group);
    DevTools(id, "DOM.resolveNode", params, [this, id, handle_id, context, frame_id, context_identity, renderer_epoch, group](bool, const std::string& json) {
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto remote = result ? result->GetDictionary("object") : nullptr;
      if (!remote || remote->GetType("objectId") != VTYPE_STRING) { Complete(id, false, "File chooser element is unavailable"); return; }
      const auto node = remote->GetString("objectId").ToString();
      auto params = CefDictionaryValue::Create(); params->SetString("objectId", node);
      params->SetString("functionDeclaration", "function(){if(!(this instanceof HTMLInputElement)||this.type!=='file')throw new Error('File chooser element is not a file input');return {value:this};}");
      params->SetBool("returnByValue", false); params->SetString("objectGroup", group);
      DevTools(id, "Runtime.callFunctionOn", params, [this, id, handle_id, context, frame_id, context_identity, renderer_epoch, group, node](bool, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto remote = result ? result->GetDictionary("result") : nullptr;
        if (!remote || result->HasKey("exceptionDetails") || remote->GetType("objectId") != VTYPE_STRING) { Complete(id, false, "File chooser input is unavailable"); return; }
        const auto frame = frame_states_.find(frame_id);
        if (frame == frame_states_.end() || frame->second.context != context || frame->second.context_identity!=context_identity || native_pristine_renderer_epoch_!=renderer_epoch) { Complete(id, false, "File chooser execution context was destroyed"); return; }
        handles_.emplace(handle_id, ObjectHandle{remote->GetString("objectId").ToString(), group, frame_id, context, frame->second.session, node, context_identity, renderer_epoch});
        pending_handle_groups_.erase(id);
        auto response = CefDictionaryValue::Create(); response->SetInt("handle", handle_id);
        response->SetString("guid", HandleGuid(handle_id)); response->SetString("frame", frame_id);
        response->SetString("preview", "JSHandle@input"); response->SetBool("node", true);
        Complete(id, true, "", Json(response));
      });
    });
  });
}
