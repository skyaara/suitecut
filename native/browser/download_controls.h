// SPDX-License-Identifier: MIT
#pragma once
#include "locator_handler_async_work.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "include/cef_download_handler.h"
#include "include/cef_resource_request_handler.h"
#include "download_budget.h"
#include "download_files.h"
#include "download_navigation.h"

namespace suitecut {
class DownloadResponseBudgets;
struct DownloadResponseStream;
// One UI-thread owner for all physical CEF downloads in the engine.
class DownloadControls : public std::enable_shared_from_this<DownloadControls> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  struct Policy {int context=0;bool accepted=false;int event_page=0;};
  struct Hooks {
    std::function<Policy(int)> policy;
    std::function<void(int,Dict)> event;
    std::function<void()> wake;
    std::function<double()> now;
    std::function<void(int)> settled_context;
    std::function<void(int)> close_provisional_page;
    std::function<void(int)> navigation_response;
    std::string directory;
  };
  explicit DownloadControls(Hooks hooks);
  ~DownloadControls();
  bool CanDownload(int page)const;
  bool Before(int page,CefRefPtr<CefDownloadItem> item,const std::string& suggested,CefRefPtr<CefBeforeDownloadCallback> callback);
  void Updated(int page,CefRefPtr<CefDownloadItem> item,CefRefPtr<CefDownloadItemCallback> callback);
  // Called on CEF IO thread; only touches immutable or locked quota state.
  CefRefPtr<CefResourceRequestHandler> ResourceHandler(int page,CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> request,bool navigation,bool download);
  std::uint64_t NavigationSequence()const{return navigation_requests_->Sequence();}
  DownloadNavigationRequests::Match NavigationClassification(int page,bool main,const std::string& url,std::uint64_t after)const{return navigation_requests_->Lookup(page,main,url,after);}
  void Handle(int owner,Dict params,double deadline,Reply reply);
  // Native SDK aliases retain actual context ownership; they never expose an
  // arbitrary process-wide download ID to another context.
  bool SDKOwns(const std::string& id,int page,int context)const {
    const auto found=records_.find(id);
    return context>0&&found!=records_.end()&&found->second->context==context&&
      found->second->event_page==page&&!found->second->context_closed;
  }
  CefRefPtr<CefListValue> SDKList(int page,int context)const {
    auto result=CefListValue::Create();
    for(const auto&[id,record]:records_) {
      (void)id;
      if(record->context==context&&!record->context_closed&&record->event_page==page)
        result->SetDictionary(result->GetSize(),Info(*record));
    }
    return result;
  }
  bool SDKSetDirectory(int context,const std::string& path,std::string& canonical,std::string& error) {
    if(context<=0||closing_contexts_.contains(context)||clearing_||path.empty()||path.size()>4096||
        path.find('\0')!=std::string::npos||!std::filesystem::path(path).is_absolute()) {
      error="Native download directory requires an open context and absolute path";return false;
    }
    if(!directories_.contains(context)&&directories_.size()>=256) {error="Native download directory context capacity exceeded";return false;}
    try {
      std::filesystem::create_directories(path);
      canonical=std::filesystem::canonical(path).string();
      if(!std::filesystem::is_directory(canonical)) {error="Native download destination is not a directory";return false;}
      directories_[context]=canonical;return true;
    } catch(...) {error="Native download directory cannot be configured";return false;}
  }
  void Cancel(int owner);
  void BeforePageClose(int page,std::function<void()> continuation);
  void ContextClosing(int context);
  void PageGone(int page);
  void ContextGone(int context);
  bool ContextCleanupReady(int context)const;
  void Tick();
  bool NeedsTick()const;
  void Diagnostics(Dict result)const;
  void Clear();
 private:
  struct Record {
    int page=0,event_page=0,context=0;std::uint32_t native_id=0;
    std::string id,url,original_url,suggested,mime,failure;
    bool unavailable_url=false,unavailable_original=false,unavailable_suggested=false,unavailable_mime=false;
    std::int64_t total=-1,received=0;
    bool finish_queued=false,emitted=false,accepted=false,settled=false,physical_settled=false,complete=false,cancel_requested=false,deleted=false,context_closed=false,browser_gone=false;
    std::shared_ptr<DownloadFiles> files;
    std::shared_ptr<DownloadQuotaLease> quota;
    CefRefPtr<CefDownloadItemCallback> callback;
    std::map<uint64_t,std::shared_ptr<NativeLocatorHandlerAsyncWork>> handler_cancellations;
  };
  struct Wait {int owner=0;std::string id,action,path;double deadline=0;Reply reply;NativeWorkContinuation origin;};
  struct Copy {
    NativeLocatorHandlerAsyncWork handler_receipt;
    int owner=0;std::string id;double deadline=0;Reply reply;
    std::atomic<bool> canceled{false};
    std::shared_ptr<DownloadFiles> files;
    std::shared_ptr<DownloadQuotaLease> quota;
    std::thread thread;
    DownloadCopyResult result;
    ~Copy(){if(thread.joinable())thread.join();}
  };
  std::shared_ptr<Record> Ensure(int page,CefRefPtr<CefDownloadItem> item);
  void Snapshot(Record& record,CefRefPtr<CefDownloadItem> item);
  Dict Info(const Record& record,const char* event="state")const;
  void RequestCancel(Record& record,const std::string& reason);
  void Finish(std::shared_ptr<Record> record);
  void QueueFinish(std::shared_ptr<Record> record);
  void CompleteWait(int owner);
  void StartCopy(Wait wait,std::shared_ptr<Record> record);
  void CopyFinished(int owner);
  void Cleanup(std::shared_ptr<Record> record);
  void CheckClosures();
  bool PageSettled(int page)const;
  std::filesystem::path DirectoryForContext(int context)const {
    const auto found=directories_.find(context);
    return found!=directories_.end()?std::filesystem::path(found->second):
      hooks_.directory.empty()?std::filesystem::temp_directory_path():std::filesystem::path(hooks_.directory);
  }
  Hooks hooks_;
  std::shared_ptr<DownloadQuota> quota_=std::make_shared<DownloadQuota>();
  std::shared_ptr<DownloadResponseBudgets> response_budgets_;
  std::shared_ptr<DownloadNavigationRequests> navigation_requests_=std::make_shared<DownloadNavigationRequests>();
  std::map<std::string,std::shared_ptr<Record>> records_;
  std::map<std::pair<int,std::uint32_t>,std::string> native_ids_;
  std::map<int,Wait> waits_;
  std::map<int,std::shared_ptr<Copy>> copies_;
  std::map<int,std::string> directories_;
  std::map<int,std::vector<std::function<void()>>> page_closures_;
  std::set<int> closing_pages_,closing_contexts_;
  std::string namespace_;
  std::uint64_t next_id_=0;
  bool clearing_=false;
};
} // namespace suitecut
