// SPDX-License-Identifier: MIT
// Context registration owns identity/snapshot; Source owns actual CDP/renderer work.
SuiteCutContextControls::Dict SuiteCutContextControls::BindingConfig(const SuiteCutContextBindings::Entry& entry) const {
  auto config = CefDictionaryValue::Create(); suitecut::InboundSetString(config,"name",entry.name); config->SetString("bridge",entry.bridge);
  config->SetString("installer",entry.installer); config->SetString("codec",entry.codec); config->SetBool("handle",entry.handle); return config;
}
bool SuiteCutContextControls::RefreshInitSnapshot(int context,int owner) {
  const auto found = contexts_.find(context); if (found == contexts_.end() || found->second.closing) return false;
  auto value = CefValue::Create(); value->SetDictionary(InitScriptSnapshot(context));
  return found->second.init_scheme_registered && CommitInitSnapshot(context,CefWriteJSON(value,JSON_WRITER_DEFAULT),owner);
}
bool SuiteCutContextControls::SeedPageBindings(int page, std::string& error) {
  const auto found = contexts_.find(ContextId(page));
  if (found == contexts_.end()) { error = "Unknown native context during binding initialization"; return false; }
  if (found->second.closing || stopping_) return true;
  for (const auto& [name,entry] : found->second.bindings.All()) {
    if (!hooks_.seed_binding || !hooks_.seed_binding(page,BindingConfig(entry),error)) return false;
  }
  return true;
}
void SuiteCutContextControls::RegisterContextBinding(int context, int owner, Dict params, Reply reply) {
  std::string name;
  if (owner <= 0 || binding_batches_.contains(owner) || binding_batches_.size() >= 64 ||
      !suitecut::InboundString(params,"name",name,256) || params->GetType("installer") != VTYPE_STRING ||
      params->GetType("codec") != VTYPE_STRING || params->GetType("handle") != VTYPE_BOOL ||
      params->HasKey("bridge") || params->HasKey("state")) { reply(false,nullptr,"Invalid native context binding registration or capacity exceeded"); return; }
  auto& registry = contexts_.at(context).bindings;
  SuiteCutContextBindings::Entry entry; std::string error;
  if (!registry.Add(binding_namespace_ + "_" + std::to_string(context),name,params->GetString("installer"),params->GetString("codec"),params->GetBool("handle"),entry,error)) {
    reply(false,nullptr,error); return;
  }
  const auto quote = [](const std::string& text) { return suitecut::InboundQuote(text); };
  const auto bootstrap = "(" + entry.installer + ")(" + quote(entry.name) + "," + quote(entry.bridge) + "," + quote(entry.state) + ",(" + entry.codec + ")," + (entry.handle ? "true" : "false") + ",true)";
  if (!registry.SetBootstrap(entry.name,entry.id,bootstrap) || !RefreshInitSnapshot(context,owner)) {
    registry.Remove(entry.name,entry.id); reply(false,nullptr,"Native context binding snapshot capacity exceeded"); return;
  }
  BindingBatch batch; batch.context = context; batch.entry = entry.id; batch.name = entry.name; batch.bridge = entry.bridge; batch.reply = std::move(reply);
  for (const auto& [page_id,page] : pages_) if (page.context == context && page.browser && !page.closing) batch.pages.insert(page_id);
  if (batch.pages.empty()) { auto result = Result(context); result->SetInt("entryId",entry.id); batch.reply(true,result,""); return; }
  const auto targets = batch.pages; binding_batches_.emplace(owner,std::move(batch));
  for (const int page : targets) {
    if (!binding_batches_.contains(owner)) break;
    if (!hooks_.register_binding) { CancelContextBinding(owner,"Native context binding page installer is unavailable",true); break; }
    hooks_.register_binding(page,owner,BindingConfig(entry),[weak=weak_from_this(),owner,page](bool ok,const std::string& failure) {
      const auto self = weak.lock(); if (!self) return;
      const auto found = self->binding_batches_.find(owner); if (found == self->binding_batches_.end() || !found->second.pages.contains(page)) return;
      if (!ok) { self->CancelContextBinding(owner,failure,true); return; }
      found->second.pages.erase(page);
      if (!found->second.pages.empty()) return;
      auto result = Result(found->second.context); result->SetInt("entryId",found->second.entry);
      auto reply = std::move(found->second.reply); self->binding_batches_.erase(found); reply(true,result,"");
    });
  }
}
void SuiteCutContextControls::CancelContextBinding(int owner, const std::string& error, bool respond) {
  const auto found = binding_batches_.find(owner); if (found == binding_batches_.end()) return;
  auto batch = std::move(found->second); binding_batches_.erase(found);
  const auto context = contexts_.find(batch.context);
  if (context != contexts_.end()) { context->second.bindings.Remove(batch.name,batch.entry); RefreshInitSnapshot(batch.context); }
  // Include pages seeded while the registration was in progress. Removal is
  // bridge-scoped; it can never dispose a newer same-name registration.
  for (const auto& [page_id,page] : pages_) if (page.context == batch.context && hooks_.remove_binding)
    hooks_.remove_binding(page_id,owner,batch.name,batch.bridge);
  if (respond && batch.reply) batch.reply(false,nullptr,error.empty() ? "Native context binding registration canceled" : error);
}
void SuiteCutContextControls::ContextBindingPageClosed(int page) {
  std::vector<int> owners; for (const auto& [owner,batch] : binding_batches_) if (batch.pages.contains(page)) owners.push_back(owner);
  for (const int owner : owners) CancelContextBinding(owner,"Native page closed during context binding registration",true);
}
