// SPDX-License-Identifier: MIT
// Root Source adapter. Central Source admission/lifecycle hooks are composed by
// the embedder; neither a logical abort nor realm death retires Inspector work.
std::unique_ptr<suitecut::DeveloperDebuggerControls> native_developer_debugger_;
suitecut::DeveloperDebuggerControls::Document NativeDeveloperDocument()const{
  const auto root=frame_states_.find(main_frame_id_);
  if(closing_page_||renderer_failed_||root==frame_states_.end()||!root->second.session.empty())return {};
  return {native_pristine_renderer_epoch_,root->second.context,root->second.context_identity};
}
bool NativeDeveloperOwnsPause(Dict packet,const suitecut::DeveloperDebuggerControls::Document&owner)const{
  if(NativeDeveloperDocument()!=owner||!packet||packet->GetType("callFrames")!=VTYPE_LIST)return false;
  const auto frames=packet->GetList("callFrames");if(!frames->GetSize()||frames->GetSize()>64)return false;
  for(size_t i=0;i<frames->GetSize();++i){
    if(frames->GetType(i)!=VTYPE_DICTIONARY)return false;
    const auto frame=frames->GetDictionary(i);if(frame->GetType("location")!=VTYPE_DICTIONARY)return false;
    const auto location=frame->GetDictionary("location");
    if(location->GetType("scriptId")!=VTYPE_STRING||!native_coverage_inventory_.Contains("",owner.context,location->GetString("scriptId").ToString()))return false;
  }
  return true;
}
bool NativeDeveloperBreakpointScript(const std::string&script,const suitecut::DeveloperDebuggerControls::Document&owner)const{
  Dict metadata;
  if(NativeDeveloperDocument()!=owner||!native_coverage_inventory_.BreakpointCandidate("",owner.context,script,metadata))return false;
  return true;
}
void NativeDeveloperEnsure(){
  if(native_developer_debugger_)return;
  suitecut::DeveloperDebuggerControls::Hooks hooks;
  hooks.now=[]{return Now();};hooks.domains=NativeDebuggerDomains();
  hooks.owner_namespace=handle_namespace_;hooks.lease=[this]{return NativeDebuggerLease();};
  hooks.current=[this]{return NativeDeveloperDocument();};
  hooks.scopes=native_frame_debugger_scopes_;
  hooks.scope_pin=[this](const auto&owner,auto&pin){return owner==NativeDeveloperDocument()&&native_frame_debugger_documents_&&native_frame_debugger_documents_->Resolve(main_frame_id_,pin)&&pin.root&&NativeFrameDebuggerPinCurrent(pin);};
  hooks.pause_eligibility=[this](auto token,Dict packet){return NativeDeveloperScopedPause(token,packet);};
  hooks.owns_pause=[this](Dict packet,const auto&owner){return NativeDeveloperOwnsPause(packet,owner);};
  hooks.owns_script=[this](const std::string&script,const auto&owner){return NativeDeveloperDocument()==owner&&native_coverage_inventory_.Contains("",owner.context,script);};
  hooks.owns_breakpoint_script=[this](const std::string&script,const auto&owner){return NativeDeveloperBreakpointScript(script,owner);};
  hooks.script_page=[this](const auto&owner,const std::string&after,const uint64_t*revision,double deadline,suitecut::DeveloperDebuggerScripts::Page&page,std::string&error){
    if(NativeDeveloperDocument()!=owner){error="Native debugger script inventory document changed";return false;}
    suitecut::CoverageScriptInventory::Page actual;
    if(!native_coverage_inventory_.ScriptPage("",owner.context,after,revision,actual,error,deadline,[]{return Now();}))return false;
    page.revision=actual.revision;page.json=std::move(actual.json);page.last=std::move(actual.last);page.count=actual.count;page.more=actual.more;
    return NativeDeveloperDocument()==owner;
  };
  hooks.send=[this](const std::string&method,Dict params,double deadline,auto reply){return NativeDebuggerSend("",method,params,deadline,std::move(reply));};
  hooks.wake=[this]{WakeDeadlines();};
  hooks.close_channel=[this](const std::string&error){NativeDebuggerRequestClose(error);};
  native_developer_debugger_=std::make_unique<suitecut::DeveloperDebuggerControls>(std::move(hooks));
}
void NativeDeveloperCommand(int owner,Dict params){
  if(params->HasKey("worker")){NativeDeveloperWorkerCommand(owner,params);return;}
  if(params->HasKey("frame")){NativeDeveloperFrameCommand(owner,params);return;}
  if(params->HasKey("frameId")||params->HasKey("framePath")){
    Complete(owner,false,"Native debugger sessions belong to an actual root page");return;
  }
  const auto operation=operations_.Get(owner);if(!operation)return;
  NativeDeveloperEnsure();
  native_developer_debugger_->Handle(owner,params,operation->deadline_ms,[this,owner,generation=operation->generation](bool ok,const std::string&json,const std::string&error){
    if(operations_.IsCurrent(owner,generation))Complete(owner,ok,error,json);
  });
}
void NativeDeveloperRealmDestroyed(){if(native_developer_debugger_)native_developer_debugger_->RealmDestroyed(native_pristine_renderer_epoch_);if(native_frame_developer_debuggers_)native_frame_developer_debuggers_->RootRealmDestroyed(native_pristine_renderer_epoch_);}
void NativeDeveloperChanged(){if(native_developer_debugger_)native_developer_debugger_->Changed();}
// Observe before mutating the exact Source root-context registry. No child
// context removal can invalidate the independently owned root developer lease.
void NativeDeveloperEvent(const std::string&method,Dict packet,const std::string&session){
  if(!native_developer_debugger_||!session.empty()||!packet)return;
  if(method=="Runtime.executionContextsCleared"){NativeDeveloperChanged();return;}
  const auto root=NativeDeveloperDocument();
  if(method=="Runtime.executionContextDestroyed"&&packet->GetType("executionContextId")==VTYPE_INT&&packet->GetInt("executionContextId")==root.context){
    const auto identity=packet->GetString("executionContextUniqueId").ToString();
    if(identity.empty()||identity==root.unique_context)NativeDeveloperChanged();
  }else if(method=="Page.frameNavigated"&&packet->GetType("frame")==VTYPE_DICTIONARY&&packet->GetDictionary("frame")->GetString("id")==main_frame_id_)NativeDeveloperChanged();
}
void NativeDeveloperTick(){if(native_developer_debugger_)native_developer_debugger_->Tick(Now());if(native_frame_developer_debuggers_)native_frame_developer_debuggers_->Tick(Now());if(native_worker_developer_debuggers_)native_worker_developer_debuggers_->Tick(Now());if(native_frame_debugger_scopes_)native_frame_debugger_scopes_->Tick();}
bool NativeDeveloperNeedsTick()const{return (native_worker_developer_debuggers_&&native_worker_developer_debuggers_->NeedsTick())||(native_frame_developer_debuggers_&&native_frame_developer_debuggers_->NeedsTick())||(native_developer_debugger_&&native_developer_debugger_->NeedsTick())||(native_frame_debugger_scopes_&&native_frame_debugger_scopes_->NeedsTick());}
void NativeDeveloperClosed(){if(native_worker_developer_debuggers_)native_worker_developer_debuggers_->Closed();if(native_developer_debugger_)native_developer_debugger_->Closed();if(native_frame_developer_debuggers_)native_frame_developer_debuggers_->Closed();NativeFrameDebuggerClosed();}
