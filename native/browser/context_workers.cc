// SPDX-License-Identifier: MIT
#include "context_workers.h"
#include "accepted_task_work.h"
#include "locator_handler_async_work.h"
#include "context_worker_event_cursor.h"
#include "cookie_filter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "network_controls.h"
#include "context_worker_network.h"
#include "response_body_reads.h"
#include "response_streams.h"
#include "request_body_reads.h"
#include "network_publication_results.h"
#include "network_fatal_teardown.h"
#include "operation_registry.h"
#include "console_journal.h"
#include "console_array_preview.h"
#include "event_predicates.h"
#include "storage_utf16.h"
#include "devtools_utf16.h"
#include "broker_dispatch_queue.h"
namespace {
class WorkerBrokerTask : public CefTask {
 public:
  explicit WorkerBrokerTask(std::function<void()> fn):fn_(std::move(fn)){}
  void Execute() override{const suitecut::AcceptedTaskFrame native_frame(native_work_);fn_();}
 private:
  suitecut::AcceptedTaskReceipt native_work_;std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(WorkerBrokerTask);
};
}
class SuiteCutContextWorkers::Client : public CefClient, public CefLifeSpanHandler,
    public CefRenderHandler, public CefDevToolsMessageObserver {
 public:
  using Source=Client;
  using Task=WorkerBrokerTask;
  using DevToolsCallback=std::function<void(bool,const std::string&)>;
  struct DevToolsCall {int owner;DevToolsCallback callback;std::string session;bool retain_after_cancel;bool lossless_scalars=false;};
  struct Control {std::string session;double deadline;DevToolsCallback callback;bool expired=false;bool dispatched=false;};
  struct Waiting {Dict params;Reply reply;suitecut::NativeWorkContinuation origin;};
  struct CookieWaiting {suitecut::CookieMethod method;Dict params;double deadline;Reply reply;suitecut::NativeWorkContinuation origin;};
  struct Record {uint64_t sequence;std::string event,json;};
  suitecut::NativeWorkContinuation handler_creation_origin_;
  std::shared_ptr<suitecut::NativeLocatorHandlerAsyncWork> handler_creation_receipt_=suitecut::RetainNativeLocatorHandlerAsync();
  Client(std::weak_ptr<SuiteCutContextWorkers> owner,int context,CefRefPtr<CefRequestContext> request,suitecut::NativeContextWorkerCursors::Lease cursor)
      :owner_(std::move(owner)),context_(context),request_context_(request),now_(owner_.lock()->hooks_.now),handle_namespace_("context-"+std::to_string(context)+"-"+std::to_string(++namespaces_)),cursor_(cursor),event_sequence_(cursor.initial) {
    if(auto owner=owner_.lock())inspector_work_=owner->hooks_.inspector_work;
    worker_context_channel_=true;
    worker_service_fetch_=true;
    worker_websocket_attach_=[this](const std::string&session,const std::string&,const std::string&,double,NetworkResult done){AttachWebSocketTarget(session,std::move(done));};
    worker_websocket_required_=[this]{auto owner=owner_.lock();return owner&&owner->hooks_.websocket_required&&owner->hooks_.websocket_required(context_);};
    worker_websocket_failure_=[this](const std::string&error){
      // A broker detach alone could release a paused shared/service worker.
      // Retire the actual owning request context through Engine's acknowledged
      // context-close path before dropping its control channel.
      if(auto owner=owner_.lock();owner&&owner->hooks_.websocket_failure)owner->hooks_.websocket_failure(context_,error);
      network_fatal_->Fail("Inherited native WebSocket worker policy failed: "+error);WakeDeadlines();
    };
    worker_websocket_detach_=[this](const std::string&session){if(auto owner=owner_.lock();owner&&owner->hooks_.detach_websocket)owner->hooks_.detach_websocket(network_policy_target_+"/ws:"+session);};
    worker_event_available_=[this](bool actual_retirement){return suitecut::NativeContextWorkerCursors::CanNext(event_sequence_,actual_retirement);};
    worker_event_failure_=[this]{
      EventCursorFailed("Native context worker event cursor capacity exceeded");
    };
  }
  void EventCursorFailed(const std::string&error){if(network_fatal_){network_fatal_->Fail(error);WakeDeadlines();}else{Error(error.c_str());Close();}}
  suitecut::NativeContextWorkerCursors::Lease cursor() const {return cursor_;}
  uint64_t final_event_sequence() const {return event_sequence_;}
  const std::string& control_owner() const {return handle_namespace_;}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {return this;}
  CefRefPtr<CefRenderHandler> GetRenderHandler() override {return this;}
  void GetViewRect(CefRefPtr<CefBrowser>,CefRect& rect) override {rect=CefRect(0,0,1,1);}
  void OnPaint(CefRefPtr<CefBrowser>,PaintElementType,const RectList&,const void*,int,int) override {}
  bool Create() {
    CefWindowInfo window;window.SetAsWindowless(0);CefBrowserSettings settings;settings.windowless_frame_rate=1;
    settings.background_color=CefColorSetARGB(255,0,0,0);
    return CefBrowserHost::CreateBrowser(window,this,"about:blank",settings,nullptr,request_context_);
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    const suitecut::NativeWorkOriginFrame handler_origin(handler_creation_origin_.Origin());
    browser_=browser;NativeInspectorOpened(inspector_work_,browser->GetIdentifier(),suitecut::AcceptedInspectorWork::Kind::Broker);
    const auto native_inspector_callback=NativeInspectorCallback();
    runtime_style_=browser->GetHost()->GetRuntimeStyle();observer_=browser->GetHost()->AddDevToolsMessageObserver(this);
    if(closing_page_){browser->GetHost()->CloseBrowser(true);return;}
    network_fatal_=std::make_unique<suitecut::NativeNetworkFatalTeardown>(
      [this](std::function<void()> task){return CefPostTask(TID_UI,new Task([self=CefRefPtr<Client>(this),task=std::move(task)]{task();}));},
      [this](const std::string&error){Error(error.c_str());},[this]{Close();});
    network_=std::make_unique<suitecut::NetworkControls>(
      [this](const std::string& session,const std::string& method,Dict params,suitecut::NetworkControls::Result done){ControlSend(method,params,session,[done=std::move(done)](bool ok,const std::string& error){done(ok,error);});},
      [this](Dict event){PublicNetworkEvent(event);},[this](const std::string& error){network_fatal_->Fail(error);WakeDeadlines();},[this]{WakeDeadlines();},[this]{return Now();},
      [this](uint64_t body,size_t offset,size_t total,std::span<const uint8_t>bytes,bool canceled){auto owner=owner_.lock();return owner&&owner->hooks_.network_body&&owner->hooks_.network_body(context_,network_policy_target_,body,offset,total,bytes,canceled);});
    network_->SetBoundedSender([this](const std::string&session,const std::string&method,Dict params,suitecut::NetworkControls::Result done,double deadline){ControlSend(method,params,session,[done=std::move(done)](bool ok,const std::string&reason){done(ok,reason);},deadline);});
    ConfigureWorkerRouteFetch();
    network_->SessionAttached("");
    network_policy_target_="broker:"+std::to_string(browser->GetIdentifier());
    if(auto owner=owner_.lock();owner&&owner->hooks_.network_owner){
      if(!owner->hooks_.network_owner(context_,network_policy_target_,
        [self=CefRefPtr<Client>(this)](int owner,Dict command,double deadline,Reply reply){self->PublicNetworkCommand(owner,command,deadline,std::move(reply));},
        [self=CefRefPtr<Client>(this)](const std::string&session,const std::string&request,double deadline,std::function<void(bool,std::vector<uint8_t>,const std::string&)>reply){self->NativeReadResponseBody(session,request,deadline,std::move(reply));},
        [self=CefRefPtr<Client>(this)](int owner){if(self->network_)self->network_->Cancel(owner);})){network_fatal_->Fail("Native context worker network owner capacity exceeded");WakeDeadlines();return;}
    }
    if(auto owner=owner_.lock();owner&&owner->hooks_.resource_owner){
      if(!owner->hooks_.resource_owner(context_,network_policy_target_,[self=CefRefPtr<Client>(this)](const std::string&session,const std::string&request,double deadline,std::function<void(bool,std::vector<uint8_t>,const std::string&)>reply){self->NativeReadResponseBody(session,request,deadline,std::move(reply));})){
        Error("Native broker network archive owner capacity exceeded");browser->GetHost()->CloseBrowser(true);return;
      }
      network_->SetObserver([this](Dict event){if(auto owner=owner_.lock();owner&&owner->hooks_.resource_event)owner->hooks_.resource_event(context_,network_policy_target_,event);});
    }
    network_->SetRequestBodyReader(
      [self=CefRefPtr<Client>(this)](const std::string&session,const std::string&request,double deadline,suitecut::NativeNetworkPostPreparation::Data done,suitecut::NativeNetworkPostPreparation::Receipt receipt){self->NativeReadRequestBody(session,request,deadline,std::move(done),std::move(receipt));},
      [this](const std::string&id){if(auto owner=owner_.lock();owner&&owner->hooks_.resource_post){auto reserved=owner->hooks_.resource_post(context_,network_policy_target_,id);return suitecut::NativeNetworkPostPreparation::Reservation{std::move(reserved.data),std::move(reserved.receipt)};}return suitecut::NativeNetworkPostPreparation::Reservation{};});
    auto initialize=[self=CefRefPtr<Client>(this)](bool ok,const std::string& error){
      if(self->closing_page_)return;
      if(!ok){self->network_fatal_->Fail(error);self->WakeDeadlines();return;}
      self->AuthenticateAndDiscover();
    };
    if(auto owner=owner_.lock();owner && owner->hooks_.attach_network){
      const bool attached=owner->hooks_.attach_network(context_,network_policy_target_,
        [self=CefRefPtr<Client>(this)](bool offline,NetworkResult done){
          if(self->closing_page_||!self->network_){done(false,"Native broker policy target is closed");return;}
          auto params=CefDictionaryValue::Create();params->SetString("action","setOffline");params->SetBool("offline",offline);
          self->network_->Command(0,params,[done=std::move(done)](bool ok,const std::string& error,const std::string&){done(ok,error);});
        },std::move(initialize));
      if(!attached){Error("Native broker network policy capacity exceeded");browser->GetHost()->CloseBrowser(true);}
    }else initialize(true,"");
  }
  void AuthenticateAndDiscover() {
    auto owner=owner_.lock();auto provider=owner&&owner->hooks_.authentication?owner->hooks_.authentication(context_):suitecut::NativeNetworkAuthentication::Provider{};
    if(!provider){InstallContextHeaders();return;}
    network_->SetAuthentication(std::move(provider),[self=CefRefPtr<Client>(this)](bool ok,const std::string&error){
      if(self->closing_page_)return;
      if(!ok){self->network_fatal_->Fail(error);self->WakeDeadlines();return;}
      self->InstallContextHeaders();
    });
  }
  void InstallContextHeaders() {
    auto discover=[self=CefRefPtr<Client>(this)](bool ok,const std::string&error){if(self->closing_page_)return;if(!ok){self->network_fatal_->Fail(error);self->WakeDeadlines();return;}self->InstallContextHAR();};
    if(auto owner=owner_.lock();owner&&owner->hooks_.attach_headers){
      if(!owner->hooks_.attach_headers(context_,network_policy_target_,[self=CefRefPtr<Client>(this)](Dict headers,NetworkResult done){
        if(self->closing_page_||!self->network_){done(false,"Native broker HTTP headers target is closed");return;}
        auto params=CefDictionaryValue::Create();params->SetString("action","setContextExtraHTTPHeaders");params->SetDictionary("headers",headers->Copy(false));
        self->network_->Command(0,params,[done=std::move(done)](bool ok,const std::string&error,const std::string&){done(ok,error);});
      },std::move(discover))){network_fatal_->Fail("Native broker HTTP headers target capacity exceeded");WakeDeadlines();}
    }else discover(true,"");
  }
  void InstallContextHAR(){
    auto ready=[self=CefRefPtr<Client>(this)](bool ok,const std::string&error){if(self->closing_page_)return;if(!ok){self->network_fatal_->Fail(error);self->WakeDeadlines();return;}self->InstallContextRoutes();};
    if(auto owner=owner_.lock();owner&&owner->hooks_.attach_har){
      if(!owner->hooks_.attach_har(context_,network_policy_target_,[self=CefRefPtr<Client>(this)](suitecut::NativeContextHARRoutes::Mutation mutation,const suitecut::NativeContextHARRoutes::Route&route,double deadline,NetworkResult done){
        if(self->closing_page_||!self->network_){done(false,"Actual broker HAR policy target is closed");return;}
        if(mutation==suitecut::NativeContextHARRoutes::Mutation::Install)self->network_->BindReplayPolicy(route.id,route.pattern,route.policy,std::move(done),deadline,true);
        else self->network_->UnbindReplayPolicy(route.id,route.policy,std::move(done),deadline);
      },std::move(ready))){network_fatal_->Fail("Native broker HAR policy capacity exceeded");WakeDeadlines();}
    }else ready(true,"");
  }
  void InstallContextRoutes(){
    auto ready=[self=CefRefPtr<Client>(this)](bool ok,const std::string&error){if(self->closing_page_)return;if(!ok){self->network_fatal_->Fail(error);self->WakeDeadlines();return;}self->InstallContextWebSockets();};
    if(auto owner=owner_.lock();owner&&owner->hooks_.attach_routes){
      if(!owner->hooks_.attach_routes(context_,network_policy_target_,[self=CefRefPtr<Client>(this)](suitecut::NativeContextRoutes::Mutation mutation,suitecut::NativeContextRoutes::Route route,double deadline,NetworkResult done){
        if(self->closing_page_||!self->network_){done(false,"Actual broker context route owner is closed");return;}
        if(mutation==suitecut::NativeContextRoutes::Mutation::Install)self->network_->BindContextRoute(std::move(route),std::move(done),deadline);
        else self->network_->UnbindContextRoutePolicy(route,std::move(done),deadline);
      },std::move(ready))){network_fatal_->Fail("Native broker context route capacity exceeded");WakeDeadlines();}
    }else ready(true,"");
  }
  bool websocket_root_ready_=false;
  std::vector<NetworkResult>websocket_readiness_;
  void WebSocketPolicyReady(NetworkResult done){
    if(closing_page_){done(false,"Actual native WebSocket broker is closed");return;}
    if(websocket_root_ready_){done(true,"");return;}
    if(websocket_readiness_.size()>=32){done(false,"Native WebSocket broker readiness capacity exceeded");return;}
    websocket_readiness_.push_back(std::move(done));
  }
  void AttachWebSocketTarget(const std::string&session,NetworkResult done){
    auto owner=owner_.lock();
    if(!owner||!owner->hooks_.attach_websocket){done(false,"Native WebSocket broker hooks unavailable");return;}
    if(!owner->hooks_.attach_websocket(context_,network_policy_target_+"/ws:"+session,
      [self=CefRefPtr<Client>(this),session](const std::string&method,Dict params,double deadline,NetworkResult reply){
        if(self->closing_page_||!self->browser_){reply(false,"Actual WebSocket broker target is closed");return;}
        self->ControlSend(method,params,session,std::move(reply),deadline);
      },[self=CefRefPtr<Client>(this)](const std::string&error){self->network_fatal_->Fail(error);self->WakeDeadlines();},std::move(done))){network_fatal_->Fail("Native WebSocket broker target capacity exceeded");WakeDeadlines();}
  }
  void InstallContextWebSockets(){
    ControlSend("Network.enable",nullptr,"",[self=CefRefPtr<Client>(this)](bool ok,const std::string&error){
      if(self->closing_page_)return;
      if(!ok){self->network_fatal_->Fail(error);self->WakeDeadlines();return;}
      self->AttachWebSocketTarget("",[self](bool attached,const std::string&reason){
        if(self->closing_page_)return;
        auto waiting=std::move(self->websocket_readiness_);
        self->websocket_root_ready_=attached;
        for(auto&done:waiting)done(attached,reason);
        if(!attached){self->network_fatal_->Fail(reason);self->WakeDeadlines();return;}
        self->DiscoverContext();
      });
    });
  }
  bool NetworkPolicyFailure(const std::string&owner,const std::string&error){if(owner!=network_policy_target_)return false;if(network_fatal_){network_fatal_->Fail(error);WakeDeadlines();}return true;}
  void DiscoverContext() {
    ControlSend("Target.getTargetInfo",nullptr,"",[this](bool ok,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto info=result?result->GetDictionary("targetInfo"):nullptr;
      if(!ok||!info||info->GetType("browserContextId")!=VTYPE_STRING||info->GetString("browserContextId").empty()||info->GetString("browserContextId").length()>128){Error("Could not establish actual worker Chromium context identity");return;}
      chromium_context_=info->GetString("browserContextId");
      if(info->GetType("targetId")==VTYPE_STRING&&!info->GetString("targetId").empty()&&info->GetString("targetId").length()<=128)root_target_id_=info->GetString("targetId");
      auto discover=CefDictionaryValue::Create();discover->SetBool("discover",true);
      ControlSend("Target.setDiscoverTargets",discover,"",[this](bool discovered,const std::string& error){
        if(!discovered){Error(error.c_str());return;}
        ready_=true;
        ControlSend("Target.getTargets",nullptr,"",[this](bool ok,const std::string& json){
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto infos=result?result->GetList("targetInfos"):nullptr;
          if(!ok||!infos){Error("Could not enumerate actual context worker targets");return;}
          for(size_t i=0;i<infos->GetSize();++i)Discover(infos->GetDictionary(i));
          handler_creation_receipt_.reset();
          FlushCookieControls();Flush();
        });
      });
    });
  }
  CefRefPtr<Client> RetainWorkerRouteFetchOwner(){return this;}
#include "controllers/workers/worker_route_fetch.inc"
  void PublicNetworkEvent(Dict event){
    auto request=event?(event->GetString("event")=="routematch"?event:event->GetDictionary("request")):nullptr;if(!request)return;
    const auto worker=request->GetDictionary("workerInfo");if(!worker||!request->GetBool("contextWorker"))return;
    if(auto owner=owner_.lock();owner&&owner->hooks_.network_event)owner->hooks_.network_event(context_,network_policy_target_,worker,event);
  }
  void PublicNetworkCommand(int owner,Dict command,double deadline,Reply reply){
    if(closing_page_||!network_||(deadline&&Now()>=deadline)){reply(false,nullptr,"Actual native worker network owner is closed or original deadline expired");return;}
    network_->Command(owner,command,[reply=std::move(reply)](bool ok,const std::string&error,const std::string&json){auto parsed=ok?CefParseJSON(json,JSON_PARSER_RFC):nullptr;reply(ok,parsed?parsed->GetDictionary():nullptr,error);},deadline,suitecut::NativeContextWorkerNetwork::RouteOperationGeneration(command),context_);WakeDeadlines();
  }
  bool NetworkBody(const std::string&owner,const std::string&body,const std::string&stage){return network_&&owner==network_policy_target_&&network_->BodyAcknowledged(body,stage);}
  void NativeImageStreamEvent(const std::string&method,Dict actual,const std::string&session) {
    if(!image_streams_)image_streams_=std::make_unique<suitecut::NativeResponseStreams>(
      [this](const std::string&session,const std::string&request,double deadline,suitecut::NativeResponseStreams::Result done){auto params=CefDictionaryValue::Create();params->SetString("requestId",request);ControlSend("Network.streamResourceContent",params,session,std::move(done),deadline);},
      [this]{return Now();},[this]{WakeDeadlines();});
    image_streams_->Event(method,actual,session);
  }
  void NativeReadResponseBody(const std::string& session,const std::string& request,double deadline,suitecut::NativeResponseBodyReads::Reply reply) {
    if(closing_page_||!browser_){reply(false,{},"Actual native broker response-body owner is closed");return;}
    if(!response_reads_)response_reads_=std::make_unique<suitecut::NativeResponseBodyReads>(
      [this](const std::string& target,const std::string& request,double original,suitecut::NativeResponseBodyReads::Result done){
        if(closing_page_||!browser_){done(false,"Actual native broker response-body owner is closed");return;}
        const int id=AllocateDevToolsId();if(!id||controls_.size()>=128){done(false,"Native broker response-body command capacity exceeded");return;}
        controls_.emplace(id,Control{target,original?original:Now()+30000,[this,target,request,done=std::move(done)](bool ok,const std::string&json){const auto result=image_streams_?image_streams_->Resolve(target,request,ok,json):std::pair<bool,std::string>{ok,json};done(result.first,result.second);}});WakeDeadlines();
        auto params=CefDictionaryValue::Create();params->SetString("requestId",request);
        if(!SubmitDevTools(id,"Network.getResponseBody",params,target)){
          const auto found=controls_.find(id);if(found!=controls_.end()){auto callback=std::move(found->second.callback);controls_.erase(found);callback(false,"CEF rejected actual broker response-body read");}
        }
      },[this]{return Now();},[this]{WakeDeadlines();});
    // HAR owns logical timeout. This callback settles only on actual CDP ACK
    // or real channel close, preserving native physical-read accounting.
    response_reads_->Read(session,request,deadline,std::move(reply));
  }
  void NativeReadRequestBody(const std::string&session,const std::string&request,double deadline,suitecut::NativeRequestBodyReads::Reply reply,suitecut::NativeRequestBodyReads::Receipt receipt) {
    if(closing_page_||!browser_){if(receipt)receipt();return;}
    if(!request_reads_)request_reads_=std::make_unique<suitecut::NativeRequestBodyReads>(
      [this](const std::string&session,const std::string&request,double original,suitecut::NativeRequestBodyReads::Result done){
        if(closing_page_||!browser_){done(false,"Actual native broker request-body owner is closed");return;}
        const int id=AllocateDevToolsId();if(!id||controls_.size()>=128){done(false,"Native broker request-body command capacity exceeded");return;}
        controls_.emplace(id,Control{session,original?original:Now()+30000,std::move(done)});WakeDeadlines();auto params=CefDictionaryValue::Create();params->SetString("requestId",request);
        if(!SubmitDevTools(id,"Network.getRequestPostData",params,session)){const auto found=controls_.find(id);if(found!=controls_.end()){auto done=std::move(found->second.callback);controls_.erase(found);done(false,"CEF rejected actual broker request-body read");}}
      },[this]{return Now();},[this]{WakeDeadlines();});
    request_reads_->Read(session,request,deadline,std::move(reply),std::move(receipt));
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> actual_browser) override {
    const auto native_inspector_callback=NativeInspectorCallback();
    closing_page_=true;
    if(auto owner=owner_.lock();owner&&owner->hooks_.detach_websocket)owner->hooks_.detach_websocket(network_policy_target_+"/ws:");
    auto websocket_waiting=std::move(websocket_readiness_);for(auto&done:websocket_waiting)done(false,"Actual native WebSocket broker channel closed");
    FailQueuedCookies("Actual native context cookie channel closed");
    if(request_reads_){request_reads_->CancelAll();request_reads_->Close();}
    if(auto owner=owner_.lock();owner&&owner->hooks_.resource_closed&&!network_policy_target_.empty())owner->hooks_.resource_closed(network_policy_target_);
    if(response_reads_)response_reads_->Close();
    if(image_streams_)image_streams_->Close();
    if(auto owner=owner_.lock();owner&&owner->hooks_.network_closed&&!network_policy_target_.empty())owner->hooks_.network_closed(network_policy_target_);
    if(auto owner=owner_.lock();owner&&owner->hooks_.detach_routes&&!network_policy_target_.empty())owner->hooks_.detach_routes(network_policy_target_);
    if(auto owner=owner_.lock();owner&&owner->hooks_.detach_har&&!network_policy_target_.empty())owner->hooks_.detach_har(network_policy_target_);
    if(auto owner=owner_.lock();owner&&owner->hooks_.detach_headers&&!network_policy_target_.empty())owner->hooks_.detach_headers(network_policy_target_);
    if(network_fatal_)network_fatal_->Closed();
    if(auto owner=owner_.lock();owner&&owner->hooks_.detach_network&&!network_policy_target_.empty())owner->hooks_.detach_network(network_policy_target_);
    WorkerConsoleClear(true);WorkerClear();if(network_){network_->HARWorkChannelClosed();network_->Clear();}dispatch_queue_.Clear();
    auto controls=std::move(controls_);controls_.clear();for(auto&[id,control]:controls)control.callback(false,control.expired?"Native context worker original control deadline exceeded":"Actual native context worker channel closed");
    devtools_.clear();observer_=nullptr;browser_=nullptr;request_context_=nullptr;
    const auto owners=operations_.Ids();for(int id:owners)Complete(id,false,"Native context worker channel closed");
    handler_creation_receipt_.reset();
    NativeInspectorClosed(actual_browser->GetIdentifier());
    if(auto owner=owner_.lock())owner->Closed(this);
  }
  bool OnDevToolsMessage(CefRefPtr<CefBrowser>,const void* message,size_t size) override {
    const auto native_inspector_callback=NativeInspectorCallback();
    if(std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS")&&diagnostic_messages_++<2000)std::fprintf(stderr,"native context worker received context=%d: %.*s\n",context_,int(std::min(size,size_t(65536))),static_cast<const char*>(message));
    if(size>16*1048576){Error("Context worker DevTools response exceeds limit");return true;}
    auto parsed=CefParseJSON(std::string(static_cast<const char*>(message),size),JSON_PARSER_RFC);auto data=parsed?parsed->GetDictionary():nullptr;
    if(!data){
      // Recover only the actual reply envelope from a separate validation
      // projection. Unsupported scalar metadata is never published or copied.
      const auto original=std::string(static_cast<const char*>(message),size);
      const auto projection=suitecut::StorageJSONValidationProjection(original);
      if(projection!=original){const auto validated=CefParseJSON(projection,JSON_PARSER_RFC);const auto envelope=validated?validated->GetDictionary():nullptr;
        if(envelope&&envelope->GetType("id")==VTYPE_INT&&(!envelope->HasKey("sessionId")||envelope->GetType("sessionId")==VTYPE_STRING)){
          const int id=envelope->GetInt("id");const auto session=envelope->GetString("sessionId").ToString();const auto pending=devtools_.find(id);
          if(pending!=devtools_.end()&&pending->second.session==session&&pending->second.lossless_scalars&&!envelope->HasKey("error")){
            std::string lossless,error;if(suitecut::DevToolsLosslessProjection(original,lossless,error)){auto typed=CefParseJSON(lossless,JSON_PARSER_RFC);auto packet=typed?typed->GetDictionary():nullptr;auto result=packet?packet->GetDictionary("result"):nullptr;
              if(result){ToolResult(id,true,Json(result),session);return true;}}
          }
          ToolResult(id,false,R"({"message":"Native DevTools raw UTF16 scalar is unsupported"})",session);
        }
      }
      if(projection!=original){auto validation=CefParseJSON(projection,JSON_PARSER_RFC);auto envelope=validation?validation->GetDictionary():nullptr;
        if(envelope&&!envelope->HasKey("id")&&envelope->GetString("method")=="Runtime.consoleAPICalled"){
          std::string lossless,cause;if(!suitecut::DevToolsLosslessProjection(original,lossless,cause)){Error(cause.c_str());return true;}
          auto parsed=CefParseJSON(lossless,JSON_PARSER_RFC);auto event=parsed?parsed->GetDictionary():nullptr;auto params=event?event->GetDictionary("params"):nullptr;
          if(!params){Error("Invalid native lossless context console event");return true;}ContextConsoleEvent(params,event->GetString("sessionId").ToString());
        }
      }return true;
    }
    const auto session=data->GetString("sessionId").ToString();
    if(data->GetType("id")==VTYPE_INT){
      const int id=data->GetInt("id");const bool ok=!data->HasKey("error");auto result=data->GetDictionary(ok?"result":"error");const auto json=result?Json(result):"{}";
      ToolResult(id,ok,json,session);
      return true;
    }
    const auto method=data->GetString("method").ToString();auto params=data->GetDictionary("params");if(!params)return true;
    if(method=="Network.suiteCutWebSocketRoutedV1"){
      if(auto owner=owner_.lock();owner&&owner->hooks_.websocket_event)owner->hooks_.websocket_event(network_policy_target_+"/ws:"+session,params);
      return true;
    }
    if(method=="Target.targetCreated"||method=="Target.targetInfoChanged") {Discover(params->GetDictionary("targetInfo"));WorkerUpdated(params);return true;}
    if(method=="Target.attachedToTarget") {
      auto info=params->GetDictionary("targetInfo");
      if(Matches(info)||(worker_sessions_.contains(session)&&info&&info->GetString("type")=="worker")){
        const auto generation=worker_generation_;WorkerAttached(params,session);
        WorkerRetireReattachedSnapshot(params,generation,detached_targets_);
      }
      else SendDevTools("Runtime.runIfWaitingForDebugger",nullptr,params->GetString("sessionId"));
      return true;
    }
    if(method=="Target.detachedFromTarget") {
      for(const auto& closed:WorkerSessionClosures(params->GetString("sessionId").ToString())){
        const auto mapped=worker_sessions_.find(closed);
        if(mapped!=worker_sessions_.end()){const auto& target=worker_targets_.at(mapped->second);if((target.published||target.observed)&&!target.context_destroyed){if(detached_targets_.size()>=kWorkerTargets){Error("Native context worker detached target capacity exceeded");return true;}detached_targets_[target.id]=target;}}
        WorkerConsoleSessionRemoved(closed);WorkerDetached(closed);
        std::vector<DevToolsCallback> callbacks;for(auto it=controls_.begin();it!=controls_.end();)if(it->second.session==closed){callbacks.push_back(std::move(it->second.callback));it=controls_.erase(it);}else ++it;
        for(auto& callback:callbacks)callback(false,"Actual native worker target session detached");
      }return true;
    }
    if(method=="Target.targetDestroyed") {
      finished_targets_.erase(params->GetString("targetId"));
      const auto found=worker_targets_.find(params->GetString("targetId"));
      if(found!=worker_targets_.end()) {const auto child=found->second.session;WorkerConsoleSessionRemoved(child);WorkerRuntimeEvent("Runtime.executionContextsCleared",CefDictionaryValue::Create(),child);WorkerDetached(child);}
      else if(const auto detached=detached_targets_.find(params->GetString("targetId"));detached!=detached_targets_.end()){const auto target=detached->second;detached_targets_.erase(detached);WorkerPublish(target,"close");}
      return true;
    }
    if(method=="Inspector.targetCrashed"&&worker_sessions_.contains(session)) {
      // Chromium reports SharedWorkerGlobalScope.close() as an actual inspector
      // target crash before removing its global discovery metadata.
      auto& target=worker_targets_.at(worker_sessions_.at(session));
      if(finished_targets_.size()>=1024){Error("Native context worker terminated target capacity exceeded");return true;}
      finished_targets_.insert(target.id);target.context_destroyed=true;target.context=0;
      WorkerConsoleSessionRemoved(session);WorkerDetached(session);return true;
    }
    WorkerConsoleRuntimeEvent(method,params,session);
    WorkerRouteFetchRuntimeEvent(method,params,session);
    if(WorkerRuntimeEvent(method,params,session))return true;
    if(method=="Runtime.consoleAPICalled"){ContextConsoleEvent(params,session);return true;}
    if(method=="Network.requestWillBeSent")if(auto owner=owner_.lock();owner&&owner->hooks_.resource_request)owner->hooks_.resource_request(context_,network_policy_target_,session,params);
    WorkerAttributeNetwork(method,params,session);
    if(network_&&(method.starts_with("Network.")||(method=="Fetch.requestPaused"||method=="Fetch.authRequired")))network_->Event(method,params,session);
    if(method=="Network.requestWillBeSent"||method=="Network.dataReceived"||method=="Network.loadingFinished"||method=="Network.loadingFailed")NativeImageStreamEvent(method,params,session);
    return true;
  }
  void ToolResult(int id,bool ok,const std::string& json,const std::string& session){
      const suitecut::NativeWorkOriginFrame native_origin(NativeInspectorReplyOrigin(id,session));
      NativeInspectorReply(id,session);
      if(WorkerToolResult(id,ok,json.data(),json.size(),session))return;
      if(const auto found=controls_.find(id);found!=controls_.end()){
        if(found->second.session!=session){network_fatal_->Fail("Context worker command session mismatch");WakeDeadlines();return;}
        auto control=std::move(found->second);controls_.erase(found);control.callback(control.expired?false:ok,control.expired?"Native context worker original control deadline exceeded":json);return;
      }
      if(const auto found=devtools_.find(id);found!=devtools_.end()){
        if(found->second.session!=session){Error("Context worker operation session mismatch");return;}
        auto call=std::move(found->second);devtools_.erase(found);
        if(call.retain_after_cancel||operations_.Contains(call.owner))call.callback(ok,json);
      }
  }
  void Handle(int owner,Dict params,double deadline,Reply reply) {
    const double now=Now();
    if(closing_page_||(network_fatal_&&network_fatal_->pending())||(!deadline?false:deadline<=now)){reply(false,nullptr,closing_page_?"Native context worker channel closed":"Native context worker original deadline expired");return;}
    const auto operation=operations_.Begin(owner,now,deadline?deadline-now:0);if(!operation){reply(false,nullptr,"Native context worker operation capacity exceeded");return;}
    const auto action=params->GetString("action");if(action=="evaluate"||action=="create"||action=="property")publication_eligible_.insert(owner);
    waiting_.emplace(owner,Waiting{params->Copy(false),std::move(reply)});WakeDeadlines();if(ready_)Run(owner);
  }
  void CookieControl(int owner,suitecut::CookieMethod method,Dict params,double deadline,Reply reply) {
    const auto name=suitecut::NativeCookieMethodName(method);
    if(!name||owner<1||!reply||!params||params->HasKey("browserContextId")||params->HasKey("sessionId")||!std::isfinite(deadline)||deadline<0||closing_page_||(network_fatal_&&network_fatal_->pending())){if(reply)reply(false,nullptr,"Invalid or closed native cookie channel");return;}
    if(deadline&&deadline<=Now()){reply(false,nullptr,"Native cookie original deadline expired before dispatch");return;}
    auto value=CefValue::Create();value->SetDictionary(params->Copy(false));
    if(CefWriteJSON(value,JSON_WRITER_DEFAULT).length()>1048576||cookie_owners_.size()>=64||cookie_owners_.contains(owner)) {reply(false,nullptr,"Native cookie channel capacity exceeded");return;}
    cookie_owners_.insert(owner);cookie_waiting_.emplace(owner,CookieWaiting{method,params->Copy(false),deadline,std::move(reply)});WakeDeadlines();if(ready_)DispatchCookie(owner);
  }
  void DispatchCookie(int owner) {
    const auto found=cookie_waiting_.find(owner);if(found==cookie_waiting_.end()||!ready_||closing_page_)return;
    auto pending=std::move(found->second);cookie_waiting_.erase(found);
    const suitecut::NativeWorkOriginFrame handler_origin(pending.origin.Origin());
    if(pending.deadline&&pending.deadline<=Now()){cookie_owners_.erase(owner);pending.reply(false,nullptr,"Native cookie original deadline expired before dispatch");return;}
    if(pending.method==suitecut::CookieMethod::Filter){Dict evaluation;std::string error;if(!suitecut::BuildCookieFilterEvaluation(pending.params,pending.deadline,Now(),evaluation,error)){cookie_owners_.erase(owner);pending.reply(false,nullptr,error);return;}pending.params=evaluation;}
    // ControlSend retains the physical callback on original deadline expiry;
    // only a real CDP ACK or actual OnBeforeClose retires this owner.
    const int control=ControlSend(suitecut::NativeCookieMethodName(pending.method),pending.params,"",[self=CefRefPtr<Client>(this),owner,reply=std::move(pending.reply)](bool ok,const std::string& json){
      self->cookie_owners_.erase(owner);self->cookie_controls_.erase(owner);
      auto parsed=ok?CefParseJSON(json,JSON_PARSER_RFC):nullptr;auto result=parsed?parsed->GetDictionary():nullptr;
      if(ok&&!result){reply(false,nullptr,"Actual native cookie reply is not an object");return;}
      reply(ok,result,ok?"":json);
    },pending.deadline);
    if(control&&cookie_owners_.contains(owner))cookie_controls_[owner]=control;
  }
  void CancelCookie(int owner){
    if(const auto found=cookie_waiting_.find(owner);found!=cookie_waiting_.end()){
      auto reply=std::move(found->second.reply);cookie_waiting_.erase(found);cookie_owners_.erase(owner);reply(false,nullptr,"Native cookie command canceled before dispatch");return;
    }
    const auto pending=cookie_controls_.find(owner);if(pending==cookie_controls_.end())return;
    const auto control=controls_.find(pending->second);
    // Actual dispatched commands keep their physical completion callback. An
    // unsent job becomes stale in DispatchCurrent when its control is erased.
    if(control==controls_.end()||control->second.dispatched)return;
    auto callback=std::move(control->second.callback);controls_.erase(control);callback(false,"Native cookie command canceled before dispatch");
  }
  void FlushCookieControls(){std::vector<int> owners;for(const auto&[id,pending]:cookie_waiting_)owners.push_back(id);for(int id:owners)DispatchCookie(id);}
  void FailQueuedCookies(const std::string& error){auto pending=std::move(cookie_waiting_);cookie_waiting_.clear();for(auto&[id,item]:pending){cookie_owners_.erase(id);item.reply(false,nullptr,error);}}
  void CookieTick(){std::vector<int> expired;for(const auto&[id,pending]:cookie_waiting_)if(pending.deadline&&pending.deadline<=Now())expired.push_back(id);for(int id:expired){const auto found=cookie_waiting_.find(id);if(found==cookie_waiting_.end())continue;auto reply=std::move(found->second.reply);cookie_waiting_.erase(found);cookie_owners_.erase(id);reply(false,nullptr,"Native cookie original deadline expired before dispatch");}}
  void Run(int owner) {
    const auto request=waiting_.find(owner);if(request==waiting_.end()||!operations_.Contains(owner))return;
    const suitecut::NativeWorkOriginFrame handler_origin(request->second.origin.Origin());
    const auto action=request->second.params->GetString("action").ToString();
    if(action=="waitEvent") {
      const auto kind=request->second.params->GetString("kind").ToString();const auto cursor=suitecut::NativeContextWorkerAfterSequence(request->second.params,event_sequence_);
      if((kind!="service_worker"&&kind!="shared_worker")||!cursor){Complete(owner,false,"Invalid native context worker event wait");return;}
      const auto after=*cursor;
      if(!journal_.empty()&&after<double(journal_.front().sequence)-1){Complete(owner,false,"Native context worker event journal capacity exceeded");return;}
      for(const auto& record:journal_)if(record.sequence>after&&record.event=="worker"){
        auto parsed=CefParseJSON(record.json,JSON_PARSER_RFC);auto info=parsed?parsed->GetDictionary():nullptr;
        if(info&&info->GetDictionary("worker")->GetString("type")==kind){Complete(owner,true,"",record.json);return;}
      }
      event_waiters_[owner]="broker-"+kind;return;
    }
    if(action=="diagnostics"){auto result=CefDictionaryValue::Create();Diagnostics(result);Complete(owner,true,"",Json(result));return;}
    if(action=="console"||action=="claimConsoleArguments"||action=="acknowledgeConsole"||action=="waitEventPredicate"||action=="continueEventPredicate"){ContextConsoleControl(owner,request->second.params);return;}
    WorkerDispatch(owner,request->second.params);
  }
  void Flush() {const auto owners=operations_.Ids();for(int id:owners)Run(id);}
  void Close() {if(closing_page_)return;closing_page_=true;FailQueuedCookies("Native browser context is closing");for(int id:operations_.Ids())Complete(id,false,"Native browser context is closing");if(browser_)browser_->GetHost()->CloseBrowser(true);}
  void Cancel(int owner) {Complete(owner,false,"Native context worker operation canceled");}
  bool Matches(Dict info) const {return ready_&&!chromium_context_.empty()&&info&&info->GetString("browserContextId")==chromium_context_&&(info->GetString("type")=="shared_worker"||info->GetString("type")=="service_worker");}
  void Discover(Dict info) {
    if(!ready_||!Matches(info)||closing_page_)return;
    const auto id=info->GetString("targetId").ToString();
    if(id.empty()||id.size()>128||worker_targets_.contains(id)||attaching_.contains(id)||finished_targets_.contains(id))return;
    if(attaching_.size()+worker_targets_.size()>=1024){Error("Context worker target capacity exceeded");return;}
    attaching_.insert(id);auto params=CefDictionaryValue::Create();params->SetString("targetId",id);params->SetBool("flatten",true);
    ControlSend("Target.attachToTarget",params,"",[this,id](bool ok,const std::string& error){attaching_.erase(id);if(!ok)Error(("Actual context worker attach failed: "+error).c_str());});
  }
  double Now() const{return now_();}
  static std::string Json(Dict dict){auto value=CefValue::Create();value->SetDictionary(dict);return CefWriteJSON(value,JSON_WRITER_DEFAULT);}
  int AllocateDevToolsId(){return next_call_==std::numeric_limits<int>::max()?0:++next_call_;}
  void DeferredFailure(int id,const std::string& error) {
    if(auto found=controls_.find(id);found!=controls_.end()){auto callback=std::move(found->second.callback);controls_.erase(found);callback(false,error);return;}
    if(auto found=worker_calls_.find(id);found!=worker_calls_.end()){auto callback=std::move(found->second.callback);worker_calls_.erase(found);callback(false,error);return;}
    if(auto found=devtools_.find(id);found!=devtools_.end()){auto call=std::move(found->second);devtools_.erase(found);if(call.retain_after_cancel||operations_.Contains(call.owner))call.callback(false,error);return;}
    Error(error.c_str());
  }
  bool DispatchCurrent(const suitecut::BrokerDispatchQueue::Job& job) {
    using Owner=suitecut::BrokerDispatchQueue::Owner;
    if(job.owner==Owner::Control){const auto found=controls_.find(job.id);if(found==controls_.end())return false;if(Now()>=found->second.deadline){found->second.expired=true;network_fatal_->Fail("Native context worker original control deadline exceeded");WakeDeadlines();return false;}found->second.dispatched=true;}
    if(job.owner==Owner::Bootstrap){const auto found=worker_calls_.find(job.id);if(found==worker_calls_.end())return false;if(Now()>=found->second.deadline){DeferredFailure(job.id,"Native context worker original bootstrap deadline expired before dispatch");return false;}}
    if(job.owner==Owner::Operation){const auto found=devtools_.find(job.id);if(found==devtools_.end())return false;const auto operation=operations_.Get(found->second.owner);if(!operation||(operation->deadline_ms>0&&Now()>=operation->deadline_ms)){DeferredFailure(job.id,"Native context worker original operation deadline expired before dispatch");return false;}}
    return true;
  }
  void WakeDispatch() {
    if(dispatch_task_||dispatch_queue_.Empty()||closing_page_)return;
    dispatch_task_=true;
    if(!CefPostTask(TID_UI,new Task([self=CefRefPtr<Client>(this)]{
      // Every broker submission returns to the CEF UI loop before dispatch,
      // including calls from another Source/global network-policy observer.
      for(int count=0;count<64&&!self->dispatch_queue_.Empty();++count){auto job=self->dispatch_queue_.Pop();if(!self->browser_||self->closing_page_)break;if(!self->DispatchCurrent(*job)){self->NativeInspectorNeverDispatched(job->id);continue;}const auto actual_browser=self->browser_;if(!actual_browser){self->NativeInspectorNeverDispatched(job->id);break;}if(!self->NativeInspectorCall(job->id,job->session,[actual_browser,&job]{return actual_browser->GetHost()->SendDevToolsMessage(job->json.data(),job->json.size());}))self->DeferredFailure(job->id,"CEF rejected deferred native context worker command");}
      if(!self->browser_||self->closing_page_)self->dispatch_queue_.Clear();
      self->dispatch_task_=false;self->WakeDispatch();
    }))){
      dispatch_task_=false;
      while(auto job=dispatch_queue_.Pop()){NativeInspectorNeverDispatched(job->id);DeferredFailure(job->id,"CEF rejected native context worker UI dispatch task");}
    }
  }
  bool SubmitDevTools(int id,const std::string& method,Dict params,const std::string& session){
    if(id<1||!browser_||closing_page_)return false;
    auto packet=CefDictionaryValue::Create();packet->SetInt("id",id);packet->SetString("method",method);if(!session.empty())packet->SetString("sessionId",session);if(params)packet->SetDictionary("params",params->Copy(false));
    using Owner=suitecut::BrokerDispatchQueue::Owner;
    const auto owner=controls_.contains(id)?Owner::Control:worker_calls_.contains(id)?Owner::Bootstrap:devtools_.contains(id)?Owner::Operation:Owner::None;
    NativeInspectorReserve(id,session);
    if(!dispatch_queue_.Push(id,Json(packet),owner,session)){
      NativeInspectorNeverDispatched(id);
      // A rejected resume/bootstrap must not leave an author target paused.
      // Closing this actual private debugger channel releases its attachment.
      if(!dispatch_close_task_){dispatch_close_task_=true;CefPostTask(TID_UI,new Task([self=CefRefPtr<Client>(this)]{self->Error("Native context worker dispatch queue capacity exceeded");self->Close();}));}
      return false;
    }
    WakeDispatch();return true;
  }
  #include "controllers/work/accepted_inspector_owner.inc"
  int SendDevTools(const std::string& method,Dict params=nullptr,const std::string& session="") {const int id=AllocateDevToolsId();return id&&SubmitDevTools(id,method,params,session)?id:0;}
  int ControlSend(const std::string& method,Dict params,const std::string& session,DevToolsCallback callback,double original_deadline=0){
    const int id=AllocateDevToolsId();if(!id||controls_.size()>=128){callback(false,"Native context worker control capacity exceeded");return 0;}
    controls_.emplace(id,Control{session,original_deadline?original_deadline:Now()+30000,suitecut::BindNativeLocatorHandlerAsync(std::move(callback))});WakeDeadlines();
    if(!SubmitDevTools(id,method,params,session)){auto call=std::move(controls_.at(id));controls_.erase(id);call.callback(false,"CEF rejected native context worker command");return 0;}
    return id;
  }
  void AutoAttach(const std::string& session){auto params=CefDictionaryValue::Create();params->SetBool("autoAttach",true);params->SetBool("waitForDebuggerOnStart",true);params->SetBool("flatten",true);SendDevTools("Target.setAutoAttach",params,session);}
  void DevTools(int owner,const std::string& method,Dict params,DevToolsCallback callback,std::optional<std::string> session=std::nullopt,bool retained=false,size_t limit=16*1048576,bool lossless_scalars=false){
    if(limit>16*1048576||!operations_.Contains(owner))return;
    const auto handler=waiting_.find(owner);auto actual=suitecut::CurrentNativeWorkOrigin();actual.locator_handler_invocation=0;
    const suitecut::NativeWorkOriginFrame handler_origin(handler==waiting_.end()?actual:handler->second.origin.Origin());
    callback=suitecut::BindNativeLocatorHandlerAsync(std::move(callback));
    const int id=AllocateDevToolsId();
    if(!id){Complete(owner,false,"Context worker DevTools ID exhausted");return;}
    const auto target=session.value_or(operation_sessions_[owner]);devtools_.emplace(id,DevToolsCall{owner,std::move(callback),target,retained,lossless_scalars});
    if(!SubmitDevTools(id,method,params,target)){devtools_.erase(id);Complete(owner,false,"CEF rejected context worker operation");}
  }
  void Complete(int owner,bool ok,const std::string& error="",const std::string& json=""){
    const auto handler=waiting_.find(owner);auto actual=suitecut::CurrentNativeWorkOrigin();actual.locator_handler_invocation=0;
    const suitecut::NativeWorkOriginFrame handler_origin(handler==waiting_.end()?actual:handler->second.origin.Origin());
    if(ok&&!closing_page_&&network_&&publication_eligible_.contains(owner)){
      const auto operation=operations_.Get(owner);const auto cursor=network_->QueuedPublication();
      if(operation&&cursor>network_->PublishedPublication()){
        if(publication_results_.Hold(owner,operation->generation,cursor,json)!=suitecut::NativeNetworkPublicationResults::Admission::Full){WakeDeadlines();return;}
        Complete(owner,false,"Native worker deferred network-publication results exceed 64 owners or 16 MiB");return;
      }
    }
    publication_results_.Cancel(owner);publication_eligible_.erase(owner);
    if(!operations_.Finish(owner))return;
    WorkerCancel(owner);event_predicates_.Cancel(owner);event_waiters_.erase(owner);operation_sessions_.erase(owner);
    for(auto it=devtools_.begin();it!=devtools_.end();)if(it->second.owner==owner&&!it->second.retain_after_cancel)it=devtools_.erase(it);else++it;
    const auto found=waiting_.find(owner);if(found==waiting_.end())return;auto reply=std::move(found->second.reply);waiting_.erase(found);
    auto parsed=json.empty()?nullptr:CefParseJSON(json,JSON_PARSER_RFC);auto value=parsed?parsed->GetDictionary():nullptr;
    // Arrays/primitive operation results are carried intact in this envelope.
    if(parsed&&!value){value=CefDictionaryValue::Create();value->SetValue("result",parsed);}
    reply(ok,value,error);
  }
  Dict Header(const char* type){auto result=CefDictionaryValue::Create();result->SetString("type",type);return result;}
  void SendPacket(Dict,const std::vector<uint8_t>& payload){
    auto parsed=CefParseJSON(std::string(payload.begin(),payload.end()),JSON_PARSER_RFC);auto event=parsed?parsed->GetDictionary():nullptr;if(event)if(auto owner=owner_.lock())owner->hooks_.event(context_,event);
  }
  void JournalEvent(uint64_t sequence,const std::string& event,const std::string& json){
    while(!journal_.empty()&&(journal_.size()>=128||journal_bytes_+json.size()>4*1048576)){journal_bytes_-=journal_.front().json.size();journal_.pop_front();}
    if(json.size()>1048576){Error("Native context worker journal payload exceeds limit");return;}
    journal_.push_back({sequence,event,json});journal_bytes_+=json.size();
    for(const auto scope:event_predicates_.Active())PollContextPredicate(scope.owner,scope.generation);
    if(event=="worker"){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto info=parsed?parsed->GetDictionary():nullptr;auto worker=info?info->GetDictionary("worker"):nullptr;if(!worker)return;
      std::vector<int> completed;for(const auto& [id,name]:event_waiters_)if(name=="broker-"+worker->GetString("type").ToString()&&waiting_.contains(id)&&waiting_.at(id).params->GetDouble("afterSequence")<double(sequence))completed.push_back(id);
      for(int id:completed)Complete(id,true,"",json);
    }
  }
  void Error(const char* message){if(auto owner=owner_.lock())owner->hooks_.error(context_,message);for(int id:operations_.Ids())Complete(id,false,message);}
  void WakeDeadlines(){if(timer_||closing_page_)return;timer_=true;CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Client>(this)]{self->Tick();}),10);}
  void Tick(){timer_=false;if(closing_page_)return;CookieTick();for(int id:operations_.Expired(Now()))Complete(id,false,"Native context worker original deadline expired");WorkerTick();if(request_reads_)request_reads_->Tick(Now());if(image_streams_)image_streams_->Tick(Now());if(network_)network_->Tick(Now());
    if(network_){auto ready=publication_results_.Ready(network_->PublishedPublication(),[this](int owner,uint64_t generation){return operations_.IsCurrent(owner,generation)&&!operations_.IsExpired(owner,generation,Now());});for(auto& result:ready){publication_eligible_.erase(result.owner);Complete(result.owner,true,"",result.json);}}

    if(network_fatal_)network_fatal_->Tick();
    for(auto&[id,control]:controls_)if(!control.expired&&Now()>=control.deadline){control.expired=true;network_fatal_->Fail("Native context worker original control deadline exceeded");}
    if((network_fatal_&&network_fatal_->pending())||!cookie_waiting_.empty()||operations_.Size()||WorkerNeedsTick()||!controls_.empty()||(image_streams_&&image_streams_->NeedsTick())||(network_&&network_->NeedsTick()))WakeDeadlines();}
  void Diagnostics(Dict output) const {output->SetString("rootTargetId",root_target_id_);output->SetString("controlOwner",handle_namespace_);output->SetString("eventCursor",std::to_string(event_sequence_));output->SetString("initialEventCursor",std::to_string(cursor_.initial));output->SetInt("pendingImageStreamCommands",image_streams_?int(image_streams_->physical()):0);output->SetInt("imageStreamEntries",image_streams_?int(image_streams_->entries()):0);output->SetDouble("imageStreamBytes",image_streams_?double(image_streams_->bytes()):0);output->SetInt("pendingRequestBodyReads",request_reads_?static_cast<int>(request_reads_->physical()):0);output->SetBool("networkFatalPending",network_fatal_&&network_fatal_->pending());output->SetBool("networkCloseRequested",network_fatal_&&network_fatal_->requested());output->SetInt("pendingNetworkPublicationResults",int(publication_results_.size()));output->SetDouble("pendingNetworkPublicationResultBytes",double(publication_results_.bytes()));output->SetInt("cookieChannelOwners",int(cookie_owners_.size()));output->SetInt("cookieQueuedCommands",int(cookie_waiting_.size()));output->SetInt("networkPhysicalControls",int(controls_.size()));if(network_)network_->Diagnostics(output);output->SetInt("targets",static_cast<int>(worker_targets_.size()));output->SetInt("handles",static_cast<int>(worker_handles_.size()));output->SetInt("physical",static_cast<int>(worker_physical_));output->SetString("chromiumContextId",chromium_context_);output->SetInt("queuedCommands",int(dispatch_queue_.Count()));output->SetDouble("queuedCommandBytes",double(dispatch_queue_.Bytes()));output->SetString("runtimeStyle",runtime_style_==CEF_RUNTIME_STYLE_ALLOY?"alloy":runtime_style_==CEF_RUNTIME_STYLE_CHROME?"chrome":"default");}
  bool AcceptedWorkReady()const{
    return browser_&&ready_&&!closing_page_&&!operations_.Size()&&waiting_.empty()&&event_waiters_.empty()&&
      cookie_waiting_.empty()&&cookie_owners_.empty()&&cookie_controls_.empty()&&controls_.empty()&&
      devtools_.empty()&&dispatch_queue_.Empty()&&!dispatch_task_&&!dispatch_close_task_&&attaching_.empty()&&
      !worker_physical_&&!WorkerNeedsTick()&&!publication_results_.size()&&
      (!request_reads_||!request_reads_->physical())&&(!response_reads_||!response_reads_->physical())&&
      (!image_streams_||!image_streams_->physical())&&(!network_||!network_->NeedsTick())&&
      (!network_fatal_||(!network_fatal_->pending()&&!network_fatal_->requested()));
  }
  int context() const{return context_;}
 private:
  std::string network_policy_target_;
  std::weak_ptr<SuiteCutContextWorkers> owner_;int context_;CefRefPtr<CefRequestContext> request_context_;
  std::function<double()> now_;
  const std::string handle_namespace_;inline static uint64_t namespaces_=0;
  const suitecut::NativeContextWorkerCursors::Lease cursor_;
  CefRefPtr<CefBrowser> browser_;CefRefPtr<CefRegistration> observer_;
  cef_runtime_style_t runtime_style_=CEF_RUNTIME_STYLE_DEFAULT;
  suitecut::BrokerDispatchQueue dispatch_queue_;bool dispatch_task_=false,dispatch_close_task_=false;
  size_t diagnostic_messages_=0;
  std::string chromium_context_,root_target_id_;bool ready_=false,closing_page_=false,timer_=false;int next_call_=0,next_handle_=0;
  uint64_t event_sequence_=0;SuiteCutOperationRegistry operations_;
  std::unique_ptr<suitecut::NativeNetworkFatalTeardown> network_fatal_;
  std::unique_ptr<suitecut::NativeRequestBodyReads> request_reads_;
  std::unique_ptr<suitecut::NativeResponseBodyReads> response_reads_;
  std::unique_ptr<suitecut::NativeResponseStreams> image_streams_;
  std::unique_ptr<suitecut::NetworkControls> network_;
  std::map<int,Waiting> waiting_;std::map<int,std::string> operation_sessions_,event_waiters_;
  suitecut::NativeNetworkPublicationResults publication_results_;std::set<int> publication_eligible_;
  std::map<int,CookieWaiting> cookie_waiting_;std::set<int> cookie_owners_;std::map<int,int> cookie_controls_;
  std::map<int,Control> controls_;std::set<std::string> attaching_,finished_targets_;
  std::deque<Record> journal_;size_t journal_bytes_=0;
  #include "controllers/workers/worker_controller.inc"
  static_assert(suitecut::NativeContextWorkerCursors::kTargets==kWorkerTargets);
  std::map<std::string,WorkerTarget> detached_targets_;
  #include "controllers/workers/context_worker_console_refs.inc"
  #include "controllers/workers/context_worker_console.inc"
  std::map<int,DevToolsCall> devtools_;
  IMPLEMENT_REFCOUNTING(Client);
};
SuiteCutContextWorkers::SuiteCutContextWorkers(Hooks hooks):hooks_(std::move(hooks)){}
SuiteCutContextWorkers::~SuiteCutContextWorkers(){Clear();}
void SuiteCutContextWorkers::Ensure(int context){if(closing_||clients_.contains(context))return;const auto request=hooks_.context(context);if(!request)return;const auto cursor=cursors_.Acquire(context);if(!cursor){hooks_.error(context,"Native context worker cursor history capacity exceeded or physical owner remains active");return;}auto client=CefRefPtr<Client>(new Client(weak_from_this(),context,request,*cursor));clients_.emplace(context,client);if(!client->Create()){cursors_.Uncreated(*cursor);clients_.erase(context);hooks_.error(context,"CEF rejected private context worker browser");}}
void SuiteCutContextWorkers::Handle(int owner,int context,Dict params,double deadline,Reply reply){
  if(params&&params->GetString("action")=="waitOwnerClosed"){
    // Observing closure must never create or replace the owner being observed.
    const auto found=clients_.find(context);
    if(closing_||found==clients_.end()||params->GetType("controlOwner")!=VTYPE_STRING||params->GetString("controlOwner").length()>256||params->GetString("controlOwner")!=found->second->control_owner()){
      reply(false,nullptr,"Exact active native worker control owner is unavailable");return;
    }
    const auto control=found->second->control_owner();
    if(!close_waits_.Admit(owner,context,found->second->cursor().token,deadline,hooks_.now(),
      [context,control,reply](bool ok,uint64_t cursor,const std::string&error){
        auto result=CefDictionaryValue::Create();
        if(ok){result->SetInt("contextId",context);result->SetString("controlOwner",control);result->SetString("finalEventCursor",std::to_string(cursor));}
        reply(ok,ok?result:nullptr,error);
      }))reply(false,nullptr,"Native worker owner-close wait expired or exceeds capacity");
    return;
  }
  Ensure(context);const auto found=clients_.find(context);if(found==clients_.end()){reply(false,nullptr,"Native worker context is closed or unavailable");return;}found->second->Handle(owner,params,deadline,std::move(reply));
}
void SuiteCutContextWorkers::WebSocketPolicyReady(int context,NetworkResult ready){Ensure(context);const auto found=clients_.find(context);if(found==clients_.end()){ready(false,"Native WebSocket broker context is unavailable");return;}found->second->WebSocketPolicyReady(std::move(ready));}
void SuiteCutContextWorkers::CookieControl(int owner,int context,suitecut::CookieMethod method,Dict params,double deadline,Reply reply){Ensure(context);const auto found=clients_.find(context);if(found==clients_.end()){reply(false,nullptr,"Native cookie context is closed or unavailable");return;}found->second->CookieControl(owner,method,params,deadline,std::move(reply));}
bool SuiteCutContextWorkers::NetworkBody(int context,const std::string&owner,const std::string&body,const std::string&stage){const auto found=clients_.find(context);return found!=clients_.end()&&found->second->NetworkBody(owner,body,stage);}
bool SuiteCutContextWorkers::NetworkPolicyFailure(const std::string&owner,const std::string&error){for(const auto&[context,client]:clients_)if(client->NetworkPolicyFailure(owner,error))return true;return false;}
void SuiteCutContextWorkers::CancelCookie(int owner){for(const auto&[context,client]:clients_)client->CancelCookie(owner);}
void SuiteCutContextWorkers::Cancel(int owner){close_waits_.Cancel(owner);for(const auto& [context,client]:clients_)client->Cancel(owner);}
void SuiteCutContextWorkers::CloseContext(int context){const auto found=clients_.find(context);if(found!=clients_.end())found->second->Close();}
void SuiteCutContextWorkers::ContextGone(int context){if(!cursors_.ContextGone(context))hooks_.error(context,"Native context worker context-gone preceded actual broker close");}
void SuiteCutContextWorkers::Clear(){closing_=true;std::vector<CefRefPtr<Client>> clients;for(const auto& [context,client]:clients_)clients.push_back(client);close_waits_.Clear();for(auto& client:clients)client->Close();}
bool SuiteCutContextWorkers::ContextClosed(int context) const{return !clients_.contains(context);}
size_t SuiteCutContextWorkers::BrowserCount() const{return clients_.size();}
void SuiteCutContextWorkers::Closed(Client* client){
  const auto context=client->context();const auto lease=client->cursor();const auto cursor=client->final_event_sequence();
  const auto found=clients_.find(context);
  if(found!=clients_.end()&&found->second.get()==client){
    const bool accepted=cursors_.Closed(lease,cursor);clients_.erase(found);
    close_waits_.Closed(context,lease.token,cursor,hooks_.now(),accepted);
    if(!accepted)hooks_.error(context,"Native context worker final physical cursor rejected");
  }
  if(hooks_.idle)hooks_.idle(context);
}
void SuiteCutContextWorkers::Diagnostics(Dict output) const {output->SetInt("nativeWorkerOwnerCloseWaits",int(close_waits_.size()));output->SetInt("nativeWorkerCursorContexts",int(cursors_.size()));output->SetInt("nativeWorkerBrokers",static_cast<int>(clients_.size()));auto entries=CefListValue::Create();for(const auto& [context,client]:clients_){auto info=CefDictionaryValue::Create();info->SetInt("contextId",context);client->Diagnostics(info);entries->SetDictionary(entries->GetSize(),info);}output->SetList("nativeWorkerBrokerContexts",entries);}

bool SuiteCutContextWorkers::AcceptedWorkReady()const{
  if(closing_||close_waits_.size())return false;
  for(const auto&[id,client]:clients_){(void)id;if(!client->AcceptedWorkReady())return false;}
  return true;
}
