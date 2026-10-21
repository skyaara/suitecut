// SPDX-License-Identifier: MIT
// Include inside Source. Uses actual browser drag data and trusted protocol input.
struct NativeDragWork {
  int owner = 0;
  uint64_t generation = 0;
  Dict source, target, data;
  bool held = false;
};
#include "drag_completion_controller.inc"
bool NativeDragCurrent(const std::shared_ptr<NativeDragWork>&work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  const auto current=drag_work_.find(work->owner);
  if(current==drag_work_.end()||current->second!=work)return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"Native drag original operation timed out");return false;}
  return true;
}
bool NativeDragReply(const std::shared_ptr<NativeDragWork>&work,bool ok,const std::string&json,const char*method){
  if(!NativeDragCurrent(work))return false;
  if(!ok){Complete(work->owner,false,std::string("Native drag ")+method+" failed"+(json.empty()?"":": "+json.substr(0,4096)));return false;}
  return true;
}
void NativeDragCancel(int id) {
  if (NativeDropOwns(id)) { drag_work_.erase(id); return; }
  const auto found = drag_work_.find(id); if (found == drag_work_.end()) return;
  const auto work = found->second; drag_work_.erase(found);
  auto intercept = CefDictionaryValue::Create(); intercept->SetBool("enabled", false);
  SendDevTools("Input.setInterceptDrags", intercept);
  if (work->held&&(!native_widget_mouse_||!native_widget_mouse_->held())) {
    if(work->data)SendDevTools("Input.dispatchDragEvent",NativeManualDragPacket("dragCancel",work->data));
    else SendDevTools("Input.cancelDragging", nullptr);
    SendDevTools("Input.dispatchMouseEvent", MousePacket("mouseReleased", "left", 1));
  }
}
void NativeDragFinish(std::shared_ptr<NativeDragWork> work) {
  if (!NativeDragCurrent(work)) return;
  work->held = false;
  auto params = CefDictionaryValue::Create(); params->SetBool("enabled", false);
  operation_sessions_[work->owner] = "";
  DevTools(work->owner, "Input.setInterceptDrags", params, [this, work](bool ok, const std::string&json) {
    if(!NativeDragReply(work,ok,json,"setInterceptDrags"))return;
    drag_work_.erase(work->owner);
    Complete(work->owner, true);
  });
}
void NativeDragInput(std::shared_ptr<NativeDragWork> work, const std::string& action, double x, double y, std::function<void()> done) {
  if (!NativeDragCurrent(work)) return;
  auto params = CefDictionaryValue::Create(); params->SetString("kind", "mouse"); params->SetString("action", action);
  params->SetString("button", "left"); params->SetInt("clickCount", 1);
  if (action == "move") { params->SetDouble("x", x); params->SetDouble("y", y); params->SetInt("steps", 1); }
  form_clicks_[work->owner] = [this,work,done=std::move(done)]{if(NativeDragCurrent(work))done();};
  BeginInput(work->owner, work->generation, params);
}
void NativeDragDispatch(std::shared_ptr<NativeDragWork> work, const std::string& type, std::function<void()> done) {
  if (!NativeDragCurrent(work)) return;
  if(!work->data){Complete(work->owner,false,"Native intercepted drag data is unavailable");return;}
  if (type == "drop") {
    NativeDropBegin(work->owner, work->generation, work->data, work, nullptr,
                    [this, work] { Complete(work->owner, true); });
    return;
  }
  auto params = CefDictionaryValue::Create(); params->SetString("type", type);
  params->SetDouble("x", mouse_x_); params->SetDouble("y", mouse_y_);
  params->SetDictionary("data", work->data->Copy(false)); params->SetInt("modifiers", key_modifiers_);
  operation_sessions_[work->owner] = "";
  DevTools(work->owner, "Input.dispatchDragEvent", params, [this,work,done=std::move(done)](bool ok,const std::string&json){if(NativeDragReply(work,ok,json,"dispatchDragEvent"))done();});
}
void NativeDragTarget(std::shared_ptr<NativeDragWork> work) {
  if (!NativeDragCurrent(work)) return;
  form_clicks_[work->owner] = [this, work] {
    if(!NativeDragCurrent(work))return;
    if (work->source->GetBool("trial")) { drag_work_.erase(work->owner); Complete(work->owner, true); return; }
    // A renderer barrier orders genuine dragstart/interception before drop.
    Evaluate(work->owner, "true", [this, work](bool ok, const std::string&json) {
      if(!NativeDragReply(work,ok,json,"renderer barrier"))return;
      const auto release = [this, work] {
        NativeDragInput(work, "up", 0, 0, [this, work] { NativeDragFinish(work); });
      };
      if (!work->data) { release(); return; }
      NativeDragDispatch(work, "dragEnter", [this, work, release] {
        // Chromium's native drop performs its own DragTargetDragOver first.
        NativeDragDispatch(work, "drop", release);
      });
    }, main_frame_id_);
  };
  LocatorAction(work->owner, work->generation, suitecut::InboundDisplay(suitecut::InboundString(work->target,"selector")), "hover", "", work->target);
}
void NativeDragEvent(Dict data) {
  if(NativeWidgetPointerOwned()){NetworkFatal("Raw original-widget input cannot enter the legacy intercepted drag controller");return;}
  if (NativeDropBusy()) {
    NetworkFatal("Native intercepted drag arrived while a completion is physically owned"); return;
  }
  const auto drag = data->GetDictionary("data");
  if (!drag || Json(drag).size() > 1048576 || !drag->GetList("items") || drag->GetList("items")->GetSize() > 1024) {
    if (!drag_work_.empty()) Complete(drag_work_.begin()->first, false, "Native drag data exceeds its bounded capacity");
    else { SendDevTools("Input.cancelDragging", nullptr); manual_drag_ = nullptr; }
    return;
  }
  if (!drag_work_.empty()) { drag_work_.begin()->second->data = drag->Copy(false); return; }
  if (!(mouse_buttons_ & 1)) { SendDevTools("Input.cancelDragging", nullptr); return; }
  if (!NativeDragNextIncarnation()) return;
  manual_drag_ = drag->Copy(false);
  SendDevTools("Input.dispatchDragEvent", NativeManualDragPacket("dragEnter", manual_drag_));
}

void NativeDragStart(int id, uint64_t generation, Dict message) {
  if(!locator_handlers_.empty()&&LocatorHandlerBefore(id,generation,message,[this,id,generation,message]{NativeDragStart(id,generation,message);}))return;
  if (NativeWidgetPointerOwned() || NativeDropBusy() || !drag_work_.empty() || mouse_buttons_) { Complete(id, false, "Native pointer is already performing a drag or holding a button"); return; }
  const auto target = message->GetDictionary("target");
  std::string source_selector,target_selector;
  if (!suitecut::InboundString(message,"selector",source_selector) || !target || !suitecut::InboundString(target,"selector",target_selector) ||
      (message->HasKey("force") && message->GetType("force") != VTYPE_BOOL) ||
      (message->HasKey("trial") && message->GetType("trial") != VTYPE_BOOL) ||
      (message->HasKey("steps") && (message->GetType("steps") != VTYPE_INT || message->GetInt("steps") < 1 || message->GetInt("steps") > 10000))) {
    Complete(id, false, "Invalid native drag action"); return;
  }
  for (const char* name : {"sourcePosition", "targetPosition"}) if (message->HasKey(name)) {
    const auto position = message->GetDictionary(name);
    if (!position) { Complete(id, false, "Invalid native drag position"); return; }
    for (const char* axis : {"x", "y"}) if ((position->GetType(axis) != VTYPE_INT && position->GetType(axis) != VTYPE_DOUBLE) || !std::isfinite(Number(position, axis))) { Complete(id, false, "Invalid native drag position"); return; }
  }
  if (!NativeDragNextIncarnation()) { Complete(id, false, "Native drag incarnation unavailable"); return; }
  auto work = std::make_shared<NativeDragWork>(); work->owner = id; work->generation = generation;
  work->source = message->Copy(false); work->source->Remove("target"); work->source->SetBool("formCheck", true);
  work->target = target->Copy(false); work->target->SetBool("formCheck", true);
  work->target->SetInt("steps", message->HasKey("steps") ? message->GetInt("steps") : 1);
  work->source->SetInt("steps", 1);
  work->target->SetBool("force", message->GetBool("force")); work->target->SetBool("trial", message->GetBool("trial"));
  if (message->HasKey("sourcePosition")) work->source->SetDictionary("position", message->GetDictionary("sourcePosition"));
  if (message->HasKey("targetPosition")) work->target->SetDictionary("position", message->GetDictionary("targetPosition"));
  drag_work_[id] = work;
  form_clicks_[id] = [this, work] {
    if(!NativeDragCurrent(work))return;
    if (work->source->GetBool("trial")) { NativeDragTarget(work); return; }
    auto params = CefDictionaryValue::Create(); params->SetBool("enabled", true);
    operation_sessions_[work->owner] = "";
    DevTools(work->owner, "Input.setInterceptDrags", params, [this, work](bool ok, const std::string&json) {
      if(!NativeDragReply(work,ok,json,"setInterceptDrags"))return;
      work->held = true;
      NativeDragInput(work, "down", 0, 0, [this, work] { NativeDragTarget(work); });
    });
  };
  LocatorAction(id, generation, suitecut::InboundDisplay(suitecut::InboundString(work->source,"selector")), "hover", "", work->source);
}

Dict NativeManualDragPacket(const std::string& type, Dict data) {
  auto params = CefDictionaryValue::Create(); params->SetString("type", type);
  params->SetDouble("x", mouse_x_); params->SetDouble("y", mouse_y_);
  params->SetInt("modifiers", key_modifiers_); params->SetDictionary("data", data->Copy(false));
  return params;
}
void NativeManualCancel(int id, bool failed) {
  if (NativeDropOwns(id)) return;
  if (!failed) return;
  if(native_widget_mouse_&&native_widget_mouse_->held())return;
  const auto input = input_work_.find(id);
  if (input == input_work_.end() || input->second->kind != "mouse" || !drag_work_.empty() || input->second->message->GetString("backend")=="widget") return;
  if (manual_drag_) { SendDevTools("Input.dispatchDragEvent", NativeManualDragPacket("dragCancel", manual_drag_)); manual_drag_ = nullptr; }
  if (mouse_buttons_ & 1) SendDevTools("Input.dispatchMouseEvent", MousePacket("mouseReleased", "left", 1));
  auto params = CefDictionaryValue::Create(); params->SetBool("enabled", false); SendDevTools("Input.setInterceptDrags", params);
}
bool NativeManualCurrent(const std::shared_ptr<InputWork>&work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  const auto current=input_work_.find(work->owner);
  if(current==input_work_.end()||current->second!=work)return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"Native input original operation timed out");return false;}
  return true;
}
bool NativeManualReply(const std::shared_ptr<InputWork>&work,bool ok,const std::string&json,const char*method){
  if(!NativeManualCurrent(work))return false;
  if(!ok){Complete(work->owner,false,std::string("Native intercepted input ")+method+" failed"+(json.empty()?"":": "+json.substr(0,4096)));return false;}
  return true;
}
bool NativeManualInput(std::shared_ptr<InputWork> work, const std::string& method, Dict params, std::function<void()> done) {
  if(!NativeManualCurrent(work))return true;
  if (NativeDropBusy()) {
    if (!NativeDropOwns(work->owner))
      Complete(work->owner, false, "Native input conflicts with a physically owned drag completion");
    return true;
  }
  if (!drag_work_.empty()) return false;
  const auto type = params->GetString("type").ToString();
  if (method == "Input.dispatchMouseEvent" && type == "mousePressed" && params->GetString("button") == "left" && !work->manual_prepared) {
    work->manual_prepared = true; work->busy = true; operation_sessions_[work->owner] = "";
    auto intercept = CefDictionaryValue::Create(); intercept->SetBool("enabled", true);
    DevTools(work->owner, "Input.setInterceptDrags", intercept, [this, work, method, params, done](bool ok, const std::string&json) {
      if(!NativeManualReply(work,ok,json,"setInterceptDrags"))return;
      work->busy = false; InputCall(work, method, params, done);
    }); return true;
  }
  if (!manual_drag_) return false;
  if (method == "Input.dispatchKeyEvent" && params->GetString("key") == "Escape" && type != "keyUp") {
    work->busy = true; operation_sessions_[work->owner] = "";
    // Interception clears Chromium's DragController state, so cancelDragging
    // would ACK without ending this genuine intercepted source drag.
    DevTools(work->owner, "Input.dispatchDragEvent", NativeManualDragPacket("dragCancel",manual_drag_), [this, work, method, params, done](bool ok,const std::string&json) {
      if(!NativeManualReply(work,ok,json,"dragCancel"))return;
      manual_drag_ = nullptr; work->busy = false; InputCall(work, method, params, done);
    }); return true;
  }
  if (method != "Input.dispatchMouseEvent") return false;
  if (type == "mouseMoved") {
    work->busy = true; operation_sessions_[work->owner] = "";
    DevTools(work->owner, "Input.dispatchDragEvent", NativeManualDragPacket("dragOver", manual_drag_), [this,work,done](bool ok,const std::string&json) {
      if(!NativeManualReply(work,ok,json,"dragOver"))return;
      work->busy = false; done();
    }); return true;
  }
  if (type == "mouseReleased" && params->GetString("button") == "left") {
    work->busy = true;
    NativeDropBegin(work->owner, work->generation, manual_drag_, nullptr, work,
                    std::move(done));
    return true;
  }
  return false;
}
