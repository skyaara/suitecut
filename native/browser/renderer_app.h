// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"
#include "init_scripts.h"
#include "context_bindings.h"
#include "init_snapshot_scheme.h"
#include "renderer_script_sources.h"
#include "init_snapshot_message.h"
#include "init_snapshot_control.h"

// Runs authored context init scripts at CEF's actual document-context creation.
// This renderer counterpart retains at most one actual initial empty main realm
// per browser until its native settings and initialization ACK.
class SuiteCutRendererApp : public CefApp, public CefRenderProcessHandler {
 public:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override { RegisterSuiteCutInitScheme(registrar); }
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> info) override {
    auto snapshot = info ? info->GetDictionary("suitecutInitScripts") : nullptr;
    if (!snapshot) return;
    const int context = snapshot->GetInt("contextId");
    if (!Apply(snapshot) || context <= 0) return;
    auto& entry = browsers_[browser->GetIdentifier()];entry.settings_bootstrap=info->GetBool("suitecutContextSettingsBootstrap");entry.initial_empty=info->GetBool("suitecutInitialEmptyDocument"); entry.context = context; ++entry.references;
  }
  void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override {
    const auto found = browsers_.find(browser->GetIdentifier()); if (found == browsers_.end()) return;
    const int context = found->second.context;
    if (--found->second.references > 0) return;
    if(found->second.snapshot_epoch){
      snapshot_receiver_.Retire({static_cast<uint64_t>(context),static_cast<uint64_t>(browser->GetIdentifier()),found->second.snapshot_epoch,found->second.snapshot_frame_id});SnapshotFlush(found->second.snapshot_route);
      if(found->second.snapshot_route&&found->second.snapshot_route->IsValid())found->second.snapshot_route->SendProcessMessage(PID_BROWSER,suitecut::InitSnapshotControl::Admission(suitecut::InitSnapshotControl::kClosed,context,found->second.snapshot_epoch,found->second.snapshot_request,found->second.snapshot_frame_id));
    }
    found->second.snapshot_context=nullptr;
    browsers_.erase(found);
    for (const auto& [id, entry] : browsers_) if (entry.context == context) return;
    contexts_.erase(context);
  }
  void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override {
    const auto owner = browsers_.find(browser->GetIdentifier()); if (owner == browsers_.end()) return;
    const auto main_world=frame->GetV8Context();if(!main_world||!main_world->IsSame(context))return;
    // A recovered renderer can expose a speculative main V8 realm before it
    // has a committed document or a bound subresource factory. Its native frame
    // URL is empty. The later real document has its own OnContextCreated callback
    // and still bootstraps synchronously before any authored script executes.
    if(frame->IsMain()&&frame->GetURL().empty())return;
    SnapshotContextCreated(browser,frame,context);
    if(owner->second.initial_empty&&frame->IsMain()){
      CefRefPtr<CefV8Value>pending;CefRefPtr<CefV8Exception>error;
      if(!context->Eval(suitecut::kNativeIdentityScript.source,suitecut::kNativeIdentityScript.name,1,pending,error)||!pending||!pending->IsBool()){
        auto failure=CefProcessMessage::Create("suitecutInitialEmptyFailure");failure->GetArgumentList()->SetString(0,"Actual initial document identity is unavailable");frame->SendProcessMessage(PID_BROWSER,failure);return;
      }
      if(!pending->GetBoolValue()){owner->second.initial_empty=false;owner->second.settings_bootstrap=false;}
    }
    const bool initial=owner->second.initial_empty&&frame->IsMain();
    if(initial&&owner->second.settings_bootstrap){
      // Only the actual initial empty main realm is retained, one per browser.
      // Its native settings run while this renderer returns to its idle loop.
      if(owner->second.initial_context&&!owner->second.initial_context->IsSame(context))return;
      owner->second.initial_context=context;owner->second.initial_frame=frame;
      frame->SendProcessMessage(PID_BROWSER,CefProcessMessage::Create("suitecutContextSettingsEmptyReady"));return;
    }
    if(BootstrapDocument(browser,frame,context,initial)&&initial){
      owner->second.initial_empty=false;
      SendInitialReady(browser,frame,context);
    }
  }
  void OnContextReleased(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame>frame,CefRefPtr<CefV8Context>context)override{
    SnapshotContextReleased(browser,frame,context);
    const auto found=browsers_.find(browser->GetIdentifier());if(found!=browsers_.end()&&found->second.initial_context&&found->second.initial_context->IsSame(context)){
      found->second.initial_context=nullptr;found->second.initial_frame=nullptr;
    }
  }
  void SendInitialReady(CefRefPtr<CefBrowser>browser,CefRefPtr<CefFrame>frame,CefRefPtr<CefV8Context>context){
    CefRefPtr<CefV8Value>value;CefRefPtr<CefV8Exception>error;
    if(!context->Eval(suitecut::kNativeEvidenceScript.source,suitecut::kNativeEvidenceScript.name,1,value,error)||!value||!value->IsObject()){
      auto failure=CefProcessMessage::Create("suitecutInitialEmptyFailure");failure->GetArgumentList()->SetString(0,"Actual initial document evidence is unavailable");frame->SendProcessMessage(PID_BROWSER,failure);return;
    }
    const auto url=value->GetValue("url"),state=value->GetValue("state"),length=value->GetValue("length");
    if(!url||!url->IsString()||!state||!state->IsString()||!length||!length->IsInt())return;
    auto message=CefProcessMessage::Create("suitecutInitialEmptyReady");auto args=message->GetArgumentList();args->SetInt(0,browser->GetIdentifier());args->SetString(1,frame->GetIdentifier());args->SetString(2,url->GetStringValue());args->SetString(3,state->GetStringValue());args->SetInt(4,length->GetIntValue());frame->SendProcessMessage(PID_BROWSER,message);
  }
  bool BootstrapDocument(CefRefPtr<CefBrowser>browser,CefRefPtr<CefFrame>frame,CefRefPtr<CefV8Context>context,bool initial=false){
    const auto owner=browsers_.find(browser->GetIdentifier());if(owner==browsers_.end())return false;
    const int context_id=owner->second.context;
    CefRefPtr<CefV8Value> current; CefRefPtr<CefV8Exception> failure;
    const auto& snapshot_script=suitecut::NativeSnapshotScriptForFrame(frame);
    const bool fetched = context->Eval(
        snapshot_script.source,snapshot_script.name, 1, current, failure);
    const auto parsed = fetched && current && current->IsString() ? CefParseJSON(current->GetStringValue(), JSON_PARSER_RFC) : nullptr;
    const auto native = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
    if (!native || native->GetInt("contextId") != context_id || !Apply(native)) {
      auto message = CefProcessMessage::Create(initial?"suitecutInitialEmptyFailure":"suitecutInitScriptBootstrapError");
      message->GetArgumentList()->SetString(0, failure ? failure->GetMessage().ToString().substr(0, 4096) : "Native init script snapshot is unavailable");
      frame->SendProcessMessage(PID_BROWSER, message); return false;
    }
    const auto scripts = contexts_.find(context_id); if (scripts == contexts_.end()) return false;
    const auto snapshot = scripts->second;
    if(snapshot->credentials_installed){
      CefRefPtr<CefV8Value>value;CefRefPtr<CefV8Exception>error;
      if(!context->Eval(suitecut::kNativeCredentialScript.source,suitecut::kNativeCredentialScript.name,1,value,error)){
        auto message=CefProcessMessage::Create(initial?"suitecutInitialEmptyFailure":"suitecutInitScriptBootstrapError");message->GetArgumentList()->SetString(0,error?error->GetMessage().ToString().substr(0,4096):"Native credential readiness failed");frame->SendProcessMessage(PID_BROWSER,message);return false;
      }
    }
    // Queued inherited bindings exist before context init scripts or author code.
    for (const auto& binding : snapshot->bindings) {
      CefRefPtr<CefV8Value> value; CefRefPtr<CefV8Exception> error;
      if (!context->Eval(binding.source,"suitecut:context-binding:"+std::to_string(binding.id),1,value,error)) {
        auto message = CefProcessMessage::Create(initial?"suitecutInitialEmptyFailure":"suitecutInitScriptBootstrapError");
        message->GetArgumentList()->SetString(0,error ? error->GetMessage().ToString().substr(0,4096) : "Native context binding bootstrap failed");
        frame->SendProcessMessage(PID_BROWSER,message); return false;
      }
    }
    for (const auto& script : snapshot->entries) {
      CefRefPtr<CefV8Value> value; CefRefPtr<CefV8Exception> error;
      if (!context->Eval(script.source, "suitecut:init-script:" + std::to_string(script.id), 1, value, error)) {
        auto message = CefProcessMessage::Create("suitecutInitScriptError"); auto args = message->GetArgumentList();
        args->SetString(0, error ? error->GetMessage().ToString().substr(0, 4096) : "Native init script evaluation failed");
        frame->SendProcessMessage(PID_BROWSER, message);
      }
    }
    return true;
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefProcessId sender, CefRefPtr<CefProcessMessage> message) override {
    if(SnapshotMessage(browser,frame,sender,message))return true;
    if(sender==PID_BROWSER&&message->GetName()=="suitecutApplyInitialSettings"){
      const auto owner=browsers_.find(browser->GetIdentifier());
      if(owner==browsers_.end()||!frame->IsMain()||!owner->second.initial_empty||!owner->second.settings_bootstrap||!owner->second.initial_context||!owner->second.initial_frame||owner->second.initial_frame->GetIdentifier()!=frame->GetIdentifier())return true;
      auto context=owner->second.initial_context;owner->second.initial_context=nullptr;owner->second.initial_frame=nullptr;
      owner->second.initial_empty=false;owner->second.settings_bootstrap=false;
      if(!context->IsValid()||!context->Enter()){
        auto failure=CefProcessMessage::Create("suitecutInitialEmptyFailure");failure->GetArgumentList()->SetString(0,"Actual initial empty context was released before native settings applied");frame->SendProcessMessage(PID_BROWSER,failure);return true;
      }
      const bool installed=BootstrapDocument(browser,frame,context,true);
      if(installed){SendInitialReady(browser,frame,context);}
      context->Exit();
      return true;
    }
    if (sender != PID_BROWSER || message->GetName() != "suitecutInitScripts") return false;
    const auto args = message->GetArgumentList();
    const auto snapshot = args->GetDictionary(1);
    const auto owner = browsers_.find(browser->GetIdentifier());
    const bool success = args->GetSize() == 2 && args->GetType(0) == VTYPE_STRING && snapshot &&
        owner != browsers_.end() && owner->second.context == snapshot->GetInt("contextId") && Apply(snapshot);
    auto reply = CefProcessMessage::Create("suitecutInitScriptsAck"); reply->GetArgumentList()->SetString(0, args->GetString(0));
    reply->GetArgumentList()->SetBool(1, success); frame->SendProcessMessage(PID_BROWSER, reply); return true;
  }
 private:
  struct Snapshot { int version = 0, binding_version = 0,credential_version=0;bool credentials_installed=false; std::vector<SuiteCutInitScripts::Entry> entries, bindings; };
  struct Browser { int context = 0, references = 0;bool settings_bootstrap=false,initial_empty=false;CefRefPtr<CefV8Context>initial_context;CefRefPtr<CefFrame>initial_frame;
    uint64_t snapshot_request=0,snapshot_epoch=0;std::string snapshot_frame_id;CefRefPtr<CefV8Context>snapshot_context;CefRefPtr<CefFrame>snapshot_route; };
  bool Apply(CefRefPtr<CefDictionaryValue> value) {
    if (!value || value->GetType("contextId") != VTYPE_INT || value->GetType("version") != VTYPE_INT || value->GetType("scripts") != VTYPE_LIST) return false;
    const int id = value->GetInt("contextId"), version = value->GetInt("version");
    const auto input = value->GetList("scripts");
    if (value->GetType("bindingVersion") != VTYPE_INT || value->GetType("bindings") != VTYPE_LIST) return false;
    const int binding_version = value->GetInt("bindingVersion"); const auto bindings = value->GetList("bindings");
    if (binding_version < 0 || !bindings || bindings->GetSize() > SuiteCutContextBindings::kMaxEntries) return false;
    if (id <= 0 || version < 0 || !input || input->GetSize() > SuiteCutInitScripts::kMaxScripts ||
        (!contexts_.contains(id) && contexts_.size() >= 32)) return false;
    if(value->GetType("credentialVersion")!=VTYPE_INT||value->GetInt("credentialVersion")<0||value->GetType("credentialsInstalled")!=VTYPE_BOOL)return false;
    const int credential_version=value->GetInt("credentialVersion");
    const auto previous = contexts_.find(id);
    if (previous != contexts_.end()) {
      if (previous->second->version > version || previous->second->binding_version > binding_version||previous->second->credential_version>credential_version) return true;
      if (previous->second->version == version && previous->second->binding_version == binding_version&&previous->second->credential_version==credential_version) return true;
    }
    auto snapshot = std::make_shared<Snapshot>(); snapshot->version = version;snapshot->credential_version=credential_version;snapshot->credentials_installed=value->GetBool("credentialsInstalled"); size_t bytes = 0;
    for (size_t index = 0; index < input->GetSize(); ++index) {
      const auto item = input->GetDictionary(index);
      if (!item || item->GetType("id") != VTYPE_INT || item->GetInt("id") != static_cast<int>(index + 1) || item->GetType("source") != VTYPE_STRING) return false;
      const auto source = item->GetString("source").ToString();
      if (source.empty() || source.find('\0') != std::string::npos || source.size() > SuiteCutInitScripts::kMaxScriptBytes || source.size() > SuiteCutInitScripts::kMaxBytes - bytes) return false;
      bytes += source.size(); snapshot->entries.push_back({item->GetInt("id"), source});
    }
    if (static_cast<int>(snapshot->entries.size()) != version) return false;
    snapshot->binding_version = binding_version; size_t binding_bytes = 0; std::set<int> binding_ids;
    for (size_t index = 0; index < bindings->GetSize(); ++index) {
      const auto entry = bindings->GetDictionary(index);
      if (!entry || entry->GetType("id") != VTYPE_INT || entry->GetInt("id") <= 0 || !binding_ids.insert(entry->GetInt("id")).second || entry->GetType("source") != VTYPE_STRING) return false;
      const auto source = entry->GetString("source").ToString();
      if (source.empty() || source.find('\0') != std::string::npos || source.size() > SuiteCutContextBindings::kMaxEntryBytes || source.size() > SuiteCutContextBindings::kMaxBytes-binding_bytes) return false;
      binding_bytes += source.size(); snapshot->bindings.push_back({entry->GetInt("id"),source});
    }
    contexts_[id] = std::move(snapshot); return true;
  }
  std::map<int, Browser> browsers_;
  std::map<int, std::shared_ptr<Snapshot>> contexts_;
  suitecut::InitSnapshotReceiver snapshot_receiver_;uint64_t snapshot_next_request_=0;
#include "renderer_snapshot_receiver.inc"
  IMPLEMENT_REFCOUNTING(SuiteCutRendererApp);
};
