// SPDX-License-Identifier: MIT
// Actual native input retirement; retained callbacks precede queue handoff.
  void CleanupInput(int id) {
    const auto found = input_work_.find(id); if (found == input_work_.end()) return;
    const auto work = found->second;
    const suitecut::NativeWorkOriginFrame handler_origin(work->native_origin);
    if (SendKeysPhysicalPending(id)) return;
    if ((work->standards_send_keys || work->keyboard_target) && CleanupSendKeysSequence(work)) return;
    for (auto it = work->held_keys.rbegin(); !work->keyboard_target && it != work->held_keys.rend(); ++it) {
      auto params = KeyPacket(*it, false); if (params) SendDevTools("Input.dispatchKeyEvent", params);
    }
    if (work->held_mouse && work->message->GetString("backend")!="widget" && !NativeDropOwns(id)) {
      auto params = MousePacket("mouseReleased", work->message->GetString("button").ToString(), work->count);
      if (work->message->GetString("backend") == "cef") DispatchCefMouse(params);
      else SendDevTools("Input.dispatchMouseEvent", params);
    }
    input_work_.erase(found);
    int& active = work->kind == "mouse" ? mouse_input_ : keyboard_input_;
    auto& queue = work->kind == "mouse" ? mouse_queue_ : keyboard_queue_;
    queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
    if (active == id) {
      active = 0;
      CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), kind = work->kind] { self->StartInput(kind); }));
    }
  }
