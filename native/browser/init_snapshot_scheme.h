// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "include/cef_resource_handler.h"
#include "include/cef_scheme.h"
#include "credential_ready.h"

inline constexpr const char* kSuiteCutInitScheme = "suitecut-init";
inline constexpr const char* kSuiteCutInitSnapshotURL = "suitecut-init://registry/snapshot";
inline void RegisterSuiteCutInitScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(kSuiteCutInitScheme, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
      CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_CSP_BYPASSING | CEF_SCHEME_OPTION_FETCH_ENABLED);
}
// Browser-owned immutable snapshots, published on UI and read on IO. Factories
// bind actual CefRequestContext identity; page-supplied IDs never select data.
class SuiteCutInitSnapshotStore : public std::enable_shared_from_this<SuiteCutInitSnapshotStore> {
 public:
  struct Lease {
    std::shared_ptr<const std::string> text; std::shared_ptr<SuiteCutInitSnapshotStore> store;
    ~Lease() { if (store && text) store->Release(text->size()); }
  };
  bool Set(int id, CefRefPtr<CefRequestContext> context, std::string text) {
    return SetOwned(id,std::move(context),std::make_shared<const std::string>(std::move(text)));
  }
  bool SetOwned(int id,CefRefPtr<CefRequestContext>context,std::shared_ptr<const std::string>text){
    if(!text)return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto old = snapshots_.find(id); const size_t previous = old == snapshots_.end() ? 0 : old->second.text->size();
    if ((!snapshots_.contains(id) && snapshots_.size() >= 32) || text->size() > 8 * 1048576 || text->size() > 64 * 1048576 - bytes_ + previous) return false;
    // Allocate a new map node before changing accounting or accepted bytes.
    auto slot=old;if(slot==snapshots_.end())slot=snapshots_.emplace(id,Snapshot{}).first;
    const auto next=bytes_-previous+text->size();slot->second=Snapshot{context,std::move(text)};bytes_=next;return true;
  }
  void Drop(int id) {
    std::lock_guard<std::mutex> lock(mutex_); const auto found = snapshots_.find(id);
    if (found != snapshots_.end()) { bytes_ -= found->second.text->size(); snapshots_.erase(found); }
  }
  std::shared_ptr<Lease> Get(int id, CefRefPtr<CefRequestContext> actual) {
    std::lock_guard<std::mutex> lock(mutex_); const auto found = snapshots_.find(id);
    if (found == snapshots_.end() || !actual || !found->second.context->IsSame(actual) || leases_ >= 64 || found->second.text->size() > 64 * 1048576 - lease_bytes_) return nullptr;
    ++leases_; lease_bytes_ += found->second.text->size();
    auto lease = std::make_shared<Lease>(); lease->text = found->second.text; lease->store = shared_from_this(); return lease;
  }
  void Diagnostics(size_t& bytes, size_t& leases, size_t& lease_bytes) const {
    std::lock_guard<std::mutex> lock(mutex_); bytes = bytes_; leases = leases_; lease_bytes = lease_bytes_;
  }
 private:
  void Release(size_t bytes) { std::lock_guard<std::mutex> lock(mutex_); --leases_; lease_bytes_ -= bytes; }
  struct Snapshot { CefRefPtr<CefRequestContext> context; std::shared_ptr<const std::string> text; };
  mutable std::mutex mutex_; std::map<int, Snapshot> snapshots_;
  size_t bytes_ = 0, leases_ = 0, lease_bytes_ = 0;
};
class SuiteCutInitSnapshotHandler : public CefResourceHandler {
 public:
  explicit SuiteCutInitSnapshotHandler(std::shared_ptr<SuiteCutInitSnapshotStore::Lease> lease) : lease_(std::move(lease)) {}
  bool Open(CefRefPtr<CefRequest>, bool& handle_request, CefRefPtr<CefCallback>) override { handle_request = true; return true; }
  void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t& length, CefString&) override {
    response->SetStatus(lease_ ? 200 : 403); response->SetMimeType("application/json");
    CefResponse::HeaderMap headers; headers.emplace("Cache-Control", "no-store"); headers.emplace("Access-Control-Allow-Origin", "*");
    response->SetHeaderMap(headers); length = lease_ ? lease_->text->size() : 0;
  }
  bool Read(void* data, int capacity, int& read, CefRefPtr<CefResourceReadCallback>) override {
    read = lease_ ? static_cast<int>(std::min<size_t>(std::max(capacity, 0), lease_->text->size() - offset_)) : 0;
    if (!read) return false;
    std::memcpy(data, lease_->text->data() + offset_, read); offset_ += read; return true;
  }
  void Cancel() override { lease_.reset(); }
 private:
  std::shared_ptr<SuiteCutInitSnapshotStore::Lease> lease_; size_t offset_ = 0;
  IMPLEMENT_REFCOUNTING(SuiteCutInitSnapshotHandler);
};
// A private empty startup document. Identity is checked by the owning factory;
// no author content, external request or per-page user token is involved.
class SuiteCutSettingsDocumentHandler:public CefResourceHandler{
 public:
  bool Open(CefRefPtr<CefRequest>,bool&handled,CefRefPtr<CefCallback>)override{handled=true;return true;}
  void GetResponseHeaders(CefRefPtr<CefResponse>response,int64_t&length,CefString&)override{response->SetStatus(200);response->SetMimeType("text/html");CefResponse::HeaderMap headers;headers.emplace("Cache-Control","no-store");headers.emplace("Content-Security-Policy","default-src 'none'");response->SetHeaderMap(headers);length=sizeof(document_)-1;}
  bool Read(void*data,int capacity,int&read,CefRefPtr<CefResourceReadCallback>)override{read=static_cast<int>(std::min<size_t>(std::max(capacity,0),sizeof(document_)-1-offset_));if(!read)return false;std::memcpy(data,document_+offset_,read);offset_+=read;return true;}
  void Cancel()override{}
 private:
  inline static constexpr char document_[]="<!doctype html><html><head></head><body></body></html>";size_t offset_=0;IMPLEMENT_REFCOUNTING(SuiteCutSettingsDocumentHandler);
};
class SuiteCutInitSnapshotFactory : public CefSchemeHandlerFactory {
 public:
  SuiteCutInitSnapshotFactory(std::shared_ptr<SuiteCutInitSnapshotStore> store, int context,std::shared_ptr<suitecut::CredentialReadiness> ready=nullptr)
      : store_(std::move(store)), context_(context),ready_(std::move(ready)) {}
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString&, CefRefPtr<CefRequest> request) override {
    if(ready_&&browser&&frame&&request->GetMethod()=="GET"&&request->GetURL()=="suitecut-init://registry/credentials-ready")return new suitecut::CredentialReadyHandler(ready_,browser->GetIdentifier(),browser->GetHost()->GetRequestContext());
    if(ready_&&browser&&frame&&request->GetMethod()=="GET"&&request->GetURL()=="suitecut-init://registry/settings-bootstrap")return new suitecut::CredentialBootstrapHandler(ready_,browser->GetIdentifier(),browser->GetHost()->GetRequestContext());
    if(browser&&frame&&request->GetMethod()=="GET"&&request->GetURL()=="suitecut-init://registry/settings-document"){
      if(const auto store=store_.lock())if(store->Get(context_,browser->GetHost()->GetRequestContext()))return new SuiteCutSettingsDocumentHandler();
    }
    std::shared_ptr<SuiteCutInitSnapshotStore::Lease> lease;
    if (browser && frame && request->GetMethod() == "GET" && request->GetURL() == kSuiteCutInitSnapshotURL) {
      auto actual = browser->GetHost()->GetRequestContext();
      if (const auto store = store_.lock()) lease = store->Get(context_, actual);
    }
    return new SuiteCutInitSnapshotHandler(std::move(lease));
  }
 private:
  std::weak_ptr<SuiteCutInitSnapshotStore> store_; int context_;std::shared_ptr<suitecut::CredentialReadiness>ready_;
  IMPLEMENT_REFCOUNTING(SuiteCutInitSnapshotFactory);
};
