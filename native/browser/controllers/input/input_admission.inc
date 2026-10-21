// SPDX-License-Identifier: MIT
// Existing native input validation/admission; no separate target scheduler.
  void BeginInput(int id, uint64_t generation, Dict message, Dict keyboard_target = nullptr,
                  Dict pointer_target = nullptr) {
    if (NativeDropBusy()) { Complete(id, false, "Native pointer cleanup is physically owned by an earlier drag"); return; }
    if (message->HasKey("backend") && (message->GetType("backend") != VTYPE_STRING ||
        (message->GetString("backend") != "cef" && message->GetString("backend") != "devtools" && message->GetString("backend") != "widget"))) {
      Complete(id, false, "Invalid native pointer backend"); return;
    }
    const bool widget_backend=message->GetString("backend")=="widget";
    if(widget_backend&&(message->GetString("kind")!="mouse"||message->GetString("action")=="wheel"||message->GetString("action")=="tap")){
      Complete(id,false,"Original-widget backend supports raw mouse move, press and release only");return;
    }
    if(widget_backend&&(!native_widget_mouse_||!native_widget_mouse_->available())){Complete(id,false,"Original-widget receipts require the custom CEF backend");return;}
    if(message->GetString("kind")=="mouse"&&native_widget_mouse_&&native_widget_mouse_->held()&&!widget_backend){Complete(id,false,"Native mouse buttons are owned by the original-widget backend");return;}
    if(widget_backend&&mouse_buttons_&&(!native_widget_mouse_||!native_widget_mouse_->held())){Complete(id,false,"Native mouse buttons are owned by the legacy input backend");return;}
    auto work = std::make_shared<InputWork>(); work->owner = id; work->generation = generation;
    work->kind = message->GetString("kind").ToString(); work->action = message->GetString("action").ToString();
    if(work->kind=="touch"&&work->action=="tap")work->kind="mouse";
    work->key = message->GetString("key").ToString(); work->text = message->GetString("text").ToString();
    work->message = message->Copy(false);
    work->keyboard_target = keyboard_target;
    work->pointer_target = pointer_target;
    const int delay = message->GetInt("delay");
    if ((message->HasKey("delay") && message->GetType("delay") != VTYPE_INT) || delay < 0 || delay > 120000) { Complete(id, false, "Invalid native input delay"); return; }
    if (work->kind == "mouse") {
      if (work->action != "move" && work->action != "down" && work->action != "up" && work->action != "click" && work->action != "tap" && work->action != "wheel") { Complete(id, false, "Invalid native pointer action"); return; }
      if (((work->action == "move" || work->action == "click" || work->action=="tap") && (!message->HasKey("x") || !message->HasKey("y"))) ||
          (work->action == "wheel" && (!message->HasKey("deltaX") || !message->HasKey("deltaY")))) { Complete(id, false, "Missing native pointer coordinates"); return; }
      for (const char* field : {"x", "y", "deltaX", "deltaY"}) if (message->HasKey(field) &&
          ((message->GetType(field) != VTYPE_INT && message->GetType(field) != VTYPE_DOUBLE) || !std::isfinite(Number(message, field)) || std::abs(Number(message, field)) > 1000000)) { Complete(id, false, "Invalid native pointer coordinate"); return; }
      const auto button = message->GetString("button").ToString();
      if (!button.empty() && button != "left" && button != "right" && button != "middle") { Complete(id, false, "Invalid native pointer button"); return; }
      if ((message->HasKey("steps") && (message->GetType("steps") != VTYPE_INT || message->GetInt("steps") < 1 || message->GetInt("steps") > 10000)) ||
          (message->HasKey("clickCount") && (message->GetType("clickCount") != VTYPE_INT || message->GetInt("clickCount") < 1 || message->GetInt("clickCount") > 1000))) { Complete(id, false, "Invalid native pointer count"); return; }
    } else if (work->kind == "keyboard") {
      if (work->action != "down" && work->action != "up" && work->action != "press" && work->action != "type" && work->action != "insertText") { Complete(id, false, "Invalid native keyboard action"); return; }
      if (((work->action == "down" || work->action == "up" || work->action == "press") && (message->GetType("key") != VTYPE_STRING || work->key.empty() || work->key.size() > 128)) ||
          ((work->action == "type" || work->action == "insertText") && message->GetType("text") != VTYPE_STRING)) { Complete(id, false, "Invalid native keyboard payload"); return; }
      if (work->action == "press") {
        auto chord = work->key;
        const bool plus = chord == "+" || chord.ends_with("++");
        if (plus) chord.erase(chord.size() - (chord == "+" ? 1 : 2));
        size_t start = 0;
        for (size_t index = 0; index <= chord.size(); ++index) if (index == chord.size() || chord[index] == '+') {
          if (index > start) work->chord.push_back(chord.substr(start, index - start));
          else if (!chord.empty()) { Complete(id, false, "Unknown key: " + Literal(work->key)); return; }
          start = index + 1;
        }
        if (plus) work->chord.push_back("+");
        if (work->chord.empty() || work->chord.size() > 9) { Complete(id, false, "Unknown key: " + Literal(work->key)); return; }
        for (size_t index = 0; index < work->chord.size(); ++index) {
          auto key = SuiteCutDescribeKey(work->chord[index], 0);
          if (!key || (index + 1 < work->chord.size() && !key->modifier)) { Complete(id, false, "Unknown key: " + Literal(work->chord[index])); return; }
        }
      }
    } else { Complete(id, false, "Invalid native input device"); return; }
    input_work_[id] = work;
    (work->kind == "mouse" ? mouse_queue_ : keyboard_queue_).push_back(id); StartInput(work->kind);
  }
