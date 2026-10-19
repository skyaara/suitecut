// SPDX-License-Identifier: MIT
#include "context_controls.h"
#include "accepted_task_work.h"
#include "context_settings.h"
#include "inbound_string.h"
#include "cookie_filter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>
#include "include/cef_cookie.h"
#include "include/cef_frame.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
namespace {
using Dict = SuiteCutContextControls::Dict;
using Reply = SuiteCutContextControls::Reply;
class ContextPolicyCloseTask:public CefTask{
 public:explicit ContextPolicyCloseTask(std::function<void()> task):task_(std::move(task)){}void Execute()override{const suitecut::AcceptedTaskFrame native_frame(native_work_);task_();}
 private:suitecut::AcceptedTaskReceipt native_work_;std::function<void()>task_;IMPLEMENT_REFCOUNTING(ContextPolicyCloseTask);
};
Dict Result(int context, int page = 0) {
  auto value = CefDictionaryValue::Create(); value->SetInt("contextId", context);
  if (page) value->SetInt("pageId", page);
  return value;
}
std::string Origin(const std::string& value) {
  CefURLParts parts;
  if (!CefParseURL(value, parts)) return {};
  const auto scheme = CefString(&parts.scheme).ToString();
  const auto host = CefString(&parts.host).ToString();
  if ((scheme != "http" && scheme != "https") || host.empty()) return {};
  const auto port = CefString(&parts.port).ToString();
  return scheme + "://" + host + ((port.empty() || (scheme == "http" && port == "80") || (scheme == "https" && port == "443")) ? "" : ":" + port);
}
struct CookieInput { std::string url; CefCookie cookie; };
bool ParseCookie(Dict value, CookieInput& input, std::string& error) {
  if (!value || value->GetType("name") != VTYPE_STRING || value->GetType("value") != VTYPE_STRING) { error = "Cookie name and value must be strings"; return false; }
  CefDictionaryValue::KeyList keys; value->GetKeys(keys);
  for (const auto& key : keys) {
    const auto field = key.ToString();
    if (field != "name" && field != "value" && field != "url" && field != "domain" && field != "path" && field != "expires" && field != "httpOnly" && field != "secure" && field != "sameSite" && field != "partitionKey" && field != "partitionKeyHasCrossSiteAncestor") {
      error = "Unsupported native cookie attribute: " + field; return false;
    }
    if ((field == "url" || field == "domain" || field == "path" || field == "sameSite") && value->GetType(key) != VTYPE_STRING) { error = "Cookie string attribute has invalid type"; return false; }
    if ((field == "httpOnly" || field == "secure") && value->GetType(key) != VTYPE_BOOL) { error = "Cookie boolean attribute has invalid type"; return false; }
  }
  const auto name = value->GetString("name").ToString(), content = value->GetString("value").ToString();
  if (name.size() + content.size() > 4096 || name.find_first_of(";\r\n") != std::string::npos || content.find_first_of(";\r\n") != std::string::npos) { error = "Cookie attributes exceed 4096 bytes or contain disallowed characters"; return false; }
  const auto domain = value->GetString("domain").ToString(), path = value->GetString("path").ToString();
  input.url = value->GetString("url").ToString();
  if (!input.url.empty()) {
    if (!domain.empty() || !path.empty() || Origin(input.url).empty()) { error = "Cookie must specify an HTTP(S) URL or domain and path"; return false; }
  } else {
    if (domain.empty() || path.empty() || path.front() != '/' || domain.find_first_of("/\r\n;: ") != std::string::npos) { error = "Cookie should have a url or a domain/path pair; domain and an absolute path are required when URL is absent"; return false; }
    input.url = std::string(value->GetBool("secure") ? "https://" : "http://") + (domain.front() == '.' ? domain.substr(1) : domain) + path;
    if (Origin(input.url).empty()) { error = "Invalid cookie domain"; return false; }
  }
  CefString(&input.cookie.name) = name; CefString(&input.cookie.value) = content;
  CefString(&input.cookie.domain) = domain; CefString(&input.cookie.path) = path;
  input.cookie.secure = value->GetBool("secure"); input.cookie.httponly = value->GetBool("httpOnly");
  const auto same = value->GetString("sameSite").ToString();
  if (!same.empty() && same != "Strict" && same != "Lax" && same != "None") { error = "Invalid cookie sameSite"; return false; }
  input.cookie.same_site = same == "Strict" ? CEF_COOKIE_SAME_SITE_STRICT_MODE : same == "None" ? CEF_COOKIE_SAME_SITE_NO_RESTRICTION : CEF_COOKIE_SAME_SITE_LAX_MODE;
  if (value->HasKey("expires")) {
    if (value->GetType("expires") != VTYPE_DOUBLE && value->GetType("expires") != VTYPE_INT) { error = "Cookie expires must be a finite Unix timestamp or -1"; return false; }
    const double expires = value->GetType("expires") == VTYPE_INT ? value->GetInt("expires") : value->GetDouble("expires");
    if (!std::isfinite(expires) || (expires < 0 && expires != -1) || expires > 253402300799.0) { error = "Invalid cookie expiration"; return false; }
    if (expires >= 0) {
      cef_time_t time{};
      if (!cef_time_from_doublet(expires, &time) || !cef_time_to_basetime(&time, &input.cookie.expires)) { error = "Cookie expiration could not be represented"; return false; }
      input.cookie.has_expires = true;
    }
  }
  return suitecut::CookieControls::Partition(value,error);
}
class SetCallback : public CefSetCookieCallback {
 public:
  explicit SetCallback(std::function<void(bool)> callback) : callback_(std::move(callback)) {}
  void OnComplete(bool success) override { callback_(success); }
 private:
  std::function<void(bool)> callback_;
  IMPLEMENT_REFCOUNTING(SetCallback);
};
struct CookieWriter : std::enable_shared_from_this<CookieWriter> {
  CefRefPtr<CefCookieManager> manager;
  std::vector<CookieInput> cookies;
  Reply reply;
  size_t index = 0;
  bool done = false;
  void Cancel() { done = true; reply = {}; cookies.clear(); }
  void Next(bool success = true) {
    if (done) return;
    if (!success || index == cookies.size()) {
      done = true; auto callback = std::move(reply);
      callback(success, nullptr, success ? "" : "CEF rejected cookie attributes"); return;
    }
    const auto& cookie = cookies[index++];
    auto callback = new SetCallback([self = shared_from_this()](bool ok) { self->Next(ok); });
    if (!manager->SetCookie(cookie.url, cookie.cookie, callback)) Next(false);
  }
};
class CookieVisitor : public CefCookieVisitor {
 public:
  CookieVisitor(std::vector<std::string> urls, Dict filter, bool clear, Reply reply, std::shared_ptr<bool> canceled)
      : urls_(std::move(urls)), filter_(filter), clear_(clear), reply_(std::move(reply)), list_(CefListValue::Create()), canceled_(std::move(canceled)) {}
  ~CookieVisitor() override { Finish(); }
  void Reject() { error_ = "CEF cookie storage is unavailable"; Finish(); }
  bool Visit(const CefCookie& cookie, int, int, bool& remove) override {
    if (!reply_ || *canceled_) { list_->SetSize(0); return false; }
    const auto domain = CefString(&cookie.domain).ToString(), path = CefString(&cookie.path).ToString(), name = CefString(&cookie.name).ToString();
    if (filter_) {
      for (const auto& field : {"name", "domain", "path"}) {
        if (!filter_->HasKey(field)) continue;
        if (filter_->GetString(field).ToString() != (std::string(field) == "name" ? name : std::string(field) == "domain" ? domain : path)) return true;
      }
    }
    if (!urls_.empty()) {
      bool matches = false;
      for (const auto& url : urls_) {
        CefURLParts parts; if (!CefParseURL(url, parts)) continue;
        const auto host = CefString(&parts.host).ToString(), url_path = CefString(&parts.path).ToString(), scheme = CefString(&parts.scheme).ToString();
        const auto host_domain = !domain.empty() && domain.front() == '.' ? domain.substr(1) : domain;
        const bool domain_match = host == host_domain || (!domain.empty() && domain.front() == '.' && host.size() > host_domain.size() && host.ends_with("." + host_domain));
        const bool path_match = url_path.starts_with(path) && (url_path.size() == path.size() || path.ends_with('/') || url_path[path.size()] == '/');
        if (domain_match && path_match && (!cookie.secure || scheme == "https")) { matches = true; break; }
      }
      if (!matches) return true;
    }
    if (clear_) { remove = true; return true; }
    const auto value = CefString(&cookie.value).ToString();
    bytes_ += name.size() + value.size() + domain.size() + path.size() + 256;
    if (list_->GetSize() >= 4096 || bytes_ > 1048576) { error_ = "Cookie result exceeds bounded native output"; return false; }
    auto item = CefDictionaryValue::Create(); item->SetString("name", name); item->SetString("value", value); item->SetString("domain", domain); item->SetString("path", path);
    double expiration = -1;
    if (cookie.has_expires) { cef_time_t time{}; cef_time_from_basetime(cookie.expires, &time); cef_time_to_doublet(&time, &expiration); }
    item->SetDouble("expires", expiration); item->SetBool("httpOnly", cookie.httponly); item->SetBool("secure", cookie.secure);
    item->SetString("sameSite", cookie.same_site == CEF_COOKIE_SAME_SITE_STRICT_MODE ? "Strict" : cookie.same_site == CEF_COOKIE_SAME_SITE_NO_RESTRICTION ? "None" : "Lax");
    list_->SetDictionary(list_->GetSize(), item); return true;
  }
 private:
  void Finish() {
    if (!reply_) return;
    auto reply = std::move(reply_); auto result = CefDictionaryValue::Create(); result->SetList("cookies", list_);
    reply(!*canceled_ && error_.empty(), result, *canceled_ ? "Native cookie query canceled" : error_);
  }
  std::vector<std::string> urls_; Dict filter_; bool clear_; Reply reply_; CefRefPtr<CefListValue> list_; size_t bytes_ = 0; std::string error_; std::shared_ptr<bool> canceled_;
  IMPLEMENT_REFCOUNTING(CookieVisitor);
};
uint32_t Permission(const std::string& name) {
  const auto entry=suitecut::NativePermission(name);return entry?entry->flags:0;
}
}
SuiteCutContextControls::SuiteCutContextControls(Hooks hooks) : hooks_(std::move(hooks)) {
  if(hooks_.cookie_channel)cookies_=std::make_shared<suitecut::CookieControls>(hooks_.cookie_channel);
  static uint64_t managers = 0; binding_namespace_ = std::to_string(++managers);
  suitecut::CredentialControls::Hooks credential_hooks;
  credential_hooks.open=[this](int context){return static_cast<bool>(RequestContextForContext(context));};
  credential_hooks.pages=[this](int context){std::vector<int>result;for(const auto&[id,page]:pages_)if(page.context==context&&!page.closing&&page.browser)result.push_back(id);return result;};
  credential_hooks.fatal=[this](int context,const std::string&reason){auto params=CefDictionaryValue::Create();params->SetInt("contextId",context);params->SetString("reason",reason);Handle("close",params,[](bool,Dict,const std::string&){});};
  credential_hooks.apply=hooks_.apply_credentials;credential_hooks.cancel=hooks_.cancel_credentials;credential_hooks.adopt_observed=hooks_.adopt_credentials;
  credential_hooks.publish_owned=[this](int context,int owner,std::shared_ptr<const suitecut::CredentialPage::Config>config){
    for(const auto&[id,page]:pages_)if(page.context==context&&!page.closing&&page.browser)credential_ready_->Register(page.browser->GetIdentifier(),page.browser->GetHost()->GetRequestContext());
    return RefreshInitSnapshot(context,owner);
  };
  credentials_=std::make_shared<suitecut::CredentialControls>(std::move(credential_hooks));
}
std::shared_ptr<const suitecut::CredentialPage::Config> SuiteCutContextControls::CredentialConfig(int page) const {return credentials_->Config(ContextId(page));}
bool SuiteCutContextControls::CredentialObserved(int page,const suitecut::VirtualCredential&credential,std::string&error){return credentials_->Observed(ContextId(page),page,credential,error);}
void SuiteCutContextControls::HTTPCommitCookies(int owner,int context,Dict params,Reply reply){
  const auto found=contexts_.find(context);if(!cookies_||found==contexts_.end()||found->second.closing){reply(false,nullptr,"Native HTTP cookie profile is closed");return;}
  // This entry point accepts only internally parsed actual HTTP response
  // cookies. Public cookie inputs continue through Handle's full validation.
  params->SetBool("storageInternal",true);cookies_->Handle(owner,context,"addCookies",params,hooks_.cookie_deadline?hooks_.cookie_deadline(owner):0,std::move(reply));
}
void SuiteCutContextControls::CreateInternal(Reply reply){
  Handle("create",CefDictionaryValue::Create(),[self=shared_from_this(),reply=std::move(reply)](bool ok,Dict result,const std::string&error){if(ok&&result){const auto found=self->contexts_.find(result->GetInt("contextId"));if(found==self->contexts_.end()){reply(false,nullptr,"Native internal context creation lost its owner");return;}found->second.internal=true;}reply(ok,result,error);});
}
void SuiteCutContextControls::MatchHTTPCookies(int owner,int context,Dict filter,CefRefPtr<CefListValue> cookies,double deadline,Reply reply){const auto found=contexts_.find(context);if(!cookies_||found==contexts_.end()||!found->second.internal||found->second.closing){reply(false,nullptr,"Native internal cookie matcher is closed");return;}cookies_->MatchOwned(owner,context,filter,cookies,deadline,std::move(reply));}
void SuiteCutContextControls::SetUtilities(std::shared_ptr<SuiteCutUtilityBrowsers> utilities) {
  if (storage_ || !utilities) return;
  utilities_ = utilities;
  SuiteCutStorageControls::Hooks hooks;
  hooks.settled=[weak=weak_from_this()](int context){if(const auto self=weak.lock())self->ContinueCloseContext(context);};
  hooks.context = [weak = weak_from_this()](int context) { const auto self = weak.lock(); return self ? self->RequestContextForContext(context) : nullptr; };
  hooks.origins = [weak = weak_from_this()](int context, std::vector<std::string>& origins) {
    const auto self = weak.lock(); if (!self) return false;
    const auto found = self->contexts_.find(context); if (found == self->contexts_.end() || found->second.origins_overflow) return false;
    origins.assign(found->second.origins.begin(), found->second.origins.end()); return true;
  };
  hooks.cookies = [weak = weak_from_this()](int owner, int context, const std::string& action, Dict params, Reply reply) {
    const auto self = weak.lock(); if (!self) { reply(false, nullptr, "Native storage context is closed"); return; }
    params->SetInt("contextId", context); params->SetInt("internalOwner", owner); self->Handle(action, params, std::move(reply));
  };
  hooks.validate_context_cookies=[weak=weak_from_this()](int owner,int context,Dict state,std::string&error){const auto self=weak.lock();if(!self)return false;const auto found=self->contexts_.find(context);if(found==self->contexts_.end())return false;if(found->second.internal){if(self->hooks_.validate_internal_storage)return self->hooks_.validate_internal_storage(owner,context,state,self->hooks_.cookie_deadline?self->hooks_.cookie_deadline(owner):0,error);if(!self->hooks_.validate_internal_cookies){error="Native internal cookie validation unavailable";return false;}return self->hooks_.validate_internal_cookies(owner,context,state->GetList("cookies"),self->hooks_.cookie_deadline?self->hooks_.cookie_deadline(owner):0,error);}const auto cookies=state->GetList("cookies");if(!cookies||cookies->GetSize()>4096){error="Native storage cookie capacity exceeded";return false;}for(size_t i=0;i<cookies->GetSize();++i){CookieInput input;if(!ParseCookie(cookies->GetDictionary(i),input,error))return false;}return true;};
  hooks.validate_cookies = [](Dict state, std::string& error) {
    const auto cookies = state->GetList("cookies"); if (!cookies || cookies->GetSize() > 4096) { error = "Native storage cookie capacity exceeded"; return false; }
    for (size_t index = 0; index < cookies->GetSize(); ++index) { CookieInput input; if (!ParseCookie(cookies->GetDictionary(index), input, error)) return false; }
    return true;
  };
  hooks.record_origin = [weak = weak_from_this()](int context, const std::string& origin) {
    const auto self = weak.lock(); if (!self) return;
    const auto found = self->contexts_.find(context); if (found == self->contexts_.end()) return;
    if (!found->second.origins.contains(origin) && found->second.origins.size() >= 1024) { found->second.origins_overflow = true; return; }
    found->second.origins.insert(origin); self->ApplySettings(found->second, origin);
  };
  hooks.validate_credentials=[weak=weak_from_this()](Dict state,std::string&error){const auto self=weak.lock();if(!self)return false;return !state->HasKey("credentials")||self->credentials_->ValidateState(state->GetList("credentials"),error);};
  hooks.credentials=[weak=weak_from_this()](int owner,int context,const std::string&action,Dict state,Reply reply){
    const auto self=weak.lock();if(!self){reply(false,nullptr,"Native credential context is closed");return;}
    if(action=="restore"){const bool present=state->HasKey("credentials");self->credentials_->Restore(owner,context,present?state->GetList("credentials"):CefListValue::Create(),present,self->SnapshotReply(owner,context,std::move(reply)));}
    else self->credentials_->Handle(owner,context,"get",CefDictionaryValue::Create(),std::move(reply));
  };
  storage_ = std::make_shared<SuiteCutStorageControls>(utilities, std::move(hooks));
}
bool SuiteCutContextControls::InitializeDefault(CefRefPtr<CefRequestContext> context) {
  if (!contexts_.empty()) return contexts_.contains(1);
  Context entry; entry.cef = context; contexts_.emplace(1, std::move(entry));
  std::string error;if(hooks_.context_created && !hooks_.context_created(1,CefDictionaryValue::Create(),error)){if(hooks_.context_removed)hooks_.context_removed(1);contexts_.erase(1);return false;}
  credentials_->ContextCreated(1);
  if(!RegisterInitContext(1)){credentials_->ContextClosed(1);if(hooks_.context_removed)hooks_.context_removed(1);contexts_.erase(1);return false;}
  Page page; page.context = 1; pages_.emplace(1, std::move(page));
  return true;
}
int SuiteCutContextControls::ReservePage(int context, int opener, int popup_id) {
  const auto entry = contexts_.find(context);
  if (stopping_ || entry == contexts_.end() || entry->second.closing || pages_.size() >= 128 || next_page_ == std::numeric_limits<int>::max()) return 0;
  const int id = ++next_page_; Page page; page.context = context; page.opener = opener; page.popup_id = popup_id;
  pages_.emplace(id, std::move(page)); return id;
}
int SuiteCutContextControls::ReservePopup(int opener_page, int popup_id, const std::string& initial_url) {
  const auto opener = pages_.find(opener_page);
  if(opener==pages_.end()){return 0;}const int page=ReservePage(opener->second.context,opener_page,popup_id);
  if(page)pages_.at(page).initial_popup_navigation=!initial_url.empty()&&!initial_url.starts_with("about:")&&!initial_url.starts_with("javascript:");
  return page;
}
void SuiteCutContextControls::AbortPopup(int opener_page, int popup_id) {
  for (const auto& [id, page] : pages_) if (page.opener == opener_page && page.popup_id == popup_id && !page.browser) { PageCreationFailed(id, "Popup creation was aborted"); return; }
}
bool SuiteCutContextControls::IsPopup(int page)const {
  const auto found=pages_.find(page);return found!=pages_.end()&&found->second.opener!=0;
}
int SuiteCutContextControls::PopupOpener(int page)const {
  const auto found=pages_.find(page);if(found==pages_.end()||!found->second.opener||!found->second.initial_popup_navigation)return 0;
  const auto opener=pages_.find(found->second.opener);return opener!=pages_.end()&&!opener->second.closing?found->second.opener:0;
}
bool SuiteCutContextControls::AcceptDownloads(int page) const {
  const auto found=contexts_.find(ContextId(page));return found!=contexts_.end()&&found->second.accept_downloads&&!found->second.closing&&!stopping_;
}
void SuiteCutContextControls::ContinueCloseContext(int context){FinishContext(context);if(stopping_&&pages_.empty()&&hooks_.idle)hooks_.idle();}
void SuiteCutContextControls::PageCreated(int page_id, CefRefPtr<CefBrowser> browser) {
  const auto page = pages_.find(page_id);
  if (page == pages_.end()) { browser->GetHost()->CloseBrowser(true); return; }
  page->second.browser = browser;
  PageOriginObserved(page_id, browser->GetMainFrame()->GetURL().ToString());
  // The initial page is announced by the process handshake; emitting before
  // that handshake would violate protocol startup ordering.
  if (page_id != 1) Emit("page-created", page->second.context, page_id, page->second.opener);
  if (page->second.created_reply) { auto reply = std::move(page->second.created_reply); reply(true, PageResult(page->second.context, page_id), ""); }
  if (stopping_ || contexts_.at(page->second.context).closing || page->second.closing)
    BeginPageClose(page_id, contexts_.at(page->second.context).closing ? contexts_.at(page->second.context).close_reason : page->second.close_reason);
}
void SuiteCutContextControls::PageCreationFailed(int page_id, const std::string& error) {
  const auto page = pages_.find(page_id); if (page == pages_.end()) return;
  const int context = page->second.context;
  auto created = std::move(page->second.created_reply); auto closed = std::move(page->second.close_replies);
  pages_.erase(page);
  const auto owning = contexts_.find(context); if (owning != contexts_.end() && owning->second.owned_page == page_id) owning->second.closing = true;
  if (created) created(false, nullptr, error);
  for (auto& reply : closed) reply(true, nullptr, "");
  FinishContext(context);
  if (stopping_ && pages_.empty() && hooks_.idle) hooks_.idle();
}
void SuiteCutContextControls::PageClosed(int page_id, const std::string& actual_reason) {
  SnapshotPageClosed(page_id);
  InitScriptFrameDestroyed(page_id, ""); ContextBindingPageClosed(page_id);
  const auto page = pages_.find(page_id); if (page == pages_.end()) return;
  const int context = page->second.context, opener = page->second.opener;
  const auto close_reason = page->second.close_reason.empty()?actual_reason:page->second.close_reason;const double close_deadline=page->second.close_deadline;
  auto closed = std::move(page->second.close_replies); auto created = std::move(page->second.created_reply);
  pages_.erase(page); Emit("page-closed", context, page_id, opener,close_reason);
  const auto owning = contexts_.find(context);
  if (owning != contexts_.end() && owning->second.owned_page == page_id) {
    if (!owning->second.closing) { owning->second.closing = true; owning->second.close_reason = close_reason; owning->second.close_deadline=close_deadline; }
    if(storage_)storage_->ContextClosed(context);
    if(cookies_)cookies_->Closed(context);
    if(hooks_.context_closing)hooks_.context_closing(context,owning->second.close_deadline);
    std::vector<int> remaining;
    for (const auto& [id, other] : pages_) if (other.context == context) remaining.push_back(id);
    for (const int id : remaining) BeginPageClose(id, owning->second.close_reason);
  }
  if (created) created(false, nullptr, "Page closed before initialization");
  for (auto& reply : closed) reply(true, nullptr, "");
  std::vector<int> aborted;
  for (const auto& [id, pending] : pages_) if (pending.opener == page_id && !pending.browser) aborted.push_back(id);
  for (const int id : aborted) PageCreationFailed(id, "Popup opener closed before initialization");
  FinishContext(context); if (stopping_ && pages_.empty() && hooks_.idle) hooks_.idle();
}
void SuiteCutContextControls::FinishContext(int context_id) {
  const auto context = contexts_.find(context_id); if (context == contexts_.end() || !context->second.closing) return;
  for (const auto& [id, page] : pages_) if (page.context == context_id) return;
  if((storage_&&storage_->PhysicalContext(context_id))||(cookies_&&cookies_->PhysicalContext(context_id)))return;
  if(hooks_.context_cleanup_ready&&!hooks_.context_cleanup_ready(context_id))return;
  if (storage_) storage_->ContextClosed(context_id);
  credentials_->ContextClosed(context_id);
  const auto close_error=hooks_.context_cleanup_error?hooks_.context_cleanup_error(context_id):std::string{};
  const bool internal=context->second.internal;const auto close_reason=context->second.close_reason;auto replies = std::move(context->second.close_replies); init_snapshot_store_->Drop(context_id);snapshot_publications_.Drop(context_id);if(hooks_.context_removed)hooks_.context_removed(context_id); contexts_.erase(context);if(hooks_.context_gone)hooks_.context_gone(context_id);if(!internal)Emit("context-closed", context_id,0,0,close_reason);
  for (auto& reply : replies) {auto result=Result(context_id);if(!close_reason.empty())result->SetString("closedReason",close_reason);reply(close_error.empty(), result, close_error);}
}
void SuiteCutContextControls::BeginPageClose(int page_id, const std::string& reason) {
  const auto page = pages_.find(page_id); if (page == pages_.end()) return;
  page->second.before_unload.Force();
  if (!page->second.closing) { page->second.closing = true; page->second.close_reason = reason; }
  const auto snapshot_peer=snapshot_peers_.find(page_id);if(snapshot_peer!=snapshot_peers_.end())snapshot_peer->second.ready=false;SnapshotCheckWaiters();
  ContextBindingPageClosed(page_id);
  if (hooks_.page_closing) hooks_.page_closing(page_id, page->second.close_reason);
  const auto context=contexts_.find(page->second.context);if(context!=contexts_.end()&&context->second.closing&&!page->second.close_deadline)page->second.close_deadline=context->second.close_deadline;
  const auto browser = page->second.browser; if(!browser||page->second.download_close_started)return;
  page->second.download_close_started=true;
  if(hooks_.before_page_close)hooks_.before_page_close(page_id,page->second.close_deadline,[browser]{browser->GetHost()->CloseBrowser(true);});
  else browser->GetHost()->CloseBrowser(true);
}
uint64_t SuiteCutContextControls::BeforeUnloadCloseToken(int page)const{
  const auto found=pages_.find(page);return found==pages_.end()?0:found->second.before_unload.Pending();
}
void SuiteCutContextControls::BeforeUnloadCloseCanceled(int page,uint64_t token){
  const auto found=pages_.find(page);if(found!=pages_.end())found->second.before_unload.Cancel(token);
}
bool SuiteCutContextControls::BeforeUnloadCloseApproved(int page_id,CefRefPtr<CefBrowser> browser){
  const auto found=pages_.find(page_id);
  if(found==pages_.end()||found->second.closing||!found->second.browser||!found->second.browser->IsSame(browser))return false;
  auto approved=found->second.before_unload.Approve();if(!approved)return false;
  found->second.closing=true;found->second.close_reason=std::move(approved->reason);found->second.close_deadline=approved->deadline;
  // DoClose is the real unload approval. Defer its irreversible cleanup out of
  // that CEF callback; its existing final continuation calls CloseBrowser(true).
  const auto self=shared_from_this();
  if(!CefPostTask(TID_UI,new ContextPolicyCloseTask([self,page_id]{
    const auto page=self->pages_.find(page_id);if(page!=self->pages_.end())self->BeginPageClose(page_id,page->second.close_reason);
  })))BeginPageClose(page_id,found->second.close_reason);
  return true;
}
void SuiteCutContextControls::CloseAll(const std::string& reason) {
  if (storage_) storage_->Clear();
  std::vector<int> binding_owners; for (const auto& [owner,batch] : binding_batches_) binding_owners.push_back(owner);
  for (const int owner : binding_owners) CancelContextBinding(owner,"Native browser closed",true);
  stopping_ = true;
  std::vector<int> contexts;for (auto& [id, context] : contexts_){if(!context.closing){context.closing=true;context.close_reason=reason;}contexts.push_back(id);}
  for(const int id:contexts){if(cookies_)cookies_->Closed(id);if(hooks_.context_closing)hooks_.context_closing(id,contexts_.at(id).close_deadline);}
  std::vector<int> pages; for (const auto& [id, page] : pages_) pages.push_back(id);
  for (const int id : pages) { const auto page = pages_.find(id); if (page != pages_.end()) BeginPageClose(id, contexts_.at(page->second.context).close_reason); }
  if (pages_.empty() && hooks_.idle) hooks_.idle();
}
SuiteCutContextControls::Dict SuiteCutContextControls::PageResult(int context,int page) const {
  auto info=Result(context,page);
  if(page&&hooks_.page_metadata){const auto metadata=hooks_.page_metadata(page);if(metadata&&metadata->HasKey("video"))info->SetDictionary("video",metadata->GetDictionary("video"));}
  return info;
}
void SuiteCutContextControls::Emit(const char* event, int context, int page, int opener,const std::string&reason) {
  auto info = PageResult(context, page); info->SetString("event", event);if(!reason.empty())info->SetString("closedReason",reason); if (opener) info->SetInt("openerId", opener);
  info->SetDouble("sequence", static_cast<double>(++sequence_));
  journal_.push_back(info->Copy(false)); if (journal_.size() > 256) journal_.pop_front();
  if (hooks_.event) hooks_.event(info);
  std::vector<int> completed;
  for (const auto& [owner, wait] : waits_) if (Matches(wait, info)) completed.push_back(owner);
  for (const int owner : completed) {
    const auto found = waits_.find(owner); if (found == waits_.end()) continue;
    auto reply = std::move(found->second.reply); waits_.erase(found); reply(true, info->Copy(false), "");
  }
}
CefRefPtr<CefRequestContext> SuiteCutContextControls::RequestContext(int page_id) const {
  const auto page = pages_.find(page_id); if (page == pages_.end()) return nullptr;
  const auto context = contexts_.find(page->second.context); return context == contexts_.end() ? nullptr : context->second.cef;
}
CefRefPtr<CefRequestContext> SuiteCutContextControls::RequestContextForContext(int context_id) const {
  const auto context = contexts_.find(context_id);
  return context == contexts_.end() || context->second.closing || stopping_ ? nullptr : context->second.cef;
}
int SuiteCutContextControls::ContextId(int page_id) const { const auto page = pages_.find(page_id); return page == pages_.end() ? 0 : page->second.context; }
double SuiteCutContextControls::PageCreationDeadline(int page)const{
  const auto found=pages_.find(page);return found==pages_.end()?0:found->second.creation_deadline;
}
bool SuiteCutContextControls::PageClosePending(int page)const{
  const auto found=pages_.find(page);return found!=pages_.end()&&(found->second.closing||found->second.before_unload.Pending());
}
uint32_t SuiteCutContextControls::AllowedPermissions(int page_id, const std::string& origin) const {
  const auto page=pages_.find(page_id);
  if(stopping_||page==pages_.end()||page->second.closing)return 0;
  const auto context=contexts_.find(page->second.context);
  if(context==contexts_.end()||context->second.closing)return 0;
  const auto canonical=Origin(origin);
  // CEF reports the actual requesting origin. A globally granted HTTP(S)
  // policy must not authorize an opaque/data/invalid origin by its wildcard.
  if(canonical.empty())return 0;
  const auto exact = context->second.permissions.find(canonical), all = context->second.permissions.find("*");
  return (exact == context->second.permissions.end() ? 0 : exact->second) | (all == context->second.permissions.end() ? 0 : all->second);
}
uint32_t SuiteCutContextControls::AllowedMediaPermissions(int page_id, const std::string& origin) const {
  const auto page=pages_.find(page_id);
  if(stopping_||page==pages_.end()||page->second.closing)return 0;
  const auto context=contexts_.find(page->second.context);
  if(context==contexts_.end()||context->second.closing)return 0;
  const auto canonical=Origin(origin);if(canonical.empty())return 0;
  const auto exact = context->second.media_permissions.find(canonical), all = context->second.media_permissions.find("*");
  return (exact == context->second.media_permissions.end() ? 0 : exact->second) | (all == context->second.media_permissions.end() ? 0 : all->second);
}
bool SuiteCutContextControls::Handle(const std::string& action, Dict params, Reply reply) {
  if (!params) params = CefDictionaryValue::Create();
  if (action == "waitEvent") {
    const int owner = params->GetInt("internalOwner"); EventWait wait;
    wait.event = params->GetString("event"); wait.page = params->GetInt("pageId"); wait.context = params->GetInt("contextId");
    const double after = params->GetType("afterSequence") == VTYPE_INT ? params->GetInt("afterSequence") : params->GetDouble("afterSequence");
    if (owner < 1 || waits_.size() >= 64 || (wait.event != "popup" && wait.event != "close" && wait.event != "page" && wait.event != "contextclose") ||
        !std::isfinite(after) || after < 0 || std::floor(after) != after || after > static_cast<double>(sequence_)) {
      reply(false, nullptr, "Invalid native context event wait or capacity exceeded"); return true;
    }
    if (!journal_.empty() && after < journal_.front()->GetDouble("sequence") - 1) { reply(false, nullptr, "Native context event journal capacity exceeded before registration"); return true; }
    for (const auto& event : journal_) if (event->GetDouble("sequence") > after && Matches(wait, event)) { reply(true, event->Copy(false), ""); return true; }
    wait.reply = std::move(reply); waits_[owner] = std::move(wait); return true;
  }
  if (action == "list") {
    auto result = CefDictionaryValue::Create(); auto contexts = CefListValue::Create();
    for (const auto& [id, context] : contexts_) {if(context.internal)continue; auto item = Result(id); item->SetBool("closing", context.closing); contexts->SetDictionary(contexts->GetSize(), item); }
    if (storage_) storage_->Diagnostics(result);
    credentials_->Diagnostics(result);SnapshotDiagnostics(result);
    size_t credential_browsers=0,credential_waiters=0;credential_ready_->Diagnostics(credential_browsers,credential_waiters);result->SetInt("credentialReadyBrowsers",static_cast<int>(credential_browsers));result->SetInt("credentialReadyWaiters",static_cast<int>(credential_waiters));
    if (utilities_) utilities_->Diagnostics(result);
    result->SetList("contexts", contexts); result->SetInt("pageCount", static_cast<int>(pages_.size())); result->SetInt("pendingEventWaits", static_cast<int>(waits_.size()));
    size_t snapshot_bytes = 0, snapshot_leases = 0, snapshot_lease_bytes = 0; init_snapshot_store_->Diagnostics(snapshot_bytes, snapshot_leases, snapshot_lease_bytes);
    result->SetDouble("initSnapshotBytes", static_cast<double>(snapshot_bytes)); result->SetInt("initSnapshotLeases", static_cast<int>(snapshot_leases)); result->SetDouble("initSnapshotLeaseBytes", static_cast<double>(snapshot_lease_bytes));
    size_t binding_bytes = 0, binding_count = 0; for (const auto& [id,context] : contexts_) { binding_bytes += context.bindings.Bytes(); binding_count += context.bindings.Size(); }
    result->SetInt("contextBindings",static_cast<int>(binding_count)); result->SetDouble("contextBindingBytes",static_cast<double>(binding_bytes)); result->SetInt("pendingContextBindings",static_cast<int>(binding_batches_.size()));
    result->SetInt("pendingInitScriptRegistrations", static_cast<int>(script_batches_.size())); result->SetInt("pendingInitScriptAcks", static_cast<int>(script_acks_.size())); result->SetDouble("pendingInitScriptBytes", static_cast<double>(pending_script_bytes_));
    result->SetInt("eventJournal", static_cast<int>(journal_.size())); result->SetInt("pendingCookieOperations", static_cast<int>(cookie_cancellations_.size())); result->SetInt("nativeCookiePhysical",cookies_?static_cast<int>(cookies_->Physical()):0); reply(true, result, ""); return true;
  }
  if ((action=="create"||action=="newOwnedPage")&&params->HasKey("acceptDownloads")&&params->GetType("acceptDownloads")!=VTYPE_BOOL){reply(false,nullptr,"acceptDownloads must be a boolean");return true;}
  if (action == "newOwnedPage") {
    if (stopping_ || contexts_.size() >= 32 || pages_.size() >= 128 || next_context_ == std::numeric_limits<int>::max()) { reply(false, nullptr, "Native context/page capacity exceeded or browser closing"); return true; }
    suitecut::ContextSettings native_settings;std::string settings_error;
    if(!suitecut::ContextSettings::Parse(params,native_settings,settings_error)){reply(false,nullptr,settings_error);return true;}
    CefRequestContextSettings settings; settings.persist_session_cookies = false;
    if(native_settings.locale)CefString(&settings.accept_language_list)=*native_settings.locale;
    CefRefPtr<SuiteCutContextRequestPolicy>request_policy=new SuiteCutContextRequestPolicy(std::make_shared<const suitecut::ContextSettings>(native_settings));
    auto cef = CefRequestContext::CreateContext(settings, request_policy);
    if (!cef) { reply(false, nullptr, "CEF request context creation failed"); return true; }
    const int id = ++next_context_; Context context; context.cef = cef;context.request_policy=request_policy; context.accept_downloads=params->HasKey("acceptDownloads")?params->GetBool("acceptDownloads"):true;
    if(params->HasKey("permissions"))context.permissions["*"]=0;
    for(const auto&name:native_settings.permissions){context.content_permissions["*"].insert(name);const auto bit=Permission(name);if(name=="clipboard-write")context.clipboard_write_origins.insert("*");else context.permissions["*"]|=bit;if(name=="camera")context.media_permissions["*"]|=CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;if(name=="microphone")context.media_permissions["*"]|=CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE;}
    contexts_.emplace(id, std::move(context));
    std::string seed_error;if(hooks_.context_created && !hooks_.context_created(id,params,seed_error)){if(hooks_.context_removed)hooks_.context_removed(id);contexts_.erase(id);reply(false,nullptr,seed_error);return true;}
    credentials_->ContextCreated(id);
    if (!RegisterInitContext(id)) { credentials_->ContextClosed(id);if(hooks_.context_removed)hooks_.context_removed(id);contexts_.erase(id); reply(false, nullptr, "Native init snapshot registration failed"); return true; }
    const int page_id = ReservePage(id, 0, 0);
    if (!page_id) { init_snapshot_store_->Drop(id);snapshot_publications_.Drop(id);credentials_->ContextClosed(id);if(hooks_.context_removed)hooks_.context_removed(id);contexts_.erase(id); reply(false, nullptr, "Native page capacity exceeded"); return true; }
    contexts_.at(id).owned_page = page_id; pages_.at(page_id).created_reply = std::move(reply);
    pages_.at(page_id).creation_deadline=hooks_.operation_deadline?hooks_.operation_deadline(params->GetInt("internalOwner")):0;
    if (!hooks_.create_page || !hooks_.create_page(page_id, id, cef)) PageCreationFailed(page_id, "CEF browser creation failed");
    return true;
  }
  if (action == "create") {
    if (stopping_ || contexts_.size() >= 32 || next_context_ == std::numeric_limits<int>::max()) { reply(false, nullptr, "Native context capacity exceeded or browser closing"); return true; }
    // Empty cache_path selects an independent off-the-record storage partition.
    suitecut::ContextSettings native_settings;std::string settings_error;
    if(!suitecut::ContextSettings::Parse(params,native_settings,settings_error)){reply(false,nullptr,settings_error);return true;}
    CefRequestContextSettings settings; settings.persist_session_cookies = false;
    if(native_settings.locale)CefString(&settings.accept_language_list)=*native_settings.locale;
    CefRefPtr<SuiteCutContextRequestPolicy>request_policy=new SuiteCutContextRequestPolicy(std::make_shared<const suitecut::ContextSettings>(native_settings));
    auto cef = CefRequestContext::CreateContext(settings, request_policy);
    if (!cef) { reply(false, nullptr, "CEF request context creation failed"); return true; }
    const int id = ++next_context_; Context context; context.cef = cef;context.request_policy=request_policy; context.accept_downloads=params->HasKey("acceptDownloads")?params->GetBool("acceptDownloads"):true;
    if(params->HasKey("permissions"))context.permissions["*"]=0;
    for(const auto&name:native_settings.permissions){context.content_permissions["*"].insert(name);const auto bit=Permission(name);if(name=="clipboard-write")context.clipboard_write_origins.insert("*");else context.permissions["*"]|=bit;if(name=="camera")context.media_permissions["*"]|=CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;if(name=="microphone")context.media_permissions["*"]|=CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE;}
    contexts_.emplace(id, std::move(context));
    std::string seed_error;if(hooks_.context_created && !hooks_.context_created(id,params,seed_error)){if(hooks_.context_removed)hooks_.context_removed(id);contexts_.erase(id);reply(false,nullptr,seed_error);return true;}
    credentials_->ContextCreated(id);
    if (!RegisterInitContext(id)) { credentials_->ContextClosed(id);if(hooks_.context_removed)hooks_.context_removed(id);contexts_.erase(id); reply(false, nullptr, "Native init snapshot registration failed"); return true; }
    reply(true, Result(id), ""); return true;
  }
  if (action == "closePage" || action == "close") {
    if (params->HasKey("reason") && (params->GetType("reason") != VTYPE_STRING || params->GetString("reason").ToString().size() > 16384 || params->GetString("reason").ToString().find('\0') != std::string::npos)) {
      reply(false, nullptr, "Invalid native closure reason"); return true;
    }
  }
  if (action == "closePage") {
    if(params->HasKey("runBeforeUnload")&&params->GetType("runBeforeUnload")!=VTYPE_BOOL){reply(false,nullptr,"runBeforeUnload must be boolean");return true;}
    const auto page = pages_.find(params->GetInt("pageId"));
    if (page == pages_.end()) { reply(true, nullptr, ""); return true; }
    if(params->GetBool("runBeforeUnload")){
      const int context=page->second.context;
      if(!page->second.closing&&!stopping_&&!contexts_.at(context).closing){
        const double deadline=hooks_.operation_deadline?hooks_.operation_deadline(params->GetInt("internalOwner")):0;
        const double now=hooks_.operation_now?hooks_.operation_now():0;
        const auto pending=page->second.before_unload.Pending();
        if(!page->second.browser||!page->second.before_unload.Begin(params->GetString("reason").ToString(),deadline,now)){
          reply(false,nullptr,"Native beforeunload close is unavailable or its original deadline expired");return true;
        }
        if(!pending)page->second.browser->GetHost()->CloseBrowser(false);
      }
      // Dispatch acknowledgement is not a page-closed receipt. Do not mutate
      // the context/page or finalize resources before genuine DoClose approval.
      reply(true,Result(context,params->GetInt("pageId")),"");return true;
    }
    if (page->second.close_replies.size() >= 64) { reply(false, nullptr, "Too many pending page closure requests"); return true; }
    page->second.close_replies.push_back(std::move(reply));
    if(hooks_.operation_deadline&&!page->second.close_deadline)page->second.close_deadline=hooks_.operation_deadline(params->GetInt("internalOwner"));
    BeginPageClose(params->GetInt("pageId"), params->GetString("reason").ToString());
    return true;
  }
  const int id = params->GetInt("contextId"); const auto found = contexts_.find(id);
  if (found == contexts_.end()) {
    if (action == "close") reply(true, nullptr, ""); else reply(false, nullptr, "Native browser context is closed or unknown");
    return true;
  }
  auto& context = found->second;
  if (action == "close") {
    if (context.close_replies.size() >= 64) { reply(false, nullptr, "Too many pending context closure requests"); return true; }
    std::vector<int> binding_owners; for (const auto& [owner,batch] : binding_batches_) if (batch.context == id) binding_owners.push_back(owner);
    for (const int owner : binding_owners) CancelContextBinding(owner,"Native browser context closed",true);
    if (!context.closing) { context.closing = true; context.close_reason = params->GetString("reason").ToString(); }
    context.close_replies.push_back(std::move(reply));
    if(hooks_.operation_deadline&&!context.close_deadline)context.close_deadline=hooks_.operation_deadline(params->GetInt("internalOwner"));
    if(storage_)storage_->ContextClosed(id);
    if(cookies_)cookies_->Closed(id);
    if(hooks_.context_closing)hooks_.context_closing(id,contexts_.at(id).close_deadline);
    std::vector<int> pages; for (const auto& [page_id, page] : pages_) if (page.context == id) pages.push_back(page_id);
    for (const int page_id : pages) BeginPageClose(page_id, context.close_reason);
    FinishContext(id); return true;
  }
  if (context.closing || stopping_) { reply(false, nullptr, "Native browser context is closing"); return true; }
  if(action=="addInitScript"||action=="exposeBinding"||action=="removeBinding"||action=="credentialsCreate"||action=="credentialsDelete"||action=="credentialsInstall")reply=SnapshotReply(params->GetInt("internalOwner"),id,std::move(reply));
  if(action=="credentialsCreate"||action=="credentialsGet"||action=="credentialsDelete"||action=="credentialsInstall"){const std::string method=action=="credentialsCreate"?"create":action=="credentialsGet"?"get":action=="credentialsDelete"?"delete":"install";credentials_->Handle(params->GetInt("internalOwner"),id,method,params,std::move(reply));return true;}
  if (action == "storageState" || action == "setStorageState" || action == "storageUploadBegin" || action == "storageUploadAppend" || action == "storageUploadDiscard") {
    if (!storage_) { reply(false, nullptr, "Native storage utility service is unavailable"); return true; }
    storage_->Handle(params->GetInt("internalOwner"), action, params, std::move(reply)); return true;
  }
  if (action == "exposeBinding") { RegisterContextBinding(id, params->GetInt("internalOwner"), params, std::move(reply)); return true; }
  if (action == "removeBinding") {
    std::string name;
    if (!suitecut::InboundString(params,"name",name,256) || params->GetType("entryId") != VTYPE_INT) { reply(false,nullptr,"Invalid native context binding disposal"); return true; }
    const auto entry = context.bindings.Find(name);
    if (!entry || entry->id != params->GetInt("entryId")) { reply(true,Result(id),""); return true; }
    const int entry_id = entry->id; const auto bridge = entry->bridge;
    std::vector<int> pending; for (const auto& [owner,batch] : binding_batches_) if (batch.context == id && batch.entry == entry_id) pending.push_back(owner);
    for (const int owner : pending) CancelContextBinding(owner,"Native context binding was disposed",true);
    context.bindings.Remove(name,entry_id); if(!RefreshInitSnapshot(id,params->GetInt("internalOwner"))){reply(false,nullptr,"Native binding removal snapshot publication failed");return true;}
    for (const auto& [page_id,page] : pages_) if (page.context == id && hooks_.remove_binding) hooks_.remove_binding(page_id,0,name,bridge);
    reply(true,Result(id),""); return true;
  }
  if (action == "addInitScript") {
    if (params->GetType("source") != VTYPE_STRING) { reply(false, nullptr, "Native init script source must be a string"); return true; }
    RegisterInitScript(id, params->GetInt("internalOwner"), params->GetString("source").ToString(), std::move(reply)); return true;
  }
  if (action == "pages") {
    auto result = Result(id); auto pages = CefListValue::Create();
    for (const auto& [page_id, page] : pages_) if (page.context == id && page.browser) { auto item = PageResult(id, page_id); if (page.opener) item->SetInt("openerId", page.opener); pages->SetDictionary(pages->GetSize(), item); }
    result->SetList("pages", pages); reply(true, result, ""); return true;
  }
  if (action == "newPage") {
    if(context.internal){reply(false,nullptr,"Internal native HTTP profiles cannot create public pages");return true;}
    if (params->HasKey("owned") && params->GetType("owned") != VTYPE_BOOL) { reply(false, nullptr, "Invalid native page ownership option"); return true; }
    if (params->GetBool("owned")) {
      for (const auto& [page_id, page] : pages_) if (page.context == id) { reply(false, nullptr, "Native owned page requires an unused context"); return true; }
    }
    const int page_id = ReservePage(id, 0, 0);
    if (!page_id) { reply(false, nullptr, "Native page capacity exceeded"); return true; }
    if (params->GetBool("owned")) context.owned_page = page_id;
    pages_.at(page_id).created_reply = std::move(reply);
    pages_.at(page_id).creation_deadline=hooks_.operation_deadline?hooks_.operation_deadline(params->GetInt("internalOwner")):0;
    if (!hooks_.create_page || !hooks_.create_page(page_id, id, context.cef)) PageCreationFailed(page_id, "CEF browser creation failed");
    return true;
  }
  if (action == "grantPermissions") {
    std::string origin = "*";
    if (params->HasKey("origin")) { origin = Origin(params->GetString("origin").ToString()); if (origin.empty()) { reply(false, nullptr, "Permission origin must be HTTP(S)"); return true; } }
    const auto list = params->GetList("permissions");
    if (!list || list->GetSize() > 32) { reply(false, nullptr, "Permissions must be an array of at most 32 names"); return true; }
    uint32_t permissions = 0, media = 0; bool clipboard_write = false;
    for (size_t index = 0; index < list->GetSize(); ++index) {
      const auto name = list->GetString(index).ToString(); const auto bit = Permission(name);
      if (const auto invalid=suitecut::NativePermissionError(name);!invalid.empty()) { reply(false, nullptr, invalid); return true; }
      if (name == "clipboard-write") clipboard_write = true; else permissions |= bit;
      if (name == "camera") media |= CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;
      if (name == "microphone") media |= CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE;
    }
    if (!context.permissions.contains(origin) && context.permissions.size() >= 256) { reply(false, nullptr, "Native permission origin capacity exceeded"); return true; }
    context.permissions[origin] |= permissions; context.media_permissions[origin] |= media;
    for(size_t index=0;index<list->GetSize();++index)context.content_permissions[origin].insert(list->GetString(index).ToString());
    if (clipboard_write) context.clipboard_write_origins.insert(origin);
    if (origin != "*") ApplySettings(context, origin);
    else for (const auto& seen : context.origins) ApplySettings(context, seen);
    reply(true, nullptr, ""); return true;
  }
  if (action == "clearPermissions") {
    for (const auto& [origin, settings] : context.applied_settings)
      for (const auto& [type,value] : settings) context.cef->SetContentSetting(origin, type==CEF_CONTENT_SETTING_TYPE_STORAGE_ACCESS?CefString(origin):CefString(), type, CEF_CONTENT_SETTING_VALUE_DEFAULT);
    for(auto&[scope,value]:context.storage_permission_scopes.entries())if(value!=CEF_CONTENT_SETTING_VALUE_DEFAULT){context.cef->SetContentSetting(scope.first,scope.second,CEF_CONTENT_SETTING_TYPE_STORAGE_ACCESS,CEF_CONTENT_SETTING_VALUE_DEFAULT);value=CEF_CONTENT_SETTING_VALUE_DEFAULT;}
    context.applied_settings.clear(); context.content_permissions.clear(); context.permissions.clear(); context.media_permissions.clear(); context.clipboard_write_origins.clear();
    reply(true, nullptr, ""); return true;
  }
  if(context.internal&&(action=="cookies"||action=="addCookies"||action=="clearCookies")){
    const int owner=params->GetInt("internalOwner");if(hooks_.internal_cookies&&hooks_.internal_cookies(owner,id,action,params,hooks_.cookie_deadline?hooks_.cookie_deadline(owner):0,reply))return true;
    reply(false,nullptr,"Native internal API cookie owner is unavailable");return true;
  }
  if(cookies_&&(action=="cookies"||action=="addCookies"||action=="clearCookies")){
    if(action=="addCookies"){
      const auto values=params->GetList("cookies");if(!values||values->GetSize()>(params->GetBool("storageInternal")?4096u:1024u)){reply(false,nullptr,"Cookies exceed native entry capacity");return true;}
      for(size_t i=0;i<values->GetSize();++i){CookieInput input;std::string error;if(!ParseCookie(values->GetDictionary(i),input,error)){reply(false,nullptr,error);return true;}}
    }else{
      if(params->HasKey("urls")){const auto values=params->GetList("urls");if(!values||values->GetSize()>128){reply(false,nullptr,"Cookie URL filters exceed native capacity");return true;}for(size_t i=0;i<values->GetSize();++i)if(values->GetType(i)!=VTYPE_STRING||Origin(values->GetString(i)).empty()){reply(false,nullptr,"Cookie filters require HTTP(S) URLs");return true;}}
      bool regex=false;std::string error;if(!suitecut::ValidateCookieFilterParameters(params,action,regex,error)){reply(false,nullptr,error);return true;}
    }
    const int owner=params->GetInt("internalOwner");cookies_->Handle(owner,id,action,params,hooks_.cookie_deadline?hooks_.cookie_deadline(owner):0,std::move(reply));return true;
  }
  auto manager = context.cef->GetCookieManager(nullptr);
  if (!manager) { reply(false, nullptr, "CEF cookie manager is unavailable"); return true; }
  if (action == "cookies" || action == "clearCookies") {
    std::vector<std::string> urls;
    if (params->HasKey("urls")) {
      const auto input = params->GetList("urls");
      if (!input || input->GetSize() > 128) { reply(false, nullptr, "Cookie URL filters exceed native capacity"); return true; }
      for (size_t index = 0; index < input->GetSize(); ++index) {
        const auto url = input->GetString(index).ToString(); if (Origin(url).empty()) { reply(false, nullptr, "Cookie filters require HTTP(S) URLs"); return true; } urls.push_back(url);
      }
    }
    auto filter = params->GetDictionary("filter");
    if (filter) for (const auto& field : {"name", "domain", "path"}) if (filter->HasKey(field) && filter->GetType(field) != VTYPE_STRING) { reply(false, nullptr, "Cookie clearing filters must be strings"); return true; }
    auto canceled = std::make_shared<bool>(false);
    cookie_cancellations_[params->GetInt("internalOwner")] = [canceled] { *canceled = true; };
    CefRefPtr<CookieVisitor> visitor = new CookieVisitor(std::move(urls), filter ? filter->Copy(false) : nullptr, action == "clearCookies", std::move(reply), canceled);
    if (!manager->VisitAllCookies(visitor)) visitor->Reject();
    return true;
  }
  if (action == "addCookies") {
    const auto input = params->GetList("cookies");
    if (!input || input->GetSize() > (params->GetBool("storageInternal") ? 4096u : 1024u)) { reply(false, nullptr, "Cookies exceed native entry capacity"); return true; }
    auto writer = std::make_shared<CookieWriter>(); writer->manager = manager; writer->reply = std::move(reply);
    for (size_t index = 0; index < input->GetSize(); ++index) {
      CookieInput item; std::string error;
      if (!ParseCookie(input->GetDictionary(index), item, error) || (input->GetDictionary(index)->HasKey("partitionKey") && (error="Partitioned cookies require the native context cookie channel",true))) { auto done = std::move(writer->reply); done(false, nullptr, error); return true; }
      writer->cookies.push_back(std::move(item));
    }
    cookie_cancellations_[params->GetInt("internalOwner")] = [weak = std::weak_ptr<CookieWriter>(writer)] { if (const auto work = weak.lock()) work->Cancel(); };
    writer->Next(); return true;
  }
  reply(false, nullptr, "Unknown native context action: " + action); return true;
}

void SuiteCutContextControls::CancelWait(int owner) {
  snapshot_waits_.erase(owner);snapshot_targets_.erase(owner);
  for(const auto&[id,flight]:snapshot_flights_)if(flight.owner==owner)snapshot_receipts_.Cancel(id,SnapshotNow());
  SnapshotWake();
  credentials_->Cancel(owner);
  if(cookies_)cookies_->Cancel(owner);
  if(hooks_.cancel_cookie)hooks_.cancel_cookie(owner);
  waits_.erase(owner); CancelInitScript(owner); CancelContextBinding(owner);
  if (storage_) storage_->Cancel(owner);
  const auto cancel = cookie_cancellations_.find(owner);
  if (cancel != cookie_cancellations_.end()) { auto callback = std::move(cancel->second); cookie_cancellations_.erase(cancel); callback(); }
}
bool SuiteCutContextControls::Matches(const EventWait& wait, Dict event) {
  const auto name = event->GetString("event").ToString();
  if (wait.event == "popup") return name == "page-created" && event->GetInt("openerId") == wait.page && wait.page > 0;
  if (wait.event == "close") return name == "page-closed" && event->GetInt("pageId") == wait.page && wait.page > 0;
  if (wait.event == "page") return name == "page-created" && event->GetInt("contextId") == wait.context && wait.context > 0;
  return name == "context-closed" && event->GetInt("contextId") == wait.context && wait.context > 0;
}

bool SuiteCutContextControls::FailPermissionScopes(int id,const std::string&reason){
  const auto found=contexts_.find(id);if(found==contexts_.end())return false;
  if(!found->second.closing){
    found->second.closing=true;found->second.close_reason=reason;found->second.origins_overflow=true;
    const auto weak=weak_from_this();
    // Return cancellation to CEF before physically closing its browser clients.
    // During shutdown a rejected post leaves the context marked closing; normal
    // engine teardown still owns every browser and request-context reference.
    CefPostTask(TID_UI,new ContextPolicyCloseTask([weak,id]{if(const auto self=weak.lock()){auto params=CefDictionaryValue::Create();params->SetInt("contextId",id);self->Handle("close",params,[](bool,Dict,const std::string&){});}}));
  }
  return false;
}
bool SuiteCutContextControls::PageOriginObserved(int page,const std::string&url){
  const int id=ContextId(page);const auto context=contexts_.find(id);if(context==contexts_.end()||context->second.closing)return false;
  const auto origin=Origin(url);if(origin.empty())return true;
  if(!context->second.origins.contains(origin)&&context->second.origins.size()>=1024)return FailPermissionScopes(id,"Native context permission origin capacity exceeded");
  const auto entry=pages_.find(page);
  const auto top=entry!=pages_.end()&&entry->second.browser?Origin(entry->second.browser->GetMainFrame()->GetURL().ToString()):std::string{};
  if(!context->second.storage_permission_scopes.Observe(origin,top))return FailPermissionScopes(id,"Native context permission origin-pair capacity exceeded");
  context->second.origins.insert(origin);ApplySettings(context->second,origin);return true;
}
void SuiteCutContextControls::ApplySettings(Context& context, const std::string& origin) {
  const auto exact = context.permissions.find(origin), all = context.permissions.find("*");
  if(exact==context.permissions.end()&&all==context.permissions.end())return;
  std::set<std::string> names;
  const auto global=context.content_permissions.find("*"),local=context.content_permissions.find(origin);
  if(global!=context.content_permissions.end())names.insert(global->second.begin(),global->second.end());
  if(local!=context.content_permissions.end())names.insert(local->second.begin(),local->second.end());
  auto&applied=context.applied_settings[origin];
  for(const auto&[type,value]:suitecut::NativePermissionSettings(names)){
    if(type==CEF_CONTENT_SETTING_TYPE_STORAGE_ACCESS){
      for(auto&[scope,current]:context.storage_permission_scopes.entries())if(scope.first==origin&&current!=value){context.cef->SetContentSetting(scope.first,scope.second,type,value);current=value;}
      continue;
    }
    const auto previous=applied.find(type);if(previous!=applied.end()&&previous->second==value)continue;
    context.cef->SetContentSetting(origin,CefString(),type,value);applied[type]=value;
  }
}

#include "context_init_scripts.inc"

#include "context_bindings.inc"
#include "context_snapshot_publication.inc"

std::string SuiteCutContextControls::RequestPolicyError(int context)const{
  const auto found=contexts_.find(context);if(found==contexts_.end())return "Native request context is closed";
  return found->second.request_policy?found->second.request_policy->Error():"";
}

bool SuiteCutContextControls::AcceptedWorkReady()const{
  if(stopping_||!waits_.empty()||!cookie_cancellations_.empty()||
     (cookies_&&cookies_->Physical())||(storage_&&!storage_->AcceptedWorkReady())||
     (credentials_&&!credentials_->AcceptedWorkReady())||!credential_ready_->AcceptedWorkReady()||
     !script_batches_.empty()||!script_acks_.empty()||!binding_batches_.empty()||
     !snapshot_waits_.empty()||!snapshot_targets_.empty()||!snapshot_closing_.empty()||
     snapshot_receipts_.AcceptedWork().pending_receipts)return false;
  for(const auto&[id,context]:contexts_){(void)id;if(context.closing||!context.close_replies.empty())return false;}
  for(const auto&[id,page]:pages_){(void)id;if(!page.browser||page.closing||page.before_unload.Pending()||page.created_reply||!page.close_replies.empty())return false;}
  for(const auto&[id,flight]:snapshot_flights_){
    (void)id;const auto peer=snapshot_peers_.find(flight.page);
    if(!flight.acknowledged||!flight.installed||flight.released||peer==snapshot_peers_.end()||
       !peer->second.ready||peer->second.pin!=flight.pin.owner||peer->second.installed!=flight.pin.version||
       snapshot_publications_.Current(static_cast<int>(flight.pin.owner.context)).version!=flight.pin.version)return false;
  }
  return true; // All immutable resident mappings/definitions stay owned.
}
