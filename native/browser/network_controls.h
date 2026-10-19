// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
#include "network_glob.h"
#include "route_registry.h"
#include "route_matcher_selections.h"
#include "network_preflights.h"
#include <charconv>
#include "route_callback_leases.h"
#include "network_body_controls.h"
#include "network_post_preparation.h"
#include "network_response_extras.h"
#include "network_response_details.h"
#include "network_authentication.h"
#include "context_network_headers.h"
#include "har_route_registration.h"
#include "har_fetch_work.h"
#include "page_route_removals.h"
#include "route_fetch_policy.h"

namespace suitecut {
// Per-page UI-thread network state. No browser-control work is delegated to a
// language SDK: Fetch interception, matching, deadlines and ownership live here.
class NetworkControls {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Result = std::function<void(bool, const std::string&)>;
  using Send = std::function<void(const std::string&, const std::string&, Dict, Result)>;
  using BoundedSend = std::function<void(const std::string&,const std::string&,Dict,Result,double)>;
  void SetBoundedSender(BoundedSend send){bounded_send_=std::move(send);}
  using Done = std::function<void(bool, const std::string&, const std::string&)>;
  using RouteFetch=std::function<void(int,const std::string&,const std::string&,Dict,Dict,double,std::shared_ptr<void>,std::function<bool(bool,Dict,const std::string&)>)>;
  void SetRouteFetch(RouteFetch fetch,std::function<void(int)> cancel){route_fetch_=std::move(fetch);cancel_route_fetch_=std::move(cancel);}
  using Emit = std::function<void(Dict)>;
  NetworkControls(Send send, Emit emit, std::function<void(const std::string&)> failure,
                  std::function<void()> wake, std::function<double()> now,
                  NativeNetworkBodyPublication::Emit body_emit={})
      : send_(std::move(send)), emit_(std::move(emit)), failure_(std::move(failure)),
        wake_(std::move(wake)), now_(std::move(now)) {
    page_removals_=std::make_unique<NativePageRouteRemovals>(callback_scope_,now_,
      [this](bool applied,const std::string&mutation,const std::vector<std::string>&ids,const NativePageRouteCallbackSelection&selected){
        auto event=CefDictionaryValue::Create();event->SetString("event","routemutation");event->SetString("phase",applied?"applied":"admitted");event->SetString("mutationId",mutation);event->SetString("callbackScope",std::to_string(selected.scope));
        auto registrations=CefListValue::Create();for(size_t index=0;index<ids.size();++index)registrations->SetString(index,ids[index]);event->SetList("registrationIds",registrations);
        auto tokens=CefListValue::Create();for(size_t index=0;index<selected.tokens.size();++index)tokens->SetString(index,std::to_string(selected.tokens[index]));event->SetList("callbackTokens",tokens);
        // Native control receipts precede their result without entering the
        // resource-body gate or evicting the observable network event journal.
        if(disposed_)return;
        if(sequence_>=9007199254740991ULL){failure_("Native network publication sequence exhausted");return;}
        event->SetDouble("sequence",static_cast<double>(++sequence_));emit_(event);
      });
    matchers_=std::make_unique<NativeRouteMatcherSelections>(routes_,GlobalRouteCallbackLeases(),callback_scope_,[this](const auto&){failure_("Native URL matcher has no actual pause projection");});
    authentication_=std::make_unique<NativeNetworkAuthentication>(send_,failure_,wake_);
    preflights_=std::make_unique<NativeNetworkPreflights>(
      [this](const std::string&session,const std::string&method,Dict params,Result done,double deadline){SendControl(session,method,params,std::move(done),deadline);},
      [this](Dict data,const std::string&session,NativeRouteRegistry::Plan plan,double deadline){PauseReady(data,session,std::move(plan),deadline);},failure_,wake_,now_,[this]{MaybeReleaseAdmittedPolicy();});
    request_extras_=std::make_unique<NativeNetworkResponseExtras>(
      [this](const std::string&id,Dict headers){auto request=FindLogical(id);if(!request)return;request->info->SetDictionary("headers",headers->Copy(false));request->request_headers_ready=true;if(!RetainMetadata(*request))return;Publish(BaseEvent("requestupdated",*request));FinishWaiters(*request);},
      [this](const std::string&id,const std::string&error){auto request=FindLogical(id);if(!request||!request->request_headers_expected)return;request->request_header_error=error;if(!RetainMetadata(*request))return;Publish(BaseEvent("requestupdated",*request));FinishWaiters(*request);},failure_,"request",4194304);
    response_extras_=std::make_unique<NativeNetworkResponseExtras>(
      [this](const std::string&id,Dict headers){auto request=FindLogical(id);if(!request||!request->response)return;request->response->SetDictionary("headers",headers->Copy(false));request->response->SetBool("_nativeHeadersObserved",true);request->response_headers_ready=true;if(!RetainMetadata(*request))return;Publish(BaseEvent("requestupdated",*request));FinishWaiters(*request);},
      [this](const std::string&id,const std::string&error){auto request=FindLogical(id);if(!request)return;request->response_header_error=error;if(!RetainMetadata(*request))return;Publish(BaseEvent("requestupdated",*request));FinishWaiters(*request);},failure_);

    if(body_emit)bodies_=std::make_unique<NativeNetworkBodyControls>(std::move(body_emit),
      [this](Dict event){PublishReady(event);},[this](const std::string& error){failure_(error);});
  }

  uint64_t QueuedPublication() const{return bodies_?bodies_->publication().QueuedCursor():0;}
  uint64_t PublishedPublication() const{return bodies_?bodies_->publication().PublishedCursor():0;}
  void SetObserver(Emit observer,std::function<bool()> active={}){observer_=std::move(observer);observer_active_=std::move(active);}
  using PostRead=NativeNetworkPostPreparation::Read;
  using PostReserve=NativeNetworkPostPreparation::Reserve;
  void SetRequestBodyReader(PostRead read,PostReserve reserve={}) {
    if(disposed_||post_reads_||!bodies_||!read)return;
    post_reads_=std::make_unique<NativeNetworkPostPreparation>(bodies_->publication(),std::move(read),std::move(reserve),
      [this](const std::string&id,Dict fields,bool available,const std::vector<uint8_t>&bytes,bool text){
        if(!available||fields->GetBool("postDataUnavailable")){const auto known=FindLogical(id);if(known&&!known->info->GetBool("postDataUnavailable")&&(known->info->HasKey("postData")||known->info->HasKey("postDataBase64")||known->info->HasKey("postDataBodyId"))){fields->Clear();for(const char*name:{"postData","postDataBase64","postDataBodyId"})if(known->info->HasKey(name))fields->SetValue(name,known->info->GetValue(name)->Copy());}}
        const auto update=[&](Request&request){for(const char*name:{"postData","postDataBase64","postDataBodyId","postDataUnavailable"}){request.info->Remove(name);if(fields->HasKey(name))request.info->SetValue(name,fields->GetValue(name)->Copy());}};
        if(auto request=FindLogical(id)){update(*request);if(!RetainMetadata(*request))return;}
        std::vector<std::string> resume;
        for(auto&[key,pause]:paused_)if(pause.request.logical_id==id){
          pause.har_post_attempted=true;if(fields->HasKey("postDataBase64"))pause.public_prepared=false;auto staged=pause.request;staged.info=pause.request.info->Copy(false);update(staged);auto har=pause.har_info?pause.har_info->Copy(false):nullptr;
          if(har){for(const char*name:{"postData","postDataBase64","postDataBodyId","postDataEntries","postDataUnavailable"})har->Remove(name);if(available){if(text)har->SetString("postData",std::string(bytes.begin(),bytes.end()));else har->SetString("postDataBase64",CefBase64Encode(bytes.data(),bytes.size()));}else har->SetBool("postDataUnavailable",true);}
          auto fetch_body=pause.fetch_body;if(fields->HasKey("postDataBodyId")&&available)fetch_body=std::make_shared<const std::vector<uint8_t>>(bytes);
          const auto cost=(fetch_body?fetch_body->capacity():0)+pause.plan.bytes()+pause.original_url.capacity()+Json(staged.info).size()+Json(pause.overrides).size()+(har?Json(har).size():0);if(cost>16777216-(pause_metadata_bytes_-pause.metadata_bytes)){failure_("Native asynchronous request body pause metadata capacity exceeded");return;}pause_metadata_bytes_=pause_metadata_bytes_-pause.metadata_bytes+cost;pause.metadata_bytes=cost;pause.request=std::move(staged);pause.har_info=std::move(har);pause.fetch_body=std::move(fetch_body);
          if(pause.replay_done)resume.push_back(key);
        }
        if(const auto current=FindLogical(id))Publish(BaseEvent("requestupdated",*current));
        for(const auto&key:resume){const auto found=paused_.find(key);if(found==paused_.end())continue;auto done=std::move(found->second.replay_done);ReplayPaused(key,"",std::move(done));}
      });
  }
  ~NetworkControls() {CancelAllRouteFetches();GlobalHARWorkLeases().ObserverUnavailable(callback_scope_);*alive_=false;if(post_reads_)post_reads_->Clear();GlobalRouteCallbackLeases().Close(callback_scope_);}

  static std::string Json(Dict value) {
    auto wrapper = CefValue::Create(); wrapper->SetDictionary(value);
    return CefWriteJSON(wrapper, JSON_WRITER_DEFAULT).ToString();
  }
  // Glob grammar: * matches within a path component, ** across separators,
  // ? is a literal query separator. Patterns must match the entire URL.
  static bool Matches(const std::string& pattern, const std::string& url) {
    return NativeURLGlobMatches(pattern,url);
  }
  using AuthProvider=NativeNetworkAuthentication::Provider;
  void SetAuthentication(AuthProvider provider,Result done) {
    if(disposed_||authentication_->failed()||authentication_->pending()){done(false,"Native authentication owner is closed or has pending challenges");return;}
    authentication_->Set(std::move(provider));
    ConfigureAll([done=std::move(done)](bool ok,const std::string&error,const std::string&){done(ok,error);});
  }
  bool BindReplay(const std::string&id,const std::string&pattern,std::shared_ptr<const NativeHARReplay> replay,bool fallback,Result done,double deadline=0,bool context=false,NativeRouteRegistrationOrder order={}) {
    return BindReplayPolicy(id,pattern,NativeHARRouteRegistration::Create(std::move(replay),fallback,order),std::move(done),deadline,context);
  }
  bool BindReplayPolicy(const std::string&id,const std::string&pattern,NativeHARRouteRegistration::Policy policy,Result done,double deadline=0,bool context=false) {
    if(disposed_||!policy||har_replays_.size()>=16||policy->archive()->bytes()>67108864-har_replay_bytes_||!routes_.Register(id,pattern,0,context,false,policy->order())){done(false,"Invalid or excessive native HAR route");return false;}
    har_replay_bytes_+=policy->archive()->bytes();har_replays_.emplace(id,policy);
    ConfigureAll([this,id,policy,done=std::move(done)](bool ok,const std::string&error,const std::string&)mutable{
      auto actual=std::move(policy);
      if(!ok){const auto current=har_replays_.find(id);if(current!=har_replays_.end()&&current->second==actual){DropReplay(id);routes_.Remove(id);}ConfigureAll([this,policy=actual](bool rolled,const std::string&reason,const std::string&)mutable{policy.reset();if(!rolled&&!disposed_)failure_("Could not roll back native HAR route: "+reason);});}
      done(ok,error);
    },deadline);return true;
  }
  bool BindContextRoute(std::shared_ptr<NativeContextRouteRegistration> policy,Result done,double deadline=0) {
    if(disposed_||!policy||!policy->available()||!routes_.RegisterContext(policy)){done(false,"Invalid or excessive native context route binding");return false;}
    const auto id=policy->id();
    ConfigureAll([this,id,done=std::move(done)](bool ok,const std::string&error,const std::string&){
      if(!ok){routes_.Remove(id);ConfigureAll([this](bool restored,const std::string&reason,const std::string&){if(!restored&&!disposed_)failure_("Could not roll back native context route: "+reason);});}
      done(ok,error);
    },deadline);return true;
  }
  void UnbindContextRoute(const std::string&id,Result done,double deadline=0) {
    routes_.Remove(id);ConfigureAll([done=std::move(done)](bool ok,const std::string&error,const std::string&){done(ok,error);},deadline);
  }
  void UnbindContextRoutePolicy(const std::shared_ptr<NativeContextRouteRegistration>&policy,Result done,double deadline){
    if(!policy){done(false,"Native context removal lacks its original policy");return;}
    if(deadline&&now_()>=deadline){routes_.RemoveContext(policy);done(false,"Native context route removal original deadline exceeded");return;}
    // A queued old removal must not remove a newly installed same-ID policy.
    // No native submission is needed if this exact original is already absent.
    if(!routes_.RemoveContext(policy)){done(true,"");return;}
    ConfigureAll([this,deadline,done=std::move(done)](bool ok,const std::string&error,const std::string&){if(ok&&deadline&&now_()>=deadline)done(false,"Native context removal original deadline exceeded");else done(ok,error);},deadline);
  }
  void UnbindReplayPolicy(const std::string&id,const NativeHARRouteRegistration::Policy&policy,Result done,double deadline){
    if(!policy){done(false,"Native HAR removal lacks its original policy");return;}
    const auto found=har_replays_.find(id);if(found==har_replays_.end()||found->second!=policy){const bool expired=deadline&&now_()>=deadline;done(!expired,expired?"Native HAR removal original deadline exceeded":"");return;}
    DropReplay(id);routes_.Remove(id);
    if(deadline&&now_()>=deadline){done(false,"Native HAR removal original deadline exceeded");return;}
    ConfigureAll([this,deadline,done=std::move(done)](bool ok,const std::string&error,const std::string&){if(ok&&deadline&&now_()>=deadline)done(false,"Native context removal original deadline exceeded");else done(ok,error);},deadline);
  }
  void UnbindReplay(const std::string&id,Result done) {DropReplay(id);routes_.Remove(id);ConfigureAll([done=std::move(done)](bool ok,const std::string&error,const std::string&){done(ok,error);});}
  void SessionAttached(const std::string& session,Result done={},bool fetch_available=true) {
    if(authentication_->failed()){if(done)done(false,"Native network owner awaits physical teardown");return;}
    if(disposed_){if(done)done(false,"Native page is closed");return;}
    if(!fetch_available)network_only_sessions_.insert(session);
    if (!sessions_.insert(session).second) {if(done)done(true,"");return;}
    struct Installation {size_t pending;bool success=true;std::string error;Result done;};
    const size_t pending=static_cast<size_t>(!routes_.empty()||authentication_->enabled())+static_cast<size_t>(!!headers_||!!context_headers_)+static_cast<size_t>(offline_);
    if(!done)done=[this](bool ok,const std::string& error){if(!ok && !disposed_)failure_("Could not initialize native network target: "+error);};
    if(!pending){done(true,"");return;}
    auto installation=std::make_shared<Installation>(Installation{pending,true,"",std::move(done)});
    const auto acknowledge=[this,session,installation](bool ok,const std::string& error){
      if(disposed_)return;
      if(!ok || !sessions_.contains(session)){installation->success=false;installation->error=ok?"Native network target detached":error;}
      if(!--installation->pending)installation->done(installation->success,installation->error);
    };
    if (!routes_.empty()||authentication_->enabled()) ConfigureFetch(session, acknowledge);
    if (headers_||context_headers_) { auto params=CefDictionaryValue::Create();params->SetDictionary("headers",EffectiveHeaders());
      send_(session,"Network.setExtraHTTPHeaders",params,acknowledge); }
    if (offline_) ApplyOffline(session, true, acknowledge);
  }
  // Chromium emits a worker script request in its parent session, then its
  // real response/completion in the attached target. Preserve its logical ID
  // while migrating physical response/body ownership using actual target ID.
  bool WorkerScriptAttached(const std::string& target,const std::string& parent,const std::string& child,const std::string& url) {
    if(disposed_ || target.empty() || child.empty() || target.size()>128 || child.size()>128 || parent.size()>128 || child==parent)return false;
    if(worker_parents_.size()>=1024 && !worker_parents_.contains(child))return false;
    worker_parents_[child]=parent;authentication_->NetworkMoved(target,parent,child);
    const auto old_key=parent+"\n"+target,new_key=child+"\n"+target;
    const auto found=requests_.find(old_key);
    if(found==requests_.end() || found->second.finished || found->second.url!=url || requests_.contains(new_key))return false;
    auto record=requests_.extract(found);record.key()=new_key;record.mapped().session=child;
    requests_.insert(std::move(record));for(auto& key:order_)if(key==old_key)key=new_key;if(!request_extras_->Move(old_key,new_key,now_()+30000)||!response_extras_->Move(old_key,new_key,now_()+30000))return false;
    Publish(BaseEvent("requestupdated",requests_.at(new_key)));return true;
  }
  using SessionHandoff=std::function<void(const std::string&,const std::string&,const std::string&)>;
  void SetSessionHandoff(SessionHandoff handoff,std::function<bool(const std::string&,const std::string&)> descendant={}) {
    session_handoff_=std::move(handoff);frame_descendant_=std::move(descendant);
  }
  // Only genuine iframe Target.attachedToTarget identities enter this graph.
  // Network IDs are matched exactly and uniquely inside the observed frame scope.
  bool FrameSessionAttached(const std::string& child,const std::string& parent,const std::string& frame) {
    PruneFrameSessions();
    if(child.empty()||child.size()>128||parent.size()>128||frame.empty()||frame.size()>256||child==parent||
       (frame_sessions_.size()>=1024&&!frame_sessions_.contains(child))){failure_("Invalid or excessive native network frame session identity");return false;}
    frame_sessions_[child]={parent,frame,false};return true;
  }
  void FrameRemoved(const std::string& frame) {
    std::vector<std::string> failed;
    for(const auto&[key,request]:requests_)if(!request.finished&&request.frame==frame)failed.push_back(key);
    for(const auto&key:failed){auto&request=requests_.at(key);request.finished=true;request_extras_->Complete(key);response_extras_->Complete(key);request.error="Frame detached";
      auto event=BaseEvent("requestfailed",request);event->SetString("errorText",request.error);Publish(event);FinishWaiters(request);}
    PruneFrameSessions();
  }
  // Called only at the real target-detach boundary, before its retained
  // transport callbacks report closure. It does not settle a sent receipt.
  void InterceptionSessionClosed(const std::string&session){matchers_->CancelOwner(session);preflights_->SessionClosed(session);}
  // Real request-context loss invalidates fetch IO without inventing a
  // transport close for its still-live interception/pause channel.
  void RequestSessionContextDestroyed(const std::string& session) {
    std::vector<int> fetches;
    for(const auto&[owner,pending]:route_fetch_pending_){const auto pause=paused_.find(pending.route);if(pause!=paused_.end()&&pause->second.request.session==session)fetches.push_back(owner);}
    for(const int owner:fetches)CancelRouteFetch(owner);
  }
  void SessionDetached(const std::string& session) {
    InterceptionSessionClosed(session);
    authentication_->SessionDetached(session);
    // A worker request may be intercepted on its real parent target. Its
    // request-session death cancels HTTP IO without pretending that parent
    // interception/HAR channel has physically closed.
    std::vector<int> worker_fetches;
    for(const auto&[owner,pending]:route_fetch_pending_){const auto pause=paused_.find(pending.route);if(pause!=paused_.end()&&pause->second.request.session==session&&pause->second.session!=session)worker_fetches.push_back(owner);}
    for(const int owner:worker_fetches)CancelRouteFetch(owner);
    const auto frame_session=frame_sessions_.find(session);if(frame_session!=frame_sessions_.end())frame_session->second.detached=true;
    sessions_.erase(session);network_only_sessions_.erase(session);worker_parents_.erase(session);if(frame_session==frame_sessions_.end()){request_extras_->SessionClosed(session);response_extras_->SessionClosed(session);}
    for(auto it=rejected_.begin();it!=rejected_.end();) {
      if(it->second.session==session)it=rejected_.erase(it);else ++it;
    }
    for (auto it=paused_.begin();it!=paused_.end();) {
      if (it->second.session==session) {if(it->second.har_work)it->second.har_work->ActualChannelClosed();GlobalRouteCallbackLeases().Revoke(callback_scope_,it->first);upload_bytes_-=it->second.body.size();pause_metadata_bytes_-=it->second.metadata_bytes;CancelRouteFetches(it->first);it=paused_.erase(it);} else ++it;
    }
    std::vector<std::string> failed;
    for (const auto& [key,request]:requests_) if(request.session==session && !request.finished && !(frame_session!=frame_sessions_.end()&&SessionCovers(session,request.frame))) failed.push_back(key);
    for(const auto& key:failed) {
      auto& request=requests_.at(key);request.finished=true;request.error="Target closed";
      auto event=BaseEvent("requestfailed",request);event->SetString("errorText",request.error);Publish(event);FinishWaiters(request);
    }
  }
  // Only real Source/broker OnBeforeClose may invoke this. Generic Clear,
  // logical cancellation and renderer loss are not a channel-close receipt.
  void HARWorkChannelClosed(){GlobalHARWorkLeases().ScopeClosed(callback_scope_);}
  void Clear() { disposed_=true;CancelAllRouteFetches();matchers_->Clear();matcher_operations_.clear();preflights_->Clear();request_extras_->Clear();response_extras_->Clear();if(post_reads_)post_reads_->Clear();observer_={};authentication_->Clear();if(bodies_)bodies_->publication().Clear();GlobalRouteCallbackLeases().Close(callback_scope_);headers_=nullptr;context_headers_=nullptr;offline_=false;retained_admission_=false;registration_pending_.clear();routes_.Clear();har_replays_.clear();har_replay_bytes_=0; paused_.clear(); rejected_.clear(); requests_.clear(); request_metadata_bytes_=0;order_.clear(); waiters_.clear(); history_.clear();history_bytes_=0;sessions_.clear();network_only_sessions_.clear();worker_parents_.clear();frame_sessions_.clear();session_handoff_={};frame_descendant_={};upload_bytes_=pause_metadata_bytes_=0;if(page_removals_)page_removals_->Clear(); }
  void Cancel(int id) {
    if(disposed_)return;
    CancelRouteFetch(id,false);
    CancelMatcherOperation(id);CancelReplayOperation(id);
    waiters_.erase(id);
    page_removals_->Cancel(id);
    const auto pending=registration_pending_.find(id);
    if(pending!=registration_pending_.end()) {
      const auto route_id=pending->second;registration_pending_.erase(pending);
      routes_.Remove(route_id);
      ConfigureAll([this](bool ok,const std::string& error,const std::string&){if(!ok && !disposed_)failure_("Could not remove canceled native route: "+error);});
    }
  }
  bool AcceptedWorkReady(bool viewer=false)const{
    return !disposed_&&!NeedsTick(viewer)&&registration_pending_.empty()&&matcher_operations_.empty()&&
      (!post_reads_||!post_reads_->pending())&&(!page_removals_||!page_removals_->retained_applies())&&
      !GlobalRouteCallbackLeases().SizeForScope(callback_scope_);
  }
  // ExtraInfo FIFO metadata can await an optional late receipt after a failed
  // request. Its unchanged retention timer is resident during viewer handover;
  // route pauses, callbacks, reads and every actual command remain fenced.
  bool NeedsTick(bool viewer=false) const { return (route_fetch_physical_&&*route_fetch_physical_) || matchers_->pending() || page_removals_->pending() || preflights_->NeedsTick() || (!viewer&&(request_extras_->NeedsTick() || response_extras_->NeedsTick())) || authentication_->NeedsTick() || !paused_.empty() || !rejected_.empty() || !waiters_.empty() || (bodies_ && bodies_->publication().NeedsTick()); }
  // Source invokes this before dropping an oversized Fetch.requestPaused packet.
  // Retain physical interception ownership until Chromium acknowledges rejection.
  void RejectOversizedPause(Dict data,const std::string& session,const std::string& reason="Native intercepted request metadata exceeds 1 MiB; actual request aborted",bool report=true,std::shared_ptr<NativeHARFetchWork>work={}) {
    if(disposed_)return;
    const auto fetch=data?data->GetString("requestId").ToString():std::string{};
    if(fetch.empty() || fetch.size()>256){failure_("Invalid oversized native interception identifier");return;}
    const auto key=session+"\n"+fetch;
    if(rejected_.contains(key)){if(work)failure_("Native HAR rejection already has a physical owner; actual channel must close");return;}
    if(paused_.size()+rejected_.size()>=1024){failure_("Native interception rejection ownership capacity exceeded");return;}
    rejected_.emplace(key,Rejected{fetch,session,now_()+30000});
    auto event=CefDictionaryValue::Create();event->SetString("event","routeerror");
    event->SetString("errorText",reason);if(report)Publish(event);wake_();
    auto params=CefDictionaryValue::Create();params->SetString("requestId",fetch);params->SetString("errorReason","BlockedByClient");
    send_(session,"Fetch.failRequest",params,[this,key,work,reason](bool ok,const std::string& error){
      if(work){work->Failed(reason);if(ok)work->TerminalACK();}
      if(disposed_)return;
      const auto found=rejected_.find(key);if(found==rejected_.end())return;
      if(!ok){found->second.failure_reported=true;failure_("Could not reject oversized native intercepted request: "+error);return;}
      rejected_.erase(found);
      if(routes_.empty() && !matchers_->pending() && paused_.empty() && rejected_.empty())ConfigureAll([this](bool success,const std::string& reason,const std::string&){if(!success && !disposed_)failure_("Could not disable native rejected routes: "+reason);});
    });
  }
  void Tick(double now) {
    auto alive=alive_;
    matchers_->Tick(now);if(!*alive||disposed_)return;
    preflights_->Tick(now);if(!*alive||disposed_)return;
    request_extras_->Tick(now);if(!*alive||disposed_)return;
    response_extras_->Tick(now);if(!*alive||disposed_)return;
    authentication_->Tick(now);if(!*alive||disposed_||authentication_->failed())return;
    for(auto& [key,rejection]:rejected_)if(now>=rejection.deadline && !rejection.failure_reported){rejection.failure_reported=true;failure_("Native oversized interception rejection deadline exceeded");return;}
    std::vector<int> expired_fetches;for(const auto&[owner,fetch]:route_fetch_pending_)if(now>=fetch.deadline)expired_fetches.push_back(owner);
    for(const int owner:expired_fetches)CancelRouteFetch(owner);
    std::vector<std::string> expired;
    for(const auto& [id,paused]:paused_) if(now>=paused.deadline||(paused.replay_policy&&now>=paused.replay_deadline)) expired.push_back(id);
    for(const auto& id:expired) {
      const auto found=paused_.find(id);if(found==paused_.end())continue;const auto pause=found->second;GlobalRouteCallbackLeases().Revoke(callback_scope_,id);upload_bytes_-=pause.body.size();pause_metadata_bytes_-=pause.metadata_bytes;CancelRouteFetches(id);paused_.erase(id);
      auto params=CefDictionaryValue::Create();params->SetString("requestId",pause.fetch_id);params->SetString("errorReason","TimedOut");
      send_(pause.session,"Fetch.failRequest",params,[this,work=pause.har_work,policy=pause.replay_policy,done=pause.replay_done](bool ok,const std::string&error)mutable{
        if(work){work->Failed("Native HAR replay exceeded its original operation deadline");if(ok)work->TerminalACK();else if(!disposed_)failure_("Actual expired HAR rejection failed: "+error);}
        policy.reset();if(done){auto reply=std::move(done);reply(false,"Native HAR replay exceeded its original operation deadline","");}});
      if(!*alive||disposed_)return;
      auto event=CefDictionaryValue::Create();event->SetString("event","routeexpired");event->SetString("routeId",id);Publish(event);if(!*alive||disposed_)return;
    }
    if(bodies_)bodies_->publication().Tick(now);
    if(!*alive||disposed_)return;
    page_removals_->Tick();
  }
  void Diagnostics(Dict output) const {
    output->SetInt("networkMatcherSelections",int(matchers_->pending()));output->SetInt("nativeMatcherSelections",int(NativeRouteMatcherSelections::retained_count()));output->SetDouble("nativeMatcherBytes",double(NativeRouteMatcherSelections::retained_bytes()));
    output->SetInt("networkRouteRemovalObservers",static_cast<int>(page_removals_->pending()));output->SetInt("networkRouteRemovalApplyCallbacks",static_cast<int>(page_removals_->retained_applies()));
    output->SetInt("networkClassifyingPauses",int(preflights_->pending()));output->SetDouble("networkClassifyingBytes",double(preflights_->bytes()));output->SetInt("nativeClassifyingPauses",int(NativeNetworkPreflights::global_pending()));output->SetDouble("nativeClassifyingBytes",double(NativeNetworkPreflights::global_bytes()));
    output->SetDouble("networkResponseHeaderBytes",double(response_extras_->bytes()));output->SetInt("networkResponseHeaderHops",int(response_extras_->hops()));output->SetInt("networkResponseHeaderAliases",int(response_extras_->aliases()));output->SetInt("networkResponseHeaderReceipts",int(response_extras_->receipts()));
    output->SetInt("networkPostPreparations",post_reads_?static_cast<int>(post_reads_->pending()):0);
    output->SetBool("networkAuthFatal",authentication_->failed());
    output->SetInt("networkAuthChallenges",static_cast<int>(authentication_->pending()));
    output->SetInt("networkAuthAttempts",static_cast<int>(authentication_->attempts()));
    output->SetInt("networkAuthCorrelations",static_cast<int>(authentication_->correlations()));
    output->SetInt("networkBodyOwners",bodies_?static_cast<int>(bodies_->publication().owners()):0);
    output->SetInt("networkBodyBytes",bodies_?static_cast<int>(bodies_->publication().bytes()):0);
    output->SetInt("networkRouteCallbacks",static_cast<int>(GlobalRouteCallbackLeases().size()));
    output->SetInt("networkRunningRouteCallbacks",static_cast<int>(GlobalRouteCallbackLeases().running()));
    output->SetInt("networkScopeRouteCallbacks",static_cast<int>(GlobalRouteCallbackLeases().SizeForScope(callback_scope_)));
    output->SetInt("networkRequests",static_cast<int>(requests_.size()));
    output->SetDouble("networkRequestMetadataBytes",static_cast<double>(request_metadata_bytes_));
    output->SetInt("networkRoutes",static_cast<int>(routes_.size()));
    output->SetInt("nativeHARRetainedPolicies",int(NativeHARRouteRegistration::retained_policies()));output->SetInt("nativeHARRetainedArchives",int(NativeHARRouteRegistration::retained_archives()));output->SetDouble("nativeHARRetainedPolicyBytes",double(NativeHARRouteRegistration::retained_bytes()));
    output->SetInt("nativeHARWorkPhysical",static_cast<int>(GlobalHARWorkLeases().physical()));
    output->SetInt("networkHARRoutes",static_cast<int>(har_replays_.size()));
    output->SetDouble("networkHARBytes",static_cast<double>(har_replay_bytes_));
    output->SetInt("networkPaused",static_cast<int>(paused_.size()+rejected_.size()));
    output->SetInt("networkRejectedPauses",static_cast<int>(rejected_.size()));
    output->SetDouble("networkPausedMetadataBytes",static_cast<double>(pause_metadata_bytes_));
    output->SetDouble("networkPendingHeaderBytes",static_cast<double>(request_extras_->bytes()));
    output->SetInt("networkRequestHeaderHops",int(request_extras_->hops()));output->SetInt("networkRequestHeaderReceipts",int(request_extras_->receipts()));output->SetInt("networkRequestHeaderAliases",int(request_extras_->aliases()));
    output->SetInt("networkWaiters",static_cast<int>(waiters_.size()));
    output->SetInt("networkJournal",static_cast<int>(history_.size()));
  }
  void Event(const std::string& method, Dict data, const std::string& session) {
    if(disposed_||authentication_->failed())return;
    if(method=="Fetch.authRequired"){authentication_->Event(data,session,now_());return;}
    if(method=="Fetch.requestPaused") { Pause(data,session);return; }
    const auto native_id=data->GetString("requestId").ToString();
    if(native_id.empty() || native_id.size()>256) return;
    auto key=session+"\n"+native_id;
    if(method!="Network.requestWillBeSent"&&!requests_.contains(key)) {
      const auto original=FindFrameRequest(native_id,session,data->GetString("frameId").ToString());
      if(!original.empty()) {
        if(method=="Network.responseReceived"||method=="Network.dataReceived"||method=="Network.loadingFinished"||method=="Network.loadingFailed") {
          if(!MoveFrameRequest(original,key,session))return;
        } else key=original;
      }
    }
    if(method=="Network.loadingFinished"||method=="Network.loadingFailed")authentication_->NetworkFinished(session,native_id);
    if(method=="Network.responseReceivedExtraInfo"){const auto headers=data->GetDictionary("headers");if(headers){response_extras_->Receive(key,headers,now_()+30000);wake_();}return;}
    if(method=="Network.requestWillBeSentExtraInfo"){const auto headers=data->GetDictionary("headers");if(headers){request_extras_->Receive(key,headers,now_()+30000);wake_();}return;}
    if(method=="Network.requestWillBeSent") {
      auto ancestor=session;
      for(size_t depth=0;depth<32;++depth){const auto parent=worker_parents_.find(ancestor);if(parent==worker_parents_.end())break;ancestor=parent->second;authentication_->NetworkMoved(native_id,ancestor,session);}
      auto info=data->GetDictionary("request");if(!info)return;
      std::string redirected;
      const auto previous=requests_.find(key);
      if(previous!=requests_.end() && data->HasKey("redirectResponse")) {
        auto& old=previous->second;if(post_reads_)post_reads_->Invalidate(old.logical_id);old.response=Response(data->GetDictionary("redirectResponse"),old);
        old.response_headers_expected=data->GetBool("redirectHasExtraInfo");old.response_headers_ready=!old.response_headers_expected;
        old.request_headers_expected=old.response_headers_expected;old.request_headers_ready=!old.request_headers_expected;
        if(!request_extras_->Expect(key,old.logical_id,old.request_headers_expected,now_()+30000))return;
        if(!response_extras_->Expect(key,old.logical_id,old.response_headers_expected,now_()+30000)||!RetainMetadata(old))return;
        Publish(BaseEvent("response",old));old.finished=true;
        Publish(BaseEvent("requestfinished",old));FinishWaiters(old);redirected=old.logical_id;
        // Keep the genuine old logical response under the same existing native
        // count/byte budgets while its explicitly expected receipt may arrive.
        const auto history=key+"\nredirect:"+old.logical_id;auto previous_node=requests_.extract(previous);std::string stored(history);previous_node.key().swap(stored);requests_.insert(std::move(previous_node));for(auto&entry:order_)if(entry==key)entry=history;

      }
      if(!Reserve(key)) return;
      Request request;request.native_id=native_id;request.session=session;
      request.logical_id=std::to_string(++request_serial_);request.frame=data->GetString("frameId").ToString();
      request.method=info->GetString("method").ToString();request.url=info->GetString("url").ToString();
      request.info=info->Copy(false);if(auto worker=data->GetDictionary("suitecutWorkerInfo"))request.info->SetDictionary("workerInfo",worker->Copy(false));if(data->GetBool("suitecutContextWorker"))request.info->SetBool("contextWorker",true);if(data->GetBool("suitecutNavigationFrameUnavailable"))request.info->SetBool("navigationFrameUnavailable",true);if(data->GetBool("suitecutServiceWorker"))request.info->SetBool("serviceWorker",true);const bool read_post=post_reads_&&CaptureNativePostData(request.info).kind==NativePostData::Kind::Unavailable;PrepareBody(request.info,now_()+30000);request.resource=data->GetString("type").ToString();
      request.navigation=request.resource=="Document";request.redirected_from=redirected;
      for(const auto&[route,pause]:paused_)if(pause.request.native_id==native_id&&pause.request.session==session&&pause.request.intercepted_policy)request.intercepted_policy=true;
      request.wall_time=Number(data,"wallTime")*1000;request.timestamp=Number(data,"timestamp");
      auto initiator=data->GetDictionary("initiator");request.preflight=initiator&&initiator->GetString("type")=="preflight";if(request.preflight){request.preflight_parent=initiator->GetString("requestId").ToString();if(request.preflight_parent.size()>256){failure_("Invalid native preflight parent request identity");return;}}
      auto previous_record=requests_.find(key);if(previous_record!=requests_.end())request_metadata_bytes_-=previous_record->second.metadata_bytes;
      requests_[key]=std::move(request);for(const auto&[route,pause]:paused_)if(pause.request.intercepted_policy&&pause.request.native_id==native_id&&RouteRequest(native_id,pause.session)==&requests_.at(key))requests_.at(key).intercepted_policy=true;if(!RetainMetadata(requests_.at(key)))return;
      const auto logical=requests_.at(key).logical_id;
      if(read_post)post_reads_->Prepare(logical,session,native_id,now_()+30000);
      Publish(BaseEvent("request",requests_.at(key)));
      if(read_post)post_reads_->Start(logical);
      ResolvePreflights();
    } else {
      auto found=requests_.find(key);if(found==requests_.end())return;auto& request=found->second;
      if(method=="Network.responseReceived") {
        request.response=Response(data->GetDictionary("response"),request);
        const auto resource=data->GetString("type").ToString();if(!resource.empty())request.resource=resource;
        request.response_headers_expected=data->GetBool("hasExtraInfo");request.response_headers_ready=!request.response_headers_expected;
        request.request_headers_expected=request.response_headers_expected;request.request_headers_ready=!request.request_headers_expected;
        if(!request_extras_->Expect(key,request.logical_id,request.request_headers_expected,now_()+30000))return;
        if(!response_extras_->Expect(key,request.logical_id,request.response_headers_expected,now_()+30000))return;
        if(!RetainMetadata(request))return;
        Publish(BaseEvent("response",request));
        FinishWaiters(request);
      } else if(method=="Network.loadingFinished" || method=="Network.loadingFailed") {
        request.finished=true;request.encoded=Number(data,"encodedDataLength");
        request.end_time=(Number(data,"timestamp")-request.timestamp)*1000;
        if(method=="Network.loadingFailed"){
          request.error=data->GetString("errorText").ToString();const auto blocked=data->GetString("blockedReason").ToString();request.blocked_reason=blocked;if(blocked=="mixed-content")request.error=blocked;
          // No response marker exists for this genuinely failed final hop.
          // All previous redirect hops are already marked, so any remaining
          // FIFO receipt belongs to this last request. Its absence is optional:
          // return observed partial headers without claiming an emitted receipt.
          if(!request.response&&!request_extras_->Expect(key,request.logical_id,true,now_()+30000))return;
        }
        request_extras_->Complete(key);response_extras_->Complete(key);
        auto event=BaseEvent(request.error.empty()?"requestfinished":"requestfailed",request);
        if(!request.error.empty())event->SetString("errorText",request.error);
        Publish(event);FinishWaiters(request);
      }
    }
    MaybeReleaseAdmittedPolicy();
  }
  bool BodyAcknowledged(const std::string& token,const std::string& stage) {
    uint64_t id=0;const auto parsed=std::from_chars(token.data(),token.data()+token.size(),id);
    if(!bodies_ || !id || parsed.ec!=std::errc{} || parsed.ptr!=token.data()+token.size())return false;
    const bool accepted=stage=="received"?bodies_->publication().Received(id):stage=="taken"&&bodies_->publication().Taken(id);
    if(accepted)wake_();
    return accepted;
  }
  void Command(int owner, Dict config, Done done,double original_deadline=0,uint64_t generation=0,int context=0) {
    if(disposed_){done(false,"Native page is closed","");return;}
    if(authentication_->failed()){done(false,"Native network owner awaits physical teardown","");return;}
    const auto action=config->GetString("action").ToString();
    if(config->HasKey("callbackScope")){
      if(config->GetType("callbackScope")!=VTYPE_STRING||config->GetString("callbackScope").ToString()!=std::to_string(callback_scope_)||(action!="matchRoute"&&!paused_.contains(config->GetString("routeId").ToString()))){done(false,"Native route belongs to another actual callback owner or is no longer paused","");return;}
    }
    if(action=="fetch"){FetchRoute(owner,config,std::move(done),original_deadline);return;}
    if(action=="matchRoute"){MatchResult(config,std::move(done));return;}
    if(action=="registerRoute") {
      const auto id=config->GetString("registrationId").ToString();const auto pattern=config->GetString("url").ToString();
      const int times=config->GetInt("times");
      if((config->HasKey("hostMatcher")&&config->GetType("hostMatcher")!=VTYPE_BOOL)||!routes_.Register(id,pattern,times,false,config->GetBool("hostMatcher"))){done(false,"Invalid, duplicate or excessive native route registration","");return;}
      registration_pending_[owner]=id;
      ConfigureAll([this,owner,id,done=std::move(done)](bool ok,const std::string& error,const std::string& json){
        if(!registration_pending_.erase(owner))return;
        if(!ok){routes_.Remove(id);
          ConfigureAll([this](bool success,const std::string& reason,const std::string&){if(!success && !disposed_)failure_("Could not roll back native route: "+reason);});}
        done(ok,error,json);
      });return;
    }
    if(action=="removeRoute") {
      const bool specified=config->HasKey("behavior");const auto behavior=config->GetString("behavior").ToString();
      if(specified&&(config->GetType("behavior")!=VTYPE_STRING||(behavior!="default"&&behavior!="wait"&&behavior!="ignoreErrors"))){done(false,"Invalid native Page route removal behavior","");return;}
      if(behavior=="wait"||behavior=="ignoreErrors"){
        if(!bounded_send_||config->GetType("registrationId")!=VTYPE_STRING||!config->GetString("registrationId").empty()||config->GetType("mutationId")!=VTYPE_STRING){done(false,"Native Page route removal requires its original bounded owner operation","");return;}
        const auto ids=routes_.PageIDs();auto wake=wake_;wake();
        const bool accepted=page_removals_->Remove(owner,context,generation,GlobalRouteCallbackLeases().CapturePage(callback_scope_),ids,config->GetString("mutationId"),behavior=="wait",original_deadline,
          [this,ids]{for(const auto&id:ids){routes_.Remove(id);DropReplay(id);}},
          [this](double deadline,Result ack){ConfigureAll([ack=std::move(ack)](bool ok,const std::string&error,const std::string&){ack(ok,error);},deadline);},
          [done](bool ok,const std::string&error){done(ok,error,"{}");});
        if(!accepted)done(false,"Invalid, pending or excessive native Page route removal observer","");
        return;
      }
      const auto id=config->GetString("registrationId").ToString();
      if(id.empty()){for(const auto&page:routes_.PageIDs()){routes_.Remove(page);DropReplay(page);}}
      else{routes_.Remove(id);DropReplay(id);}
      ConfigureAll(done);return;
    }
    if(action=="resetBody") {
      const auto found=paused_.find(config->GetString("routeId").ToString());if(found==paused_.end()){done(false,"Route is already handled or no longer available","");return;}
      double fetch_deadline=0;
      if(config->HasKey("fetchOptions")){
        if(!config->HasKey("callbackScope")||config->GetType("fetchOptions")!=VTYPE_DICTIONARY){done(false,"Native route fetch upload lacks its actual callback owner","");return;}
        std::string error;auto cap=NativeRouteFetchDeadline(config->GetDictionary("fetchOptions"),now_(),original_deadline>0?original_deadline:found->second.deadline,found->second.deadline,error);
        if(!cap){done(false,error,"");return;}fetch_deadline=*cap;
      }
      found->second.fetch_upload_deadline=fetch_deadline;
      upload_bytes_-=found->second.body.size();std::vector<uint8_t>().swap(found->second.body);done(true,"","{}");return;
    }
    if(action=="bodyChunk") {
      const auto id=config->GetString("routeId").ToString();auto found=paused_.find(id);
      if(found==paused_.end()){done(false,"Route is already handled or no longer available","");return;}
      if(found->second.fetch_upload_deadline&&now_()>=found->second.fetch_upload_deadline){done(false,"Native route fetch upload original deadline expired","");return;}
      const auto encoded=config->GetString("body").ToString();
      if(encoded.size()>32768){done(false,"Native route upload chunk exceeds 24 KiB","");return;}
      const auto decoded=CefBase64Decode(encoded);if(!decoded){done(false,"Invalid route body base64","");return;}
      const size_t bytes=decoded->GetSize();
      if(found->second.body.size()+bytes>2097152 || upload_bytes_+bytes>8388608){done(false,"Native route upload capacity exceeded","");return;}
      const size_t offset=found->second.body.size();found->second.body.resize(offset+bytes);
      decoded->GetData(found->second.body.data()+offset,bytes,0);upload_bytes_+=bytes;done(true,"","{}");return;
    }
    if(action=="continue" || action=="abort" || action=="fulfill" || action=="fallback") { Resolve(config,action,std::move(done),owner,original_deadline,generation);return; }
    if(action=="setExtraHTTPHeaders"||action=="setContextExtraHTTPHeaders") {
      auto headers=config->GetDictionary("headers");if(!NativeContextNetworkHeaders::Valid(headers)){done(false,"Invalid native HTTP headers","");return;}
      if(action=="setExtraHTTPHeaders")headers_=headers->Copy(false);else context_headers_=headers->Copy(false);
      ForSessions([this](const std::string& session,Result result){auto params=CefDictionaryValue::Create();params->SetDictionary("headers",EffectiveHeaders());send_(session,"Network.setExtraHTTPHeaders",params,std::move(result));},std::move(done));return;
    }
    if(action=="setOffline") {
      if(config->GetType("offline")!=VTYPE_BOOL){done(false,"Invalid native offline option","");return;}
      offline_=config->GetBool("offline");ForSessions([this](const std::string& session,Result result){ApplyOffline(session,offline_,std::move(result));},std::move(done));return;
    }
    if(action=="waitEvent") {
      const auto event=config->GetString("event").ToString();const auto pattern=config->GetString("url").ToString();
      const double after=Number(config,"afterSequence");
      if((event!="request" && event!="response" && event!="requestfinished" && event!="requestfailed") || pattern.size()>8192 || after<0 || !std::isfinite(after)){done(false,"Invalid native network event wait","");return;}
      if(!history_.empty() && after<Number(history_.front().event,"sequence")-1){done(false,"Native network event journal capacity exceeded","");return;}
      for(const auto& record:history_)if(Number(record.event,"sequence")>after && EventMatches(record.event,event,pattern)) {done(true,"",Json(record.event));return;}
      waiters_[owner]={event,pattern,"",action,std::move(done)};wake_();return;
    }
    if(action=="response" || action=="finished" || action=="timing" || action=="requestHeaders" || action=="responseHeaders") {
      const auto id=config->GetString("requestId").ToString();
      // The actual owned Fetch pause has intercepted header metadata before
      // continuing it can produce the Network ExtraInfo marker/receipt.
      if(action=="requestHeaders")for(const auto&[route,pause]:paused_)if(pause.request.logical_id==id){
        if(now_()>=pause.deadline){done(false,"Native route original deadline expired","");return;}
        auto headers=pause.request.info?pause.request.info->GetDictionary("headers"):nullptr;done(true,"",Json(headers?headers:CefDictionaryValue::Create()));return;
      }
      const auto request=FindLogical(id);
      if(!request){done(false,"Native request is no longer retained","");return;}
      if(action=="requestHeaders" || action=="responseHeaders") {
        if(HeaderReady(*request,action)){DoneRequest(*request,action,done);return;}
        waiters_[owner]={"","",id,action,std::move(done)};wake_();return;
      }
      if(action=="timing") {done(true,"",Json(Timing(*request)));return;}
      if((action=="response" && (request->response || request->finished)) || (action=="finished" && request->finished)) {DoneRequest(*request,action,done);return;}
      waiters_[owner]={"","",id,action,std::move(done)};wake_();return;
    }
    done(false,"Unsupported native network action","");
  }
 private:
  struct RouteFetchPending {std::string route;Done done;double deadline=0;};
  void CancelRouteFetch(int owner,bool notify=true){const auto found=route_fetch_pending_.find(owner);if(found==route_fetch_pending_.end())return;auto done=std::move(found->second.done);route_fetch_pending_.erase(found);if(cancel_route_fetch_)cancel_route_fetch_(owner);if(notify&&done)done(false,"Native route fetch paused owner was retired","");}
  void CancelRouteFetches(const std::string& route){std::vector<int> owners;for(const auto&[id,entry]:route_fetch_pending_)if(entry.route==route)owners.push_back(id);for(int id:owners)CancelRouteFetch(id);}
  void CancelAllRouteFetches(){while(!route_fetch_pending_.empty())CancelRouteFetch(route_fetch_pending_.begin()->first,false);}
  void FetchRoute(int owner,Dict config,Done done,double original){
    const auto id=config->GetString("routeId").ToString();const auto pause=paused_.find(id);
    if(!route_fetch_||!config->HasKey("callbackScope")||pause==paused_.end()||pause->second.matching||route_fetch_pending_.contains(owner)||route_fetch_pending_.size()>=64||owner<1){done(false,"Native route fetch lacks its actual paused callback owner or capacity","");return;}
    for(const auto&[key,entry]:route_fetch_pending_)if(entry.route==id){done(false,"Native route fetch is already active for this pause","");return;}
    auto deadline=original>0?std::min(original,pause->second.deadline):pause->second.deadline;
    if(config->GetBool("bodyUpload")){
      if(!pause->second.fetch_upload_deadline){done(false,"Native route fetch upload has no original preparation deadline","");return;}
      deadline=std::min(deadline,pause->second.fetch_upload_deadline);
    }
    std::string fetch_error;auto cap=NativeRouteFetchDeadline(config,now_(),deadline,deadline,fetch_error);
    if(!cap){done(false,fetch_error,"");return;}deadline=*cap;
    auto options=config->Copy(false);
    if(config->GetBool("bodyUpload"))options->SetString("postData",CefBase64Encode(pause->second.body.data(),pause->second.body.size()));
    else if(!config->HasKey("postData")&&pause->second.fetch_body)options->SetString("postData",CefBase64Encode(pause->second.fetch_body->data(),pause->second.fetch_body->size()));
    route_fetch_pending_.emplace(owner,RouteFetchPending{id,std::move(done),deadline});
    if(!route_fetch_physical_)route_fetch_physical_=std::make_shared<std::atomic_size_t>(0);
    auto lease=std::make_shared<NativeRouteFetchLease>(route_fetch_physical_);
    route_fetch_(owner,pause->second.session,pause->second.request.session,pause->second.request.info->Copy(false),options,deadline,std::move(lease),[this,alive=alive_,owner](bool ok,Dict result,const std::string&error){
      if(!*alive)return false;
      const auto found=route_fetch_pending_.find(owner);if(found==route_fetch_pending_.end())return false;
      auto entry=std::move(found->second);route_fetch_pending_.erase(found);
      if(now_()>=entry.deadline){entry.done(false,"Native route fetch original operation deadline expired","");return false;}
      entry.done(ok,error,ok&&result?Json(result):std::string{});return ok;
    });wake_();
  }
  RouteFetch route_fetch_;std::function<void(int)>cancel_route_fetch_;
  std::map<int,RouteFetchPending>route_fetch_pending_;std::shared_ptr<std::atomic_size_t>route_fetch_physical_;
  struct FrameSession {std::string parent,frame;bool detached=false;};
  bool SessionCovers(const std::string& session,const std::string& frame) const {
    if(frame.empty())return false;
    if(session.empty())return true;
    const auto owner=frame_sessions_.find(session);if(owner==frame_sessions_.end())return false;
    return owner->second.frame==frame||(frame_descendant_&&frame_descendant_(frame,owner->second.frame));
  }
  std::string FindFrameRequest(const std::string& native,const std::string& session,const std::string& frame) {
    if(!session.empty()&&!frame_sessions_.contains(session))return {};
    std::string selected;
    for(const auto&[key,request]:requests_) {
      if(request.finished||request.native_id!=native||(!frame.empty()&&request.frame!=frame)||
         !SessionCovers(request.session,request.frame)||!SessionCovers(session,request.frame))continue;
      if(!selected.empty()){failure_("Ambiguous native network request session handoff");return {};}
      selected=key;
    }return selected;
  }
  bool MoveFrameRequest(const std::string& old_key,const std::string& new_key,const std::string& session) {
    auto record=requests_.extract(old_key);if(record.empty()||requests_.contains(new_key)){failure_("Conflicting native network request session handoff");return false;}
    const auto previous=record.mapped().session,native=record.mapped().native_id;
    record.key()=new_key;record.mapped().session=session;
    if(record.mapped().response)record.mapped().response->SetString("session",session);
    requests_.insert(std::move(record));for(auto&key:order_)if(key==old_key)key=new_key;if(!request_extras_->Move(old_key,new_key,now_()+30000)||!response_extras_->Move(old_key,new_key,now_()+30000))return false;
    authentication_->NetworkMoved(native,previous,session);
    if(session_handoff_)session_handoff_(native,previous,session);
    auto&request=requests_.at(new_key);if(!RetainMetadata(request))return false;
    Publish(BaseEvent("requestupdated",request));PruneFrameSessions();return true;
  }
  void PruneFrameSessions() {
    const auto bound=frame_sessions_.size();
    for(size_t pass=0;pass<=bound;++pass) {
      bool removed=false;
      for(auto it=frame_sessions_.begin();it!=frame_sessions_.end();) {
        bool retained=!it->second.detached;
        if(!retained)for(const auto&[key,request]:requests_)if(!request.finished&&request.session==it->first){retained=true;break;}
        if(!retained)for(const auto&[child,owner]:frame_sessions_)if(owner.parent==it->first){retained=true;break;}
        if(retained)++it;else {it=frame_sessions_.erase(it);removed=true;}
      }if(!removed)break;
    }
  }
  void DropReplay(const std::string&id) {
    if(id.empty()){har_replays_.clear();har_replay_bytes_=0;return;}
    const auto found=har_replays_.find(id);if(found!=har_replays_.end()){har_replay_bytes_-=found->second->archive()->bytes();har_replays_.erase(found);}
  }
  bool NextNeedsCallback(const NativeRouteRegistry::Plan&plan)const {
    for(size_t i=plan.next;i<plan.candidates.size();++i){auto single=plan.Single(i);if(routes_.HasNext(single))return !har_replays_.contains(plan.candidates[i]);}return false;
  }
  void ReplayPaused(const std::string&id,const std::string&registration,Done done,double deadline=0,int operation=0,uint64_t generation=0) {
    const auto found=paused_.find(id);if(found==paused_.end()){done(false,"Native HAR interception is no longer owned","");return;}
    auto&pause=found->second;
    if(!pause.replay_policy){const auto route=har_replays_.find(registration);if(route==har_replays_.end()){done(false,"Native HAR route is no longer available","");return;}pause.replay_policy=route->second;pause.replay_deadline=deadline>0?std::min(deadline,pause.deadline):pause.deadline;pause.replay_operation=operation;pause.replay_generation=generation;}
    const auto policy=pause.replay_policy;const auto actual_deadline=pause.replay_deadline;
    if(!pause.har_work)pause.har_work=std::make_shared<NativeHARFetchWork>();
    if(!pause.har_work->Add(callback_scope_,pause.session,policy)){done(false,"Native HAR Fetch work receipt capacity exceeded","");return;}
    operation=pause.replay_operation;generation=pause.replay_generation;
    if(now_()>=actual_deadline){auto params=CefDictionaryValue::Create();params->SetString("requestId",pause.fetch_id);params->SetString("errorReason","TimedOut");FinishPause(id,"Fetch.failRequest",params,[done=std::move(done)](bool,const std::string&,const std::string&){done(false,"Native HAR replay exceeded its original operation deadline","");});return;}
    if(post_reads_&&!pause.har_post_attempted&&!post_reads_->Gate(pause.request.logical_id)&&CaptureNativePostData(pause.har_info?pause.har_info:pause.request.info).kind==NativePostData::Kind::Unavailable){
      pause.har_post_attempted=true;
      if(post_reads_->Prepare(pause.request.logical_id,pause.request.session,pause.request.native_id,actual_deadline,FindLogical(pause.request.logical_id)!=nullptr)){
        const auto logical=pause.request.logical_id;
        if(!post_reads_->RetainHAR(logical,callback_scope_,policy)){done(false,"Native HAR request body archive retention capacity exceeded","");return;}
        pause.replay_done=std::move(done);post_reads_->Start(logical);return;
      }
    }
    if(post_reads_&&post_reads_->Gate(pause.request.logical_id)){
      if(pause.replay_done){done(false,"Native HAR request body preparation already has a route owner","");return;}
      if(!post_reads_->RetainHAR(pause.request.logical_id,callback_scope_,policy)){done(false,"Native HAR request body archive retention capacity exceeded","");return;}
      pause.replay_done=std::move(done);return;
    }
    auto config=CefDictionaryValue::Create();config->SetString("routeId",id);const auto match=policy->archive()->Lookup(pause.har_info?pause.har_info:pause.request.info);
    if(!match){if(policy->fallback())Resolve(config,"fallback",std::move(done),operation,actual_deadline,generation);else{config->SetString("errorReason","Failed");Resolve(config,"abort",std::move(done),operation,actual_deadline,generation);}return;}
    if(match->body.size()>8388608-upload_bytes_){config->SetString("errorReason","BlockedByClient");Resolve(config,"abort",[done=std::move(done)](bool,const std::string&,const std::string&){done(false,"Native HAR fulfillment body capacity exceeded","");},operation,actual_deadline,generation);return;}
    upload_bytes_+=match->body.size();pause.body=match->body;config->SetBool("bodyUpload",true);config->SetInt("status",match->status);config->SetString("statusText",match->status_text);auto headers=CefListValue::Create();size_t i=0;
    for(const auto&[name,value]:match->headers){auto header=CefDictionaryValue::Create();header->SetString("name",name);header->SetString("value",value);headers->SetDictionary(i++,header);}config->SetList("headers",headers);Resolve(config,"fulfill",std::move(done),operation,actual_deadline,generation);
  }
  void CancelReplayOperation(int operation){
    if(operation<=0)return;
    std::vector<std::string>ids;for(const auto&[id,pause]:paused_)if(pause.replay_policy&&pause.replay_operation==operation)ids.push_back(id);
    for(const auto&id:ids){const auto found=paused_.find(id);if(found==paused_.end())continue;auto params=CefDictionaryValue::Create();params->SetString("requestId",found->second.fetch_id);params->SetString("errorReason","Aborted");FinishPause(id,"Fetch.failRequest",params,[](bool,const std::string&,const std::string&){});}
  }
  struct Rejected {std::string fetch_id,session;double deadline;bool failure_reported=false;};
  struct Request {
    std::string logical_id,native_id,session,frame,method,url,resource,redirected_from,error,blocked_reason,request_header_error;
    std::string response_header_error,preflight_parent;
    Dict info,response;size_t metadata_bytes=0;bool finished=false,navigation=false,preflight=false,intercepted_policy=false,route_selection_complete=false,route_selection_failed=false,request_headers_ready=false,request_headers_expected=false,response_headers_ready=false,response_headers_expected=false;double wall_time=0,timestamp=0,end_time=-1,encoded=0;
  };
  struct Paused {std::string fetch_id,session;double deadline=0;std::vector<uint8_t> body;NativeRouteRegistry::Plan plan;Request request;Dict overrides;size_t metadata_bytes=0;Dict har_info{};bool public_prepared=true,har_post_attempted=false;NativeHARRouteRegistration::Policy replay_policy{};double replay_deadline=0;int replay_operation=0;uint64_t replay_generation=0;Done replay_done{};std::string original_url{};bool matching=false;std::shared_ptr<NativeHARFetchWork>har_work{};std::shared_ptr<const std::vector<uint8_t>>fetch_body{};double fetch_upload_deadline=0;};
  #include "network_route_matchers.inc"
  struct Waiter {std::string event,pattern,request_id,action;Done done;};
  static double Number(Dict value,const char* key) {return value->GetType(key)==VTYPE_DOUBLE?value->GetDouble(key):value->GetInt(key);}
  static bool Evictable(const Request&request){return request.finished&&(!request.request_headers_expected||request.request_headers_ready||!request.request_header_error.empty())&&(!request.response_headers_expected||request.response_headers_ready||!request.response_header_error.empty());}
  bool Reserve(const std::string& key) {
    if(requests_.contains(key))return true;
    if(requests_.size()>=4096) {
      const auto oldest=std::find_if(order_.begin(),order_.end(),[this](const std::string& candidate){const auto found=requests_.find(candidate);return found==requests_.end() || Evictable(found->second);});
      if(oldest==order_.end()){failure_("Native network request metadata capacity exceeded");return false;}
      const auto old=requests_.find(*oldest);if(old!=requests_.end()){request_metadata_bytes_-=old->second.metadata_bytes;requests_.erase(old);}order_.erase(oldest);
    }
    order_.push_back(key);return true;
  }
  bool RetainMetadata(Request& request) {
    const size_t bytes=(request.info?Json(request.info).size():0)+(request.response?Json(request.response).size():0)+request.logical_id.size()+request.native_id.size()+request.session.size()+request.frame.size()+request.method.size()+request.url.size()+request.resource.size()+request.redirected_from.size()+request.error.size()+request.response_header_error.size()+request.request_header_error.size()+request.preflight_parent.size()+512;
    if(bytes>33554432){failure_("Native request metadata exceeds the 32 MiB byte budget");return false;}
    while(bytes>33554432-(request_metadata_bytes_-request.metadata_bytes)) {
      const auto oldest=std::find_if(order_.begin(),order_.end(),[&](const std::string& key){const auto found=requests_.find(key);return found==requests_.end() || (&found->second!=&request && Evictable(found->second));});
      if(oldest==order_.end()){failure_("Native active request metadata byte budget exceeded");return false;}
      const auto found=requests_.find(*oldest);if(found!=requests_.end()){request_metadata_bytes_-=found->second.metadata_bytes;requests_.erase(found);}order_.erase(oldest);
    }
    request_metadata_bytes_=request_metadata_bytes_-request.metadata_bytes+bytes;request.metadata_bytes=bytes;return true;
  }
  Request* FindLogical(const std::string& id) {for(auto& [key,request]:requests_)if(request.logical_id==id)return &request;return nullptr;}
  Dict RequestInfo(const Request& request) const {
    auto info=CefDictionaryValue::Create();info->SetString("id",request.logical_id);info->SetString("requestId",request.native_id);
    info->SetString("session",request.session);info->SetString("frameId",request.frame);info->SetString("url",request.url);
    info->SetString("method",request.method);info->SetString("resourceType",request.resource);
    info->SetBool("isNavigationRequest",request.navigation);info->SetBool("headersReady",request.request_headers_ready||(request.finished&&!request.request_headers_expected));
    if(!request.request_header_error.empty())info->SetString("_nativeHeadersError",request.request_header_error);
    info->SetDictionary("timing",Timing(request));
    auto headers=request.info?request.info->GetDictionary("headers"):nullptr;
    info->SetDictionary("headers",headers?headers->Copy(false):CefDictionaryValue::Create());
    if(request.info && request.info->GetType("postData")==VTYPE_STRING)info->SetString("postData",request.info->GetString("postData"));
    if(request.info && request.info->GetType("postDataBase64")==VTYPE_STRING){info->Remove("postData");info->SetString("postDataBase64",request.info->GetString("postDataBase64"));}
    if(request.info && request.info->GetType("postDataBodyId")==VTYPE_STRING)info->SetString("postDataBodyId",request.info->GetString("postDataBodyId"));
    if(request.info && request.info->GetBool("postDataUnavailable"))info->SetBool("postDataUnavailable",true);
    if(request.info && request.info->GetBool("navigationFrameUnavailable"))info->SetBool("navigationFrameUnavailable",true);
    if(request.info && request.info->GetBool("serviceWorker"))info->SetBool("serviceWorker",true);
    if(request.info){if(auto worker=request.info->GetDictionary("workerInfo"))info->SetDictionary("workerInfo",worker->Copy(false));if(request.info->GetBool("contextWorker"))info->SetBool("contextWorker",true);}
    if(!request.blocked_reason.empty())info->SetString("blockedReason",request.blocked_reason);
    if(!request.redirected_from.empty())info->SetString("redirectedFrom",request.redirected_from);
    return info;
  }
  Dict Response(Dict source,const Request& request) const {
    return NativeResponseWireInfo(source,request.native_id,request.session,request.frame,request.method);
  }
  Dict BaseEvent(const char* event,const Request& request) const {
    auto output=CefDictionaryValue::Create();output->SetString("event",event);output->SetDictionary("request",RequestInfo(request));
    if(request.preflight)output->SetBool("suitecutPreflight",true);
    if(request.response){auto response=request.response->Copy(false);response->SetBool("headersReady",request.response_headers_ready);if(!request.response_header_error.empty())response->SetString("_nativeHeadersError",request.response_header_error);output->SetDictionary("response",response);}
    return output;
  }
  static bool EventMatches(Dict data,const std::string& event,const std::string& pattern) {
    auto request=data->GetDictionary("request");return data->GetString("event")==event && request && !request->GetBool("serviceWorker") && (pattern.empty() || Matches(pattern,request->GetString("url")));
  }
  void PrepareBody(Dict info,double deadline) {
    if(!bodies_)return;
    if(info->GetType("postDataBase64")==VTYPE_STRING)bodies_->PrepareOverride(info,deadline);
    else bodies_->Prepare(info,deadline,false);
    if(bodies_->publication().NeedsTick())wake_();
  }
  void Publish(Dict data) {
    if(observer_&&(!observer_active_||observer_active_()))observer_(data->Copy(false));
    // CORS preflight is native browser transport work. Keep its private/HAR
    // observation and terminal accounting, while public events describe author
    // requests. An authored OPTIONS request has Fetch/XHR provenance.
    if(data->GetBool("suitecutPreflight"))return;
    if(bodies_){const auto request=data->GetDictionary("request");const auto gate=post_reads_&&request?post_reads_->Gate(request->GetString("id").ToString()):0;bodies_->publication().Queue(data,gate);wake_();return;}
    PublishReady(data);
  }
  void PublishReady(Dict data) {
    if(disposed_)return;
    data->SetDouble("sequence",static_cast<double>(++sequence_));
    auto json=Json(data);
    if(json.size()>1048576 && data->GetString("event")!="route") {
      auto info=data->GetDictionary("request");
      if(info && (info->HasKey("postData") || info->HasKey("postDataBase64"))){info->Remove("postData");info->Remove("postDataBase64");info->SetBool("postDataUnavailable",true);json=Json(data);}
    }
    if(json.size()>1048576){failure_("Native network event exceeds 1 MiB");return;}
    history_.push_back({data->Copy(false),json.size()});history_bytes_+=json.size();
    while(history_.size()>256 || history_bytes_>4194304) {history_bytes_-=history_.front().bytes;history_.pop_front();}
    emit_(data);
    std::vector<int> matches;
    for(const auto& [id,waiter]:waiters_)if(!waiter.event.empty() && EventMatches(data,waiter.event,waiter.pattern))matches.push_back(id);
    for(const int id:matches){auto done=waiters_.at(id).done;waiters_.erase(id);done(true,"",json);}
  }
  static bool HeaderReady(const Request& request,const std::string& action) {
    return (action=="requestHeaders" && (request.request_headers_ready || !request.request_header_error.empty() || (request.finished&&!request.request_headers_expected))) ||
      (action=="responseHeaders" && (request.response_headers_ready || !request.response_header_error.empty() || (request.finished&&!request.response_headers_expected)));
  }
  static void DoneRequest(const Request& request,const std::string& action,const Done& done) {
    if(action=="requestHeaders" || action=="responseHeaders") {
      if(action=="requestHeaders"&&!request.request_header_error.empty()){done(false,request.request_header_error,"");return;}
      if(action=="responseHeaders"&&!request.response_header_error.empty()){done(false,request.response_header_error,"");return;}
      auto target=action=="requestHeaders"?request.info:request.response;auto headers=target?target->GetDictionary("headers"):nullptr;
      done(true,"",Json(headers?headers:CefDictionaryValue::Create()));return;
    }
    if(action=="response"){auto response=request.response?request.response->Copy(false):nullptr;if(response)response->SetBool("headersReady",request.response_headers_ready);done(true,"",response?Json(response):"null");}
    else {auto value=CefDictionaryValue::Create();if(!request.error.empty())value->SetString("errorText",request.error);else value->SetNull("errorText");done(true,"",Json(value));}
  }
  void FinishWaiters(const Request& request) {
    std::vector<int> ready;
    for(const auto& [id,waiter]:waiters_)if(waiter.request_id==request.logical_id && ((waiter.action=="response" && (request.response || request.finished)) || (waiter.action=="finished" && request.finished) || HeaderReady(request,waiter.action)))ready.push_back(id);
    const auto snapshot=request;for(const int id:ready){const auto found=waiters_.find(id);if(found==waiters_.end())continue;auto waiter=std::move(found->second);waiters_.erase(found);DoneRequest(snapshot,waiter.action,waiter.done);}
  }
  Dict Timing(const Request& request) const {
    auto out=CefDictionaryValue::Create();out->SetDouble("startTime",request.wall_time);out->SetDouble("responseEnd",request.end_time);
    const auto timing=request.response?request.response->GetDictionary("timing"):nullptr;
    for(const auto* key:{"domainLookupStart","domainLookupEnd","connectStart","secureConnectionStart","connectEnd","requestStart","responseStart"})out->SetDouble(key,-1);
    if(timing){for(const auto& [dest,source]:std::vector<std::pair<std::string,std::string>>{{"domainLookupStart","dnsStart"},{"domainLookupEnd","dnsEnd"},{"connectStart","connectStart"},{"secureConnectionStart","sslStart"},{"connectEnd","connectEnd"},{"requestStart","sendStart"},{"responseStart","receiveHeadersEnd"}})out->SetDouble(dest,Number(timing,source.c_str()));}
    return out;
  }
  Dict EffectiveHeaders() const {
    auto headers=context_headers_?context_headers_->Copy(false):CefDictionaryValue::Create();
    if(!headers_)return headers;
    CefDictionaryValue::KeyList names;headers_->GetKeys(names);
    for(const auto&name:names){auto lower=name.ToString();std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
      CefDictionaryValue::KeyList inherited;headers->GetKeys(inherited);for(const auto&key:inherited){auto match=key.ToString();std::transform(match.begin(),match.end(),match.begin(),[](unsigned char c){return char(std::tolower(c));});if(match==lower)headers->Remove(key);}
      headers->SetString(name,headers_->GetString(name));
    }return headers;
  }
  void ConfigureFetch(const std::string& session,Result done,double deadline=0) {
    const bool intercepted=!routes_.empty() || matchers_->pending() || !paused_.empty() || !rejected_.empty() || preflights_->pending() || HasAdmittedParentPolicy();
    const bool active=intercepted || authentication_->enabled();
    if(network_only_sessions_.contains(session)) {
      auto cache=CefDictionaryValue::Create();cache->SetBool("cacheDisabled",intercepted);
      SendControl(session,"Network.setCacheDisabled",cache,std::move(done),deadline);return;
    }
    struct Configuration {size_t pending=2;bool success=true;std::string error;Result done;};
    auto configuration=std::make_shared<Configuration>();configuration->done=std::move(done);
    const auto acknowledge=[configuration](bool ok,const std::string& error){if(!ok){configuration->success=false;configuration->error=error;}if(!--configuration->pending)configuration->done(configuration->success,configuration->error);};
    auto cache=CefDictionaryValue::Create();cache->SetBool("cacheDisabled",intercepted);SendControl(session,"Network.setCacheDisabled",cache,acknowledge,deadline);
    if(!active){SendControl(session,"Fetch.disable",nullptr,acknowledge,deadline);return;}
    auto params=CefDictionaryValue::Create();auto patterns=CefListValue::Create();auto pattern=CefDictionaryValue::Create();
    if(active){pattern->SetString("urlPattern","*");pattern->SetString("requestStage","Request");patterns->SetDictionary(0,pattern);}
    params->SetBool("handleAuthRequests",authentication_->enabled());
    params->SetList("patterns",patterns);SendControl(session,"Fetch.enable",params,acknowledge,deadline);

  }
  void ApplyOffline(const std::string& session,bool offline,Result done) {
    // Real worker ancestry keeps the existing worker offline exclusion even
    // when a specifically supported child now exposes Fetch interception.
    if(network_only_sessions_.contains(session)||worker_parents_.contains(session)){done(true,"");return;}
    auto params=CefDictionaryValue::Create();params->SetBool("offline",offline);params->SetInt("latency",0);params->SetInt("downloadThroughput",-1);params->SetInt("uploadThroughput",-1);
    send_(session,"Network.emulateNetworkConditions",params,std::move(done));
  }
  void ForSessions(const std::function<void(const std::string&,Result)>& apply,Done done) {
    struct Batch {size_t pending;bool success=true;std::string error;Done done;};
    if(sessions_.empty()) sessions_.insert("");
    auto batch=std::make_shared<Batch>(Batch{sessions_.size(),true,"",std::move(done)});
    const auto snapshot=sessions_;
    for(const auto& session:snapshot)apply(session,[batch](bool success,const std::string& error){if(!success){batch->success=false;batch->error=error;}if(!--batch->pending)batch->done(batch->success,batch->error,"{}");});
  }
  void SendControl(const std::string&session,const std::string&method,Dict params,Result done,double deadline){if(bounded_send_)bounded_send_(session,method,params,std::move(done),deadline);else send_(session,method,params,std::move(done));}
  void ConfigureAll(Done done,double deadline=0) {ForSessions([this,deadline](const std::string& session,Result result){ConfigureFetch(session,std::move(result),deadline);},std::move(done));}
  Request* RouteRequest(const std::string& native_id,const std::string& fetch_session) {
    const auto exact=requests_.find(fetch_session+"\n"+native_id);if(exact!=requests_.end())return &exact->second;
    // Fetch may be delivered by a page ancestor while worker Network events
    // retain their genuine child session. Correlate only real request IDs and
    // the recorded target-parent graph; URLs never establish ownership.
    for(auto& [key,request]:requests_) {
      if(request.native_id!=native_id)continue;
      auto session=request.session;
      for(size_t depth=0;depth<32;++depth){const auto parent=worker_parents_.find(session);if(parent==worker_parents_.end())break;session=parent->second;if(session==fetch_session)return &request;}
    }
    return nullptr;
  }
  bool HasAdmittedParentPolicy()const {
    for(const auto&[key,request]:requests_)if(!request.finished&&request.intercepted_policy)return true;
    for(const auto&[key,pause]:paused_)if(!pause.request.native_id.empty()&&pause.request.intercepted_policy)return true;
    return false;
  }
  void MarkAdmittedPolicy(const std::string&id) {
    const auto pause=paused_.find(id);if(pause==paused_.end())return;pause->second.request.intercepted_policy=true;retained_admission_=true;
    if(const auto actual=FindLogical(pause->second.request.logical_id))actual->intercepted_policy=true;
  }
  void MaybeReleaseAdmittedPolicy() {
    if(disposed_||!retained_admission_||!routes_.empty()||matchers_->pending()||!paused_.empty()||!rejected_.empty()||preflights_->pending()||HasAdmittedParentPolicy())return;
    retained_admission_=false;ConfigureAll([this](bool ok,const std::string&error,const std::string&){if(!ok&&!disposed_)failure_("Could not release native admitted interception policy: "+error);});
  }
  Request* AdmittedParentPolicy(const std::string&native,const std::string&session) {
    if(auto actual=RouteRequest(native,session))return actual;
    for(auto&[key,pause]:paused_)if(pause.request.native_id==native&&pause.request.session==session&&pause.request.intercepted_policy)return &pause.request;
    return nullptr;
  }
  void ResolvePreflights() {
    preflights_->Resolve([this](const std::string&native,const std::string&session,const NativeRouteRegistry::Plan&plan){
      const auto actual=RouteRequest(native,session);if(!actual)return NativeNetworkPreflights::Classification{};
      if(!actual->preflight)return NativeNetworkPreflights::Classification{true,false,false,{}};
      const auto parent=actual->preflight_parent.empty()?nullptr:AdmittedParentPolicy(actual->preflight_parent,actual->session);
      if(!actual->preflight_parent.empty()&&!parent)return NativeNetworkPreflights::Classification{};
      if(parent&&!parent->finished&&!parent->intercepted_policy&&routes_.HostPending(plan)&&!parent->route_selection_complete)return NativeNetworkPreflights::Classification{};
      const bool authorized=parent&&!parent->finished&&!parent->preflight&&(parent->intercepted_policy||routes_.Allows(plan,parent->url));
      return NativeNetworkPreflights::Classification{true,true,authorized,authorized?parent->method:std::string{},parent&&parent->route_selection_failed};
    });
  }
  void Pause(Dict data,const std::string& session) {
    const auto fetch=data->GetString("requestId").ToString();auto info=data->GetDictionary("request");
    if(fetch.empty() || !info)return;
    const auto network=data->GetString("networkId").ToString();const auto actual=RouteRequest(network,session);
    if(authentication_->enabled())authentication_->Paused(session,fetch,actual?actual->session:session,network,now_());
    auto plan=routes_.Match(info->GetString("url").ToString());
    const double deadline=now_()+30000;
    // Only OPTIONS requires deferred initiator classification. The method
    // selects this proof obligation; it never identifies an automatic request.
    // A times-limited registration may already be consumed by its genuine
    // admitted parent. Preserve that request's actual policy until terminal.
    if(!network.empty()&&info->GetString("method")=="OPTIONS"&&(!actual||actual->preflight)&&(!plan.candidates.empty()||HasAdmittedParentPolicy())) {
      if(!preflights_->Hold(data,session,std::move(plan),deadline)){RejectOversizedPause(data,session,"Native intercepted request classification capacity exceeded; actual request aborted");return;}
      ResolvePreflights();return;
    }
    if(plan.candidates.empty()) {auto params=CefDictionaryValue::Create();params->SetString("requestId",fetch);send_(session,"Fetch.continueRequest",params,[](bool,const std::string&){});return;}
    PauseReady(data,session,std::move(plan),deadline);
  }
  void AdmitPause(Dict data,const std::string& session,NativeRouteRegistry::Plan plan,double deadline,std::optional<NativeRouteRegistry::Invocation>invocation) {
    if(disposed_||authentication_->failed())return;
    const auto fetch=data->GetString("requestId").ToString();auto info=data->GetDictionary("request");
    if(fetch.empty()||!info)return;
    const auto url=info->GetString("url").ToString();
    if(paused_.size()+rejected_.size()>=1024){auto params=CefDictionaryValue::Create();params->SetString("requestId",fetch);params->SetString("errorReason","BlockedByClient");send_(session,"Fetch.failRequest",params,[](bool,const std::string&){});failure_("Native paused route limit exceeded");return;}
    if(!invocation){if(auto parent=RouteRequest(data->GetString("networkId"),session))parent->route_selection_complete=true;ResolvePreflights();auto params=CefDictionaryValue::Create();params->SetString("requestId",fetch);send_(session,"Fetch.continueRequest",params,[this](bool,const std::string&){if(!disposed_&&routes_.empty()&&!matchers_->pending()&&paused_.empty())ConfigureAll([this](bool ok,const std::string&error,const std::string&){if(!ok&&!disposed_)failure_(error);});});return;}
    const auto replay=har_replays_.find(invocation->registration);const auto admitted_replay=replay==har_replays_.end()?NativeHARRouteRegistration::Policy{}:replay->second;const bool native_replay=bool(admitted_replay);
    auto har_info=har_replays_.empty()?nullptr:info->Copy(false);
    std::shared_ptr<const std::vector<uint8_t>> fetch_body;
    auto raw_body=CaptureNativePostData(info);
    if(raw_body.kind==NativePostData::Kind::Binary)fetch_body=std::make_shared<const std::vector<uint8_t>>(std::move(raw_body.bytes));
    else if(raw_body.kind==NativePostData::Kind::Text){const auto text=info->GetString("postData").ToString();if(text.size()>524288)fetch_body=std::make_shared<const std::vector<uint8_t>>(text.begin(),text.end());}
    const auto id=std::to_string(++route_serial_);
    Request request;request.logical_id="fetch:"+id;request.native_id=data->GetString("networkId").ToString();request.session=session;request.frame=data->GetString("frameId").ToString();request.url=url;request.method=info->GetString("method").ToString();request.info=info->Copy(false);if(auto worker=data->GetDictionary("suitecutWorkerInfo"))request.info->SetDictionary("workerInfo",worker->Copy(false));if(data->GetBool("suitecutContextWorker"))request.info->SetBool("contextWorker",true);if(data->GetBool("suitecutNavigationFrameUnavailable"))request.info->SetBool("navigationFrameUnavailable",true);if(data->GetBool("suitecutServiceWorker"))request.info->SetBool("serviceWorker",true);if(!native_replay)PrepareBody(request.info,deadline);request.resource=data->GetString("resourceType").ToString();request.navigation=request.resource=="Document";
    const auto found=RouteRequest(request.native_id,session);if(found){request.logical_id=found->logical_id;request.session=found->session;request.frame=found->frame;request.wall_time=found->wall_time;request.timestamp=found->timestamp;request.redirected_from=found->redirected_from;
      if(CaptureNativePostData(info).kind==NativePostData::Kind::Unavailable&&!found->info->GetBool("postDataUnavailable")){for(const char*name:{"postData","postDataBase64","postDataBodyId","postDataUnavailable"})request.info->Remove(name);for(const char*name:{"postData","postDataBase64","postDataBodyId"})if(found->info->HasKey(name))request.info->SetValue(name,found->info->GetValue(name)->Copy());}
    }
    const auto logical=request.logical_id;const bool read_post=post_reads_&&!request.native_id.empty()&&request.info->GetBool("postDataUnavailable")&&CaptureNativePostData(info).kind==NativePostData::Kind::Unavailable;
    Paused pause{fetch,session,deadline,{},std::move(plan),std::move(request),CefDictionaryValue::Create()};
    pause.fetch_body=std::move(fetch_body);
    pause.original_url=url;pause.har_info=std::move(har_info);pause.public_prepared=!native_replay;pause.replay_policy=admitted_replay;if(admitted_replay)pause.replay_deadline=deadline;
    if(!native_replay&&Json(BaseEvent("route",pause.request)).size()>1048576-2048){RejectOversizedPause(data,session);return;}
    pause.metadata_bytes=(pause.fetch_body?pause.fetch_body->capacity():0)+pause.plan.bytes()+pause.original_url.capacity()+Json(pause.request.info).size()+Json(pause.overrides).size()+(pause.har_info?Json(pause.har_info).size():0);
    if(pause.metadata_bytes>16777216-pause_metadata_bytes_){RejectOversizedPause(data,session,"Native paused request metadata budget exceeded; actual request aborted");return;}
    if(native_replay){pause_metadata_bytes_+=pause.metadata_bytes;paused_.emplace(id,std::move(pause));MarkAdmittedPolicy(id);ResolvePreflights();if(read_post){const auto&actual=paused_.at(id);post_reads_->Prepare(logical,actual.request.session,actual.request.native_id,actual.deadline,FindLogical(logical)!=nullptr);}ReplayPaused(id,invocation->registration,[this,alive=alive_,failure=failure_](bool ok,const std::string&error,const std::string&){if(!ok&&*alive&&!disposed_)failure(error);});if(read_post)post_reads_->Start(logical);wake_();return;}
    if(!GlobalRouteCallbackLeases().Available()){RejectOversizedPause(data,session,"Native route callback lease capacity exceeded; actual request aborted");return;}
    auto event=RouteEvent(id,pause,*invocation);
    if(!event){RejectOversizedPause(data,session,"Native route callback lease allocation failed; actual request aborted");return;}
    pause_metadata_bytes_+=pause.metadata_bytes;
    paused_.emplace(id,std::move(pause));MarkAdmittedPolicy(id);
    if(read_post){const auto&actual=paused_.at(id);post_reads_->Prepare(logical,actual.request.session,actual.request.native_id,actual.deadline);}
    Publish(event);ResolvePreflights();if(read_post)post_reads_->Start(logical);wake_();
  }
  Dict RouteEvent(const std::string& id,const Paused& pause,const NativeRouteRegistry::Invocation& invocation) const {
    const auto lease=GlobalRouteCallbackLeases().Issue(callback_scope_,id,invocation.policy);if(!lease)return nullptr;
    auto event=BaseEvent("route",pause.request);auto route=CefDictionaryValue::Create();
    route->SetString("callbackScope",std::to_string(callback_scope_));route->SetString("callbackToken",std::to_string(*lease));
    route->SetString("id",id);route->SetString("registrationId",invocation.registration);route->SetBool("exhausted",invocation.exhausted);event->SetDictionary("route",route);return event;
  }
  void Resolve(Dict config,const std::string& action,Done done,int operation=0,double original_deadline=0,uint64_t generation=0) {
    const auto id=config->GetString("routeId").ToString();const auto found=paused_.find(id);
    if(found==paused_.end()){done(false,"Route is already handled or no longer available","");return;}
    auto& pause=found->second;
    if(pause.matching){done(false,"Native route URL matching is already pending","");return;}
    auto params=(action=="continue" || action=="fallback")?pause.overrides->Copy(false):CefDictionaryValue::Create();params->SetString("requestId",pause.fetch_id);
    std::string method;
    if(action=="abort") {
      const auto reason=config->GetString("errorReason").ToString();
      static const std::set<std::string> reasons{"Failed","Aborted","TimedOut","AccessDenied","ConnectionClosed","ConnectionReset","ConnectionRefused","ConnectionAborted","ConnectionFailed","NameNotResolved","InternetDisconnected","AddressUnreachable","BlockedByClient","BlockedByResponse"};
      if(!reasons.contains(reason)){done(false,"Invalid native route abort reason","");return;}
      params->SetString("errorReason",reason);method="Fetch.failRequest";
    } else if(action=="fulfill") {
      const int status=config->GetInt("status");if(status<100 || status>599){done(false,"Invalid route response status","");return;}
      params->SetInt("responseCode",status);if(config->HasKey("statusText"))params->SetString("responsePhrase",config->GetString("statusText"));
      if(config->HasKey("body")){const auto body=config->GetString("body").ToString();if(body.size()>2796204){done(false,"Native route body exceeds 2 MiB","");return;}params->SetString("body",body);}
      auto headers=config->GetList("headers");if(headers)params->SetList("responseHeaders",headers->Copy());method="Fetch.fulfillRequest";
    } else {
      for(const auto* key:{"url","method","postData"})if(config->HasKey(key))params->SetString(key,config->GetString(key));
      auto headers=config->GetList("headers");if(headers)params->SetList("headers",headers->Copy());method="Fetch.continueRequest";
    }
    if(config->GetBool("bodyUpload")) {
      const auto encoded=CefBase64Encode(pause.body.data(),pause.body.size());
      params->SetString(action=="fulfill"?"body":"postData",encoded);
    }
    if(action=="continue" || action=="fallback") {
      if(action=="fallback" && NextNeedsCallback(pause.plan) && !GlobalRouteCallbackLeases().Available()){done(false,"Native route callback lease capacity exceeded; request remains owned for retry","");return;}
      for(const auto* key:{"url","method"})if(params->HasKey(key)){const auto value=params->GetString(key).ToString();if(value.empty() || value.size()>(std::string(key)=="url"?8192:128)){done(false,"Invalid native route override","");return;}}
      if(params->HasKey("postData")){const auto encoded=params->GetString("postData").ToString();auto decoded=encoded.empty()?nullptr:CefBase64Decode(encoded);if(encoded.size()>2796204 || (!encoded.empty() && !decoded) || (decoded && decoded->GetSize()>2097152)){done(false,"Invalid native route post data","");return;}}
      auto staged_fetch_body=pause.fetch_body;
      if(params->HasKey("postData")){auto exact=CefDictionaryValue::Create();exact->SetString("postDataBase64",params->GetString("postData"));auto captured=CaptureNativePostData(exact);staged_fetch_body=std::make_shared<const std::vector<uint8_t>>(std::move(captured.bytes));}
      auto staged_request=pause.request;staged_request.info=staged_request.info->Copy(false);UpdateRequest(staged_request,params);if(params->HasKey("postData"))PrepareBody(staged_request.info,pause.deadline);
      auto staged_har_info=pause.har_info?pause.har_info->Copy(false):nullptr;if(staged_har_info){auto actual=pause.request;actual.info=staged_har_info;UpdateRequest(actual,params);}
      auto staged_overrides=params->Copy(false);staged_overrides->Remove("requestId");
      if(action=="fallback" && pause.public_prepared && NextNeedsCallback(pause.plan) && Json(BaseEvent("route",staged_request)).size()>1048576-2048){done(false,"Native fallback body metadata exceeds 1 MiB; request remains owned for retry","");return;}
      const auto metadata_bytes=(staged_fetch_body?staged_fetch_body->capacity():0)+pause.plan.bytes()+pause.original_url.capacity()+Json(staged_request.info).size()+Json(staged_overrides).size()+(staged_har_info?Json(staged_har_info).size():0);
      if(metadata_bytes>16777216-(pause_metadata_bytes_-pause.metadata_bytes)){done(false,"Native paused request metadata budget exceeded","");return;}
      pause_metadata_bytes_=pause_metadata_bytes_-pause.metadata_bytes+metadata_bytes;pause.metadata_bytes=metadata_bytes;
      pause.fetch_body=std::move(staged_fetch_body);pause.request=std::move(staged_request);pause.overrides=std::move(staged_overrides);pause.har_info=std::move(staged_har_info);
      if(auto native=FindLogical(pause.request.logical_id)) {
        auto staged=*native;staged.info=staged.info->Copy(false);UpdateRequest(staged,params);if(params->HasKey("postData")) {for(const auto* field:{"postData","postDataBase64","postDataBodyId","postDataUnavailable"})staged.info->Remove(field);for(const auto* field:{"postData","postDataBase64","postDataBodyId","postDataUnavailable"})if(pause.request.info->HasKey(field))staged.info->SetValue(field,pause.request.info->GetValue(field)->Copy());}
        *native=std::move(staged);if(!RetainMetadata(*native)){done(false,"Native request metadata byte budget exceeded","");return;}auto updated=BaseEvent("requestupdated",*native);
        if(Json(updated).size()>1048576){auto info=updated->GetDictionary("request");info->Remove("postData");info->Remove("postDataBase64");info->SetBool("postDataUnavailable",true);}
        Publish(updated);
      }
      if(action=="fallback") {
        if(pause.public_prepared && NextNeedsCallback(pause.plan) && Json(BaseEvent("route",pause.request)).size()>1048576-2048){const auto fetch=pause.fetch_id,session=pause.session;auto work=std::move(pause.har_work);upload_bytes_-=pause.body.size();pause_metadata_bytes_-=pause.metadata_bytes;CancelRouteFetches(found->first);paused_.erase(found);auto rejected=CefDictionaryValue::Create();rejected->SetString("requestId",fetch);RejectOversizedPause(rejected,session,"Native fallback request metadata exceeds 1 MiB",true,std::move(work));done(false,"Native fallback request metadata exceeds 1 MiB","");return;}
        SelectFallback(id,std::move(done),operation,original_deadline,generation);return;
      }
      method="Fetch.continueRequest";
    }
    FinishPause(id,method,params,std::move(done),original_deadline);
  }
  static void UpdateRequest(Request& request,Dict overrides) {
    if(overrides->HasKey("url")){request.url=overrides->GetString("url").ToString();request.info->SetString("url",request.url);}
    if(overrides->HasKey("method")){request.method=overrides->GetString("method").ToString();request.info->SetString("method",request.method);}
    if(overrides->HasKey("postData")){request.info->Remove("postData");request.info->Remove("postDataEntries");request.info->Remove("postDataBodyId");request.info->Remove("postDataUnavailable");request.info->SetString("postDataBase64",overrides->GetString("postData"));}
    if(auto source=overrides->GetList("headers")){auto headers=CefDictionaryValue::Create();for(size_t i=0;i<source->GetSize();++i){auto pair=source->GetDictionary(i);if(pair)headers->SetString(pair->GetString("name"),pair->GetString("value"));}request.info->SetDictionary("headers",headers);}
  }
  std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
  uint64_t callback_scope_=GlobalRouteCallbackLeases().Scope().value_or(0);
  std::unique_ptr<NativeRouteMatcherSelections> matchers_;
  std::unique_ptr<NativeNetworkPreflights> preflights_;
  std::unique_ptr<NativeNetworkResponseExtras> request_extras_,response_extras_;
  std::unique_ptr<NativeNetworkAuthentication> authentication_;
  std::unique_ptr<NativeNetworkBodyControls> bodies_;
  std::unique_ptr<NativeNetworkPostPreparation> post_reads_;
  BoundedSend bounded_send_;
  Send send_;Emit emit_;Emit observer_;std::function<bool()> observer_active_;std::function<void(const std::string&)> failure_;std::function<void()> wake_;std::function<double()> now_;
  std::map<std::string,std::string> worker_parents_;
  std::map<std::string,FrameSession> frame_sessions_;
  SessionHandoff session_handoff_;std::function<bool(const std::string&,const std::string&)> frame_descendant_;
  std::map<std::string,NativeHARRouteRegistration::Policy> har_replays_;size_t har_replay_bytes_=0;
  std::map<int,std::string> registration_pending_;std::set<std::string> sessions_,network_only_sessions_;NativeRouteRegistry routes_;std::map<std::string,Paused> paused_;
  std::map<std::string,Rejected> rejected_;
  std::map<std::string,Request> requests_;std::deque<std::string> order_;std::map<int,Waiter> waiters_;
  struct HistoryEntry {Dict event;size_t bytes;};
  std::deque<HistoryEntry> history_;size_t history_bytes_=0,upload_bytes_=0,pause_metadata_bytes_=0,request_metadata_bytes_=0;uint64_t sequence_=0,request_serial_=0,route_serial_=0;Dict headers_,context_headers_;bool offline_=false,disposed_=false,retained_admission_=false;
  std::unique_ptr<NativePageRouteRemovals>page_removals_;
};
}  // namespace suitecut
