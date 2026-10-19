// SPDX-License-Identifier: MIT
// Definitions included once by context_controls.cc after context ownership.
namespace {
class SuiteCutInitDeadlineTask : public CefTask {
 public:
  explicit SuiteCutInitDeadlineTask(std::function<void()> callback) : callback_(std::move(callback)) {}
  void Execute() override{const suitecut::AcceptedTaskFrame native_frame(native_work_);callback_();}
 private:
  suitecut::AcceptedTaskReceipt native_work_;std::function<void()> callback_;
  IMPLEMENT_REFCOUNTING(SuiteCutInitDeadlineTask);
};
double SuiteCutInitNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
bool SuiteCutContextControls::RegisterInitContext(int context_id) {
  const auto found = contexts_.find(context_id); if (found == contexts_.end()) return false;
  auto value = CefValue::Create(); value->SetDictionary(InitScriptSnapshot(context_id));
  if (!CommitInitSnapshot(context_id,CefWriteJSON(value, JSON_WRITER_DEFAULT))) return false;
  if (!found->second.cef->RegisterSchemeHandlerFactory(kSuiteCutInitScheme, "", new SuiteCutInitSnapshotFactory(init_snapshot_store_, context_id, credential_ready_))) {
    init_snapshot_store_->Drop(context_id);snapshot_publications_.Drop(context_id); return false;
  }
  found->second.init_scheme_registered = true; return true;
}
SuiteCutContextControls::Dict SuiteCutContextControls::InitScriptSnapshot(int context_id) const {
  const auto context = contexts_.find(context_id);
  if (context == contexts_.end() || context->second.closing) return nullptr;
  auto value = CefDictionaryValue::Create(); value->SetInt("contextId", context_id);
  value->SetInt("version", context->second.init_scripts.Version());
  auto scripts = CefListValue::Create();
  for (const auto& entry : context->second.init_scripts.Entries()) {
    auto script = CefDictionaryValue::Create(); script->SetInt("id", entry.id); script->SetString("source", entry.source);
    scripts->SetDictionary(scripts->GetSize(), script);
  }
  value->SetList("scripts", scripts);
  value->SetInt("bindingVersion",context->second.bindings.Version()); auto bindings = CefListValue::Create();
  for (const auto& [name,entry] : context->second.bindings.All()) { auto binding = CefDictionaryValue::Create(); binding->SetInt("id",entry.id); binding->SetString("source",entry.bootstrap); bindings->SetDictionary(bindings->GetSize(),binding); }
  value->SetList("bindings",bindings);
  const auto credential=credentials_->Config(context_id);value->SetInt("credentialVersion",credential?static_cast<int>(credential->version):0);value->SetBool("credentialsInstalled",credential&&credential->installed);return value;
}
SuiteCutContextControls::Dict SuiteCutContextControls::BrowserExtraInfo(int page_id) const {
  auto extra = CefDictionaryValue::Create(); const auto snapshot = InitScriptSnapshot(ContextId(page_id));
  if (snapshot) extra->SetDictionary("suitecutInitScripts", snapshot);
  return extra;
}
bool SuiteCutContextControls::InitScriptFrameCreated(int page_id, CefRefPtr<CefFrame> frame) {
  if (stopping_ || !frame) return true;
  if(frame->IsMain()){
    const auto peer=snapshot_peers_.find(page_id);
    if(peer!=snapshot_peers_.end()&&peer->second.route!=frame->GetIdentifier().ToString()){peer->second.ready=false;SnapshotCheckWaiters();}
  }
  const auto snapshot = InitScriptSnapshot(ContextId(page_id));
  if (!snapshot || (!snapshot->GetInt("version") && !snapshot->GetInt("bindingVersion"))) return true;
  auto value = CefValue::Create(); value->SetDictionary(snapshot);
  const size_t bytes = CefWriteJSON(value, JSON_WRITER_DEFAULT).length() + 256;
  if (script_acks_.size() >= 1024 || bytes > 64 * 1024 * 1024 - pending_script_bytes_ || next_script_ack_ == std::numeric_limits<uint64_t>::max()) return false;
  const auto token = std::to_string(++next_script_ack_);
  script_acks_.emplace(token, ScriptAck{0, page_id, frame->GetIdentifier().ToString(), bytes, SuiteCutInitNow() + 30}); pending_script_bytes_ += bytes;
  auto message = CefProcessMessage::Create("suitecutInitScripts");
  message->GetArgumentList()->SetString(0, token); message->GetArgumentList()->SetDictionary(1, snapshot);
  frame->SendProcessMessage(PID_RENDERER, message); WakeInitScriptDeadlines(); return true;
}
void SuiteCutContextControls::RegisterInitScript(int context_id, int owner, const std::string& source, Reply reply) {
  if (owner <= 0 || script_batches_.contains(owner) || script_batches_.size() >= 64) {
    reply(false, nullptr, "Native init script registration capacity exceeded"); return;
  }
  auto& scripts = contexts_.at(context_id).init_scripts;
  // Validate before committing the registration or queuing renderer work.
  auto proposed = scripts; std::string error;
  if (!proposed.Add(source, error)) { reply(false, nullptr, error); return; }
  struct Target { int page; CefRefPtr<CefFrame> frame; };
  std::vector<Target> targets;
  for (const auto& [page_id, page] : pages_) if (page.context == context_id && page.browser) {
    std::vector<CefString> ids; page.browser->GetFrameIdentifiers(ids);
    for (const auto& id : ids) {
      auto frame = page.browser->GetFrameByIdentifier(id);
      if (frame && frame->IsValid()) targets.push_back({page_id, frame});
      if (targets.size() > 1024) { reply(false, nullptr, "Native init script frame capacity exceeded"); return; }
    }
  }
  // Each frame gets a full immutable snapshot. Bound queued native IPC bytes,
  // including JSON escaping, before making any mutation or send observable.
  auto budget_value = InitScriptSnapshot(context_id); auto budget_list = CefListValue::Create();
  for (const auto& entry : proposed.Entries()) {
    auto value = CefDictionaryValue::Create(); value->SetString("source", entry.source); value->SetInt("id", entry.id);
    budget_list->SetDictionary(budget_list->GetSize(), value);
  }
  budget_value->SetList("scripts", budget_list); budget_value->SetInt("contextId", context_id); budget_value->SetInt("version", proposed.Version());
  auto serialized = CefValue::Create(); serialized->SetDictionary(budget_value);
  const size_t message_bytes = CefWriteJSON(serialized, JSON_WRITER_DEFAULT).length() + 256;
  constexpr size_t max_bytes = 64 * 1024 * 1024;
  if (targets.size() > 1024 - script_acks_.size() ||
      (targets.size() && message_bytes > (max_bytes - pending_script_bytes_) / targets.size()) ||
      targets.size() > std::numeric_limits<uint64_t>::max() - next_script_ack_) {
    reply(false, nullptr, "Native init script pending IPC capacity exceeded"); return;
  }
  if (!contexts_.at(context_id).init_scheme_registered || !CommitInitSnapshot(context_id,CefWriteJSON(serialized, JSON_WRITER_DEFAULT),owner)) {
    reply(false, nullptr, "Native init snapshot bytes capacity exceeded or scheme unavailable"); return;
  }
  scripts = std::move(proposed);
  if (targets.empty()) { reply(true, Result(context_id), ""); return; }
  ScriptBatch batch; batch.reply = std::move(reply); batch.context = context_id;
  const auto snapshot = InitScriptSnapshot(context_id);
  std::vector<std::pair<CefRefPtr<CefFrame>, std::string>> messages;
  for (const auto& target : targets) {
    const auto token = std::to_string(++next_script_ack_);
    script_acks_.emplace(token, ScriptAck{owner, target.page, target.frame->GetIdentifier().ToString(), message_bytes});
    batch.tokens.insert(token); pending_script_bytes_ += message_bytes;
    messages.emplace_back(target.frame, token);
  }
  script_batches_.emplace(owner, std::move(batch));
  for (const auto& [frame, token] : messages) {
    auto message = CefProcessMessage::Create("suitecutInitScripts"); auto args = message->GetArgumentList();
    args->SetString(0, token); args->SetDictionary(1, snapshot->Copy(false));
    frame->SendProcessMessage(PID_RENDERER, message);
  }
}
bool SuiteCutContextControls::InitScriptMessage(int page_id, CefRefPtr<CefFrame> frame, CefProcessId sender, CefRefPtr<CefProcessMessage> message) {
  if(SnapshotMessage(page_id,frame,sender,message))return true;
  if (sender != PID_RENDERER || message->GetName() != "suitecutInitScriptsAck") return false;
  const auto args = message->GetArgumentList();
  if (args->GetSize() != 2 || args->GetType(0) != VTYPE_STRING || args->GetType(1) != VTYPE_BOOL) return true;
  const auto token = args->GetString(0).ToString(); const auto found = script_acks_.find(token);
  if (found == script_acks_.end() || found->second.page != page_id || !frame || found->second.frame != frame->GetIdentifier().ToString()) return true;
  const int owner = found->second.owner; pending_script_bytes_ -= found->second.bytes; script_acks_.erase(found);
  if (!owner) {
    if (!args->GetBool(1)) {
      const auto page = pages_.find(page_id);
      if (page != pages_.end() && page->second.browser) page->second.browser->GetHost()->CloseBrowser(true);
    }
    return true;
  }
  const auto batch = script_batches_.find(owner); if (batch == script_batches_.end()) return true;
  batch->second.tokens.erase(token);
  if (!args->GetBool(1)) {
    auto reply = std::move(batch->second.reply); CancelInitScript(owner);
    reply(false, nullptr, "Native renderer rejected context init script snapshot"); return true;
  }
  if (batch->second.tokens.empty()) {
    const int context = batch->second.context; auto reply = std::move(batch->second.reply); script_batches_.erase(batch);
    reply(true, Result(context), "");
  }
  return true;
}
void SuiteCutContextControls::CancelInitScript(int owner) {
  const auto batch = script_batches_.find(owner); if (batch == script_batches_.end()) return;
  for (const auto& token : batch->second.tokens) {
    const auto ack = script_acks_.find(token);
    if (ack != script_acks_.end()) { pending_script_bytes_ -= ack->second.bytes; script_acks_.erase(ack); }
  }
  script_batches_.erase(batch);
}
void SuiteCutContextControls::InitScriptFrameDestroyed(int page_id, const std::string& frame_id) {
  std::set<int> failed;
  for (auto it = script_acks_.begin(); it != script_acks_.end();) {
    if (it->second.page == page_id && (frame_id.empty() || it->second.frame == frame_id)) {
      if (it->second.owner) { failed.insert(it->second.owner); ++it; }
      else { pending_script_bytes_ -= it->second.bytes; it = script_acks_.erase(it); }
    } else ++it;
  }
  for (const int owner : failed) {
    const auto batch = script_batches_.find(owner); if (batch == script_batches_.end()) continue;
    auto reply = std::move(batch->second.reply); CancelInitScript(owner);
    reply(false, nullptr, "Native frame closed during context init script registration");
  }
}

void SuiteCutContextControls::WakeInitScriptDeadlines() {
  if (script_deadline_timer_ || script_acks_.empty() || stopping_) return;
  script_deadline_timer_ = true;
  CefPostDelayedTask(TID_UI, new SuiteCutInitDeadlineTask([weak = weak_from_this()] {
    if (const auto self = weak.lock()) self->PumpInitScriptDeadlines();
  }), 1000);
}
void SuiteCutContextControls::PumpInitScriptDeadlines() {
  script_deadline_timer_ = false;
  std::set<int> expired_pages;
  for (const auto& [token, ack] : script_acks_) if (!ack.owner && ack.deadline <= SuiteCutInitNow()) expired_pages.insert(ack.page);
  for (const int page : expired_pages) {
    InitScriptFrameDestroyed(page, "");
    const auto found = pages_.find(page);
    // An attached frame that cannot install its context snapshot must not keep
    // silently running future documents with incomplete initialization.
    if (found != pages_.end() && found->second.browser) found->second.browser->GetHost()->CloseBrowser(true);
  }
  if (!script_acks_.empty()) WakeInitScriptDeadlines();
}
