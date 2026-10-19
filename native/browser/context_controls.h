// SPDX-License-Identifier: MIT
#ifndef SUITECUT_CONTEXT_CONTROLS_H_
#define SUITECUT_CONTEXT_CONTROLS_H_
#include <functional>
#include <deque>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "include/cef_values.h"
#include "include/cef_process_message.h"
#include "init_scripts.h"
#include "context_request_policy.h"
#include "context_permissions.h"
#include "context_permission_scopes.h"
#include "context_bindings.h"
#include "init_snapshot_scheme.h"
#include "init_snapshot_publication.h"
#include "init_snapshot_message.h"
#include "init_snapshot_control.h"
#include "storage_controls.h"
#include "cookie_controls.h"
#include "page_before_unload.h"

// UI-thread-only ownership of real CEF request contexts and browser lifetimes.
// Page clients and private command routing remain with the embedder. No disk
// cache is shared between created contexts; popups retain their opener context.
#include "credential_controls.h"
#include "credential_ready.h"
class SuiteCutContextControls : public std::enable_shared_from_this<SuiteCutContextControls> {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Reply = std::function<void(bool, Dict, const std::string&)>;
  struct Hooks {
    suitecut::CookieChannel cookie_channel;
    std::function<double(int)> cookie_deadline;
    std::function<void(int)> cancel_cookie;
    std::function<bool(int,int,const std::string&,Dict,double,Reply)> internal_cookies;
    std::function<bool(int,int,CefRefPtr<CefListValue>,double,std::string&)> validate_internal_cookies;
    std::function<bool(int,int,Dict,double,std::string&)> validate_internal_storage;
    std::function<bool(int, int, CefRefPtr<CefRequestContext>)> create_page;
    std::function<bool(int,Dict,std::string&)> context_created;
    std::function<void(int)> context_removed;
    std::function<void(int)> context_gone;
    std::function<void(Dict)> event;
    std::function<void()> idle;
    std::function<void(int,double,std::function<void()>)> before_page_close;
    std::function<void(int,double)> context_closing;
    std::function<double(int)> operation_deadline;
    std::function<double()> operation_now;
    std::function<uint64_t(int)> operation_generation;
    std::function<bool(int,uint64_t)> operation_current;
    std::function<std::string(int)> context_cleanup_error;
    std::function<bool(int)> context_cleanup_ready;
    std::function<void(int, const std::string&)> page_closing;
    std::function<bool(int, Dict, std::string&)> seed_binding;
    std::function<void(int, int, Dict, std::function<void(bool,const std::string&)>)> register_binding;
    std::function<void(int, int, const std::string&, const std::string&)> remove_binding;
    std::function<void(int,int,std::shared_ptr<const suitecut::CredentialPage::Config>,suitecut::CredentialPage::Result)> apply_credentials;
    std::function<void(int,int)> cancel_credentials;
    std::function<bool(int,std::shared_ptr<const suitecut::CredentialPage::Config>)> adopt_credentials;
    // Optional engine-owned publication metadata, populated before PageCreated.
    std::function<Dict(int)> page_metadata;
  };
  explicit SuiteCutContextControls(Hooks hooks);
  std::shared_ptr<const suitecut::CredentialPage::Config> CredentialConfig(int page) const;
  std::shared_ptr<suitecut::CredentialReadiness> CredentialReadyStore() const { return credential_ready_; }
  bool CredentialObserved(int page,const suitecut::VirtualCredential& credential,std::string&error);
  void SetUtilities(std::shared_ptr<SuiteCutUtilityBrowsers> utilities);
  bool InitializeDefault(CefRefPtr<CefRequestContext> context);
  bool Handle(const std::string& action, Dict params, Reply reply);
  void CreateInternal(Reply reply);
  void HTTPCommitCookies(int owner,int context,Dict params,Reply reply);
  void MatchHTTPCookies(int owner,int context,Dict filter,CefRefPtr<CefListValue> cookies,double deadline,Reply reply);
  bool ContextClosePending(int context)const{const auto found=contexts_.find(context);return found!=contexts_.end()&&found->second.closing;}
  bool PhysicalOwner(int owner)const{return (cookies_&&cookies_->PhysicalOwner(owner))||(storage_&&storage_->PhysicalOwner(owner));}
  int ReservePopup(int opener_page, int popup_id, const std::string& initial_url);
  void AbortPopup(int opener_page, int popup_id);
  void PageCreated(int page_id, CefRefPtr<CefBrowser> browser);
  void PageCreationFailed(int page_id, const std::string& error);
  void PageClosed(int page_id, const std::string& actual_reason = "");
  uint64_t BeforeUnloadCloseToken(int page) const;
  void BeforeUnloadCloseCanceled(int page,uint64_t token);
  bool BeforeUnloadCloseApproved(int page,CefRefPtr<CefBrowser> browser);
  void CloseAll(const std::string& reason = "");
  void ContinueCloseContext(int context);
  bool AcceptDownloads(int page) const;
  int PopupOpener(int page) const;
  bool IsPopup(int page) const;
  void CancelWait(int owner);
  Dict BrowserExtraInfo(int page_id) const;
  bool SeedPageBindings(int page, std::string& error);
  bool InitScriptFrameCreated(int page_id, CefRefPtr<CefFrame> frame);
  void InitScriptFrameDestroyed(int page_id, const std::string& frame_id);
  void SnapshotRendererTerminated(int page);
  bool InitScriptMessage(int page_id, CefRefPtr<CefFrame> frame, CefProcessId sender, CefRefPtr<CefProcessMessage> message);
  bool PageOriginObserved(int page, const std::string& url);
  CefRefPtr<CefRequestContext> RequestContext(int page_id) const;
  CefRefPtr<CefRequestContext> RequestContextForContext(int context_id) const;
  int ContextId(int page_id) const;
  // Exact native page-close ownership, independent of author callback RAW.
  bool PageClosePending(int page) const;
  double PageCreationDeadline(int page) const;
  std::string RequestPolicyError(int context_id)const;
  uint32_t AllowedPermissions(int page_id, const std::string& origin) const;
  uint32_t AllowedMediaPermissions(int page_id, const std::string& origin) const;
  bool AcceptedWorkReady()const;
  size_t ContextCount() const { return contexts_.size(); }
  size_t PageCount() const { return pages_.size(); }
 private:
  struct Context {
    CefRefPtr<CefRequestContext> cef;
    CefRefPtr<SuiteCutContextRequestPolicy> request_policy;
    bool closing = false;
    bool internal = false;
    bool accept_downloads = true;
    std::string close_reason;double close_deadline=0;
    SuiteCutInitScripts init_scripts;
    SuiteCutContextBindings bindings;
    bool init_scheme_registered = false;
    int owned_page = 0;
    std::map<std::string, uint32_t> permissions, media_permissions;
    std::vector<Reply> close_replies;
    std::set<std::string> origins;
    bool origins_overflow = false;
    std::set<std::string> clipboard_write_origins;
    std::map<std::string, std::set<std::string>> content_permissions;
    suitecut::NativePermissionScopes<cef_content_setting_values_t> storage_permission_scopes;
    std::map<std::string, std::map<cef_content_setting_types_t,cef_content_setting_values_t>> applied_settings;
  };
  Dict InitScriptSnapshot(int context_id) const;
  Dict BindingConfig(const SuiteCutContextBindings::Entry& entry) const;
  bool RefreshInitSnapshot(int context,int owner=0);
  bool CommitInitSnapshot(int context,std::string text,int owner=0);
  double SnapshotNow()const;
  double SnapshotDeadline(int owner)const;
  bool SnapshotMessage(int page,CefRefPtr<CefFrame>frame,CefProcessId sender,CefRefPtr<CefProcessMessage>message);
  bool SnapshotSend(int page,int owner=0);
  void SnapshotBroadcast(int context,int owner);
  void SnapshotAwait(int owner,int context,uint64_t generation,double deadline,Dict result,Reply reply);
  Reply SnapshotReply(int owner,int context,Reply reply);
  void SnapshotCheckWaiters();
  void SnapshotWake();
  void SnapshotTick();
  void SnapshotFatal(int page,const std::string&reason);
  void SnapshotPageClosed(int page);
  void SnapshotDiagnostics(Dict result)const;
  struct SnapshotPeer {suitecut::InitSnapshotOwner pin;uint64_t request=0,installed=0,sent=0;std::string route;bool ready=false;};
  struct SnapshotFlight {suitecut::InitSnapshotPin pin;int page=0,owner=0;uint64_t request=0;bool acknowledged=false,installed=false,released=false;};
  struct SnapshotWait {int context=0;uint64_t version=0,generation=0;double deadline=0;Dict result;Reply reply;std::map<int,uint64_t>epochs;};
  suitecut::InitSnapshotPublications snapshot_publications_;
  suitecut::InitSnapshotReceipts snapshot_receipts_;
  std::map<int,SnapshotPeer>snapshot_peers_;std::set<int>snapshot_closing_;
  std::map<uint64_t,SnapshotFlight>snapshot_flights_;
  std::map<int,SnapshotWait>snapshot_waits_;
  struct SnapshotTarget {int context=0;uint64_t version=0;std::map<int,uint64_t>epochs;};
  std::map<int,std::shared_ptr<SnapshotTarget>>snapshot_targets_;
  uint64_t snapshot_next_epoch_=0,snapshot_install_acks_=0,snapshot_release_acks_=0;bool snapshot_timer_=false;
  void RegisterContextBinding(int context, int owner, Dict params, Reply reply);
  void CancelContextBinding(int owner, const std::string& error = "", bool respond = false);
  void ContextBindingPageClosed(int page);
  struct BindingBatch { int context = 0, entry = 0; std::string name, bridge; std::set<int> pages; Reply reply; };
  std::map<int, BindingBatch> binding_batches_;
  std::string binding_namespace_;
  bool RegisterInitContext(int context_id);
  std::shared_ptr<SuiteCutInitSnapshotStore> init_snapshot_store_ = std::make_shared<SuiteCutInitSnapshotStore>();
  void RegisterInitScript(int context_id, int owner, const std::string& source, Reply reply);
  void CancelInitScript(int owner);
  void WakeInitScriptDeadlines();
  void PumpInitScriptDeadlines();
  struct ScriptBatch { int context = 0; std::set<std::string> tokens; Reply reply; };
  struct ScriptAck { int owner = 0, page = 0; std::string frame; size_t bytes = 0; double deadline = 0; };
  std::map<int, ScriptBatch> script_batches_;
  std::map<std::string, ScriptAck> script_acks_;
  size_t pending_script_bytes_ = 0;
  uint64_t next_script_ack_ = 0;
  bool script_deadline_timer_ = false;
  void ApplySettings(Context& context, const std::string& origin);
  bool FailPermissionScopes(int context,const std::string& reason);
  struct Page {
    suitecut::PageBeforeUnload before_unload;
    bool download_close_started = false;
    bool initial_popup_navigation = false;
    int context = 0, opener = 0, popup_id = 0;
    bool closing = false;
    std::string close_reason;double close_deadline=0;
    CefRefPtr<CefBrowser> browser;
    double creation_deadline = 0;
    Reply created_reply;
    std::vector<Reply> close_replies;
  };
  int ReservePage(int context, int opener, int popup_id);
  void FinishContext(int context);
  void BeginPageClose(int page, const std::string& reason);
  Dict PageResult(int context, int page) const;
  void Emit(const char* event, int context, int page = 0, int opener = 0, const std::string& reason = "");
  struct EventWait { std::string event; int page = 0, context = 0; Reply reply; };
  static bool Matches(const EventWait& wait, Dict event);
  std::map<int, EventWait> waits_;
  std::map<int, std::function<void()>> cookie_cancellations_;
  std::deque<Dict> journal_;
  uint64_t sequence_ = 0;
  std::shared_ptr<SuiteCutStorageControls> storage_;
  std::shared_ptr<SuiteCutUtilityBrowsers> utilities_;
  std::shared_ptr<suitecut::CredentialControls> credentials_;
  std::shared_ptr<suitecut::CredentialReadiness> credential_ready_=std::make_shared<suitecut::CredentialReadiness>();
  std::shared_ptr<suitecut::CookieControls> cookies_;
  Hooks hooks_;
  std::map<int, Context> contexts_;
  std::map<int, Page> pages_;
  int next_context_ = 1, next_page_ = 1;
  bool stopping_ = false;
};
#endif
