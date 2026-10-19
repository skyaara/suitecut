// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include "binding_name.h"
#include <functional>
#include <map>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
#include "inbound_string.h"

namespace suitecut {
// Runtime.addBinding remains private CEF traffic. The native page owns renderer
// call identities, context epochs, cancellation, admission bounds and deadlines.
class BindingControls {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using List = CefRefPtr<CefListValue>;
  using Result = std::function<void(bool, const std::string&)>;
  using Send = std::function<void(const std::string&, const std::string&, Dict, Result)>;
  using Done = std::function<void(bool, const std::string&, const std::string&)>;
  using Emit = std::function<void(Dict)>;
  struct Target { std::string frame, session; int context = 0; };
  // Source adopts native object arguments in this exact default context. It
  // must reject a changed context rather than reusing a document after reload.
  using Reply = std::function<void(int, const Target&, Dict, Dict, Result)>;
  BindingControls(std::string scope, Send send, Reply reply, Emit emit,
                  std::function<void()> wake, std::function<double()> now)
      : scope_(std::move(scope)), send_(std::move(send)), reply_(std::move(reply)),
        emit_(std::move(emit)), wake_(std::move(wake)), now_(std::move(now)) {}

  static std::string Json(Dict value) {
    auto wrapper = CefValue::Create(); wrapper->SetDictionary(value);
    return CefWriteJSON(wrapper, JSON_WRITER_DEFAULT).ToString();
  }
  static std::string Quote(const std::string& value) {
    return InboundQuote(value);
  }
  // Only the native context manager calls these methods with its allocated
  // bridge identity. Ordinary SDK commands never accept a caller-owned bridge.
  bool SeedInherited(const std::string& name,const std::string& bridge,const std::string& installer,const std::string& codec,bool handle,std::string& error){
    if(!sessions_.empty()){error="Native context binding seed must precede session attachment";return false;}
    if(!InheritedBridge(bridge)){error="Invalid native context binding bridge identity";return false;}
    return static_cast<bool>(CreateEntry(name,bridge,installer,codec,handle,true,error));
  }
  void RegisterInherited(int owner,Dict config,Done done){
    std::string inherited_name;
    if(owner<=0||pending_.contains(owner)||!config||!InboundString(config,"name",inherited_name,256)||config->GetType("bridge")!=VTYPE_STRING||config->GetType("installer")!=VTYPE_STRING||config->GetType("codec")!=VTYPE_STRING||config->GetType("handle")!=VTYPE_BOOL||!InheritedBridge(config->GetString("bridge").ToString())){done(false,"Invalid native context binding registration","");return;}
    std::string error;auto entry=CreateEntry(inherited_name,config->GetString("bridge"),config->GetString("installer"),config->GetString("codec"),config->GetBool("handle"),true,error);
    if(!entry){done(false,error,"");return;}
    StartInstallation(owner,entry,std::move(done));
  }
  bool RemoveInherited(const std::string& name,const std::string& bridge){
    const auto found=registrations_.find(name);
    if(found==registrations_.end()||!found->second->inherited||found->second->bridge!=bridge)return false;
    RemoveEntry(found->second,"Native context binding was disposed");return true;
  }
  void SessionAttached(const std::string& session, Result done = {}) {
    if(disposed_) { if(done)done(false,"Native binding page is closed");return; }
    const auto waiting=session_installations_.find(session);
    if(waiting!=session_installations_.end()){
      if(done){if(waiting->second->waiters.size()>=64){done(false,"Native binding session readiness capacity exceeded");return;}waiting->second->waiters.push_back(std::move(done));}return;
    }
    if(!sessions_.insert(session).second){const auto error=session_errors_.find(session);if(done)done(error==session_errors_.end(),error==session_errors_.end()?"":error->second);return;}
    if(sessions_.size()>1024){sessions_.erase(session);if(done)done(false,"Native binding session capacity exceeded");return;}
    auto work=std::make_shared<SessionInstallation>();work->session=session;
    for(const auto& [name,entry]:registrations_)work->entries.push_back(entry);
    if(done)work->waiters.push_back(std::move(done));
    session_installations_[session]=work;
    InstallSessionNext(work);
  }
  void SessionDetached(const std::string& session) {
    sessions_.erase(session);session_errors_.erase(session);
    const auto waiting=session_installations_.find(session);
    if(waiting!=session_installations_.end())FinishSession(waiting->second,false,"Native binding session was detached");
    ContextRemoved(session);
    for (auto& [name, entry] : registrations_) entry->scripts.erase(session);
  }
  void ContextAvailable(Dict frame, const std::string& session, int context) {
    if (disposed_ || context <= 0 || !frame || frame->GetString("id").empty()) return;
    const auto key = std::make_pair(session, context);
    if (!contexts_.contains(key) && contexts_.size() >= 1024) return;
    contexts_[key] = {frame->Copy(false), session, context};
    for (const auto& [name, entry] : registrations_) {
      EvaluateInstaller(entry,contexts_.at(key),[](bool,const std::string&){});
    }
  }
  void FrameRemoved(const std::string& frame) {
    std::vector<std::pair<std::string,int>> removed;
    for (const auto& [key, context] : contexts_) if (context.frame->GetString("id") == frame) removed.push_back(key);
    for (const auto& key : removed) ContextRemoved(key.first, key.second);
  }
  void ContextRemoved(const std::string& session, int context = 0) {
    for(auto it=installer_groups_.begin();it!=installer_groups_.end();){auto group=it->second;if(group->session==session&&(!context||group->context==context)){group->active=false;it=installer_groups_.erase(it);}else ++it;}
    std::vector<std::string> removed;
    for (const auto& [id, call] : calls_)
      if (call.target.session == session && (!context || call.target.context == context)) removed.push_back(id);
    for (const auto& id : removed) CancelCall(id, "Native binding execution context was destroyed", false);
    for (auto it = contexts_.begin(); it != contexts_.end();) {
      if (it->first.first == session && (!context || it->first.second == context)) it = contexts_.erase(it);
      else ++it;
    }
  }
  void Clear() {
    if (disposed_) return;
    std::vector<std::string> ids;
    for (const auto& [id, call] : calls_) ids.push_back(id);
    for (const auto& id : ids) CancelCall(id, "Native page is closed", false);
    disposed_ = true;
    for(const auto& [id,group]:installer_groups_){group->active=false;}installer_groups_.clear();
    const auto waiting=session_installations_;
    for(const auto& [session,work]:waiting)FinishSession(work,false,"Native binding page is closed");
    registrations_.clear(); pending_.clear(); replies_.clear(); arguments_.clear(); contexts_.clear(); sessions_.clear(); calls_.clear(); bytes_ = source_bytes_ = result_bytes_ = 0;
  }
  void Cancel(int owner) {
    const auto reply = replies_.find(owner);
    if (reply != replies_.end()) { const auto id = reply->second; replies_.erase(reply); CancelCall(id, "Native binding callback delivery was canceled", true); }
    ArgumentFinished(owner,false);
    const auto pending = pending_.find(owner);
    if (pending == pending_.end()) return;
    auto entry = pending->second;
    pending_.erase(pending);
    RemoveEntry(entry, "Native binding registration was canceled");
  }
  bool PrepareArgument(int owner,Dict message,Dict& output,std::string& error){
    const auto id=message->GetString("bindingCallId").ToString();const auto found=calls_.find(id);
    if(found==calls_.end()){error="Native binding call is no longer active";return false;}
    auto& call=found->second;
    if(!call.entry->handle||call.argument_requested||now_()>=call.deadline||!contexts_.contains({call.target.session,call.target.context})){error="Native binding argument handle is unavailable";return false;}
    if(arguments_.size()>=64){error="Native binding argument control capacity exceeded";return false;}
    call.argument_requested=true;arguments_[owner]=id;
    output=CefDictionaryValue::Create();output->SetString("action","create");output->SetString("frame",call.target.frame);
    output->SetInt("bindingArgumentContext",call.target.context);output->SetString("bindingArgumentSession",call.target.session);
    output->SetList("handles",CefListValue::Create());
    output->SetString("function","function(){const state=globalThis["+Quote(call.entry->state)+"];if(!state)throw new Error('Native binding argument context is unavailable');const argument=state.argument("+std::to_string(static_cast<uint64_t>(call.client))+","+Quote(call.nonce)+");return {get value(){return argument}};}");
    return true;
  }
  void ArgumentFinished(int owner,bool ok){
    const auto found=arguments_.find(owner);if(found==arguments_.end())return;const auto id=found->second;arguments_.erase(found);
    if(!ok)CancelCall(id,"Native binding argument handle creation was canceled",true);
  }
  bool AcceptedWorkReady()const noexcept{return !physical_installations_&&pending_.empty()&&calls_.empty()&&arguments_.empty()&&installer_groups_.empty();}
  bool NeedsTick() const {if(!calls_.empty())return true;for(const auto& [id,group]:installer_groups_)if(!group->inflight)return true;return false;}
  void Tick(double now) {
    std::vector<std::shared_ptr<InstallerGroup>> groups;for(const auto& [id,group]:installer_groups_)if(!group->inflight&&now>=group->retry)groups.push_back(group);
    for(const auto& group:groups)ReleaseInstallerGroup(group);
    std::vector<std::string> expired;
    for (const auto& [id, call] : calls_) if (now >= call.deadline) expired.push_back(id);
    for (const auto& id : expired) CancelCall(id, "Native binding callback timed out", true);
  }
  void Diagnostics(Dict output) const {
    output->SetInt("bindingPhysicalInstallations",static_cast<int>(physical_installations_));
    output->SetInt("bindingInstallerGroups",static_cast<int>(installer_groups_.size()));
    size_t releasing=0;for(const auto& [id,group]:installer_groups_)if(group->release_started&&group->inflight)++releasing;
    output->SetInt("bindingInstallerGroupReleases",static_cast<int>(releasing));
    output->SetInt("bindingRegistrations", static_cast<int>(registrations_.size()));
    output->SetInt("bindingPendingRegistrations", static_cast<int>(pending_.size()));
    output->SetInt("bindingCalls", static_cast<int>(calls_.size()));
    output->SetInt("bindingArgumentControls",static_cast<int>(arguments_.size()));
    output->SetInt("bindingArgumentBytes", static_cast<int>(bytes_));
    output->SetInt("bindingResultBytes",static_cast<int>(result_bytes_));
    output->SetInt("bindingSourceBytes", static_cast<int>(source_bytes_));
  }
  void Command(int owner, Dict message, Done done) {
    if (disposed_) { done(false, "Native binding page is closed", ""); return; }
    const auto action = message->GetString("action").ToString();
    if (action == "resolve") { Resolve(owner, message, std::move(done)); return; }
    if (action == "stage") { Stage(message,std::move(done)); return; }
    if(message->HasKey("bridge")||message->HasKey("state")){done(false,"Native binding bridge identities are native-owned","");return;}
    std::string name;
    if (!InboundString(message,"name",name,256) || name.empty()) { done(false, "Invalid native binding name", ""); return; }
    if (action == "remove") {
      const auto found = registrations_.find(name);
      if(found!=registrations_.end()&&found->second->inherited){done(false,"Native context binding disposal is context-owned","");return;}
      if (found != registrations_.end()) RemoveEntry(found->second, "Native binding was disposed");
      done(true, "", ""); return;
    }
    if (action != "register" || message->GetType("installer") != VTYPE_STRING || message->GetType("codec") != VTYPE_STRING) {
      done(false, "Invalid native binding registration", ""); return;
    }
    if(message->HasKey("handle")&&message->GetType("handle")!=VTYPE_BOOL){done(false,"Invalid native binding handle option","");return;}
    if (registrations_.contains(name)) { done(false, "Function " + Quote(name) + " has been already registered", ""); return; }
    std::string error;
    auto entry=CreateEntry(name,"__suitecut_binding_"+scope_+"_"+std::to_string(++registration_sequence_),message->GetString("installer"),message->GetString("codec"),message->GetBool("handle"),false,error);
    if(!entry){done(false,error,"");return;}
    StartInstallation(owner,entry,std::move(done));
  }
  void Event(Dict data, const std::string& session) {
    if (disposed_) return;
    const auto bridge = data->GetString("name").ToString();
    std::shared_ptr<Entry> entry;
    for (const auto& [name, value] : registrations_) if (value->bridge == bridge) { entry = value; break; }
    if (!entry || !entry->active) return;
    const int context = data->GetInt("executionContextId");
    const auto found = contexts_.find({session, context});
    if (found == contexts_.end()) return; // Only an actual default frame context can call a public binding.
    const auto payload = data->GetString("payload").ToString();
    auto parsed = payload.size() <= 1048576 ? CefParseJSON(payload, JSON_PARSER_RFC) : nullptr;
    auto args = parsed ? parsed->GetDictionary() : nullptr;
    const auto nonce = args ? args->GetString("contextNonce").ToString() : "";
    if(nonce.size() != 32 || !std::all_of(nonce.begin(),nonce.end(),[](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) return;
    const auto number_type = args ? args->GetType("id") : VTYPE_INVALID;
    const double client = args && number_type == VTYPE_INT ? args->GetInt("id") : args && number_type == VTYPE_DOUBLE ? args->GetDouble("id") : 0;
    if (client < 1 || client > 9007199254740991.0 || static_cast<uint64_t>(client) != client || !args || args->GetType("args") != VTYPE_LIST) return;
    double remaining=30000;
    if(args->HasKey("timeoutMillis")) {
      const auto type=args->GetType("timeoutMillis");
      if(type!=VTYPE_INT&&type!=VTYPE_DOUBLE)return;
      remaining=type==VTYPE_INT?args->GetInt("timeoutMillis"):args->GetDouble("timeoutMillis");
      if(!(remaining>=1&&remaining<=30000))return;
    }
    Target target{found->second.frame->GetString("id").ToString(), session, context};
    if (calls_.size() >= 128 || bytes_ + payload.size() > 8388608) {
      Reject(*entry, target, client, nonce, "Native binding callback capacity exceeded"); return;
    }
    const auto id = scope_ + "-binding-" + std::to_string(++call_sequence_);
    calls_[id] = {entry, target, client, payload.size(), now_() + remaining}; calls_[id].nonce = nonce; bytes_ += payload.size();
    auto event = CefDictionaryValue::Create(); event->SetString("event", "call"); event->SetString("id", id);
    InboundSetString(event,"name", entry->name); event->SetDictionary("frame", found->second.frame->Copy(false));
    event->SetValue("args", args->GetValue("args")->Copy());event->SetBool("handle",entry->handle);
    emit_(event); wake_();
  }
 private:
  struct Entry {
    std::string name, bridge, state, installer, codec;
    bool active = true, accounted = true, handle = false, inherited = false;
    size_t installations = 0;
    std::map<std::string,std::string> scripts;
  };
  struct SessionInstallation {std::string session;size_t index=0;bool finished=false;std::vector<std::shared_ptr<Entry>> entries;std::vector<Result> waiters;};
  void FinishSession(std::shared_ptr<SessionInstallation> work,bool ok,const std::string& error){
    if(work->finished)return;
    work->finished=true;session_installations_.erase(work->session);
    if(!ok&&!disposed_&&sessions_.contains(work->session))session_errors_[work->session]=error;
    auto waiters=std::move(work->waiters);for(auto& done:waiters)done(ok,error);
  }
  void InstallSessionNext(const std::shared_ptr<SessionInstallation>& work){
    if(work->finished)return;
    if(disposed_||!sessions_.contains(work->session)){FinishSession(work,false,"Native binding session unavailable");return;}
    while(work->index<work->entries.size()&&!work->entries[work->index]->active)++work->index;
    if(work->index==work->entries.size()){FinishSession(work,true,"");return;}
    const auto entry=work->entries[work->index++];
    Install(entry,work->session,[this,work,entry](bool ok,const std::string& error){if(work->finished)return;if(!ok&&entry->active)FinishSession(work,false,error);else InstallSessionNext(work);});
  }
  struct Context { Dict frame; std::string session; int id; };
  struct Call { std::shared_ptr<Entry> entry; Target target; double client; size_t bytes; double deadline; std::string result, nonce; int next_chunk = 0; bool sealed = false, argument_requested = false; };
  struct Installation {
    std::shared_ptr<Entry> entry;
    int owner; size_t index = 0;
    std::vector<std::string> sessions;
    Done done;
  };
  static bool InheritedBridge(const std::string& bridge){
    return bridge.starts_with("__suitecut_context_binding_")&&bridge.size()<=512&&std::all_of(bridge.begin(),bridge.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';});
  }
  std::shared_ptr<Entry> CreateEntry(const std::string& name,const std::string& bridge,const std::string& installer,const std::string& codec,bool handle,bool inherited,std::string& error){
    if(disposed_){error="Native binding page is closed";return {};}
    if(name.empty()||SuiteCutBindingNameUnits(name)>256){error="Invalid native binding name";return {};}
    if(registrations_.contains(name)){error="Function "+Quote(name)+" has been already registered";return {};}
    const size_t bytes=installer.size()+codec.size();
    if(!bytes||installer.empty()||codec.empty()||bytes>1048576||source_bytes_+bytes>4194304||registrations_.size()>=64){error="Native binding registration capacity exceeded";return {};}
    for(const auto& [key,entry]:registrations_)if(entry->bridge==bridge){error="Native binding bridge is already registered";return {};}
    auto entry=std::make_shared<Entry>();entry->name=name;entry->bridge=bridge;entry->state=bridge+"_state";entry->installer=installer;entry->codec=codec;entry->handle=handle;entry->inherited=inherited;
    registrations_[name]=entry;source_bytes_+=bytes;return entry;
  }
  void StartInstallation(int owner,std::shared_ptr<Entry> entry,Done done){
    pending_[owner]=entry;auto work=std::make_shared<Installation>();work->entry=entry;work->owner=owner;
    work->sessions.assign(sessions_.begin(),sessions_.end());work->done=std::move(done);InstallNext(work);
  }
  std::string Installer(const Entry& entry,bool deferred=false) const {
    return "(" + entry.installer + ")(" + Quote(entry.name) + "," + Quote(entry.bridge) + "," + Quote(entry.state) + ",(" + entry.codec + "),"+(entry.handle?"true":"false")+","+(deferred?"true":"false")+")";
  }
  void InstallNext(std::shared_ptr<Installation> work) {
    if (disposed_ || !work->entry->active || !pending_.contains(work->owner)) return;
    if (work->index == work->sessions.size()) {
      pending_.erase(work->owner); work->done(true, "", ""); return;
    }
    const auto session = work->sessions[work->index++];
    Install(work->entry, session, [this, work](bool ok, const std::string& error) {
      if (disposed_ || !pending_.contains(work->owner)) return;
      if (!ok) { pending_.erase(work->owner); RemoveEntry(work->entry, "Native binding registration failed"); work->done(false, error, ""); }
      else InstallNext(work);
    });
  }
  void Install(std::shared_ptr<Entry> entry, const std::string& session, Result done) {
    if(physical_installations_>=1024){done(false,"Native binding physical installation capacity exceeded");return;}
    ++entry->installations;++physical_installations_;
    Result finish = [this, entry, done = std::move(done)](bool ok, const std::string& json) {
      if (entry->installations) --entry->installations;
      if(physical_installations_)--physical_installations_;
      ReleaseEntryBytes(entry);
      done(ok,json);
    };
    auto params = CefDictionaryValue::Create(); params->SetString("name", entry->bridge);
    send_(session, "Runtime.addBinding", params, [this, entry, session, done = std::move(finish)](bool ok, const std::string& json) {
      if (disposed_ || !entry->active) { done(false, "Native binding registration canceled"); return; }
      if (!ok) { done(false, json); return; }
      auto script = CefDictionaryValue::Create(); script->SetString("source", Installer(*entry,entry->inherited)); script->SetBool("runImmediately", true);
      send_(session, "Page.addScriptToEvaluateOnNewDocument", script, [this, entry, session, done](bool ok, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        const auto identifier = result ? result->GetString("identifier").ToString() : "";
        if (!entry->active || disposed_) {
          if (!identifier.empty()) { auto params = CefDictionaryValue::Create(); params->SetString("identifier", identifier); send_(session,"Page.removeScriptToEvaluateOnNewDocument",params,[](bool,const std::string&){}); }
          // runImmediately may have installed a wrapper after RemoveEntry's
          // earlier disposal command. Clean this exact bridge after real ack.
          DisposeInstalledContexts(entry,session);
          done(false, "Native binding registration canceled"); return;
        }
        if (!ok || identifier.empty()) { done(false, "Native binding document installation failed: " + json); return; }
        entry->scripts[session] = identifier;
        ConnectAdmitted(entry,session,done);
      });
    });
  }
  struct InstallerGroup {uint64_t token=0;std::string session,name;int context=0;bool active=true,inflight=true,release_started=false;size_t attempts=0;double retry=0;};
  void ReleaseInstallerGroup(std::shared_ptr<InstallerGroup> group){
    if(disposed_||!group->active)return;
    group->inflight=true;group->release_started=true;++group->attempts;auto release=CefDictionaryValue::Create();release->SetString("objectGroup",group->name);
    send_(group->session,"Runtime.releaseObjectGroup",release,[this,group](bool ok,const std::string&){
      if(!group->active)return;
      if(ok){group->active=false;installer_groups_.erase(group->token);return;}
      // A failed dispatch/ack cannot assert that Chromium released its group.
      // Retry boundedly; actual context/target destruction retires ownership.
      group->inflight=false;group->retry=now_()+std::min<size_t>(1000,25u<<std::min<size_t>(group->attempts,5));wake_();
    });
  }
  void EvaluateInstaller(const std::shared_ptr<Entry>& entry,const Context& target,Result done){
    if(installer_groups_.size()>=1024||installer_group_sequence_==std::numeric_limits<uint64_t>::max()){done(false,"Native binding installer group capacity exceeded");return;}
    auto group=std::make_shared<InstallerGroup>();group->token=++installer_group_sequence_;group->session=target.session;group->context=target.id;group->name=entry->bridge+"_installation_"+std::to_string(group->token);installer_groups_[group->token]=group;
    auto params=CefDictionaryValue::Create();params->SetInt("contextId",target.id);params->SetString("expression",Installer(*entry));params->SetBool("returnByValue",true);params->SetString("objectGroup",group->name);
    send_(target.session,"Runtime.evaluate",params,[this,group,done=std::move(done)](bool ok,const std::string& json){
      // These trusted Source failures mean no Inspector command was submitted.
      // Protocol failures/exceptionDetails still require a real group release.
      if(!ok&&(json=="Native binding command capacity exceeded or page closed"||json=="Native binding command dispatch failed")){if(group->active){group->active=false;installer_groups_.erase(group->token);}done(ok,json);return;}
      // Private exceptions can allocate Inspector handles. Ownership persists
      // through cancellation and until an actual release ack/context teardown.
      ReleaseInstallerGroup(group);done(ok,json);
    });
  }
  struct AdmittedInstallation {std::shared_ptr<Entry> entry;std::string session;std::vector<Context> contexts;size_t next=0;Result done;};
  void ConnectAdmitted(std::shared_ptr<Entry> entry,const std::string& session,Result done){
    auto work=std::make_shared<AdmittedInstallation>();work->entry=entry;work->session=session;work->done=std::move(done);
    for(const auto& [key,context]:contexts_)if(context.session==session)work->contexts.push_back(context);
    ConnectAdmittedNext(work);
  }
  void ConnectAdmittedNext(std::shared_ptr<AdmittedInstallation> work){
    if(disposed_||!work->entry->active||!sessions_.contains(work->session)){work->done(false,"Native binding installation canceled");return;}
    while(work->next<work->contexts.size()){
      const auto target=work->contexts[work->next++];const auto current=contexts_.find({target.session,target.id});
      // Removed contexts cannot be retargeted. Newly admitted contexts connect
      // through ContextAvailable using this same authored installer.
      if(current==contexts_.end()||current->second.frame->GetString("id")!=target.frame->GetString("id"))continue;
      EvaluateInstaller(work->entry,target,[this,work,target](bool ok,const std::string& json){
        const auto current=contexts_.find({target.session,target.id});
        if(current==contexts_.end()||current->second.frame->GetString("id")!=target.frame->GetString("id")){ConnectAdmittedNext(work);return;}
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        if(!ok||!result||result->HasKey("exceptionDetails")){work->done(false,"Native binding context installation failed: "+json);return;}
        ConnectAdmittedNext(work);
      });return;
    }
    work->done(true,"");
  }
  void DisposeInstalledContexts(const std::shared_ptr<Entry>& entry,const std::string& session){
    if(disposed_)return;
    for(const auto& [key,context]:contexts_)if(context.session==session){
      auto params=CefDictionaryValue::Create();params->SetInt("contextId",context.id);params->SetString("expression","globalThis["+Quote(entry->state)+"]?.dispose()");params->SetBool("returnByValue",true);
      send_(session,"Runtime.evaluate",params,[](bool,const std::string&){});
    }
  }
  void ReleaseEntryBytes(const std::shared_ptr<Entry>& entry) {
    // Timed-out registrations retain their source admission until physical
    // script installation commands settle, so late acknowledgments stay bounded.
    if (!disposed_ && !entry->active && !entry->installations && entry->accounted) {
      source_bytes_ -= entry->installer.size() + entry->codec.size(); entry->accounted = false;
    }
  }
  void RemoveEntry(std::shared_ptr<Entry> entry, const std::string& error) {
    if (!entry->active) return;
    entry->active = false;
    const auto found = registrations_.find(entry->name);
    if (found != registrations_.end() && found->second == entry) registrations_.erase(found);
    ReleaseEntryBytes(entry);
    std::vector<std::string> ids;
    for (const auto& [id, call] : calls_) if (call.entry == entry) ids.push_back(id);
    for (const auto& id : ids) CancelCall(id, error, true);
    for (const auto& [key, context] : contexts_) {
      auto params = CefDictionaryValue::Create(); params->SetInt("contextId", context.id);
      params->SetString("expression", "globalThis[" + Quote(entry->state) + "]?.dispose()");
      send_(context.session, "Runtime.evaluate", params, [](bool,const std::string&){});
    }
    for (const auto& session : sessions_) {
      auto params = CefDictionaryValue::Create(); params->SetString("name", entry->bridge);
      send_(session, "Runtime.removeBinding", params, [](bool,const std::string&){});
      const auto script = entry->scripts.find(session);
      if (script != entry->scripts.end()) { params = CefDictionaryValue::Create(); params->SetString("identifier", script->second); send_(session,"Page.removeScriptToEvaluateOnNewDocument",params,[](bool,const std::string&){}); }
    }
    entry->scripts.clear();
  }
  void Reject(const Entry& entry, const Target& target, double client, const std::string& nonce, const std::string& error) {
    auto params = CefDictionaryValue::Create(); params->SetInt("contextId", target.context);
    params->SetString("expression", "globalThis[" + Quote(entry.state) + "]?.settle(" + std::to_string(static_cast<uint64_t>(client)) + ",false,['error','Error'," + Quote(error) + ",''],[]," + Quote(nonce) + ")");
    send_(target.session,"Runtime.evaluate",params,[](bool,const std::string&){});
  }
  void CancelCall(const std::string& id, const std::string& error, bool reject) {
    const auto found = calls_.find(id); if (found == calls_.end()) return;
    const auto call = found->second; bytes_ -= call.bytes; result_bytes_ -= call.result.size(); calls_.erase(found);
    if (reject && contexts_.contains({call.target.session, call.target.context})) Reject(*call.entry, call.target, call.client, call.nonce, error);
    auto event = CefDictionaryValue::Create(); event->SetString("event", "canceled"); event->SetString("id", id); event->SetString("error", error); emit_(event);
  }
  void Stage(Dict message, Done done) {
    const auto id = message->GetString("bindingCallId").ToString();
    const auto found = calls_.find(id);
    if(found == calls_.end()) { done(false,"Native binding call is no longer active","");return; }
    auto& call = found->second;
    const auto fail = [this,id,done](const std::string& error) { CancelCall(id,error,true);done(false,error,""); };
    if(now_() >= call.deadline) { fail("Native binding callback timed out");return; }
    if(call.sealed || message->GetType("index") != VTYPE_INT || message->GetInt("index") != call.next_chunk ||
       message->GetType("data") != VTYPE_STRING || message->GetType("final") != VTYPE_BOOL) {
      fail("Invalid native binding result chunk sequence");return;
    }
    const auto data = message->GetString("data").ToString();
    if(data.size() > 32768) {fail("Native binding result chunk exceeds 24 KiB");return;}
    auto binary = CefBase64Decode(data);
    const auto size = binary ? binary->GetSize() : 0;
    if(!size || size > 24576 || size > 1048576-call.result.size() || size > 8388608-result_bytes_) {
      fail("Native binding staged result capacity exceeded");return;
    }
    const auto offset = call.result.size(); call.result.resize(offset+size);
    binary->GetData(call.result.data()+offset,size,0);
    result_bytes_ += size; ++call.next_chunk; call.sealed = message->GetBool("final");
    done(true,"","");
  }
  void Resolve(int owner, Dict message, Done done) {
    const auto id = message->GetString("bindingCallId").ToString();
    const auto found = calls_.find(id);
    if (found == calls_.end()) { done(false, "Native binding call is no longer active", ""); return; }
    if (message->GetType("success") != VTYPE_BOOL || (!message->HasKey("value") && !message->GetBool("staged")) || message->GetType("handles") != VTYPE_LIST || message->GetList("handles")->GetSize() > 1024) {
      done(false, "Invalid native binding callback result", ""); return;
    }
    const auto call = found->second;
    const bool staged = message->GetBool("staged");
    auto value = staged ? call.result : CefWriteJSON(message->GetValue("value"), JSON_WRITER_DEFAULT).ToString();
    if(staged && (!call.sealed || !CefParseJSON(value,JSON_PARSER_RFC))) { CancelCall(id,"Invalid native binding staged result",true);done(false,"Invalid native binding staged result","");return; }
    if (value.size() > 1048576) { CancelCall(id, "Native binding callback result exceeds 1 MiB", true); done(false,"Native binding callback result exceeds 1 MiB","");return; }
    auto params = CefDictionaryValue::Create(); params->SetInt("executionContextId", call.target.context);
    params->SetString("functionDeclaration", "function(...handles){const state=globalThis[" + Quote(call.entry->state) + "];if(!state)throw new Error('Native binding context is unavailable');const values=[];for(let i=0;i<handles.length;i++)values[i]=handles[i].value;state.settle(" + std::to_string(static_cast<uint64_t>(call.client)) + "," + (message->GetBool("success") ? "true" : "false") + ",JSON.parse(" + Quote(value) + "),values," + Quote(call.nonce) + ");}");
    params->SetBool("returnByValue",true); params->SetBool("awaitPromise",false);
    replies_[owner] = id;
    reply_(owner, call.target, message, params, [this, owner, id, call, done = std::move(done)](bool ok, const std::string& json) {
      replies_.erase(owner);
      const auto current = calls_.find(id);
      const bool active = current != calls_.end();
      if (active) { bytes_ -= current->second.bytes; result_bytes_ -= current->second.result.size(); calls_.erase(current); }
      if (active && !ok && contexts_.contains({call.target.session, call.target.context})) Reject(*call.entry, call.target, call.client, call.nonce, "Native binding callback delivery failed");
      done(ok, ok ? "" : json, "");
    });
  }
  std::string scope_;
  Send send_; Reply reply_; Emit emit_; std::function<void()> wake_; std::function<double()> now_;
  bool disposed_ = false;
  uint64_t registration_sequence_ = 0, call_sequence_ = 0;
  size_t bytes_ = 0, source_bytes_ = 0, result_bytes_ = 0,physical_installations_=0;
  uint64_t installer_group_sequence_=0;
  std::map<uint64_t,std::shared_ptr<InstallerGroup>> installer_groups_;
  std::set<std::string> sessions_;
  std::map<std::string,std::shared_ptr<SessionInstallation>> session_installations_;
  std::map<std::string,std::string> session_errors_;
  std::map<std::string,std::shared_ptr<Entry>> registrations_;
  std::map<int,std::shared_ptr<Entry>> pending_;
  std::map<int,std::string> replies_;
  std::map<int,std::string> arguments_;
  std::map<std::pair<std::string,int>,Context> contexts_;
  std::map<std::string,Call> calls_;
};
} // namespace suitecut
