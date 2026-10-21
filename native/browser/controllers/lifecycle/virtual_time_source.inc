// SPDX-License-Identifier: MIT
// Included inside Source. The policy, budgets, native observations, ownership
// and original real deadlines all remain inside the executable.
std::unique_ptr<suitecut::NativeVirtualTime> native_virtual_time_;
bool native_virtual_time_raw_used_=false;
uint64_t native_virtual_time_exception_sequence_=0;
suitecut::NativeVirtualTimeCloseFence native_virtual_time_close_fence_;
suitecut::NativeVirtualTimeDocument native_virtual_time_document_;
std::optional<suitecut::NativeVirtualTimeDocument::Identity> NativeVirtualTimeDocumentIdentity()const{
  const auto frame=frame_states_.find(main_frame_id_);
  if(frame==frame_states_.end())return std::nullopt;
  return suitecut::NativeVirtualTimeDocument::Identity{main_frame_id_,frame->second.session,frame->second.context_identity,frame->second.context,native_pristine_renderer_epoch_};
}
bool NativeVirtualTimeDocumentCurrent()const{
  const auto document=NativeVirtualTimeDocumentIdentity();
  return document&&native_virtual_time_document_.Current(*document);
}
bool NativeVirtualTimeTargetEvent(const std::string& method,Dict data,const std::string& session){
  if(!NativeVirtualTimeOwned()||!data)return false;
  bool invalidated=session.empty()&&method=="Runtime.executionContextsCleared";
  if(method=="Page.frameStartedNavigating"||method=="Page.frameDetached")
    invalidated=native_virtual_time_document_.Frame(session,data->GetString("frameId").ToString());
  else if(method=="Runtime.executionContextDestroyed")
    invalidated=native_virtual_time_document_.Destroyed(session,data->GetInt("executionContextId"),data->GetString("executionContextUniqueId").ToString());
  else if(method=="Runtime.executionContextCreated"){
    const auto context=data->GetDictionary("context"),aux=context?context->GetDictionary("auxData"):nullptr;
    if(aux&&aux->GetBool("isDefault")&&native_virtual_time_document_.Frame(session,aux->GetString("frameId").ToString()))
      invalidated=!native_virtual_time_document_.Current({aux->GetString("frameId").ToString(),session,context->GetString("uniqueId").ToString(),context->GetInt("id"),native_pristine_renderer_epoch_});
  }
  if(invalidated){native_virtual_time_->TargetChanged();return true;}
  return false;
}
Dict NativeVirtualTimeInfo(){
  auto info=CefDictionaryValue::Create();
  const auto state=native_virtual_time_?native_virtual_time_->State():suitecut::NativeVirtualTime::Snapshot{};
  info->SetBool("installed",state.installed);info->SetBool("lastAcknowledgedPaused",state.paused);
  info->SetBool("pendingPhysicalCommand",state.physical);info->SetBool("closing",state.closing);
  info->SetInt("queued",static_cast<int>(state.queued));info->SetBool("activeOwner",state.active);
  auto capabilities=CefDictionaryValue::Create();
  capabilities->SetBool("nativeDate",true);capabilities->SetBool("nativeTemporalNow",true);
  capabilities->SetBool("nativePerformance",true);capabilities->SetBool("nativeTimerBudgets",true);capabilities->SetBool("nativeRunForExceptionReceipts",true);
  capabilities->SetBool("sameProcessFrames",true);capabilities->SetBool("performanceTimeOriginRebase",false);capabilities->SetBool("workerPropagation",false);capabilities->SetBool("animationFrameControl",false);
  capabilities->SetBool("idleCallbackControl",false);capabilities->SetBool("contextWide",false);
  capabilities->SetBool("outOfProcessFrames",false);capabilities->SetBool("navigation",false);
  capabilities->SetBool("realtimeResume",false);capabilities->SetBool("fixedDateWithRunningTimers",false);
  capabilities->SetBool("systemTimeRebase",false);capabilities->SetBool("fastForwardAtMostOnce",false);
  info->SetDictionary("capabilities",capabilities);
  auto scope=CefDictionaryValue::Create();scope->SetString("controller","currentDocument");
  scope->SetString("nativeWallClock","rendererProcess");scope->SetBool("isolatedContext",false);
  info->SetDictionary("scope",scope);return info;
}
void NativeVirtualTimePumpClose(){
  if(!native_virtual_time_close_fence_.Post(Now()))return;
  if(!CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this)]{
    if(self->native_virtual_time_close_fence_.EnterPosted(Now())&&self->browser_&&!self->closing_page_)
      self->browser_->GetHost()->CloseBrowser(true);
  }))){
    native_virtual_time_close_fence_.PostRejected();
    Error("CEF rejected native virtual-time close task");WakeDeadlines();
  }
}
void NativeVirtualTimeClose(const std::string& error){
  if(closed_reason_.empty())closed_reason_=error;
  native_virtual_time_close_fence_.Request();NativeVirtualTimePumpClose();WakeDeadlines();
}
void NativeVirtualTimeRendererTerminated(){
  if(!NativeVirtualTimeOwned())return;
  std::vector<suitecut::NativeVirtualTimeCloseFence::Owner> owners;
  for(const auto id:crash_wait_owners_)if(const auto operation=operations_.Get(id))
    owners.push_back({id,operation->generation,operation->deadline_ms});
  if(!native_virtual_time_close_fence_.RendererTerminated(owners,Now())){
    if(closed_reason_.empty())closed_reason_="Native virtual-time crash close observer capacity or identity failed";
  }
  native_virtual_time_->TargetChanged();
}
void NativeVirtualTimeDispatch(const suitecut::NativeVirtualTime::Dispatch& call){
  if(!NativeVirtualTimeDocumentCurrent()){native_virtual_time_->Acknowledge(call.token,false,Now());return;}
  const int assigned=AllocateDevToolsId();
  if(!assigned){native_virtual_time_->Acknowledge(call.token,false,Now());return;}
  std::string method;auto params=CefDictionaryValue::Create();
  using Kind=suitecut::NativeVirtualTime::Kind;
  if(call.kind==Kind::EnableMetrics)method="Performance.enable";
  else if(call.kind==Kind::ObservePerformance)method="Performance.getMetrics";
  else{
    method="Emulation.setVirtualTimePolicy";
    params->SetString("policy",call.kind==Kind::Advance?"advance":"pause");
    if(call.kind==Kind::Advance){params->SetDouble("budget",call.budget);params->SetInt("maxVirtualTimeTaskStarvationCount",100);}
    if(call.epoch_ms)params->SetDouble("initialVirtualTime",*call.epoch_ms/1000.);
  }
  auto callback=[self=CefRefPtr<Source>(this),call](bool ok,const std::string& json){
    if(!self->native_virtual_time_)return;
    if(!self->NativeVirtualTimeDocumentCurrent()){self->native_virtual_time_->TargetChanged();return;}
    std::optional<double> ticks;
    if(ok&&call.kind==Kind::ObservePerformance){
      auto value=CefParseJSON(json,JSON_PARSER_RFC);auto data=value?value->GetDictionary():nullptr;
      auto metrics=data?data->GetList("metrics"):nullptr;
      if(metrics&&metrics->GetSize()<=128)for(size_t i=0;i<metrics->GetSize();++i){
        auto metric=metrics->GetDictionary(i);
        if(metric&&metric->GetString("name")=="Timestamp"&&(metric->GetType("value")==VTYPE_DOUBLE||metric->GetType("value")==VTYPE_INT)){
          const double measured=metric->GetType("value")==VTYPE_INT?metric->GetInt("value"):metric->GetDouble("value");
          if(std::isfinite(measured))ticks=measured*1000.;
        }
      }
      ok=ticks.has_value();
    }
    self->native_virtual_time_->Acknowledge(call.token,ok,Now(),ticks,self->native_virtual_time_exception_sequence_);
  };
  // This physical callback remains registered after the user's command ends.
  devtools_.emplace(assigned,DevToolsCall{call.owner,std::move(callback),"",true,16384,true,false});
  if(!SubmitDevTools(assigned,method,params,"")){
    devtools_.erase(assigned);native_virtual_time_->Acknowledge(call.token,false,Now());
  }
}
void NativeVirtualTimeException(Dict data,const std::string& session){
  if(!session.empty())return;
  const auto details=data?data->GetDictionary("exceptionDetails"):nullptr;if(!details)return;
  if(native_virtual_time_exception_sequence_==std::numeric_limits<uint64_t>::max()){
    if(NativeVirtualTimeOwned()){native_virtual_time_->TargetChanged();}
    return;
  }
  const auto sequence=++native_virtual_time_exception_sequence_;
  if(!native_virtual_time_)return;
  const auto text=details->GetString("text").ToString();
  const bool synchronous=suitecut::NativeVirtualTime::IsSynchronousException(text);
  auto error=text;const auto exception=details->GetDictionary("exception");
  if(exception&&exception->GetType("description")==VTYPE_STRING)error=exception->GetString("description").ToString();
  native_virtual_time_->Exception({sequence,details->GetInt("executionContextId"),synchronous,error});
}
void EnsureNativeVirtualTime(){
  if(native_virtual_time_)return;
  native_virtual_time_=std::make_unique<suitecut::NativeVirtualTime>(suitecut::NativeVirtualTime::Hooks{
    [this](const auto& call){NativeVirtualTimeDispatch(call);},
    [this](int id,uint64_t generation,bool ok,const std::string& error){
      if(operations_.IsCurrent(id,generation))Complete(id,ok,error,ok?Json(NativeVirtualTimeInfo()):"");
    },
    [this](const auto& error){NativeVirtualTimeClose(error);}
  });
}
void NativeVirtualTimeCommand(int id,uint64_t generation,Dict message){
  const auto action=message->GetString("action").ToString();
  if(message->HasKey("frameId")||message->HasKey("frame")){
    Complete(id,false,"Native current-document virtual time requires the root page target");return;
  }
  if(action=="status"){Complete(id,true,"",Json(NativeVirtualTimeInfo()));return;}
  if(NativeVirtualTimeOwned()&&!NativeVirtualTimeDocumentCurrent()){
    native_virtual_time_->TargetChanged();Complete(id,false,"Native virtual-time original document is no longer current");return;
  }
  if(action=="resume"||action=="fastForward"||action=="setFixedTime"||action=="setSystemTime"||action=="pauseAt"||action=="install"){
    Complete(id,false,"This clock behavior requires a native vendor clock backend; Chromium virtual-time policy cannot provide its semantics");return;
  }
  using Action=suitecut::NativeVirtualTime::Action;
  Action kind;double value=0;
  if(action=="installPaused"){
    kind=Action::InstallPaused;
    value=std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
    if(message->HasKey("timeMs")){
      if(message->GetType("timeMs")!=VTYPE_INT&&message->GetType("timeMs")!=VTYPE_DOUBLE){Complete(id,false,"Native virtual-time initial time must be a finite epoch millisecond number");return;}
      value=message->GetType("timeMs")==VTYPE_INT?message->GetInt("timeMs"):message->GetDouble("timeMs");
    }
    if(native_virtual_time_raw_used_){Complete(id,false,"Raw native virtual-time policy was already used; create a fresh page");return;}
    const auto frame=frame_states_.find(main_frame_id_);
    if(frame==frame_states_.end()||!frame->second.context||!navigations_.empty()){
      Complete(id,false,"Native virtual time requires a ready current document");return;
    }
  }else if(action=="pause")kind=Action::Pause;
  else if(action=="advance"||action=="runFor"){
    kind=action=="runFor"?Action::RunFor:Action::Advance;
    if(action=="runFor"&&message->GetType("milliseconds")==VTYPE_STRING){
      const auto parsed=suitecut::NativeVirtualTime::ParseDuration(message->GetString("milliseconds").ToString());
      if(!parsed){Complete(id,false,"Native runFor duration must be SS, MM:SS or HH:MM:SS (two digits per field), with each minute/second component below 60, and a positive total");return;}
      value=*parsed;
    }else{
      if(message->GetType("milliseconds")!=VTYPE_INT&&message->GetType("milliseconds")!=VTYPE_DOUBLE){Complete(id,false,"Native virtual-time budget must be a finite integer millisecond number");return;}
      value=message->GetType("milliseconds")==VTYPE_INT?message->GetInt("milliseconds"):message->GetDouble("milliseconds");
    }
  }else{Complete(id,false,"Unknown native virtual-time action");return;}
  int context=0;
  if(kind==Action::RunFor){
    const auto frame=frame_states_.find(main_frame_id_);
    if(frame==frame_states_.end()||!frame->second.context){Complete(id,false,"Native runFor requires its current main document execution context");return;}
    context=frame->second.context;
  }
  if(kind==Action::InstallPaused&&!NativeVirtualTimeOwned()){
    const auto document=NativeVirtualTimeDocumentIdentity();native_virtual_time_document_.Reset();
    if(!document||!native_virtual_time_document_.Pin(*document)){
      Complete(id,false,"Native virtual time requires the exact ready root document identity");return;
    }
  }
  EnsureNativeVirtualTime();const auto operation=operations_.Get(id);
  if(!operation||!native_virtual_time_->Begin({id,generation,kind,value,operation->deadline_ms==0?std::numeric_limits<double>::max():operation->deadline_ms,context},Now())){
    Complete(id,false,"Invalid native virtual-time request, expired deadline, or 64-owner capacity exceeded");return;
  }
  WakeDeadlines();
}
bool NativeVirtualTimeOwned()const{
  return native_virtual_time_&&native_virtual_time_->OwnsTarget();
}
bool NativeVirtualTimeNeedsTick()const{
  if(native_virtual_time_close_fence_.NeedsTick())return true;
  if(!native_virtual_time_)return false;
  const auto state=native_virtual_time_->State();return state.active||state.queued;
}
