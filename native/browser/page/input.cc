// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::Number(Dict message, const char* key) -> double {
  return message->GetType(key) == VTYPE_INT ? message->GetInt(key) : message->GetDouble(key);
}

auto Source::KeyPacket(const std::string& name, bool down) -> Dict {
  auto key = SuiteCutDescribeKey(name, key_modifiers_);
  if (!key) return nullptr;
  const bool repeat = down && pressed_keys_.contains(key->code);
  if (down)
    pressed_keys_[key->code] = *key;
  else
    pressed_keys_.erase(key->code);
  key_modifiers_ = 0;
  for (const auto& [code, held] : pressed_keys_) key_modifiers_ |= held.modifier;
  key = SuiteCutDescribeKey(name, key_modifiers_);
  auto params = CefDictionaryValue::Create();
  params->SetString("type", down ? (key->text.empty() ? "rawKeyDown" : "keyDown") : "keyUp");
  params->SetString("key", key->key);
  params->SetString("code", key->code);
  params->SetInt("windowsVirtualKeyCode", key->virtual_key);
  params->SetInt("nativeVirtualKeyCode", key->virtual_key);
#if defined(__APPLE__)
  if (down && key_modifiers_ == 4 && key->code == "KeyA") {
    auto commands = CefListValue::Create();
    commands->SetString(0, "selectAll");
    params->SetList("commands", commands);
  }
#endif
  params->SetInt("modifiers", key_modifiers_);
  params->SetBool("autoRepeat", repeat);
  params->SetInt("location", key->location);
  params->SetBool("isKeypad", key->location == 3);
  if (down && !key->text.empty()) {
    params->SetString("text", key->text);
    params->SetString("unmodifiedText", key->unmodified_text);
  }
  return params;
}

auto Source::MousePacket(const std::string& type, const std::string& button, int count) -> Dict {
  const int bit = button == "left" ? 1 : button == "right" ? 2 : button == "middle" ? 4 : 0;
  if (type == "mousePressed") mouse_buttons_ |= bit;
  if (type == "mouseReleased") mouse_buttons_ &= ~bit;
  auto params = CefDictionaryValue::Create();
  params->SetString("type", type);
  params->SetDouble("x", mouse_x_);
  params->SetDouble("y", mouse_y_);
  params->SetInt("buttons", mouse_buttons_);
  params->SetInt("modifiers", key_modifiers_);
  params->SetDouble("force", mouse_buttons_ ? 0.5 : 0.0);
  if (button != "none") {
    params->SetString("button", button);
    params->SetInt("clickCount", count);
  } else if (type == "mouseMoved" && mouse_buttons_)
    params->SetString("button", mouse_buttons_ & 1   ? "left"
                                : mouse_buttons_ & 2 ? "right"
                                                     : "middle");
  return params;
}

auto Source::DispatchCefMouse(Dict params) -> void {
  if (!browser_) return;
  CefMouseEvent event;
  event.x = static_cast<int>(std::lround(Number(params, "x")));
  event.y = static_cast<int>(std::lround(Number(params, "y")));
  const int keys = params->GetInt("modifiers"), buttons = params->GetInt("buttons");
  event.modifiers = (keys & 1 ? EVENTFLAG_ALT_DOWN : 0) | (keys & 2 ? EVENTFLAG_CONTROL_DOWN : 0) |
                    (keys & 4 ? EVENTFLAG_COMMAND_DOWN : 0) |
                    (keys & 8 ? EVENTFLAG_SHIFT_DOWN : 0) |
                    (buttons & 1 ? EVENTFLAG_LEFT_MOUSE_BUTTON : 0) |
                    (buttons & 2 ? EVENTFLAG_RIGHT_MOUSE_BUTTON : 0) |
                    (buttons & 4 ? EVENTFLAG_MIDDLE_MOUSE_BUTTON : 0);
  const auto type = params->GetString("type").ToString();
  if (type == "mouseMoved")
    browser_->GetHost()->SendMouseMoveEvent(event, false);
  else if (type == "mouseWheel")
    browser_->GetHost()->SendMouseWheelEvent(
        event, static_cast<int>(std::lround(Number(params, "deltaX"))),
        static_cast<int>(std::lround(Number(params, "deltaY"))));
  else {
    const auto button = params->GetString("button").ToString();
    browser_->GetHost()->SendMouseClickEvent(event,
                                             button == "right"    ? MBT_RIGHT
                                             : button == "middle" ? MBT_MIDDLE
                                                                  : MBT_LEFT,
                                             type == "mouseReleased", params->GetInt("clickCount"));
  }
}

auto Source::InputCall(std::shared_ptr<InputWork> work, const std::string& method, Dict params,
                       std::function<void()> done) -> void {
  TargetedKeyboardSubmitted(work, method, params);
  if (work->message->GetString("backend") == "widget") {
    NativeWidgetMouseCall(work, method, params, std::move(done));
    return;
  }
  if (NativeManualInput(work, method, params, done)) return;
  if (method == "Input.dispatchMouseEvent" && work->message->GetString("backend") == "cef") {
    work->busy = true;
    DispatchCefMouse(params);
    CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), work, done = std::move(done)] {
                  if (self->NativeManualCurrent(work)) {
                    work->busy = false;
                    done();
                  }
                }));
    return;
  }
  work->busy = true;
  operation_sessions_[work->owner] = "";
  DevTools(
      work->owner, method, params,
      [this, work, method, params, done = std::move(done)](bool ok, const std::string&) {
        if (!NativeManualCurrent(work)) return;
        work->busy = false;
        if (!ok) {
          Complete(work->owner, false, "Native input dispatch failed");
          return;
        }
        if (method == "Input.dispatchMouseEvent" && params->GetString("type") == "mousePressed" &&
            operation_frames_.contains(work->owner)) {
          const auto frame = frame_states_.find(operation_frames_.at(work->owner));
          if (frame == frame_states_.end() || !frame->second.context) {
            Complete(work->owner, false, "Native input frame detached after press");
            return;
          }
          auto barrier = CefDictionaryValue::Create();
          barrier->SetString("expression", "void 0");
          barrier->SetInt("contextId", frame->second.context);
          const auto identity = frame->second.context_identity, frame_id = frame->first;
          work->busy = true;
          DevTools(
              work->owner, "Runtime.evaluate", barrier,
              [this, work, identity, frame_id, done = std::move(done)](bool valid,
                                                                       const std::string& json) {
                if (!NativeManualCurrent(work)) return;
                const auto current = frame_states_.find(frame_id);
                const auto value = CefParseJSON(json, JSON_PARSER_RFC);
                const auto result = value ? value->GetDictionary() : nullptr;
                if (!valid || !result || result->HasKey("exceptionDetails") ||
                    current == frame_states_.end() ||
                    current->second.context_identity != identity) {
                  Complete(work->owner, false, "Native input frame context detached after press");
                  return;
                }
                work->busy = false;
                done();
              },
              frame->second.session);
          return;
        }
        done();
      });
}

auto Source::ContinueInput(std::shared_ptr<InputWork> work, int delay) -> void {
  if (delay > 0) {
    work->due = Now() + delay;
    return;
  }
  CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), work] { self->InputStep(work); }));
}

auto Source::PumpInput() -> void {
  for (const int id : {mouse_input_, keyboard_input_}) {
    const auto found = input_work_.find(id);
    if (found != input_work_.end() && !found->second->busy && found->second->due > 0 &&
        Now() >= found->second->due) {
      auto work = found->second;
      work->due = 0;
      InputStep(work);
    }
  }
}

auto Source::InputStep(std::shared_ptr<InputWork> work) -> void {
  const suitecut::NativeWorkOriginFrame handler_origin(work->native_origin);
  if (!browser_ || !operations_.IsCurrent(work->owner, work->generation) || work->busy ||
      work->widget_completing)
    return;
  // Recheck at queue head: another backend may have been admitted while the
  // original widget's earlier press was still awaiting its actual ACK.
  if (work->kind == "mouse" && native_widget_mouse_ && native_widget_mouse_->held() &&
      work->message->GetString("backend") != "widget") {
    Complete(work->owner, false, "Native mouse buttons are owned by the original-widget backend");
    return;
  }
  if (work->message->GetString("backend") == "widget" && mouse_buttons_ &&
      (!native_widget_mouse_ || !native_widget_mouse_->held())) {
    Complete(work->owner, false, "Native mouse buttons are owned by the legacy input backend");
    return;
  }
  if (work->message->GetString("backend") == "widget" &&
      operations_.IsExpired(work->owner, work->generation, Now())) {
    Complete(work->owner, false, "Native original-widget operation timed out before dispatch");
    return;
  }
  if (PrepareTargetedKeyboard(work)) return;
  browser_->GetHost()->SetFocus(true);
  const int id = work->owner;
  if (work->kind == "mouse") {
    if (work->action == "tap") {
      work->busy = true;
      BeginTouch(id, work->message);
      return;
    }
    const auto button = work->message->GetType("button") == VTYPE_STRING
                            ? work->message->GetString("button").ToString()
                            : "left";
    const int total =
        work->message->GetType("clickCount") == VTYPE_INT ? work->message->GetInt("clickCount") : 1;
    const int delay = work->message->GetInt("delay");
    if (work->action == "move") {
      const int steps =
          work->message->GetType("steps") == VTYPE_INT ? work->message->GetInt("steps") : 1;
      mouse_x_ = work->start_x + (Number(work->message, "x") - work->start_x) * work->step / steps;
      mouse_y_ = work->start_y + (Number(work->message, "y") - work->start_y) * work->step / steps;
      InputCall(work, "Input.dispatchMouseEvent", MousePacket("mouseMoved", "none", 0),
                [this, work, steps] {
                  if (work->step++ >= steps)
                    Complete(work->owner, true);
                  else
                    ContinueInput(work);
                });
      return;
    }
    if (work->action == "click") {
      if (work->phase == 0) {
        const int steps =
            work->message->GetType("steps") == VTYPE_INT ? work->message->GetInt("steps") : 1;
        mouse_x_ =
            work->start_x + (Number(work->message, "x") - work->start_x) * work->step / steps;
        mouse_y_ =
            work->start_y + (Number(work->message, "y") - work->start_y) * work->step / steps;
        if (work->step++ >= steps) work->phase = 1;
        InputCall(work, "Input.dispatchMouseEvent", MousePacket("mouseMoved", "none", 0),
                  [this, work] { ContinueInput(work); });
        return;
      }
      if (work->phase == 1) {
        if (work->pointer_target && operations_.IsExpired(id, work->generation, Now())) {
          Complete(id, false, "Native pointer original deadline expired before press");
          return;
        }
        // Locator geometry was collected before queueing and hovering. Focus,
        // scrolling or a mousemove handler can invalidate it before the press.
        // Reacquire only before the first down; never replay delivered input.
        if (work->pointer_target && !work->pointer_verified) {
          work->busy = true;
          LocatorAction(id, work->generation, "pointer target", "click", "", work->pointer_target,
                        work);
          return;
        }
        work->phase = 2;
        work->held_mouse = true;
        InputCall(work, "Input.dispatchMouseEvent",
                  MousePacket("mousePressed", button, work->count),
                  [this, work, delay] { ContinueInput(work, delay); });
        return;
      }
      InputCall(work, "Input.dispatchMouseEvent", MousePacket("mouseReleased", button, work->count),
                [this, work, total] {
                  work->held_mouse = false;
                  if (work->count++ >= total)
                    FinishActionInput(work);
                  else {
                    work->phase = 1;
                    ContinueInput(work);
                  }
                });
      return;
    }
    auto params = MousePacket(work->action == "down" ? "mousePressed"
                              : work->action == "up" ? "mouseReleased"
                                                     : "mouseWheel",
                              work->action == "wheel" ? "none" : button, total);
    if (work->action == "wheel") {
      params->SetDouble("deltaX", Number(work->message, "deltaX"));
      params->SetDouble("deltaY", Number(work->message, "deltaY"));
    }
    InputCall(work, "Input.dispatchMouseEvent", params, [this, id] { Complete(id, true); });
    return;
  }
  if (work->action == "prepareSendKeys") {
    work->busy = true;
    LocatorAction(id, work->generation,
                  suitecut::InboundDisplay(suitecut::InboundString(work->message, "selector")),
                  "sendKeys", work->text, work->message);
    return;
  }
  if (work->action == "insertText") {
    auto params = CefDictionaryValue::Create();
    params->SetString("text", work->text);
    InputCall(work, "Input.insertText", params, [this, id] { Complete(id, true); });
    return;
  }
  if (work->action == "down" || work->action == "up") {
    auto params = KeyPacket(work->key, work->action == "down");
    if (!params) {
      Complete(id, false, "Unknown key: " + Literal(work->key));
      return;
    }
    InputCall(work, "Input.dispatchKeyEvent", params, [this, id] { Complete(id, true); });
    return;
  }
  const int delay = work->message->GetInt("delay");
  if (work->action == "type") {
    if (work->message->GetBool("caretEnd") && !work->message->GetBool("caretPrepared")) {
      if (work->phase == 0) {
        work->phase = 1;
        work->held_keys.push_back("End");
        InputCall(work, "Input.dispatchKeyEvent", KeyPacket("End", true),
                  [this, work] { ContinueInput(work); });
        return;
      }
      auto params = KeyPacket("End", false);
      work->held_keys.clear();
      work->phase = 0;
      work->message->SetBool("caretPrepared", true);
      InputCall(work, "Input.dispatchKeyEvent", params, [this, work] { ContinueInput(work); });
      return;
    }
    if (work->standards_send_keys) {
      SendKeysSequenceStep(work);
      return;
    }
    if (work->cursor >= work->text.size()) {
      Complete(id, true);
      return;
    }
    if (work->phase == 0) {
      const auto first = static_cast<unsigned char>(work->text[work->cursor]);
      work->character_bytes = first < 128 ? 1 : first < 224 ? 2 : first < 240 ? 3 : 4;
      if (work->cursor + work->character_bytes > work->text.size()) {
        Complete(id, false, "Invalid native typing text");
        return;
      }
      work->key = work->text.substr(work->cursor, work->character_bytes);
      auto params = KeyPacket(work->key, true);
      if (!params) {
        params = CefDictionaryValue::Create();
        params->SetString("text", work->key);
        work->cursor += work->character_bytes;
        InputCall(work, "Input.insertText", params,
                  [this, work, delay] { ContinueInput(work, delay); });
        return;
      }
      work->held_keys.push_back(work->key);
      work->phase = 1;
      InputCall(work, "Input.dispatchKeyEvent", params,
                [this, work, delay] { ContinueInput(work, delay); });
      return;
    }
    auto params = KeyPacket(work->key, false);
    work->held_keys.clear();
    work->phase = 0;
    work->cursor += work->character_bytes;
    InputCall(work, "Input.dispatchKeyEvent", params, [this, work] { ContinueInput(work); });
    return;
  }
  // Chords preserve already-held modifiers and release temporary keys in
  // reverse order, including cancellation while a delay is outstanding.
  if (work->phase == 0) {
    const auto name = work->chord[work->cursor];
    const auto key = SuiteCutDescribeKey(name, key_modifiers_);
    if (work->cursor + 1 < work->chord.size() && pressed_keys_.contains(key->code)) {
      ++work->cursor;
      ContinueInput(work);
      return;
    }
    auto params = KeyPacket(name, true);
    work->held_keys.push_back(name);
    if (++work->cursor == work->chord.size()) work->phase = 1;
    InputCall(work, "Input.dispatchKeyEvent", params,
              [this, work, delay] { ContinueInput(work, work->phase == 1 ? delay : 0); });
    return;
  }
  if (work->held_keys.empty()) {
    Complete(id, true);
    return;
  }
  const auto name = work->held_keys.back();
  work->held_keys.pop_back();
  InputCall(work, "Input.dispatchKeyEvent", KeyPacket(name, false),
            [this, work] { ContinueInput(work); });
}

auto Source::SetupLocatorModifiers(int id, Dict message) -> void {
  const auto modifiers = message->GetList("modifiers");
  if (!modifiers) return;
  std::vector<std::string> before;
  for (const auto& [code, key] : pressed_keys_)
    if (key.modifier) before.push_back(code);
  locator_modifiers_[id] = before;
  int wanted = 0;
  for (size_t index = 0; index < modifiers->GetSize(); ++index) {
    auto key = SuiteCutDescribeKey(modifiers->GetString(index), 0);
    if (key) wanted |= key->modifier;
  }
  for (const auto& code : before) {
    auto key = SuiteCutDescribeKey(code, 0);
    if (!(wanted & key->modifier)) SendDevTools("Input.dispatchKeyEvent", KeyPacket(code, false));
  }
  for (const char* name : {"Alt", "Control", "Meta", "Shift"}) {
    auto key = SuiteCutDescribeKey(name, 0);
    if ((wanted & key->modifier) && !(key_modifiers_ & key->modifier))
      SendDevTools("Input.dispatchKeyEvent", KeyPacket(name, true));
  }
}

auto Source::RestoreLocatorModifiers(int id) -> void {
  const auto found = locator_modifiers_.find(id);
  if (found == locator_modifiers_.end()) return;
  const auto before = found->second;
  locator_modifiers_.erase(found);
  std::vector<std::string> current;
  for (const auto& [code, key] : pressed_keys_)
    if (key.modifier) current.push_back(code);
  for (const auto& code : current)
    if (std::find(before.begin(), before.end(), code) == before.end())
      SendDevTools("Input.dispatchKeyEvent", KeyPacket(code, false));
  for (const auto& code : before)
    if (!pressed_keys_.contains(code))
      SendDevTools("Input.dispatchKeyEvent", KeyPacket(code, true));
}

}  // namespace suitecut::browser
