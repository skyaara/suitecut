// SPDX-License-Identifier: MIT
// Private Source composition; initial settings and child bootstrap hooks are
// required before enabling main-realm dispatch. Never infer a barrier from URL.
using NativePristineKey=suitecut::PristineDispatchFactories::Context;
struct NativePristineSession {
  uint64_t lease=0,epoch=1;
  double deadline=0;
  bool armed=false,arming=false,failed=false;
  std::string error;
  std::vector<std::function<void(bool,const std::string&)>>waiting;
};
struct NativePristineContext {
  NativePristineKey key;
  uint64_t ticket=0;
  double deadline=0;
  bool capture_started=false,readable=false,fresh_creation=false,child_creation_ticket=false;
  std::string unavailable;
};
struct NativePristineNavigation {std::string session,loader;
  uint64_t lease=0;
  double deadline=0;
  bool committed=false;
  };
struct NativePristineChild {std::string frame;
  double deadline=0;
  bool waiting=false;
  suitecut::PristineChildCreation creation;
  };
struct NativePristineScript {NativePristineKey key;
  std::string script;
  };
std::unique_ptr<suitecut::PristineDispatchFactories>native_pristine_factories_;
std::map<std::string,NativePristineSession>native_pristine_sessions_;
std::map<std::pair<std::string,std::string>,NativePristineContext>native_pristine_contexts_;
// Logical contextsCleared can precede real realm destruction (or a Runtime
// re-enable snapshot). A known unique identity is never a fresh navigation.
suitecut::PristineCreationIdentities native_pristine_seen_;
std::map<std::pair<std::string,std::string>,NativePristineScript>native_pristine_scripts_;
std::map<std::string,NativePristineNavigation>native_pristine_navigations_;
std::map<std::string,NativePristineChild>native_pristine_children_;
std::map<std::string,bool>native_pristine_child_waiting_;
uint64_t native_pristine_renderer_epoch_=1,native_pristine_next_session_epoch_=0;
double native_pristine_initial_deadline_=0;
  bool native_pristine_initial_started_=false,native_pristine_initial_settled_=false;
std::optional<NativePristineKey>NativePristineOwner(const std::string&frame)const{
  const auto f=frame_states_.find(frame);
  if(f==frame_states_.end()||f->second.context<=0||f->second.context_identity.empty())return std::nullopt;
  const auto session=native_pristine_sessions_.find(f->second.session);
  if(session==native_pristine_sessions_.end())return std::nullopt;
  return NativePristineKey{f->second.session,frame,f->second.loader,f->second.context_identity,f->second.context,native_pristine_renderer_epoch_,session->second.epoch};
}
bool NativePristineCurrent(const NativePristineKey&owner)const{const auto actual=NativePristineOwner(owner.frame);
  return !closing_page_&&!renderer_failed_&&actual&&*actual==owner;
  }
NativePristineContext*NativePristineRecord(const NativePristineKey&owner){const auto found=native_pristine_contexts_.find({owner.session,owner.identity});
  return found!=native_pristine_contexts_.end()&&found->second.key==owner?&found->second:nullptr;
  }
bool NativePristineSend(const NativePristineKey&owner,const std::string&method,Dict params,double deadline,suitecut::PristineDispatchFactories::Reply reply){
  const auto session=native_pristine_sessions_.find(owner.session);
  if(session==native_pristine_sessions_.end()||!native_debugger_domains_)return false;
  const auto receipt=native_debugger_domains_->Hold(session->second.lease);
  if(!receipt)return false;
  auto domains=native_debugger_domains_;
  auto actual=[reply=std::move(reply),domains,receipt](bool ok,Dict result,const std::string&error){
    // A late-created group can admit its cleanup receipt before this one retires.
    reply(ok,result,error);
  domains->Retire(receipt);
  };
  if(!NativeDebuggerSend(owner.session,method,params,deadline,std::move(actual))){domains->Retire(receipt);
  return false;
  }return true;
}
suitecut::PristineDispatchFactories&NativePristineFactories(){
  if(!native_pristine_factories_){suitecut::PristineDispatchFactories::Hooks hooks;
  hooks.now=[]{return Now();
  };
  hooks.current=[this](const auto&owner){return NativePristineCurrent(owner);
  };
    hooks.send=[this](const auto&owner,const auto&method,Dict params,double deadline,auto reply){return NativePristineSend(owner,method,params,deadline,std::move(reply));
  };
    // Failed group-release ACKs run inside the native DevTools observer. Post
    // teardown through the existing owner; only real OnBeforeClose retires it.
    hooks.close_channel=[this](const std::string&,const std::string&error){NetworkFatal(error);};
  hooks.wake=[this]{WakeDeadlines();
  };
  native_pristine_factories_=std::make_unique<suitecut::PristineDispatchFactories>(std::move(hooks));
  }
  return *native_pristine_factories_;
}
std::optional<suitecut::PristineDispatchFactories::Factory>NativePristineFind(const std::string&frame){
  const auto owner=NativePristineOwner(frame);
  if(!owner||!native_pristine_factories_)return std::nullopt;
  const auto record=NativePristineRecord(*owner);
  if(!record||!record->readable)return std::nullopt;
  return native_pristine_factories_->Find(*owner);
}
// Called only from the successful genuine capture receipt, before exposing a
// factory or releasing its initial/bootstrap hold. Script bytes/URLs are never
// used to classify this anonymous Inspector script as native.
void NativePristineCapture(uint64_t ticket,const NativePristineKey&key,double deadline,std::function<void(bool,const std::string&)>ready){
  NativePristineFactories().Capture(ticket,[this,key,deadline,ready=std::move(ready)](bool ok,auto captured,const std::string&error){
    if(!ok||!captured||captured->owner!=key||!NativePristineCurrent(key)||(deadline!=0&&Now()>=deadline)){
      if(native_pristine_factories_)native_pristine_factories_->Invalidate(key,"Native pristine capture provenance owner or original deadline unavailable");
      ready(false,error.empty()?"Native pristine capture provenance owner or original deadline unavailable":error);return;
    }
    const auto object=captured->object,group=captured->group;
    auto params=CefDictionaryValue::Create();params->SetString("objectId",object);params->SetBool("ownProperties",true);params->SetBool("generatePreview",false);
    auto finish=[this,key,deadline,object,group,ready](bool success,Dict result,const std::string&failure){
      const auto actual=native_pristine_factories_?native_pristine_factories_->Find(key):std::nullopt;
      if(!success||!actual||actual->object!=object||actual->group!=group||!NativePristineCurrent(key)||(deadline!=0&&Now()>=deadline)){
        if(native_pristine_factories_)native_pristine_factories_->Invalidate(key,"Native pristine FunctionLocation original owner unavailable");
        ready(false,failure.empty()?"Native pristine FunctionLocation original owner or deadline unavailable":failure);return;
      }
      const auto script=suitecut::PristineFunctionScript(result);
      if(!script){
        native_pristine_factories_->Invalidate(key,"Native pristine function has no actual FunctionLocation script receipt");
        ready(false,"Native pristine function has no actual FunctionLocation script receipt");return;
      }
      // Coverage records keep this exact attestation for already-collected as
      // well as future script data; actual realm/group lifetime is independent.
      if(!NativeCoverageMarkNativeScript(key.session,key.context,*script)){
        native_pristine_factories_->Invalidate(key,"Native pristine function provenance capacity or identity unavailable");
        ready(false,"Native pristine function provenance capacity or identity unavailable");return;
      }
      // Registration is an honest native-script attestation even if its
      // bounded work crosses the caller deadline; capability publication is not.
      if(!NativePristineCurrent(key)||(deadline!=0&&Now()>=deadline)){
        native_pristine_factories_->Invalidate(key,"Native pristine FunctionLocation publication original deadline or owner unavailable");
        ready(false,"Native pristine FunctionLocation publication original deadline or owner unavailable");return;
      }
      ready(true,"");
    };
    if(!NativePristineSend(key,"Runtime.getProperties",params,deadline,finish))finish(false,nullptr,"Native pristine FunctionLocation command was not accepted");
  });
}
void NativePristineSessionReady(const std::string&session,uint64_t lease,bool ok,const std::string&error){
  const auto found=native_pristine_sessions_.find(session);
  if(found==native_pristine_sessions_.end()||found->second.lease!=lease)return;
  if(renderer_failed_||closing_page_||found->second.failed)ok=false;
  found->second.arming=false;
  found->second.armed=ok;
  found->second.failed=!ok;
  found->second.error=error.substr(0,1024);
  auto waiting=std::move(found->second.waiting);
  found->second.waiting.clear();
  if(ok){
    for(const auto&[id,frame]:frame_states_)if(frame.session==session&&frame.context>0)NativePristineContextCreated(id);
    for(auto&[identity,record]:native_pristine_contexts_)if(record.key.session==session)NativePristineAuthorizeChild(record);
  }
  if(!ok)for(auto&[identity,record]:native_pristine_contexts_)if(record.key.session==session&&!record.readable){
    record.unavailable=error.empty()?"Native pristine bootstrap admission unavailable":error.substr(0,1024);
    if(native_pristine_factories_)native_pristine_factories_->Invalidate(record.key,record.unavailable);
  }
  for(auto&done:waiting)done(ok,error);
  if(session.empty())SettingsCommit();
}
void NativePristineEnsureSession(const std::string&session,double deadline,std::function<void(bool,const std::string&)>ready){
  const auto existing=native_pristine_sessions_.find(session);
  if(existing!=native_pristine_sessions_.end()){
    if(existing->second.armed){ready(true,"");
  return;
  }if(existing->second.failed){ready(false,existing->second.error);
  return;
  }
    if(existing->second.waiting.size()>=64){ready(false,"Native pristine session waiter capacity exceeded");
  return;
  }existing->second.waiting.push_back(std::move(ready));
  return;
  }
  if(native_pristine_sessions_.size()>=1024){ready(false,"Native pristine session capacity exceeded");
  return;
  }const auto lease=NativeDebuggerLease();
  if(!lease){ready(false,"Native pristine lease identity exhausted");
  return;
  }
  if(native_pristine_next_session_epoch_==std::numeric_limits<uint64_t>::max()){ready(false,"Native pristine session epoch exhausted");return;}
  NativePristineSession state;
  state.epoch=++native_pristine_next_session_epoch_;
  state.lease=lease;
  state.deadline=deadline;
  state.waiting.push_back(std::move(ready));
  native_pristine_sessions_.emplace(session,std::move(state));
  NativeDebuggerDomains()->Acquire(session,lease,suitecut::DebuggerDomains::Kind::Factory,deadline,[this,session,lease](bool ok,const std::string&error){
    if(!ok){NativePristineSessionReady(session,lease,false,error);return;}
    const auto state=native_pristine_sessions_.find(session);
    if(state==native_pristine_sessions_.end()||state->second.lease!=lease)return;
    if(state->second.failed||renderer_failed_||closing_page_){NativePristineSessionReady(session,lease,false,"Native pristine owner retired before arm");return;}
    state->second.arming=true;
    // A genuine child wait holds author execution independently of the arm
    // reply. Its own original budget must classify a pause preceding that ACK.
    for(const auto&[id,frame]:frame_states_)if(frame.session==session&&frame.context>0)NativePristineContextCreated(id);
    for(auto&[identity,record]:native_pristine_contexts_)if(record.key.session==session)NativePristineAuthorizeChild(record);
    auto params=CefDictionaryValue::Create();params->SetString("url",suitecut::kNativeSnapshotScript.name);params->SetInt("lineNumber",0);params->SetInt("columnNumber",static_cast<int>(std::string_view(suitecut::kNativeSnapshotScript.source).find("if(typeof nativeSnapshot")));
    NativeDebuggerDomains()->Arm(lease,params,[this,session](Dict pause,double deadline,suitecut::DebuggerDomains::Resume resume){NativePristinePaused(session,pause,deadline,std::move(resume));},[this,session,lease](bool ok,const std::string&error){NativePristineSessionReady(session,lease,ok,error);});
  });
}
void NativePristineAuthorizeChild(NativePristineContext&record){
  if(record.ticket||record.capture_started||record.readable)return;
  const auto child=native_pristine_children_.find(record.key.session);
  const auto session=native_pristine_sessions_.find(record.key.session);
  if(child==native_pristine_children_.end()||session==native_pristine_sessions_.end()||!session->second.armed||session->second.failed)return;
  if(!child->second.creation.Select(record.key.frame,record.key.identity,session->second.lease,true,record.fresh_creation,Now()))return;
  record.deadline=child->second.deadline;
  record.ticket=NativePristineFactories().Expect(record.key,suitecut::PristineDispatchFactories::Barrier::NewDocumentBootstrap,record.deadline);
  if(record.ticket){record.child_creation_ticket=true;record.unavailable.clear();}
}
void NativePristineContextCreated(const std::string&frame){
  const auto owner=NativePristineOwner(frame);
  if(!owner)return;
  const auto identity=std::make_pair(owner->session,owner->identity);
  const auto existing=native_pristine_contexts_.find(identity);
  if(existing!=native_pristine_contexts_.end()&&existing->second.key==*owner)return;
  const auto observation=native_pristine_seen_.Observe(owner->session_epoch,owner->identity);
  if(observation==suitecut::PristineCreationIdentities::Observation::Capacity)return;
  const bool seen=observation==suitecut::PristineCreationIdentities::Observation::Known;
  // An initial frame-tree reply may fill a loader after Runtime supplied its
  // unique context. Replace only metadata; no later snapshot becomes fresh.
  if(existing!=native_pristine_contexts_.end()){
    const auto old=existing->second.key;
    NativePristineRetireContext(old,"Actual native context loader binding changed");
  }
  if(native_pristine_contexts_.size()>=1024)return;
  NativePristineContext record;
  record.key=*owner;
  record.fresh_creation=!seen;
  record.unavailable="No proven native pre-author bootstrap for this document";
  const auto nav=native_pristine_navigations_.find(frame);
  const auto session=native_pristine_sessions_.find(owner->session);
  if(!seen&&nav!=native_pristine_navigations_.end()&&nav->second.committed&&nav->second.session==owner->session&&nav->second.loader==owner->loader&&session!=native_pristine_sessions_.end()&&session->second.armed&&nav->second.lease==session->second.lease){
    record.deadline=nav->second.deadline;
  record.ticket=NativePristineFactories().Expect(*owner,suitecut::PristineDispatchFactories::Barrier::NewDocumentBootstrap,record.deadline);
    if(record.ticket)record.unavailable.clear();
    native_pristine_navigations_.erase(nav);
  }
  auto inserted=native_pristine_contexts_.emplace(identity,std::move(record));
  NativePristineAuthorizeChild(inserted.first->second);
}
void NativePristineNavigationStarted(const std::string&session,const std::string&frame){
  const auto owner=native_pristine_sessions_.find(session);
  if(!ready_||owner==native_pristine_sessions_.end()||!owner->second.armed||native_pristine_navigations_.size()>=1024)return;
  native_pristine_navigations_[frame]=NativePristineNavigation{session,"",owner->second.lease,Now()+30000,false};
}
void NativePristineCommitted(const std::string&session,const std::string&frame,bool restored){
  const auto nav=native_pristine_navigations_.find(frame);
  if(nav==native_pristine_navigations_.end())return;
  const auto actual=frame_states_.find(frame);
  if(restored||actual==frame_states_.end()||nav->second.session!=session){native_pristine_navigations_.erase(nav);
  return;
  }nav->second.loader=actual->second.loader;
  nav->second.committed=true;
}
void NativePristineChildStart(const std::string&session,const std::string&frame,bool actually_waiting,double deadline,std::function<void(bool,const std::string&)>ready){
  if(native_pristine_children_.size()<1024)native_pristine_children_[session]={frame,deadline,actually_waiting,suitecut::PristineChildCreation(actually_waiting,frame,deadline)};
  NativePristineEnsureSession(session,deadline,[ready=std::move(ready)](bool,const std::string&){ready(true,"");});
  const auto child=native_pristine_children_.find(session);
  const auto state=native_pristine_sessions_.find(session);
  if(child!=native_pristine_children_.end()&&state!=native_pristine_sessions_.end()){
    child->second.creation.BindLease(state->second.lease);
    for(auto&[identity,record]:native_pristine_contexts_)if(record.key.session==session)NativePristineAuthorizeChild(record);
  }
}
void NativePristineChildResumed(const std::string&session){const auto found=native_pristine_children_.find(session);
  if(found!=native_pristine_children_.end()){found->second.waiting=false;found->second.creation.Released();}
  native_pristine_child_waiting_.erase(session);
  }
bool NativePristineInitialGate(){
  if(!settings_bootstrap_||settings_committed_||native_pristine_initial_settled_)return true;
  if(Now()>=native_pristine_initial_deadline_){native_pristine_initial_settled_=true;
  return true;
  }
  if(!settings_empty_seen_)return false;
  const auto session=native_pristine_sessions_.find("");
  if(session==native_pristine_sessions_.end())return false;
  if(session->second.failed){native_pristine_initial_settled_=true;
  return true;
  }if(!session->second.armed)return false;
  const auto owner=NativePristineOwner(main_frame_id_);
  if(!owner)return false;
  NativePristineContextCreated(main_frame_id_);
  auto*record=NativePristineRecord(*owner);
  if(!record)return false;
  if(record->capture_started)return false;
  record->deadline=native_pristine_initial_deadline_;
  record->capture_started=true;
  record->unavailable.clear();
  record->ticket=NativePristineFactories().Expect(*owner,suitecut::PristineDispatchFactories::Barrier::InitialSettingsHold,record->deadline);
  NativePristineCapture(record->ticket,*owner,record->deadline,[this,key=*owner](bool ok,const std::string&error){
    auto*current=NativePristineRecord(key);if(current){current->readable=ok&&NativePristineCurrent(key);current->unavailable=ok?"":error.substr(0,1024);}native_pristine_initial_settled_=true;SettingsCommit();
  });
  return native_pristine_initial_settled_;
}
std::optional<NativePristineKey>NativePristinePauseOwner(const std::string&session,Dict pause){
  if(!pause||!native_pristine_sessions_.contains(session))return std::nullopt;
  auto frames=pause->GetList("callFrames");
  auto first=frames&&frames->GetSize()?frames->GetDictionary(0):nullptr;
  auto location=first?first->GetDictionary("location"):nullptr;
  if(!location)return std::nullopt;
  const auto script=native_pristine_scripts_.find({session,location->GetString("scriptId").ToString()});
  if(script==native_pristine_scripts_.end())return std::nullopt;
  auto*record=NativePristineRecord(script->second.key);
  if(!record||!NativePristineCurrent(record->key))return std::nullopt;
  return record->key;
}
void NativePristineObserveDebugger(const std::string&method,Dict params,const std::string&session){
  if(!params||!native_pristine_sessions_.contains(session)||method!="Debugger.scriptParsed")return;
  if(params->GetType("hasSourceURL")!=VTYPE_BOOL||params->GetBool("hasSourceURL"))return;
  const bool local_child=params->GetString("url")==suitecut::kNativeLocalChildSnapshotScript.name&&
     params->GetString("embedderName")==suitecut::kNativeLocalChildSnapshotScript.name;
  if(!local_child&&(params->GetString("url")!=suitecut::kNativeSnapshotScript.name||
     params->GetString("embedderName")!=suitecut::kNativeSnapshotScript.name))return;
  if(params->GetString("hash")!=suitecut::CoverageControls::SourceHash(local_child?suitecut::kNativeLocalChildSnapshotScript.source:suitecut::kNativeSnapshotScript.source))return;
  const auto script=params->GetString("scriptId").ToString();
  if(script.empty()||script.size()>128||native_pristine_scripts_.size()>=1024)return;
  for(auto&[identity,record]:native_pristine_contexts_){
    if(record.key.session==session&&record.key.context==params->GetInt("executionContextId")&&NativePristineCurrent(record.key)){
      if(local_child){
        // This synchronous bootstrap deliberately has no debugger capture
        // barrier. Do not authorize a factory from its later script identity.
        record.readable=false;record.capture_started=true;
        record.unavailable="Pristine main-world dispatch has no pre-author capture barrier for this same-process child";
        if(native_pristine_factories_)native_pristine_factories_->Invalidate(record.key,record.unavailable);
        break;
      }
      native_pristine_scripts_[{session,script}]={record.key,script};
  break;
    }
  }
}
double NativePristinePauseDeadline(const std::string&session,Dict pause){
  const auto owner=NativePristinePauseOwner(session,pause);
  auto*record=owner?NativePristineRecord(*owner):nullptr;
  // A replay, warmed realm or previously completed capture cannot authorize
  // another capture. Immediate refusal merely resumes the genuine pause.
  return record&&record->ticket&&!record->capture_started&&!record->readable&&record->unavailable.empty()?record->deadline:Now();
}
void NativePristinePauseExpired(uint64_t lease,Dict pause,const std::string&error){
  for(const auto&[session,state]:native_pristine_sessions_){
    if(state.lease!=lease)continue;
    const auto owner=NativePristinePauseOwner(session,pause);
    auto*record=owner?NativePristineRecord(*owner):nullptr;
    // Exact packet identity, not the latest pause on this Inspector session.
    // A refused replay must never invalidate an already valid factory.
    if(record&&record->ticket&&!record->readable&&record->unavailable.empty()){
      record->capture_started=true;
  record->unavailable=error.substr(0,1024);
      if(native_pristine_factories_)native_pristine_factories_->Invalidate(*owner,error);
    }
    return;
  }
}
void NativePausedSnapshot(const NativePristineKey&key,const std::string&call_frame,int local_scope,double budget,suitecut::DebuggerDomains::Ready finish){
      // Only this exact owned native pause may receive snapshot bytes. Resolve
      // authoritative browser state now, never a renderer cache or page value.
      // The slot is function-local and disappears when the bootstrap returns.
      auto info=engine_.contexts()->BrowserExtraInfo(page_id_);
      auto snapshot=info?info->GetDictionary("suitecutInitScripts"):nullptr;
      if(call_frame.empty()||local_scope<0||!snapshot||!NativePristineCurrent(key)||Now()>=budget){NetworkFatal("Native paused snapshot identity or deadline unavailable");finish(false,"Native paused snapshot identity or deadline unavailable");return;}
      auto body=CefValue::Create();body->SetDictionary(snapshot);
      auto value=CefDictionaryValue::Create();value->SetString("value",CefWriteJSON(body,JSON_WRITER_DEFAULT));
      auto delivery=CefDictionaryValue::Create();delivery->SetString("callFrameId",call_frame);
      delivery->SetInt("scopeNumber",local_scope);delivery->SetString("variableName","nativeSnapshot");delivery->SetDictionary("newValue",value);
      if(!NativePristineSend(key,"Debugger.setVariableValue",delivery,budget,[this,key,budget,finish](bool delivered,Dict result,const std::string&failure){
        const bool accepted=delivered&&result&&!result->HasKey("exceptionDetails")&&NativePristineCurrent(key)&&Now()<budget;
        if(!accepted){NetworkFatal("Native paused init snapshot delivery failed");finish(false,failure.empty()?"Native paused init snapshot delivery failed":failure);return;}
        finish(true,"");
      })){NetworkFatal("Native paused init snapshot delivery capacity exceeded");finish(false,"Native paused init snapshot delivery capacity exceeded");}
}
void NativePristinePaused(const std::string&session,Dict pause,double deadline,suitecut::DebuggerDomains::Resume resume){
  auto original_resume=std::move(resume);
  resume=[self=CefRefPtr<Source>(this),session,original_resume=std::move(original_resume)](suitecut::DebuggerDomains::Ready done)mutable{
    self->NativeWebSocketPolicyAttach(session,[self,original_resume=std::move(original_resume),done=std::move(done)](bool ok,const std::string&error)mutable{
      if(!ok){self->NetworkFatal(error);done(false,error);return;}
      original_resume(std::move(done));
    });
  };
  const auto selected=NativePristinePauseOwner(session,pause);
  if(!selected){resume([](bool,const std::string&){});
  return;
  }
  const auto key=*selected;
  auto*record=NativePristineRecord(key);
  if(!record||!NativePristineCurrent(key)){resume([](bool,const std::string&){});
  return;
  }
  if(record->readable&&NativePristineFactories().Find(key)){resume([](bool,const std::string&){});
  return;
  }
  if(!record->ticket||record->capture_started){resume([](bool,const std::string&){});
  return;
  }
  if(record->child_creation_ticket){
    const auto child=native_pristine_children_.find(session);
    const auto state=native_pristine_sessions_.find(session);
    if(child==native_pristine_children_.end()||state==native_pristine_sessions_.end()||!state->second.armed||!child->second.creation.Consume(key.frame,key.identity,state->second.lease,Now())){
      record->unavailable="Actual child first-document bootstrap admission was consumed or expired";
      NativePristineFactories().Invalidate(key,record->unavailable);resume([](bool,const std::string&){});return;
    }
  }
  record->capture_started=true;
  auto frames=pause->GetList("callFrames");
  auto first=frames&&frames->GetSize()?frames->GetDictionary(0):nullptr;
  auto location=first?first->GetDictionary("location"):nullptr;
  const auto script=location?location->GetString("scriptId").ToString():"";
  auto params=CefDictionaryValue::Create();
  params->SetString("scriptId",script);
  auto actual_resume=std::make_shared<suitecut::DebuggerDomains::Resume>(std::move(resume));
  auto finish=[this,key,actual_resume](bool ok,const std::string&error){
    auto*current=NativePristineRecord(key);
  if(current&&!ok)current->unavailable=error.substr(0,1024);
    auto once=std::move(*actual_resume);
  if(!once)return;
  once([this,key,ok](bool resumed,const std::string&error){auto*current=NativePristineRecord(key);if(current){current->readable=ok&&resumed&&NativePristineCurrent(key)&&native_pristine_factories_&&native_pristine_factories_->Find(key).has_value();if(!current->readable)current->unavailable=error.substr(0,1024);}});
  };
  const auto call_frame=first?first->GetString("callFrameId").ToString():"";
  int local_scope=-1;const auto scopes=first?first->GetList("scopeChain"):nullptr;
  if(scopes)for(size_t index=0;index<scopes->GetSize();++index){auto scope=scopes->GetDictionary(index);if(scope&&scope->GetString("type")=="local"){local_scope=static_cast<int>(index);break;}}
  if(!NativePristineSend(key,"Debugger.getScriptSource",params,deadline,[this,key,call_frame,local_scope,finish](bool ok,Dict source,const std::string&error){
    auto*current=NativePristineRecord(key);if(!ok||!source||source->GetString("scriptSource")!=suitecut::kNativeSnapshotScript.source||!current||!NativePristineCurrent(key)||Now()>=current->deadline){if(native_pristine_factories_)native_pristine_factories_->Invalidate(key,"Native bootstrap source or original admission unavailable");finish(false,error.empty()?"Native bootstrap source or original admission unavailable":error);return;}
    const auto budget=current->deadline;
    NativePristineCapture(current->ticket,key,budget,[this,key,call_frame,local_scope,budget,finish](bool ok,const std::string&error){
      if(!ok){finish(false,error);return;}
      NativePausedSnapshot(key,call_frame,local_scope,budget,finish);
    });
  }))finish(false,"Native pristine source inspection capacity exceeded");
}
void NativePristineStart(){
  native_pristine_initial_deadline_=Now()+30000;
  native_debugger_event_observer_=[this](const auto&method,Dict params,const auto&session){NativePristineObserveDebugger(method,params,session);
  };
  native_debugger_pause_deadline_=[this](const auto&session,Dict params){return NativePristinePauseDeadline(session,params);
  };
  native_debugger_pause_expired_=[this](uint64_t lease,Dict pause,const auto&error){NativePristinePauseExpired(lease,pause,error);
  };
  NativePristineEnsureSession("",native_pristine_initial_deadline_,[](bool,const std::string&){});
}

void NativePristineRetireContext(const NativePristineKey&key,const std::string&reason){
  if(native_pristine_factories_)native_pristine_factories_->ContextDestroyed(key,reason);
  for(auto it=native_pristine_scripts_.begin();it!=native_pristine_scripts_.end();){
    if(it->second.key==key)it=native_pristine_scripts_.erase(it);
  else ++it;
  }
  const auto found=native_pristine_contexts_.find({key.session,key.identity});
  if(found!=native_pristine_contexts_.end()&&found->second.key==key)native_pristine_contexts_.erase(found);
}
void NativePristineFrameDetached(const std::string&frame){
  std::vector<NativePristineKey>selected;
  for(const auto&[id,record]:native_pristine_contexts_)if(record.key.frame==frame)selected.push_back(record.key);
  for(const auto&key:selected)NativePristineRetireContext(key,"Actual native frame detached");
  native_pristine_navigations_.erase(frame);
  for(auto it=native_pristine_children_.begin();it!=native_pristine_children_.end();){
    if(it->second.frame==frame){native_pristine_child_waiting_.erase(it->first);it=native_pristine_children_.erase(it);}else ++it;
  }
}
void NativePristineContextDestroyed(const std::string&session,int context,const std::string&identity){
  std::vector<NativePristineKey>selected;
  for(const auto&[id,record]:native_pristine_contexts_){
    if(record.key.session==session&&record.key.context==context&&(identity.empty()||record.key.identity==identity))selected.push_back(record.key);
  }
  for(const auto&key:selected){
    NativePristineRetireContext(key,"Actual native main execution context destroyed");
    native_pristine_seen_.Destroyed(key.session_epoch,key.identity);
  }
  const auto owner=native_pristine_sessions_.find(session);
  if(!identity.empty()&&owner!=native_pristine_sessions_.end())native_pristine_seen_.Destroyed(owner->second.epoch,identity);
}
void NativePristineContextsCleared(const std::string&session){
  // Context loss is NOT Inspector session death. Accepted factory/debugger
  // receipts and real release-group callbacks remain in their physical ledgers.
  std::vector<NativePristineKey>selected;
  for(const auto&[id,record]:native_pristine_contexts_)if(record.key.session==session)selected.push_back(record.key);
  for(const auto&key:selected)NativePristineRetireContext(key,"Actual native main execution contexts cleared");
}
void NativePristineSessionDetached(const std::string&session){
  // Only the genuine Target.detachedFromTarget/session-not-found seam calls it.
  NativePristineContextsCleared(session);
  if(native_pristine_factories_)native_pristine_factories_->SessionClosed(session);
  const auto state=native_pristine_sessions_.find(session);
  if(state!=native_pristine_sessions_.end())native_pristine_seen_.SessionClosed(state->second.epoch);
  native_pristine_sessions_.erase(session);
  native_pristine_children_.erase(session);
  native_pristine_child_waiting_.erase(session);
  for(auto it=native_pristine_navigations_.begin();it!=native_pristine_navigations_.end();){
    if(it->second.session==session)it=native_pristine_navigations_.erase(it);
  else ++it;
  }
}
void NativePristineRendererFailed(){
  // Root Inspector state can outlive its renderer. Never reuse armed readiness
  // as evidence after a renderer restart, and never fabricate session closure.
  // CEF reports this browser's actual main renderer death. Its root-session
  // V8 groups are gone; OOPIF sessions may belong to surviving processes.
  // Preserve every accepted Inspector callback and shared Debugger Hold.
  if(native_pristine_factories_)native_pristine_factories_->RealmDestroyed("",native_pristine_renderer_epoch_,"Actual native renderer process terminated");
  ++native_pristine_renderer_epoch_;
  std::vector<NativePristineKey>selected;
  for(const auto&[id,record]:native_pristine_contexts_)selected.push_back(record.key);
  for(const auto&key:selected)NativePristineRetireContext(key,"Actual native renderer process terminated");
  for(auto&[id,session]:native_pristine_sessions_){session.armed=false;
  session.arming=false;
  session.failed=true;
  session.error="Pristine main dispatch recovery requires a proven native rearm barrier";
  }
  native_pristine_navigations_.clear();
  native_pristine_children_.clear();
  native_pristine_child_waiting_.clear();
}
void NativePristineClosing(){
  if(native_pristine_factories_){
    std::vector<NativePristineKey>selected;
    for(const auto&[id,record]:native_pristine_contexts_)selected.push_back(record.key);
    for(const auto&key:selected)NativePristineRetireContext(key,"Native page closing");
  }
  if(native_debugger_domains_)for(const auto&[id,session]:native_pristine_sessions_)native_debugger_domains_->Release(session.lease,Now()+30000,[](bool,const std::string&){});
}
void NativePristineClosed(){
  if(native_pristine_factories_)native_pristine_factories_->Closed();
  native_pristine_sessions_.clear();
  native_pristine_contexts_.clear();
  native_pristine_seen_.Closed();
  native_pristine_scripts_.clear();
  native_pristine_navigations_.clear();
  native_pristine_children_.clear();
  native_pristine_child_waiting_.clear();
  native_debugger_event_observer_={};
  native_debugger_pause_deadline_={};
  native_debugger_pause_expired_={};
}
void NativePristineTick(){
  if(native_pristine_factories_)native_pristine_factories_->Tick(Now());
  if(!native_pristine_initial_settled_&&native_pristine_initial_deadline_>0&&Now()>=native_pristine_initial_deadline_){native_pristine_initial_settled_=true;
  SettingsCommit();
  }
}
bool NativePristineNeedsTick()const{
  return(native_pristine_factories_&&native_pristine_factories_->NeedsTick())||(!native_pristine_initial_settled_&&settings_bootstrap_&&native_pristine_initial_deadline_>0);
}

// Runtime/Page lifecycle notifications are native ownership evidence. Keep
// these domains enabled for the exact reserved Inspector session until actual
// detach/close, including acquisition, retirement and failed-recovery states.
bool NativePristineRawLifecycleConflict(const std::string&method,const std::string&session)const{
  return (method=="Runtime.disable"||method=="Page.disable")&&native_pristine_sessions_.contains(session);
}
