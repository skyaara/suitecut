// SPDX-License-Identifier: MIT
// Public frame binding is activated only with the separately reviewed central
// Source patch, after the eager observed-document/root-adoption Kit A.
std::unique_ptr<suitecut::DeveloperDebuggerFrameTargets>native_frame_developer_debuggers_;
suitecut::DebuggerDomains::DeveloperPauseDisposition NativeDeveloperScopedPause(suitecut::FrameDebuggerScopes::Token token,Dict packet){
  return suitecut::FrameDebuggerPause::Classify(native_frame_debugger_scopes_,token,packet,
    [this](const std::string&session,const std::string&script)->std::optional<suitecut::FrameDebuggerPause::Script>{
      int context=0;bool native=false;
      if(!native_coverage_inventory_.ScriptContext(session,script,context,native))return std::nullopt;
      return suitecut::FrameDebuggerPause::Script{context,native};
    });
}
std::unique_ptr<suitecut::DeveloperDebuggerControls>NativeDeveloperFrameControl(const suitecut::FrameDebuggerScopes::Pin&pin){
  using Controls=suitecut::DeveloperDebuggerControls;
  const Controls::Document owner{native_pristine_renderer_epoch_,pin.context,pin.unique_context};
  Controls::Hooks hooks;hooks.now=[]{return Now();};hooks.domains=NativeDebuggerDomains();
  hooks.owner_namespace=handle_namespace_;hooks.lease=[this]{return NativeDebuggerLease();};
  hooks.inspector_session=pin.channel.session;hooks.agent_controls=false;hooks.scopes=native_frame_debugger_scopes_;
  hooks.current=[this,pin,owner]{return NativeFrameDebuggerPinCurrent(pin)?owner:Controls::Document{};};
  hooks.scope_pin=[this,pin,owner](const auto&actual,auto&output){if(actual!=owner||!NativeFrameDebuggerPinCurrent(pin))return false;output=pin;return true;};
  hooks.pause_eligibility=[this](auto token,Dict packet){return NativeDeveloperScopedPause(token,packet);};
  hooks.owns_pause=[this,pin,owner](Dict packet,const auto&actual){
    if(actual!=owner||!NativeFrameDebuggerPinCurrent(pin)||!packet||packet->GetType("callFrames")!=VTYPE_LIST)return false;
    const auto frames=packet->GetList("callFrames");if(!frames->GetSize()||frames->GetSize()>64)return false;
    for(size_t i=0;i<frames->GetSize();++i){
      if(frames->GetType(i)!=VTYPE_DICTIONARY)return false;
      const auto frame=frames->GetDictionary(i);if(frame->GetType("location")!=VTYPE_DICTIONARY)return false;
      const auto location=frame->GetDictionary("location");
      if(location->GetType("scriptId")!=VTYPE_STRING||!native_coverage_inventory_.Contains(pin.channel.session,pin.context,location->GetString("scriptId").ToString()))return false;
    }
    return NativeFrameDebuggerPinCurrent(pin);
  };
  hooks.owns_script=[this,pin,owner](const std::string&script,const auto&actual){return actual==owner&&NativeFrameDebuggerPinCurrent(pin)&&native_coverage_inventory_.Contains(pin.channel.session,pin.context,script);};
  hooks.owns_breakpoint_script=[this,pin,owner](const std::string&script,const auto&actual){
    Dict metadata;return actual==owner&&NativeFrameDebuggerPinCurrent(pin)&&native_coverage_inventory_.BreakpointCandidate(pin.channel.session,pin.context,script,metadata);
  };
  hooks.script_page=[this,pin,owner](const auto&actual,const std::string&after,const uint64_t*revision,double deadline,suitecut::DeveloperDebuggerScripts::Page&page,std::string&error){
    if(actual!=owner||!NativeFrameDebuggerPinCurrent(pin)){error="Native frame debugger script inventory document changed";return false;}
    suitecut::CoverageScriptInventory::Page selected;
    if(!native_coverage_inventory_.ScriptPage(pin.channel.session,pin.context,after,revision,selected,error,deadline,[]{return Now();}))return false;
    page.revision=selected.revision;page.json=std::move(selected.json);page.last=std::move(selected.last);page.count=selected.count;page.more=selected.more;
    return NativeFrameDebuggerPinCurrent(pin);
  };
  hooks.send=[this,pin](const std::string&method,Dict params,double deadline,auto reply){
    // Cleanup remains admissible after document invalidation, but never on a
    // later actual attachment reusing the same CDP session text.
    if(NativeFrameDebuggerAttachment(pin.channel.session)!=pin.attachment)return false;
    return NativeDebuggerSend(pin.channel.session,method,params,deadline,std::move(reply));
  };
  hooks.wake=[this]{WakeDeadlines();};hooks.close_channel=[this](const std::string&error){NativeDebuggerRequestClose(error);};
  return std::make_unique<Controls>(std::move(hooks));
}
void NativeDeveloperFramesEnsure(){
  if(native_frame_developer_debuggers_)return;
  suitecut::DeveloperDebuggerFrameTargets::Hooks hooks;
  hooks.current=[this](const auto&pin){return NativeFrameDebuggerPinCurrent(pin);};
  hooks.create=[this](const auto&pin){return NativeDeveloperFrameControl(pin);};
  native_frame_developer_debuggers_=std::make_unique<suitecut::DeveloperDebuggerFrameTargets>(std::move(hooks));
}
void NativeDeveloperFrameCommand(int owner,Dict params){
  const auto operation=operations_.Get(owner);if(!operation)return;
  const auto generation=operation->generation;const double deadline=operation->deadline_ms;
  if(params->GetType("action")!=VTYPE_STRING||params->GetType("frame")!=VTYPE_STRING||params->GetString("frame").empty()||params->GetString("frame").length()>256||params->HasKey("frameId")||params->HasKey("framePath")){
    Complete(owner,false,"Native frame debugger requires an action and one actual frame ID");return;
  }
  const auto requested=params->GetString("frame").ToString();
  const auto reply=[this,owner,generation](bool ok,const std::string&json,const std::string&error){if(operations_.IsCurrent(owner,generation))Complete(owner,ok,error,json);};
  try{
    NativeDeveloperFramesEnsure();
    if(params->GetString("action")=="open"){
      suitecut::FrameDebuggerScopes::Pin pin;
      if(!native_frame_debugger_documents_||!native_frame_debugger_documents_->Resolve(requested,pin)||!NativeFrameDebuggerPinCurrent(pin)){
        Complete(owner,false,"Actual native frame default document is not ready or no longer current");return;
      }
      native_frame_developer_debuggers_->Open(owner,pin,params,deadline,reply);
    }else native_frame_developer_debuggers_->Handle(owner,requested,params,deadline,reply);
  }catch(const std::bad_alloc&){
    // Some callback/command may already have been accepted. Keep all native
    // records and request posted actual closure rather than guessing refusal.
    NativeDebuggerRequestClose("Native frame debugger admission allocation failed");
    if(operations_.IsCurrent(owner,generation))Complete(owner,false,"Native frame debugger admission allocation failed");
  }
}
void NativeDeveloperFrameChanged(const suitecut::FrameDebuggerScopes::Pin&pin){if(native_frame_developer_debuggers_)native_frame_developer_debuggers_->Changed(pin);}
void NativeDeveloperFrameChannelClosed(const suitecut::FrameDebuggerScopes::Channel&channel,uint64_t attachment){if(native_frame_developer_debuggers_)native_frame_developer_debuggers_->ChannelClosed(channel,attachment);}
void NativeDeveloperCancel(int owner){if(native_worker_developer_debuggers_)native_worker_developer_debuggers_->Cancel(owner);if(native_developer_debugger_)native_developer_debugger_->Cancel(owner);if(native_frame_developer_debuggers_)native_frame_developer_debuggers_->Cancel(owner);}
void NativeDeveloperDiagnostics(Dict output){if(native_developer_debugger_)native_developer_debugger_->Diagnostics(output);if(native_frame_developer_debuggers_)native_frame_developer_debuggers_->Diagnostics(output);if(native_worker_developer_debuggers_)native_worker_developer_debuggers_->Diagnostics(output);}
