// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "include/cef_parser.h"
#include "include/cef_api_hash.h"
#include "drag_completions.h"
#include "inbound_string.h"
#include "operation_registry.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Fail exactly the first allocation after real cleanup responsibility transfer.
// The switch is cleared before throwing so error reporting/closure can allocate.
static bool* fail_after_transfer = nullptr;
[[gnu::noinline]] void* operator new(std::size_t bytes) {
  if (fail_after_transfer && !*fail_after_transfer) {
    fail_after_transfer = nullptr; throw std::bad_alloc();
  }
  if (void* value = std::malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* value) noexcept { std::free(value); }
[[gnu::noinline]] void operator delete(void* value, std::size_t) noexcept { std::free(value); }

// Actual production include, controlled Source registry and actual libCEF value
// envelopes. No CefInitialize/renderer/widget/event-delivery claim is made.
class Source {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Reply = std::function<void(bool, const std::string&)>;
  struct InputWork {
    int owner = 0; uint64_t generation = 0; std::string kind;
    bool busy = false, manual_prepared = false, held_mouse = false;
  };
  struct DevToolsCall {
    int owner; Reply callback; std::string session;
    bool retain_after_cancel = false; size_t response_limit = 1048576;
    bool retained_control = false;
  };
  struct FrameState {
    std::string session, loader = "loader1", context_identity = "identity1";
    uint64_t context_generation = 1;
  };
  struct Browser { int GetIdentifier() const { return 7; } };
  struct Call { int id, owner; std::string method; Dict params; };
  struct Result { int owner; bool ok; std::string error; };
  Source() { frame_states_[main_frame_id_] = FrameState{}; }
  ~Source() { Closed(); }
  double Now() const { return now; }
  void WakeDeadlines() { ++wakes; }
  void NetworkFatal(const std::string&) { ++posted_closes; }
  int AllocateDevToolsId() { return ++next_command; }
  bool SubmitDevTools(int id, const std::string& method, Dict params,
                      const std::string&) {
    if (refuse) return false;
    const auto found = devtools_.find(id); assert(found != devtools_.end());
    calls.push_back({id, found->second.owner, method, params->Copy(false)});
    if (!defer && !NativeDropDispatchReady(id)) calls.pop_back();
    if (synchronous && !calls.empty()) Ack();
    return true;
  }
  void DevTools(int owner, const std::string& method, Dict params, Reply reply) {
    const int command = AllocateDevToolsId();
    devtools_.emplace(command, DevToolsCall{owner, std::move(reply), ""});
    if (!params) params = CefDictionaryValue::Create();
    assert(SubmitDevTools(command, method, params, ""));
  }
  void SendDevTools(const std::string& method, Dict params) {
    cleanup.push_back({0, 0, method, params ? params->Copy(false) : nullptr});
  }
  std::string Json(Dict data) {
    auto value = CefValue::Create(); value->SetDictionary(data->Copy(false));
    return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  }
  double Number(Dict data, const char* name) {
    return data->GetType(name) == VTYPE_INT ? data->GetInt(name) : data->GetDouble(name);
  }
  Dict MousePacket(const std::string& type, const std::string& button, int) {
    auto packet = CefDictionaryValue::Create(); packet->SetString("type", type);
    packet->SetString("button", button); return packet;
  }
  void Complete(int id, bool ok, const std::string& error = "") {
    if (!operations_.Finish(id)) return;
    results.push_back({id, ok, error}); NativeDropCancel(id);
    NativeManualCancel(id, !ok); NativeDragCancel(id);
    input_work_.erase(id); form_clicks_.erase(id);
    for (auto it = devtools_.begin(); it != devtools_.end();)
      if (it->second.owner == id && !it->second.retain_after_cancel)
        it = devtools_.erase(it);
      else ++it;
  }
  void InputCall(std::shared_ptr<InputWork> work, const std::string& method,
                 Dict params, std::function<void()> done) {
    if (!NativeManualCurrent(work)) return;
    routed.push_back({0, work->owner, method, params->Copy(false)}); done();
  }
  void BeginInput(int id, uint64_t, Dict params) {
    routed.push_back({0, id, "BeginInput", params->Copy(false)});
  }
  void Evaluate(int id, const std::string&, Reply reply, const std::string&) {
    DevTools(id, "Runtime.evaluate", nullptr, std::move(reply));
  }
  void LocatorAction(int, uint64_t, const std::string&, const std::string&,
                     const std::string&, Dict) {}
  void Ack(bool ok = true, const std::string& json = "{}") {
    assert(!calls.empty()); auto call = std::move(calls.front()); calls.erase(calls.begin());
    const auto found = devtools_.find(call.id); if (found == devtools_.end()) return;
    auto reply = std::move(found->second.callback); devtools_.erase(found); reply(ok, json);
  }
  void FormAck(int id) {
    const auto found = form_clicks_.find(id); assert(found != form_clicks_.end());
    auto done = std::move(found->second); form_clicks_.erase(found); done();
  }
  void Closed() { devtools_.clear(); NativeDropClosed(); }
  Dict Data() {
    auto data = CefDictionaryValue::Create(); data->SetList("items", CefListValue::Create());
    data->SetInt("dragOperationsMask", 1); return data;
  }
  std::shared_ptr<InputWork> Manual(double timeout = 0) {
    const auto entry = operations_.Begin(1, now, timeout); assert(entry);
    auto work = std::make_shared<InputWork>(); work->owner = 1;
    work->generation = entry->generation; work->kind = "mouse";
    input_work_[1] = work; manual_drag_ = Data();
    assert(NativeDragNextIncarnation()); return work;
  }
  void DropManual(std::shared_ptr<InputWork> work, int& done) {
    auto packet = MousePacket("mouseReleased", "left", 1);
    assert(NativeManualInput(work, "Input.dispatchMouseEvent", packet, [&] { ++done; }));
  }

#include "controllers/input/drag_controller.inc"

  std::shared_ptr<NativeDragWork> Helper(double timeout = 0) {
    const auto entry = operations_.Begin(1, now, timeout); assert(entry);
    auto work = std::make_shared<NativeDragWork>(); work->owner = 1;
    work->generation = entry->generation; work->held = true;
    work->source = CefDictionaryValue::Create(); work->target = CefDictionaryValue::Create();
    work->data = Data(); drag_work_[1] = work;
    assert(NativeDragNextIncarnation()); return work;
  }
  SuiteCutOperationRegistry operations_;
  std::shared_ptr<Browser> browser_ = std::make_shared<Browser>();
  bool closing_page_ = false, renderer_failed_ = false;
  uint64_t native_pristine_renderer_epoch_ = 1;
  std::map<std::string, FrameState> frame_states_;
  std::map<int, DevToolsCall> devtools_;
  std::map<int, std::shared_ptr<InputWork>> input_work_;
  std::map<int, std::shared_ptr<NativeDragWork>> drag_work_;
  std::map<int, std::function<void()>> form_clicks_;
  std::map<int, std::string> operation_sessions_;
  Dict manual_drag_;
  std::string main_frame_id_ = "actual-frame";
  int mouse_buttons_ = 1, key_modifiers_ = 2, next_command = 0;
  int wakes = 0, posted_closes = 0;
  bool refuse = false, synchronous = false, defer = false;
  double mouse_x_ = 17, mouse_y_ = 23, now = 1;
  std::vector<Call> calls, cleanup, routed;
  std::vector<Result> results;
};

int main() {
  assert(cef_api_hash(CEF_API_VERSION, 0)); int groups = 0;
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    assert(s.calls.size() == 1 && s.devtools_.begin()->second.retain_after_cancel);
    assert(s.calls[0].params->GetString("type") == "drop"); s.Ack();
    assert(done == 0 && s.calls[0].method == "Input.setInterceptDrags"); s.Ack();
    assert(done == 1 && !s.NativeDropBusy() && !s.manual_drag_ && s.cleanup.empty()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    s.Complete(1, false, "caller abort");
    assert(s.devtools_.size() == 1 && s.cleanup.empty() && s.NativeDropBusy()); s.Ack();
    assert(s.calls.size() == 1 && s.calls[0].method == "Input.setInterceptDrags"); s.Ack();
    assert(done == 0 && s.results.size() == 1 && s.results[0].error == "caller abort");
    assert(!s.NativeDropBusy() && s.cleanup.empty() && !s.manual_drag_); ++groups; }
  { Source s; auto work = s.Manual(9); int done = 0; s.DropManual(work, done);
    s.now = 10; s.Ack(); assert(s.results.size() == 1 && !s.results[0].ok);
    assert(s.cleanup.empty() && s.calls[0].method == "Input.setInterceptDrags"); s.Ack();
    assert(!s.NativeDropBusy() && done == 0 && s.results.size() == 1); ++groups; }
  { Source s; auto work = s.Helper(); s.NativeDragDispatch(work, "drop", [] {});
    assert(!work->held); s.Complete(1, false, "caller abort"); s.Ack();
    assert(s.posted_closes == 1 && s.calls.empty() && s.cleanup.empty());
    assert(s.NativeDropBusy() && s.native_drop_->Retained() == 1); s.Closed();
    assert(!s.NativeDropBusy() && s.native_drop_->Retained() == 0); ++groups; }
  { Source s; auto work = s.Helper(); s.NativeDragDispatch(work, "drop", [] {}); s.Ack();
    assert(s.calls[0].method == "Input.dispatchMouseEvent"); s.Ack();
    assert(s.calls[0].method == "Input.setInterceptDrags"); s.Ack();
    assert(s.results.size() == 1 && s.results[0].ok && !s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Helper(); s.NativeDragDispatch(work, "drop", [] {}); s.Ack();
    s.Complete(1, false, "caller abort during mouse release"); s.Ack();
    assert(s.calls.empty() && s.cleanup.empty() && s.posted_closes == 1 && s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    ++s.frame_states_.begin()->second.context_generation; s.Ack();
    assert(s.calls.empty() && s.cleanup.empty() && done == 0 && s.posted_closes == 1);
    assert(s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    s.renderer_failed_ = true; s.Ack(false, "Target crashed");
    assert(s.NativeDropBusy() && s.posted_closes == 1 && s.cleanup.empty()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.refuse = true; s.DropManual(work, done);
    assert(s.devtools_.empty() && s.calls.empty() && s.posted_closes == 1 && done == 0);
    assert(s.NativeDropBusy() && s.cleanup.empty()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    s.Ack(false, "accepted error"); assert(s.calls.empty() && s.cleanup.empty());
    assert(s.posted_closes == 1 && s.NativeDropBusy() && s.results[0].error == "accepted error"); ++groups; }
  { Source s; auto work = s.Manual(9); int done = 0; s.DropManual(work, done);
    s.now = 30010; s.NativeDropTick(); assert(s.posted_closes == 1 && s.NativeDropBusy());
    assert(s.devtools_.size() == 1 && !s.NativeDropNeedsTick()); s.Ack();
    assert(s.calls.empty() && s.NativeDropBusy()); s.Closed(); assert(!s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.synchronous = true;
    s.DropManual(work, done); assert(done == 1 && s.calls.empty() && s.devtools_.empty());
    assert(!s.NativeDropBusy() && s.cleanup.empty()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    const auto old = s.devtools_.begin()->second.callback; s.Closed();
    old(true, "{}"); assert(done == 0 && s.calls.size() == 1 && !s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    const auto old = s.devtools_.begin()->second.callback; s.Ack(); old(true, "{}");
    assert(s.calls.size() == 1); s.Ack(); assert(done == 1 && s.cleanup.empty()); ++groups; }
  { Source s; auto work = s.Manual(); work->kind = "keyboard"; int done = 0;
    auto packet = s.MousePacket("keyDown", "left", 1); packet->SetString("key", "Escape");
    assert(s.NativeManualInput(work, "Input.dispatchKeyEvent", packet, [&] { ++done; }));
    assert(s.calls[0].params->GetString("type") == "dragCancel" && s.routed.empty());
    s.Ack(); assert(done == 1 && !s.manual_drag_ && s.routed.size() == 1); ++groups; }
  { Source s; auto work = s.Manual(); work->kind = "keyboard"; int done = 0;
    auto packet = s.MousePacket("keyDown", "left", 1); packet->SetString("key", "Escape");
    s.NativeManualInput(work, "Input.dispatchKeyEvent", packet, [&] { ++done; });
    s.Ack(false, "actual cancel error"); assert(!done && s.routed.empty() && !s.results[0].ok); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0;
    s.NativeManualInput(work, "Input.dispatchMouseEvent", s.MousePacket("mouseMoved", "left", 1), [&] { ++done; });
    assert(s.calls[0].params->GetString("type") == "dragOver"); s.Ack();
    assert(done == 1 && s.manual_drag_ && !s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0;
    s.NativeManualInput(work, "Input.dispatchMouseEvent", s.MousePacket("mouseMoved", "left", 1), [&] { ++done; });
    s.Ack(false, "actual over error"); assert(!done && !s.results[0].ok); ++groups; }
  { Source s; auto work = s.Manual(); s.manual_drag_ = nullptr; int done = 0;
    s.NativeManualInput(work, "Input.dispatchMouseEvent", s.MousePacket("mousePressed", "left", 1), [&] { ++done; });
    assert(s.calls[0].method == "Input.setInterceptDrags" && s.routed.empty());
    s.Ack(); assert(done == 1 && s.routed.size() == 1); ++groups; }
  { Source s; auto work = s.Manual(); s.manual_drag_ = nullptr; int done = 0;
    s.NativeManualInput(work, "Input.dispatchMouseEvent", s.MousePacket("mousePressed", "left", 1), [&] { ++done; });
    s.Ack(false, "actual intercept error"); assert(!done && s.routed.empty() && !s.results[0].ok); ++groups; }
  { Source s; auto work = s.Helper(); int done = 0;
    s.NativeDragDispatch(work, "dragEnter", [&] { ++done; }); s.Ack(false, "actual enter error");
    assert(!done && s.results.size() == 1 && !s.results[0].ok); ++groups; }
  { Source s; auto work = s.Helper(); s.NativeDragFinish(work); s.Ack(false, "actual disable error");
    assert(s.results.size() == 1 && !s.results[0].ok); ++groups; }
  { Source s; auto work = s.Helper(9); s.NativeDragTarget(work); s.FormAck(1);
    s.now = 10; s.Ack(); assert(s.results.size() == 1 && !s.results[0].ok && s.calls.empty()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.defer = true; s.DropManual(work, done);
    const int command = s.calls[0].id; s.Complete(1, false, "abort before queued send");
    assert(!s.NativeDropDispatchReady(command) && s.devtools_.empty());
    assert(s.cleanup.empty() && s.NativeDropBusy() && s.posted_closes == 1 && !done); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done); s.Ack();
    const int command = s.calls[0].id; ++s.frame_states_.begin()->second.context_generation;
    assert(!s.NativeDropDispatchReady(command) && s.devtools_.empty());
    assert(s.cleanup.empty() && s.NativeDropBusy() && s.posted_closes == 1 && !done); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    const int command = s.calls[0].id;
    assert(s.NativeDropDispatchRefused(command, "actual queued refusal"));
    assert(!s.NativeDropDispatchRefused(command, "duplicate refusal") && s.devtools_.empty());
    assert(s.posted_closes == 1 && s.NativeDropBusy() && !done); ++groups; }
  { Source s; auto work = s.Manual(); work->held_mouse = true; int done = 0;
    fail_after_transfer = &work->held_mouse; s.DropManual(work, done);
    assert(!fail_after_transfer && s.NativeDropBusy() && s.posted_closes == 1);
    assert(s.devtools_.empty() && s.cleanup.empty() && !done && !s.results[0].ok);
    s.Closed(); assert(!s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); work->held_mouse = true; int done = 0;
    s.NativeDropCompletions(); fail_after_transfer = &work->held_mouse; s.DropManual(work, done);
    assert(!fail_after_transfer && s.NativeDropBusy() && s.posted_closes == 1);
    assert(s.native_drop_->Retained() == 0 && s.devtools_.empty() && s.cleanup.empty());
    s.Closed(); assert(!s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    auto replacement = std::make_shared<Source::InputWork>(*work); s.input_work_[1] = replacement;
    s.Ack(); assert(!done && s.results.empty() && s.input_work_[1] == replacement);
    assert(s.NativeDropBusy() && s.posted_closes == 1 && s.calls.empty()); ++groups; }
  { Source s; auto work = s.Helper(); s.NativeDragDispatch(work, "drop", [] {});
    auto replacement = std::make_shared<Source::NativeDragWork>(*work); s.drag_work_[1] = replacement;
    s.Ack(); assert(s.calls.empty() && s.posted_closes == 1 && s.results.empty());
    assert(s.drag_work_[1] == replacement && s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    const auto entry = s.operations_.Begin(2, s.now, 0); assert(entry);
    auto keyboard = std::make_shared<Source::InputWork>(); keyboard->owner = 2;
    keyboard->generation = entry->generation; keyboard->kind = "keyboard";
    s.input_work_[2] = keyboard;
    auto packet = s.MousePacket("keyDown", "left", 1); packet->SetString("key", "Escape");
    int other_done = 0;
    assert(s.NativeManualInput(keyboard, "Input.dispatchKeyEvent", packet, [&] { ++other_done; }));
    assert(!other_done && s.calls.size() == 1 && s.cleanup.empty() && s.results.size() == 1);
    assert(s.results[0].owner == 2 && !s.results[0].ok && s.NativeDropBusy());
    s.Ack(); s.Ack(); assert(done == 1 && s.cleanup.empty() && !s.NativeDropBusy()); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    s.Complete(1, false, "caller abort"); const auto replacement = s.Data();
    s.manual_drag_ = replacement; s.Ack();
    assert(s.manual_drag_ == replacement && s.NativeDropBusy() && s.posted_closes == 1);
    assert(s.calls.empty() && s.cleanup.empty() && !done); ++groups; }
  { Source s; auto work = s.Manual(); int done = 0; s.DropManual(work, done);
    s.Complete(1, false, "caller abort"); ++s.native_drag_incarnation_;
    s.Ack(); assert(s.manual_drag_ && s.NativeDropBusy() && s.posted_closes == 1);
    assert(s.calls.empty() && s.cleanup.empty() && !done); ++groups; }
  std::printf("Native Drop retained Source/controller: %d controlled groups PASS\n", groups);
}
