// SPDX-License-Identifier: MIT
// Include inside Source; url_predicates.h is included before Source.
  SuiteCutURLPredicateRegistry url_predicates_;
  std::string URLPredicateSnapshot(bool matches, const std::string& url) {
    auto snapshot = CefDictionaryValue::Create(); snapshot->SetBool("matches", matches);
    snapshot->SetString("url", url); return Json(snapshot);
  }
  void PollURLPredicate(int id, std::uint64_t generation, Dict config) {
    if (!operations_.IsCurrent(id,generation)) return;
    if (operations_.IsExpired(id,generation,Now()) && !first_observations_.contains(id)) {
      const auto found = expect_results_.find(id);
      if (found != expect_results_.end()) { const auto snapshot = found->second; Complete(id,true,"",snapshot); }
      else Complete(id,false,"Native URL predicate Timeout exceeded");
      return;
    }
    if (!browser_ || closing_page_) { Complete(id,false,"Native page is closed"); return; }
    const auto frame = browser_->GetMainFrame();
    // CEF can replace the main frame during navigation. The actual page owner
    // still exists; keep the original operation/deadline and retry its frame.
    if (!frame) {
      CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,config] {
        self->PollURLPredicate(id,generation,config);
      }),20);
      return;
    }
    const auto url = frame->GetURL().ToString();
    if (url.size() > 8192) { Complete(id,false,"Native URL exceeds 8192 bytes"); return; }
    // Native URL observation already exists even while a host callback is in
    // flight. Its first acknowledgement gets the bounded transport watchdog.
    expect_results_[id] = URLPredicateSnapshot(config->GetBool("isNot"),url);
    auto token = url_predicates_.Pause(id,generation,config->GetString("clientToken"),
      [this,id,generation,config,url](bool matches) {
        if (!operations_.IsCurrent(id,generation)) return;
        first_observations_.erase(id);
        const auto snapshot = URLPredicateSnapshot(matches,url);
        expect_results_[id] = snapshot;
        if (matches != config->GetBool("isNot") || operations_.IsExpired(id,generation,Now())) {
          Complete(id,true,"",snapshot); return;
        }
        CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,config] {
          self->PollURLPredicate(id,generation,config);
        }),20);
      });
    if (!token) { Complete(id,false,"Native URL predicate capacity or identity is invalid"); return; }
    auto header = Header("urlpredicate"); header->SetInt("id",id);
    header->SetDouble("generation",static_cast<double>(generation));
    header->SetDouble("token",static_cast<double>(*token));
    header->SetString("clientToken",config->GetString("clientToken"));
    header->SetString("url",url); SendPacket(header);
  }
  struct URLWait {
    std::string frame,url,wait,api;
    uint64_t navigation=0;
    bool observed=false,matches=false;
  };
  std::map<int,URLWait>url_waits_;
  void ScheduleURLWait(int id,uint64_t generation,Dict config){
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,config]{self->PollURLWait(id,generation,config);}),20);
  }
  bool URLWaitLoaded(const URLWait&wait,const FrameState&frame){
    if(wait.wait=="networkidle"){
      double last=frame.network_activity;
      for(const auto&[child,state]:frame_states_)if(Descendant(child,wait.frame))last=std::max(last,state.network_activity);
      if(Now()-last<500)return false;
      for(const auto&[key,request]:active_requests_)if(Descendant(request.frame,wait.frame))return false;
      return true;
    }
    return frame.milestones.contains(wait.wait=="domcontentloaded"?"DOMContentLoaded":wait.wait);
  }
  void PollURLWait(int id,uint64_t generation,Dict config){
    if(!operations_.IsCurrent(id,generation))return;
    const auto entry=url_waits_.find(id);if(entry==url_waits_.end())return;
    if(operations_.IsExpired(id,generation,Now())){Complete(id,false,entry->second.api+": Timeout exceeded while waiting for URL");return;}
    if(!browser_||closing_page_){Complete(id,false,"Native page is closed");return;}
    auto&wait=entry->second;const auto frame=frame_states_.find(wait.frame);
    if(frame==frame_states_.end()){Complete(id,false,wait.api+": Frame was detached");return;}
    const auto url=frame->second.url;const auto navigation=frame->second.navigation;
    // Attached frames can precede a committed document URL.
    if(url.empty()){ScheduleURLWait(id,generation,config);return;}
    if(url.size()>8192){Complete(id,false,"Native frame URL exceeds 8192 bytes");return;}
    if(wait.observed&&wait.url==url&&wait.navigation==navigation){
      if(wait.matches&&URLWaitLoaded(wait,frame->second)){Complete(id,true,"",URLPredicateSnapshot(true,url));return;}
      ScheduleURLWait(id,generation,config);return;
    }
    wait.observed=true;wait.url=url;wait.navigation=navigation;wait.matches=false;
    const auto token=url_predicates_.Pause(id,generation,config->GetString("clientToken"),
      [this,id,generation,config,url,navigation](bool matches){
        if(!operations_.IsCurrent(id,generation))return;
        const auto entry=url_waits_.find(id);if(entry==url_waits_.end())return;
        const auto frame=frame_states_.find(entry->second.frame);
        if(frame!=frame_states_.end()&&frame->second.url==url&&frame->second.navigation==navigation)entry->second.matches=matches;
        PollURLWait(id,generation,config);
      });
    if(!token){Complete(id,false,"Native URL wait predicate capacity or identity is invalid");return;}
    auto header=Header("urlpredicate");header->SetInt("id",id);header->SetDouble("generation",static_cast<double>(generation));header->SetDouble("token",static_cast<double>(*token));header->SetString("clientToken",config->GetString("clientToken"));header->SetString("url",url);SendPacket(header);
  }
  void StartURLWait(int id,uint64_t generation,Dict config){
    const auto frame=FrameId(config);const auto target=frame_states_.find(frame);
    if(target==frame_states_.end()){Complete(id,false,"Native URL wait frame was detached");return;}
    const auto wait=config->GetString("waitUntil").ToString(),api=config->GetString("api").ToString();
    if((wait!="commit"&&wait!="load"&&wait!="domcontentloaded"&&wait!="networkidle")||(api!="page.waitForURL"&&api!="frame.waitForURL")){Complete(id,false,"Invalid native URL wait configuration");return;}
    url_waits_.emplace(id,URLWait{frame,"",wait,api});operation_frames_[id]=frame;operation_sessions_[id]=target->second.session;
    PollURLWait(id,generation,config);
  }
  void StartURLPredicate(int id,std::uint64_t generation,Dict config) {
    if (config->GetType("clientToken") != VTYPE_STRING || config->GetString("clientToken").length() == 0 ||
        config->GetString("clientToken").length() > 128 || config->GetType("isNot") != VTYPE_BOOL) {
      Complete(id,false,"Invalid native URL predicate request"); return;
    }
    if(config->GetBool("waitForURL")){StartURLWait(id,generation,config->Copy(false));return;}
    first_observations_[id] = Now()+1000;
    PollURLPredicate(id,generation,config->Copy(false));
  }
  void ContinueURLPredicate(int reply_id,Dict message) {
    std::uint64_t generation=0,token=0;
    if (message->GetType("target") != VTYPE_INT || message->GetInt("target") < 1 ||
        message->GetInt("target") == reply_id || message->GetType("matches") != VTYPE_BOOL ||
        message->GetType("clientToken") != VTYPE_STRING ||
        !ActionHookInteger(message,"generation",generation) || !ActionHookInteger(message,"token",token)) {
      writer_.Reply(reply_id,false,"Invalid native URL predicate continuation"); return;
    }
    const auto target=message->GetInt("target");
    if (!operations_.IsCurrent(target,generation)) { writer_.Reply(reply_id,false,"Native URL predicate continuation is stale"); return; }
    auto resume=url_predicates_.Take(target,generation,token,message->GetString("clientToken"));
    if (!resume) { writer_.Reply(reply_id,false,"Native URL predicate continuation is stale"); return; }
    writer_.Reply(reply_id,true); (*resume)(message->GetBool("matches"));
  }
