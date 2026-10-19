// SPDX-License-Identifier: MIT
#include "download_controls.h"
#include "accepted_task_work.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include "include/cef_response_filter.h"
#include "include/cef_task.h"

namespace suitecut {
namespace {
class DownloadTask : public CefTask {
 public:explicit DownloadTask(std::function<void()> fn):fn_(std::move(fn)){}
  void Execute() override{const suitecut::AcceptedTaskFrame native_frame(native_work_);fn_();}
 private:suitecut::AcceptedTaskReceipt native_work_;std::function<void()> fn_;IMPLEMENT_REFCOUNTING(DownloadTask);
};
std::string DownloadReason(cef_download_interrupt_reason_t reason){
  switch(reason){
    case CEF_DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:return "Download network timed out";
    case CEF_DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:return "Download network disconnected";
    case CEF_DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:return "Download destination has no free space";
    case CEF_DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:return "Download destination access denied";
    case CEF_DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE:return "Download file is too large";
    default:return "CEF download interrupted (reason "+std::to_string(static_cast<int>(reason))+")";
  }
}
}
struct DownloadResponseStream {
  int page=0;std::string url;double created=0;
  std::shared_ptr<DownloadQuotaLease> quota;
  std::atomic<bool> ended{false},exceeded{false};
};
class DownloadResponseBudgets : public std::enable_shared_from_this<DownloadResponseBudgets> {
 public:
  DownloadResponseBudgets(std::shared_ptr<DownloadQuota> quota,std::function<double()> now):quota_(std::move(quota)),now_(std::move(now)){}
  std::shared_ptr<DownloadResponseStream> Create(int page,const std::string& url){
    std::lock_guard lock(mutex_);if(streams_.size()>=512)return nullptr;
    auto stream=std::make_shared<DownloadResponseStream>();stream->page=page;stream->url=url;stream->created=now_();stream->quota=std::make_shared<DownloadQuotaLease>(quota_);streams_.push_back(stream);return stream;
  }
  void Collect(){std::lock_guard lock(mutex_);std::erase_if(streams_,[](const auto& stream){return stream->ended.load();});}
  void ClosedPage(int page){std::lock_guard lock(mutex_);std::erase_if(streams_,[page](const auto& stream){return stream->page==page;});}
  void Clear(){std::lock_guard lock(mutex_);streams_.clear();}
  bool Pending()const{std::lock_guard lock(mutex_);return !streams_.empty();}
 private:std::shared_ptr<DownloadQuota> quota_;std::function<double()> now_;mutable std::mutex mutex_;std::vector<std::shared_ptr<DownloadResponseStream>> streams_;
};
namespace {
class DownloadDeniedFilter : public CefResponseFilter {
 public:
  bool InitFilter()override{return true;}
  FilterStatus Filter(void*,size_t,size_t& input_read,void*,size_t,size_t& output_written)override{input_read=output_written=0;return RESPONSE_FILTER_ERROR;}
 private:IMPLEMENT_REFCOUNTING(DownloadDeniedFilter);
};
class DownloadResponseFilter : public CefResponseFilter {
 public:explicit DownloadResponseFilter(std::shared_ptr<DownloadResponseStream> stream):stream_(std::move(stream)){}
  ~DownloadResponseFilter()override{stream_->quota->ReduceTo(0);stream_->ended=true;}
  bool InitFilter()override{return true;}
  FilterStatus Filter(void* input,size_t input_size,size_t& input_read,void* output,size_t output_size,size_t& output_written)override{
    input_read=output_written=0;
    if(!input_size){stream_->quota->ReduceTo(0);stream_->ended=true;return RESPONSE_FILTER_DONE;}
    const auto count=std::min(input_size,output_size);
    if(!input||!output||!count||!stream_->quota->Add(count)){stream_->exceeded=true;return RESPONSE_FILTER_ERROR;}
    std::memcpy(output,input,count);input_read=output_written=count;
    return count==input_size?RESPONSE_FILTER_DONE:RESPONSE_FILTER_NEED_MORE_DATA;
  }
 private:std::shared_ptr<DownloadResponseStream> stream_;IMPLEMENT_REFCOUNTING(DownloadResponseFilter);
};
class DownloadResourceHandler : public CefResourceRequestHandler {
 public:
  DownloadResourceHandler(std::shared_ptr<DownloadResponseBudgets> budgets,std::shared_ptr<DownloadNavigationRequests> requests,std::shared_ptr<DownloadNavigationRequests::Entry> entry,std::function<void()> changed,int page,bool navigation,bool download)
      :budgets_(std::move(budgets)),requests_(std::move(requests)),entry_(std::move(entry)),changed_(std::move(changed)),page_(page),navigation_(navigation),download_(download){}
  void OnResourceRedirect(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest>,CefRefPtr<CefResponse>,CefString& url)override{
    requests_->Redirect(entry_,url.ToString());
  }
  bool OnResourceResponse(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest> request,CefRefPtr<CefResponse> response)override{
    Observe(request,response);return false;
  }
  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest> request,CefRefPtr<CefResponse> response)override{
    const bool candidate=Observe(request,response);
    if(!candidate)return nullptr;
    stream_=budgets_->Create(page_,request->GetURL());
    if(!stream_)return new DownloadDeniedFilter();
    return new DownloadResponseFilter(stream_);
  }
  void OnResourceLoadComplete(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> request,CefRefPtr<CefResponse> response,URLRequestStatus status,int64_t received)override{
    if(std::getenv("SUITECUT_NATIVE_RESOURCE_DIAGNOSTICS")){
      // Actual CEF callback identity only; never associate it to a CDP record
      // by URL, frame proximity or timing. Log neither headers nor body bytes.
      static std::atomic<unsigned> count{0};auto ordinal=count.load(std::memory_order_relaxed);
      while(ordinal<=512&&!count.compare_exchange_weak(ordinal,ordinal+1,std::memory_order_relaxed)){}
      if(ordinal<512){const auto frame_id=frame?frame->GetIdentifier().ToString():std::string{};
        std::fprintf(stderr,"native cef resource complete page=%d browser=%d id=%llu frame=%.*s status=%d response=%d error=%d received=%lld\n",page_,browser?browser->GetIdentifier():0,static_cast<unsigned long long>(request?request->GetIdentifier():0),static_cast<int>(std::min(frame_id.size(),size_t(128))),frame_id.data(),static_cast<int>(status),response?1:0,response?static_cast<int>(response->GetError()):0,static_cast<long long>(received));
      }else if(ordinal==512)std::fprintf(stderr,"native cef resource completion diagnostic overflow limit=512\n");
    }
    requests_->End(entry_);if(entry_)changed_();
    if(stream_){stream_->quota->ReduceTo(0);stream_->ended=true;}
  }
 private:
  bool Observe(CefRefPtr<CefRequest> request,CefRefPtr<CefResponse> response){
    const bool candidate=DownloadNavigationRequests::ResponseDownloads(download_,navigation_,response->GetHeaderByName("Content-Disposition"),response->GetMimeType());
    requests_->Response(entry_,request->GetURL(),candidate);if(entry_)changed_();return candidate;
  }
  std::shared_ptr<DownloadResponseBudgets> budgets_;
  std::shared_ptr<DownloadNavigationRequests> requests_;
  std::shared_ptr<DownloadNavigationRequests::Entry> entry_;
  std::function<void()> changed_;
  std::shared_ptr<DownloadResponseStream> stream_;
  int page_;bool navigation_,download_;IMPLEMENT_REFCOUNTING(DownloadResourceHandler);
};
}
DownloadControls::DownloadControls(Hooks hooks):hooks_(std::move(hooks)){
  response_budgets_=std::make_shared<DownloadResponseBudgets>(quota_,hooks_.now);
  std::random_device random;std::ostringstream value;value<<std::hex<<random()<<random();namespace_=value.str();
}
DownloadControls::~DownloadControls(){Clear();}
bool DownloadControls::CanDownload(int page)const{
  if(clearing_||closing_pages_.contains(page)||!hooks_.policy(page).context)return false;
  if(records_.size()<256)return true;
  for(const auto& [id,record]:records_)if(record->physical_settled&&!record->files)return true;
  return false;
}
std::shared_ptr<DownloadControls::Record> DownloadControls::Ensure(int page,CefRefPtr<CefDownloadItem> item){
  const auto key=std::make_pair(page,item->GetId());
  if(const auto found=native_ids_.find(key);found!=native_ids_.end())return records_.at(found->second);
  if(records_.size()>=256){
    for(auto it=records_.begin();it!=records_.end();++it)if(it->second->physical_settled&&!it->second->files){native_ids_.erase({it->second->page,it->second->native_id});records_.erase(it);break;}
  }
  if(records_.size()>=256||next_id_==std::numeric_limits<std::uint64_t>::max())return nullptr;
  const auto policy=hooks_.policy(page);if(policy.context<=0)return nullptr;
  auto record=std::make_shared<Record>();record->page=page;record->native_id=item->GetId();record->id="download-"+namespace_+"-"+std::to_string(++next_id_);
  record->context=policy.context;record->accepted=policy.accepted;record->event_page=policy.event_page>0?policy.event_page:page;
  Snapshot(*record,item);native_ids_[key]=record->id;records_[record->id]=record;return record;
}
void DownloadControls::Snapshot(Record& record,CefRefPtr<CefDownloadItem> item){
  const auto metadata=[&record](std::string& field,bool& unavailable,const std::string& value,size_t limit,const char* name){
    unavailable=unavailable||value.size()>limit;
    if(unavailable){field.clear();if(record.failure.empty())record.failure=std::string("Native download metadata capacity exceeded: ")+name+" unavailable";}
    else field=value;
  };
  metadata(record.url,record.unavailable_url,item->GetURL(),8192,"URL");
  metadata(record.original_url,record.unavailable_original,item->GetOriginalUrl(),8192,"original URL");
  metadata(record.mime,record.unavailable_mime,item->GetMimeType(),1024,"MIME type");
  record.total=item->GetTotalBytes();record.received=std::max<std::int64_t>(0,item->GetReceivedBytes());
  if(record.total>9007199254740991ll&&record.failure.empty())record.failure="Native download metadata capacity exceeded: totalBytes unavailable outside precise numeric range";
  if(record.suggested.empty()&&!record.unavailable_suggested)metadata(record.suggested,record.unavailable_suggested,item->GetSuggestedFileName(),4096,"suggested filename");
  if(item->IsComplete()){record.complete=true;record.settled=true;}
  else if(item->IsCanceled()){record.settled=true;if(record.failure.empty())record.failure="canceled";}
  else if(item->IsInterrupted()){record.settled=true;if(record.failure.empty())record.failure=DownloadReason(item->GetInterruptReason());}
}
DownloadControls::Dict DownloadControls::Info(const Record& record,const char* event)const{
  auto result=CefDictionaryValue::Create();result->SetString("event",event);result->SetString("id",record.id);result->SetInt("pageId",record.event_page);result->SetInt("contextId",record.context);
  if(record.unavailable_url)result->SetNull("url");else result->SetString("url",record.url);
  if(record.unavailable_original)result->SetNull("originalURL");else result->SetString("originalURL",record.original_url);
  if(record.unavailable_suggested)result->SetNull("suggestedFilename");else result->SetString("suggestedFilename",record.suggested);
  if(record.unavailable_mime)result->SetNull("mimeType");else result->SetString("mimeType",record.mime);
  if(record.total>9007199254740991ll)result->SetNull("totalBytes");else result->SetDouble("totalBytes",static_cast<double>(record.total));
  result->SetDouble("receivedBytes",static_cast<double>(record.received));result->SetBool("accepted",record.accepted);
  result->SetString("state",record.physical_settled?(record.complete&&record.failure.empty()?"complete":"failed"):"inProgress");
  if(record.failure.empty())result->SetNull("failure");else result->SetString("failure",record.failure);
  return result;
}
bool DownloadControls::Before(int page,CefRefPtr<CefDownloadItem> item,const std::string& suggested,CefRefPtr<CefBeforeDownloadCallback> callback){
  if(!item||!item->IsValid())return true;
  auto record=Ensure(page,item);if(!record)return true;
  record->unavailable_suggested=record->unavailable_suggested||suggested.size()>4096;
  if(record->unavailable_suggested){record->suggested.clear();if(record->failure.empty())record->failure="Native download metadata capacity exceeded: suggested filename unavailable";}
  else record->suggested=suggested;
  if(record->emitted)return true;
  record->emitted=true;
  const auto policy=hooks_.policy(page);record->event_page=policy.event_page>0?policy.event_page:page;
  const bool closing=clearing_||(closing_pages_.contains(page)||closing_pages_.contains(record->event_page))||closing_contexts_.contains(record->context);
  if(closing)record->failure="Target page, context or browser has been closed";
  else if(!record->accepted)record->failure="Pass { acceptDownloads: true } when you are creating your browser context";
  else if(record->url.size()>8192||record->original_url.size()>8192||record->suggested.size()>4096)record->failure="Native download metadata capacity exceeded";
  else if(record->total>static_cast<std::int64_t>(DownloadQuota::kFileLimit))record->failure="Native download exceeds the 256 MiB file limit";
  if(record->failure.empty()){
    record->quota=std::make_shared<DownloadQuotaLease>(quota_);
    // CEF and response callbacks need not arrive in matching URL order. File
    // reservations never borrow an unrelated response stream's identity.
    const auto reserve=record->total>0?static_cast<std::uint64_t>(record->total):DownloadQuota::kFileLimit;
    if(!record->quota->Add(reserve))record->failure="Native download disk capacity exceeded (1 GiB)";
    if(record->failure.empty())try{record->files=std::make_shared<DownloadFiles>(DirectoryForContext(record->context));}catch(const std::exception& error){record->failure=error.what();}
  }
  hooks_.event(record->event_page,Info(*record,"created"));
  if(record->failure.empty())callback->Continue(record->files->path(),false);
  else RequestCancel(*record,record->failure);
  if(record->physical_settled&&!record->failure.empty())Cleanup(record);
  else if(record->settled)QueueFinish(record);
  hooks_.wake();return true;
}
void DownloadControls::Updated(int page,CefRefPtr<CefDownloadItem> item,CefRefPtr<CefDownloadItemCallback> callback){
  if(!item||!item->IsValid())return;
  auto record=Ensure(page,item);if(!record){callback->Cancel();return;}
  if(record->physical_settled)return;
  record->callback=callback;Snapshot(*record,item);
  if(record->received>static_cast<std::int64_t>(DownloadQuota::kFileLimit))RequestCancel(*record,"Native download disk capacity exceeded");
  if(record->files&&record->complete)try{record->files->Adopt(item->GetFullPath());}catch(const std::exception& error){record->failure=error.what();}
  if(clearing_||(closing_pages_.contains(page)||closing_pages_.contains(record->event_page))||closing_contexts_.contains(record->context))RequestCancel(*record,"Target page, context or browser has been closed");
  if(record->cancel_requested&&!record->settled)callback->Cancel();
  if(record->settled)QueueFinish(record);
  hooks_.wake();
}
CefRefPtr<CefResourceRequestHandler> DownloadControls::ResourceHandler(int page,CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> request,bool navigation,bool download){
  const auto entry=(navigation||download)?navigation_requests_->Begin(page,request->GetIdentifier(),frame?frame->GetIdentifier().ToString():"",frame&&frame->IsMain(),request->GetMethod(),request->GetURL()):nullptr;
  auto changed=[weak=weak_from_this(),page]{CefPostTask(TID_UI,new DownloadTask([weak,page]{const auto controls=weak.lock();if(controls&&!controls->clearing_&&controls->hooks_.navigation_response)controls->hooks_.navigation_response(page);}));};
  return new DownloadResourceHandler(response_budgets_,navigation_requests_,entry,std::move(changed),page,navigation,download);
}
void DownloadControls::RequestCancel(Record& record,const std::string& reason){
  if(record.settled)return;
  record.cancel_requested=true;if(record.failure.empty())record.failure=reason;
  if(record.callback)record.callback->Cancel();
}
void DownloadControls::QueueFinish(std::shared_ptr<Record> record){
  if(record->physical_settled||record->finish_queued)return;
  record->finish_queued=true;
  // DownloadItem observer delivery is still on the stack in a CEF callback.
  // Profile destruction must wait for that actual UI callback to return.
  const auto weak=weak_from_this();
  CefPostTask(TID_UI,new DownloadTask([weak,record]{
    if(const auto self=weak.lock()){record->finish_queued=false;self->Finish(record);}
  }));
}
void DownloadControls::Finish(std::shared_ptr<Record> record){
  if(record->physical_settled||!record->settled)return;
  if(record->files&&record->files->HasOpenDescriptor())return;
  if(record->complete&&record->failure.empty()&&record->files)try{
    const auto size=record->files->Complete();
    if(!record->quota||size>record->quota->Bytes()){record->failure="Native completed download exceeded its reserved disk budget";record->complete=false;}
    else record->quota->ReduceTo(size);
    record->received=static_cast<std::int64_t>(size);
  }catch(const std::exception& error){record->failure=error.what();record->complete=false;}
  record->physical_settled=true;record->callback=nullptr;record->handler_cancellations.clear();
  if(record->emitted)hooks_.event(record->event_page,Info(*record));
  if(!record->failure.empty()||record->context_closed)Cleanup(record);
  std::vector<int> owners;for(const auto& [owner,wait]:waits_)if(wait.id==record->id)owners.push_back(owner);
  for(const int owner:owners)CompleteWait(owner);
  CheckClosures();
  if(record->event_page!=record->page&&hooks_.close_provisional_page)hooks_.close_provisional_page(record->page);
}
void DownloadControls::Handle(int owner,Dict params,double deadline,Reply reply){
  reply=BindNativeLocatorHandlerAsync(std::move(reply));
  const auto id=params->GetString("downloadId").ToString();const auto action=params->GetString("action").ToString();const auto found=records_.find(id);
  if(found==records_.end()){reply(false,nullptr,"Native download is unavailable or its owning context was closed");return;}
  auto record=found->second;
  if(action=="status"){reply(true,Info(*record),"");return;}
  if(action!="path"&&action!="failure"&&action!="saveAs"&&action!="delete"&&action!="cancel"){reply(false,nullptr,"Unsupported native download action");return;}
  if(waits_.size()+copies_.size()>=128||waits_.contains(owner)||copies_.contains(owner)){reply(false,nullptr,"Native download operation capacity exceeded");return;}
  Wait wait;wait.owner=owner;wait.id=id;wait.action=action;wait.deadline=deadline;wait.reply=std::move(reply);
  if(action=="saveAs"){
    if(params->GetType("path")!=VTYPE_STRING){wait.reply(false,nullptr,"Native download saveAs requires a destination path");return;}
    wait.path=params->GetString("path");
  }
  waits_.emplace(owner,std::move(wait));
  if(action=="cancel"){
    const auto invocation=CurrentNativeWorkOrigin().locator_handler_invocation;
    if(invocation&&!record->physical_settled&&!record->handler_cancellations.contains(invocation))record->handler_cancellations.emplace(invocation,RetainNativeLocatorHandlerAsync());
    RequestCancel(*record,"canceled");
  }
  if(record->physical_settled)CompleteWait(owner);
  hooks_.wake();
}
void DownloadControls::CompleteWait(int owner){
  const auto found=waits_.find(owner);if(found==waits_.end())return;
  const auto record=records_.find(found->second.id);if(record==records_.end())return;
  auto current=record->second;if(!current->physical_settled)return;
  if(found->second.action=="delete")for(const auto& [id,copy]:copies_)if(copy->id==current->id)return;
  auto wait=std::move(found->second);waits_.erase(found);
  if(wait.action=="failure"){
    auto result=CefDictionaryValue::Create();if(current->failure.empty())result->SetNull("failure");else result->SetString("failure",current->failure);wait.reply(true,result,"");return;
  }
  if(wait.action=="cancel"){wait.reply(true,nullptr,"");return;}
  if(wait.action=="delete"){Cleanup(current);current->deleted=true;wait.reply(true,nullptr,"");CheckClosures();return;}
  if(!current->failure.empty()){wait.reply(false,nullptr,current->failure);return;}
  if(current->deleted||current->context_closed||!current->files){wait.reply(false,nullptr,"Target page, context or browser has been closed (native download file was deleted)");return;}
  if(wait.action=="path"){auto result=CefDictionaryValue::Create();result->SetString("path",current->files->path());wait.reply(true,result,"");return;}
  StartCopy(std::move(wait),current);
}
void DownloadControls::StartCopy(Wait wait,std::shared_ptr<Record> record){
  const NativeWorkOriginFrame handler_origin(wait.origin.Origin());
  if(copies_.size()>=4){wait.reply(false,nullptr,"Native download saveAs copy capacity exceeded");return;}
  auto lease=std::make_shared<DownloadQuotaLease>(quota_);
  if(!lease->Add(static_cast<std::uint64_t>(record->received))){wait.reply(false,nullptr,"Native download saveAs disk capacity exceeded (1 GiB)");return;}
  auto copy=std::make_shared<Copy>();copy->owner=wait.owner;copy->id=record->id;copy->deadline=wait.deadline;copy->reply=std::move(wait.reply);copy->files=record->files;copy->quota=lease;copies_[copy->owner]=copy;
  const auto weak=weak_from_this();const auto now=hooks_.now;const auto path=wait.path;
  copy->thread=std::thread([copy,weak,now,path]{
    copy->result=CopyDownloadFile(copy->files->path(),path,copy->canceled,copy->deadline,now,copy->quota->Bytes());
    CefPostTask(TID_UI,new DownloadTask([weak,owner=copy->owner]{if(const auto self=weak.lock())self->CopyFinished(owner);}));
  });
}
void DownloadControls::CopyFinished(int owner){
  const auto found=copies_.find(owner);if(found==copies_.end())return;
  auto copy=found->second;if(copy->thread.joinable())copy->thread.join();copies_.erase(found);
  auto reply=std::move(copy->reply);const auto result=copy->result;const auto record=records_.find(copy->id);
  copy->files.reset();copy->quota.reset();
  if(record!=records_.end()&&record->second->context_closed)Cleanup(record->second);
  if(reply)reply(result.ok,nullptr,result.error);
  std::vector<int> ready;
  if(record!=records_.end())for(const auto& [pending,wait]:waits_)if(wait.id==record->first)ready.push_back(pending);
  for(const int pending:ready)CompleteWait(pending);
  CheckClosures();
}
void DownloadControls::Cancel(int owner){waits_.erase(owner);if(const auto found=copies_.find(owner);found!=copies_.end()){found->second->canceled=true;found->second->reply={};}}
void DownloadControls::Cleanup(std::shared_ptr<Record> record){
  if(!record->physical_settled)return;
  for(const auto& [owner,copy]:copies_)if(copy->id==record->id)return;
  record->files.reset();record->quota.reset();
}
bool DownloadControls::PageSettled(int page)const{
  for(const auto& [id,record]:records_)if((record->page==page||record->event_page==page)&&!record->physical_settled)return false;
  for(const auto& [owner,copy]:copies_)if(const auto record=records_.find(copy->id);record!=records_.end()&&(record->second->page==page||record->second->event_page==page))return false;
  return true;
}
void DownloadControls::BeforePageClose(int page,std::function<void()> continuation){
  closing_pages_.insert(page);page_closures_[page].push_back(std::move(continuation));
  for(const auto& [id,record]:records_)if((record->page==page||record->event_page==page))RequestCancel(*record,"Target page, context or browser has been closed");
  for(const auto& [owner,copy]:copies_)if(const auto record=records_.find(copy->id);record!=records_.end()&&(record->second->page==page||record->second->event_page==page))copy->canceled=true;
  CheckClosures();hooks_.wake();
}
void DownloadControls::ContextClosing(int context){
  closing_contexts_.insert(context);
  for(const auto& [id,record]:records_)if(record->context==context){record->context_closed=true;RequestCancel(*record,"Target page, context or browser has been closed");Cleanup(record);}
  for(const auto& [owner,copy]:copies_)if(const auto record=records_.find(copy->id);record!=records_.end()&&record->second->context==context)copy->canceled=true;
  CheckClosures();hooks_.wake();
}
void DownloadControls::PageGone(int page){
  navigation_requests_->PageClosed(page);
  for(const auto& [id,record]:records_)if(record->page==page||record->event_page==page){if(record->page==page)record->browser_gone=true;RequestCancel(*record,"Target page, context or browser has been closed");}
  closing_pages_.erase(page);response_budgets_->ClosedPage(page);hooks_.wake();
}
void DownloadControls::ContextGone(int context){closing_contexts_.erase(context);directories_.erase(context);}
bool DownloadControls::ContextCleanupReady(int context)const{
  for(const auto& [id,record]:records_)if(record->context==context&&(!record->physical_settled||record->files))return false;
  for(const auto& [owner,copy]:copies_)if(const auto record=records_.find(copy->id);record!=records_.end()&&record->second->context==context)return false;
  return true;
}
void DownloadControls::CheckClosures(){
  std::vector<int> pages;for(const auto& [page,callbacks]:page_closures_)if(PageSettled(page))pages.push_back(page);
  for(const int page:pages){auto callbacks=std::move(page_closures_.at(page));page_closures_.erase(page);response_budgets_->ClosedPage(page);for(auto& callback:callbacks)callback();}
  std::vector<int> contexts;for(const int context:closing_contexts_)if(ContextCleanupReady(context))contexts.push_back(context);
  for(const int context:contexts)if(hooks_.settled_context)hooks_.settled_context(context);
}
void DownloadControls::Tick(){
  response_budgets_->Collect();std::vector<std::shared_ptr<Record>> pending;
  for(const auto& [id,record]:records_){
    // A page can close itself outside the context API. Its native cancellation
    // still owns the directory until the response stream and all local writers
    // have physically stopped, even if CEF cannot deliver another client event.
    if(record->browser_gone&&record->cancel_requested&&(!record->files||!record->files->HasOpenDescriptor()))record->settled=true;
    if(record->settled&&!record->physical_settled)pending.push_back(record);
  }
  for(const auto& record:pending)Finish(record);
  CheckClosures();
}
bool DownloadControls::NeedsTick()const{
  if(response_budgets_->Pending())return true;
  if(!copies_.empty()||!page_closures_.empty())return true;
  for(const auto& [id,record]:records_)if(!record->physical_settled)return true;
  return false;
}
void DownloadControls::Diagnostics(Dict result)const{
  int active=0,files=0;std::uint64_t disk=0;
  for(const auto& [id,record]:records_){if(!record->physical_settled)++active;if(record->files){++files;disk+=record->files->DiskBytes();}}
  result->SetInt("downloadItems",static_cast<int>(records_.size()));result->SetInt("activeDownloads",active);result->SetInt("retainedDownloadFiles",files);result->SetDouble("downloadDiskBytes",static_cast<double>(disk));result->SetDouble("downloadReservedBytes",static_cast<double>(quota_->Used()));result->SetInt("downloadWaits",static_cast<int>(waits_.size()));result->SetInt("downloadCopies",static_cast<int>(copies_.size()));
}
void DownloadControls::Clear(){
  directories_.clear();
  clearing_=true;waits_.clear();
  for(const auto& [owner,copy]:copies_){copy->canceled=true;copy->reply={};}
  for(const auto& [owner,copy]:copies_)if(copy->thread.joinable())copy->thread.join();
  copies_.clear();
  for(const auto& [id,record]:records_)RequestCancel(*record,"Native browser has been closed");
  records_.clear();native_ids_.clear();response_budgets_->Clear();navigation_requests_->Clear();page_closures_.clear();
}
} // namespace suitecut
