// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "include/cef_resource_handler.h"
namespace suitecut {
inline constexpr const char* kCredentialReadyURL="suitecut-init://registry/credentials-ready";
// Source publishes real browser-owned device readiness on UI; the private
// scheme consumes it on IO. Neither URL nor renderer-supplied IDs select state.
class CredentialReadiness:public std::enable_shared_from_this<CredentialReadiness>{
 public:
  bool AcceptedWorkReady()const{std::lock_guard<std::mutex> lock(mutex_);return pending_.empty()&&!callback_stacks_->load();}
  struct Lease { std::atomic<int> status{0}; uint64_t token=0; std::weak_ptr<CredentialReadiness> store; void Cancel(){if(token)if(const auto owner=store.lock())owner->Cancel(token);}~Lease(){Cancel();} };
  bool Register(int browser,CefRefPtr<CefRequestContext> actual){
    std::lock_guard<std::mutex> lock(mutex_);if(browser<=0||!actual||(!browsers_.contains(browser)&&browsers_.size()>=128))return false;
    const auto found=browsers_.find(browser);if(found!=browsers_.end()){if(!found->second.context->IsSame(actual))return false;found->second.status=0;return true;}
    browsers_.emplace(browser,Browser{actual,0});return true;
  }
  std::shared_ptr<Lease> Begin(int browser,CefRefPtr<CefRequestContext> actual,CefRefPtr<CefCallback> callback){
    std::lock_guard<std::mutex> lock(mutex_);const auto found=browsers_.find(browser);
    if(found==browsers_.end()||!actual||!found->second.context->IsSame(actual))return nullptr;
    auto lease=std::make_shared<Lease>();lease->store=weak_from_this();lease->status=found->second.status;
    if(found->second.status)return lease;
    if(!callback||pending_.size()>=64||next_==std::numeric_limits<uint64_t>::max()){lease->status=503;return lease;}
    lease->token=++next_;pending_.emplace(lease->token,Pending{browser,lease,callback});return lease;
  }
  bool RegisterBootstrap(int browser,CefRefPtr<CefRequestContext>actual){
    std::lock_guard<std::mutex>lock(mutex_);if(browser<=0||!actual||(!browsers_.contains(browser)&&browsers_.size()>=128))return false;
    const auto found=browsers_.find(browser);if(found!=browsers_.end()){if(!found->second.context->IsSame(actual))return false;found->second.bootstrap=true;return true;}
    browsers_.emplace(browser,Browser{actual,0,true});return true;
  }
  bool Bootstrap(int browser,bool pending){std::lock_guard<std::mutex>lock(mutex_);const auto found=browsers_.find(browser);if(found==browsers_.end())return false;found->second.bootstrap=pending;return true;}
  int Bootstrap(int browser,CefRefPtr<CefRequestContext>actual)const{std::lock_guard<std::mutex>lock(mutex_);const auto found=browsers_.find(browser);return found==browsers_.end()||!actual||!found->second.context->IsSame(actual)?-1:(found->second.bootstrap?1:0);}
  void Ready(int browser,bool success){
    struct ActualCallbackFrame {std::shared_ptr<std::atomic_size_t> count;~ActualCallbackFrame(){count->fetch_sub(1);}};
    callback_stacks_->fetch_add(1);const ActualCallbackFrame actual_callbacks{callback_stacks_};
    std::vector<Pending> callbacks;{
      std::lock_guard<std::mutex> lock(mutex_);const auto found=browsers_.find(browser);if(found==browsers_.end())return;
      found->second.status=success?200:503;
      for(auto it=pending_.begin();it!=pending_.end();){if(it->second.browser==browser){callbacks.push_back(std::move(it->second));it=pending_.erase(it);}else ++it;}
    }
    for(const auto& pending:callbacks){if(const auto lease=pending.lease.lock())lease->status=success?200:503;pending.callback->Continue();}
  }
  void Drop(int browser){Ready(browser,false);std::lock_guard<std::mutex>lock(mutex_);browsers_.erase(browser);}
  void Clear(){std::vector<int>ids;{std::lock_guard<std::mutex>lock(mutex_);for(const auto&[id,value]:browsers_)ids.push_back(id);}for(const int id:ids)Drop(id);}
  void Diagnostics(size_t&browsers,size_t&pending)const{std::lock_guard<std::mutex>lock(mutex_);browsers=browsers_.size();pending=pending_.size();}
 private:
  void Cancel(uint64_t token){Pending retired;{std::lock_guard<std::mutex>lock(mutex_);const auto found=pending_.find(token);if(found!=pending_.end()){retired=std::move(found->second);pending_.erase(found);}}}
  struct Browser{CefRefPtr<CefRequestContext>context;int status=0;bool bootstrap=false;};
  struct Pending{int browser=0;std::weak_ptr<Lease>lease;CefRefPtr<CefCallback>callback;};
  std::shared_ptr<std::atomic_size_t> callback_stacks_=std::make_shared<std::atomic_size_t>(0);
  mutable std::mutex mutex_;std::map<int,Browser>browsers_;std::map<uint64_t,Pending>pending_;uint64_t next_=0;
};
class CredentialReadyHandler:public CefResourceHandler{
 public:
  CredentialReadyHandler(std::shared_ptr<CredentialReadiness>store,int browser,CefRefPtr<CefRequestContext>context):store_(std::move(store)),browser_(browser),context_(std::move(context)){}
  bool Open(CefRefPtr<CefRequest>,bool&handled,CefRefPtr<CefCallback>callback)override{lease_=store_->Begin(browser_,context_,callback);handled=!lease_||lease_->status.load()!=0;return true;}
  void GetResponseHeaders(CefRefPtr<CefResponse>response,int64_t&length,CefString&)override{response->SetStatus(lease_?lease_->status.load():403);response->SetMimeType("application/json");CefResponse::HeaderMap headers;headers.emplace("Cache-Control","no-store");headers.emplace("Access-Control-Allow-Origin","*");response->SetHeaderMap(headers);length=2;}
  bool Read(void*data,int capacity,int&read,CefRefPtr<CefResourceReadCallback>)override{read=std::min(std::max(capacity,0),2-offset_);if(!read)return false;std::memcpy(data,&"{}"[offset_],read);offset_+=read;return true;}
  void Cancel()override{if(lease_)lease_->Cancel();lease_.reset();}
 private:
  std::shared_ptr<CredentialReadiness>store_;int browser_;CefRefPtr<CefRequestContext>context_;std::shared_ptr<CredentialReadiness::Lease>lease_;int offset_=0;
  ~CredentialReadyHandler()override{Cancel();}
  IMPLEMENT_REFCOUNTING(CredentialReadyHandler);
};
}

namespace suitecut {
// Immediate identity-bound browser state. This never waits for renderer/CDP
// work, so a synchronous renderer query cannot block settings application.
class CredentialBootstrapHandler:public CefResourceHandler{
 public:
  CredentialBootstrapHandler(std::shared_ptr<CredentialReadiness>store,int browser,CefRefPtr<CefRequestContext>context):status_(store->Bootstrap(browser,context)){}
  bool Open(CefRefPtr<CefRequest>,bool&handled,CefRefPtr<CefCallback>)override{handled=true;return true;}
  void GetResponseHeaders(CefRefPtr<CefResponse>response,int64_t&length,CefString&)override{response->SetStatus(status_<0?403:200);response->SetMimeType("application/json");CefResponse::HeaderMap headers;headers.emplace("Cache-Control","no-store");headers.emplace("Access-Control-Allow-Origin","*");response->SetHeaderMap(headers);length=status_<0?0:(status_?4:5);}
  bool Read(void*data,int capacity,int&read,CefRefPtr<CefResourceReadCallback>)override{const std::string text=status_<0?"":(status_?"true":"false");read=static_cast<int>(std::min<size_t>(std::max(capacity,0),text.size()-offset_));if(!read)return false;std::memcpy(data,text.data()+offset_,read);offset_+=read;return true;}
  void Cancel()override{}
 private:
  int status_=-1;size_t offset_=0;IMPLEMENT_REFCOUNTING(CredentialBootstrapHandler);
};
}
