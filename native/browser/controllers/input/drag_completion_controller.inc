// SPDX-License-Identifier: MIT
// Include inside Source, after NativeDragWork and InputWork are defined.
// Transport receipts are independent of logical operation/selector callbacks.
using NativeDropCore = suitecut::DragCompletions;
struct NativeDropDocument {
  std::string frame, session, loader, identity;
  uint64_t generation = 0;
};
struct NativeDropPayload {
  NativeDropCore::Scope scope;
  uint64_t renderer = 0;
  std::vector<NativeDropDocument> documents;
  std::string packet;
  Dict manual_data; // Read-only identity pin; never reparented into a packet.
  std::shared_ptr<NativeDragWork> helper;
  std::shared_ptr<InputWork> manual;
  std::function<void()> done;
};
static std::shared_ptr<NativeDropCore::Budget> NativeDropBudget() {
  static const auto budget = std::make_shared<NativeDropCore::Budget>();
  return budget;
}
bool NativeDropBusy() const {
  return native_drop_failed_owner_ || native_drop_payload_ ||
         (native_drop_ && native_drop_->Busy());
}
bool NativeDropOwns(int owner) const {
  return native_drop_failed_owner_ == owner ||
         (NativeDropBusy() && native_drop_payload_ &&
          native_drop_payload_->scope.owner == owner);
}
bool NativeDropManualCurrent(const std::shared_ptr<NativeDropPayload>& payload) const {
  if (!payload || !payload->manual || !payload->manual_data || !browser_ ||
      native_drag_incarnation_ != payload->scope.incarnation ||
      static_cast<uint64_t>(browser_->GetIdentifier()) != payload->scope.channel ||
      manual_drag_.get() != payload->manual_data.get()) return false;
  const auto found = input_work_.find(payload->scope.owner);
  return found == input_work_.end() || found->second == payload->manual;
}
void NativeDropRetired() {
  if (!native_drop_ || native_drop_->Busy()) return;
  // Only actual terminal ACKs or actual ChannelClosed retire the core. Logical
  // cancellation skips public success, but must release the same old manual
  // data after those receipts. Never clear a replacement incarnation/work/data.
  const auto payload = native_drop_payload_;
  if (NativeDropManualCurrent(payload)) manual_drag_ = nullptr;
  native_drop_payload_.reset(); WakeDeadlines();
}
bool NativeDropOperationCurrent(const NativeDropCore::Scope& scope) const {
  const auto payload = native_drop_payload_;
  if (!payload || !(payload->scope == scope) ||
      !operations_.IsCurrent(scope.owner, scope.generation)) return false;
  if (payload->manual) {
    const auto found = input_work_.find(scope.owner);
    return found != input_work_.end() && found->second == payload->manual;
  }
  const auto found = drag_work_.find(scope.owner);
  return found != drag_work_.end() && found->second == payload->helper;
}
bool NativeDropDocumentCurrent(const NativeDropCore::Scope& scope) const {
  const auto payload = native_drop_payload_;
  if (!payload || !(payload->scope == scope) || !browser_ || closing_page_ ||
      static_cast<uint64_t>(browser_->GetIdentifier()) != scope.channel ||
      renderer_failed_ || native_pristine_renderer_epoch_ != payload->renderer ||
      native_drag_incarnation_ != scope.incarnation) return false;
  if (payload->manual && !NativeDropManualCurrent(payload)) return false;
  // Observe only documents present at admission. A new unrelated iframe alone
  // does not invalidate the receipt; replacement/death of any pinned document
  // is conservatively treated as loss of the possible press/target widget.
  for (const auto& pin : payload->documents) {
    const auto frame = frame_states_.find(pin.frame);
    if (frame == frame_states_.end() || frame->second.session != pin.session ||
        frame->second.loader != pin.loader ||
        frame->second.context_identity != pin.identity ||
        frame->second.context_generation != pin.generation) return false;
  }
  return true;
}
struct NativeDropTransport {
  NativeDropCore::Scope scope;
  NativeDropCore::Stage stage;
  std::shared_ptr<NativeDropCore::Reply> reply;
};
bool NativeDropDispatchRefused(int command, const std::string& error) {
  const auto found = native_drop_transport_.find(command);
  if (found == native_drop_transport_.end()) return false;
  auto receipt = std::move(found->second.reply);
  native_drop_transport_.erase(found); devtools_.erase(command);
  auto callback = std::move(*receipt);
  if (callback) callback(NativeDropCore::Outcome::Refused, error);
  return true;
}
bool NativeDropDispatchReady(int command) {
  const auto found = native_drop_transport_.find(command);
  if (found == native_drop_transport_.end()) return true;
  const auto scope = found->second.scope;
  const bool cleanup = found->second.stage == NativeDropCore::Stage::DisableIntercept;
  if (!NativeDropDocumentCurrent(scope) || (native_drop_ && native_drop_->Closing()) ||
      (!cleanup && (!NativeDropOperationCurrent(scope) ||
       operations_.IsExpired(scope.owner, scope.generation, Now())))) {
    NativeDropDispatchRefused(command, "Native drag owner expired before actual submission");
    return false;
  }
  return true;
}
bool NativeDropSend(const NativeDropCore::Scope& scope,
                    NativeDropCore::Stage stage,
                    const NativeDropCore::Payload& opaque,
                    NativeDropCore::Reply reply) {
  if (!NativeDropDocumentCurrent(scope) || !reply) return false;
  const auto payload = std::static_pointer_cast<NativeDropPayload>(opaque);
  if (payload != native_drop_payload_) return false;
  Dict params;
  std::string method;
  if (stage == NativeDropCore::Stage::Drop) {
    const auto value = CefParseJSON(payload->packet, JSON_PARSER_RFC);
    params = value ? value->GetDictionary() : nullptr;
    if (!params) return false;
    method = "Input.dispatchDragEvent";
  } else if (stage == NativeDropCore::Stage::MouseRelease) {
    // Ordinary successful helper behavior only. The core never enters this
    // stage after cancellation or observed document/drag incarnation loss.
    params = MousePacket("mouseReleased", "left", 1);
    method = "Input.dispatchMouseEvent";
  } else {
    params = CefDictionaryValue::Create(); params->SetBool("enabled", false);
    method = "Input.setInterceptDrags";
  }
  const int command = AllocateDevToolsId();
  if (!command) return false;
  auto receipt = std::make_shared<NativeDropCore::Reply>(std::move(reply));
  auto actual_reply = [this, receipt, command](bool ok, const std::string& json) {
    native_drop_transport_.erase(command);
    auto callback = std::move(*receipt);
    if (callback) callback(ok ? NativeDropCore::Outcome::Acknowledged
                             : NativeDropCore::Outcome::Error,
                           ok ? "" : json.substr(0, 4096));
    NativeDropRetired();
  };
  // Register before submission. Do not use DevTools(owner,...): selector-wait
  // wrappers legitimately stop logical continuations after cancellation.
  native_drop_transport_.emplace(command, NativeDropTransport{scope, stage, receipt});
  try {
    devtools_.emplace(command, DevToolsCall{scope.owner, std::move(actual_reply),
                                          "", true, 65536, true});
  } catch (...) {
    native_drop_transport_.erase(command); throw;
  }
  if (!SubmitDevTools(command, method, params, "")) {
    // No accepted transport command exists. Settle exactly this registered
    // reservation once; a synchronous real reply may already have removed it.
    NativeDropDispatchRefused(command, "Native drag command submission was refused");
  }
  return true;
}
NativeDropCore& NativeDropCompletions() {
  if (!native_drop_) {
    NativeDropCore::Hooks hooks;
    hooks.now = [] { return Now(); };
    hooks.wake = [this] { WakeDeadlines(); };
    hooks.send = [this](const auto& scope, auto stage, const auto& payload,
                        auto reply) {
      return NativeDropSend(scope, stage, payload, std::move(reply));
    };
    hooks.operation_current = [this](const auto& scope) {
      return NativeDropOperationCurrent(scope);
    };
    hooks.incarnation_current = [this](const auto& scope) {
      return NativeDropDocumentCurrent(scope);
    };
    hooks.logical = [this](const auto& scope, const auto& opaque, bool ok,
                           const std::string& error) {
      const auto payload = std::static_pointer_cast<NativeDropPayload>(opaque);
      if (!NativeDropOperationCurrent(scope)) return;
      if (!ok) { Complete(scope.owner, false, error); return; }
      // Core publication has rechecked original deadline and document pins.
      // Clear only the matching incarnation, never a later drag's state.
      if (payload != native_drop_payload_) return;
      if (payload->manual) {
        if (NativeDropManualCurrent(payload)) manual_drag_ = nullptr;
        payload->manual->busy = false;
      } else if (payload->helper) {
        const auto found = drag_work_.find(scope.owner);
        if (found != drag_work_.end() && found->second == payload->helper)
          drag_work_.erase(found);
      }
      native_drop_payload_.reset();
      if (payload->done) payload->done();
    };
    hooks.close_channel = [this](const auto&, const std::string& error) {
      NetworkFatal(error);
    };
    native_drop_ = NativeDropCore::Create(std::move(hooks), NativeDropBudget());
  }
  return *native_drop_;
}
bool NativeDropBegin(int owner, uint64_t generation, Dict data,
                     std::shared_ptr<NativeDragWork> helper,
                     std::shared_ptr<InputWork> manual,
                     std::function<void()> done) {
  const auto entry = operations_.Get(owner);
  if (!entry || !operations_.IsCurrent(owner, generation)) return false;
  if (NativeDropBusy() || !browser_ || closing_page_ || !data ||
      !native_drag_incarnation_) {
    native_drop_failed_owner_ = owner;
    NetworkFatal("Native drag completion admission unavailable");
    Complete(owner, false, "Native drag completion admission unavailable");
    return false;
  }
  auto payload = std::make_shared<NativeDropPayload>();
  payload->scope = {static_cast<uint64_t>(browser_->GetIdentifier()),
                    native_drag_incarnation_, owner, generation};
  payload->renderer = native_pristine_renderer_epoch_;
  payload->packet = Json(NativeManualDragPacket("drop", data));
  size_t bytes = payload->packet.size();
  for (const auto& [id, frame] : frame_states_) {
    payload->documents.push_back({id, frame.session, frame.loader,
                                 frame.context_identity, frame.context_generation});
    bytes += id.size() + frame.session.size() + frame.loader.size() +
             frame.context_identity.size() + sizeof(NativeDropDocument);
  }
  if (payload->documents.empty()) {
    native_drop_failed_owner_ = owner;
    NetworkFatal("Native drag completion has no current document");
    Complete(owner, false, "Native drag completion has no current document");
    return false;
  }
  payload->helper = std::move(helper); payload->manual = std::move(manual);
  if (payload->manual) payload->manual_data = data;
  payload->done = std::move(done);
  // Transfer cleanup responsibility before any submission can reply/reenter
  // Complete. Ordinary logical CleanupInput must not send another source-end.
  if (payload->helper) payload->helper->held = false;
  if (payload->manual) payload->manual->held_mouse = false;
  native_drop_payload_ = payload;
  try {
    if (NativeDropCompletions().Begin(payload->scope,
          payload->helper ? NativeDropCore::Kind::Helper : NativeDropCore::Kind::Manual,
          bytes, entry->deadline_ms, payload)) return true;
  } catch (...) {
    // Transfer has already suppressed ordinary logical cleanup. Allocation of
    // the controller/record/map can fail before a watchdog exists. Preserve a
    // minimal owning-browser fence and request posted closure; if the core did
    // accept a receipt, retain its payload and callback through real closure.
    native_drop_failed_owner_ = owner;
    if (!native_drop_ || !native_drop_->Busy()) native_drop_payload_.reset();
    NetworkFatal("Native drag completion admission threw before returning ownership");
    Complete(owner, false, "Native drag completion admission failed");
    return false;
  }
  native_drop_failed_owner_ = owner; native_drop_payload_.reset();
  NetworkFatal("Native drag completion exceeds its bounded admission");
  Complete(owner, false, "Native drag completion exceeds its bounded admission");
  return false;
}
void NativeDropCancel(int owner) {
  if (NativeDropOwns(owner) && native_drop_ && native_drop_payload_)
    native_drop_->Cancel(native_drop_payload_->scope);
}
void NativeDropTick() {
  if (!native_drop_) return;
  native_drop_->Tick();
  NativeDropRetired();
}
bool NativeDropNeedsTick() const { return native_drop_ && native_drop_->NeedsTick(); }
void NativeDropClosed() {
  native_drop_transport_.clear();
  if (native_drop_) native_drop_->ChannelClosed();
  native_drop_payload_.reset(); native_drop_failed_owner_ = 0;
}
bool NativeDragNextIncarnation() {
  if (NativeDropBusy() || native_drag_incarnation_ == UINT64_MAX) {
    NetworkFatal("Native drag incarnation cannot be replaced while physically owned");
    return false;
  }
  ++native_drag_incarnation_; return true;
}
std::shared_ptr<NativeDropCore> native_drop_;
std::shared_ptr<NativeDropPayload> native_drop_payload_;
uint64_t native_drag_incarnation_ = 0;
int native_drop_failed_owner_ = 0;
std::map<int, NativeDropTransport> native_drop_transport_;
