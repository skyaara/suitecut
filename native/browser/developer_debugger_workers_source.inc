// SPDX-License-Identifier: MIT
// Source-owned dedicated worker sessions. Broker/shared/service targets never
// borrow this observer, script inventory or a page debugger capability.
std::unique_ptr<suitecut::DeveloperDebuggerWorkerTargets>native_worker_developer_debuggers_;
std::unique_ptr<suitecut::DeveloperDebuggerControls>NativeDeveloperWorkerControl(const suitecut::DeveloperDebuggerWorkerDocuments::Pin&pin){
  using Controls=suitecut::DeveloperDebuggerControls;
  const Controls::Document owner{pin.observation,pin.context,pin.unique_context};
  Controls::Hooks hooks;hooks.now=[]{return Now();};hooks.domains=NativeDebuggerDomains();
  hooks.owner_namespace=handle_namespace_;hooks.lease=[this]{return NativeDebuggerLease();};
  hooks.inspector_session=pin.attachment.session;hooks.agent_controls=false;
  hooks.current=[this,pin,owner]{return NativeWorkerDebuggerPinCurrent(pin)?owner:Controls::Document{};};
  hooks.target_pause_eligibility=[this,pin](Dict packet){
    return suitecut::DeveloperDebuggerWorkerPause::Classify(pin,packet,
      [this](const auto&actual){return NativeWorkerDebuggerPinCurrent(actual);},
      [this](const auto&session,const auto&script)->std::optional<suitecut::DeveloperDebuggerWorkerPause::Script>{
        int context=0;bool native=false;
        if(!native_coverage_inventory_.ScriptContext(session,script,context,native))return std::nullopt;
        return suitecut::DeveloperDebuggerWorkerPause::Script{context,native};
      });
  };
  hooks.owns_pause=[this,pin,owner](Dict packet,const auto&actual){
    if(actual!=owner||!NativeWorkerDebuggerPinCurrent(pin)||!packet||packet->GetType("callFrames")!=VTYPE_LIST)return false;
    const auto frames=packet->GetList("callFrames");if(!frames->GetSize()||frames->GetSize()>64)return false;
    for(size_t i=0;i<frames->GetSize();++i){
      if(frames->GetType(i)!=VTYPE_DICTIONARY)return false;
      const auto frame=frames->GetDictionary(i);if(frame->GetType("location")!=VTYPE_DICTIONARY)return false;
      const auto location=frame->GetDictionary("location");
      if(location->GetType("scriptId")!=VTYPE_STRING||!native_coverage_inventory_.Contains(pin.attachment.session,pin.context,location->GetString("scriptId").ToString()))return false;
    }
    return NativeWorkerDebuggerPinCurrent(pin);
  };
  hooks.owns_script=[this,pin,owner](const std::string&script,const auto&actual){return actual==owner&&NativeWorkerDebuggerPinCurrent(pin)&&native_coverage_inventory_.Contains(pin.attachment.session,pin.context,script);};
  hooks.owns_breakpoint_script=[this,pin,owner](const std::string&script,const auto&actual){
    Dict metadata;return actual==owner&&NativeWorkerDebuggerPinCurrent(pin)&&native_coverage_inventory_.BreakpointCandidate(pin.attachment.session,pin.context,script,metadata);
  };
  hooks.script_page=[this,pin,owner](const auto&actual,const std::string&after,const uint64_t*revision,double deadline,suitecut::DeveloperDebuggerScripts::Page&page,std::string&error){
    if(actual!=owner||!NativeWorkerDebuggerPinCurrent(pin)){error="Native worker debugger script inventory document changed";return false;}
    suitecut::CoverageScriptInventory::Page selected;
    if(!native_coverage_inventory_.ScriptPage(pin.attachment.session,pin.context,after,revision,selected,error,deadline,[]{return Now();}))return false;
    page.revision=selected.revision;page.json=std::move(selected.json);page.last=std::move(selected.last);page.count=selected.count;page.more=selected.more;
    return NativeWorkerDebuggerPinCurrent(pin);
  };
  hooks.send=[this,pin](const std::string&method,Dict params,double deadline,auto reply){
    // Cleanup remains admissible after document invalidation, but never on a
    // later actual attachment reusing the same CDP session text.
    if(NativeWorkerDebuggerAttachment(pin.attachment.session)!=pin.attachment)return false;
    return NativeDebuggerSend(pin.attachment.session,method,params,deadline,std::move(reply));
  };
  hooks.wake=[this]{WakeDeadlines();};hooks.close_channel=[this](const std::string&error){NativeDebuggerRequestClose(error);};
  return std::make_unique<Controls>(std::move(hooks));
}
void NativeDeveloperWorkersEnsure(){
  if(native_worker_developer_debuggers_)return;
  // Install before any worker control can acquire the shared domain/send.
  // WorkerDetached may remove logical target maps before the real session
  // boundary drains NativeDebuggerDetached; it must not erase those callbacks.
  worker_debugger_retains_=[this](int id){return native_debugger_calls_.contains(id);};
  suitecut::DeveloperDebuggerWorkerTargets::Hooks hooks;
  hooks.current=[this](const auto&pin){return NativeWorkerDebuggerPinCurrent(pin);};
  hooks.create=[this](const auto&pin){return NativeDeveloperWorkerControl(pin);};
  native_worker_developer_debuggers_=std::make_unique<suitecut::DeveloperDebuggerWorkerTargets>(std::move(hooks));
}
void NativeDeveloperWorkerCommand(int owner,Dict params){
  const auto operation=operations_.Get(owner);if(!operation)return;
  const auto generation=operation->generation;const auto deadline=operation->deadline_ms;
  if(params->GetType("action")!=VTYPE_STRING||params->GetType("worker")!=VTYPE_STRING||params->GetString("worker").empty()||params->GetString("worker").length()>128||params->HasKey("frame")||params->HasKey("frameId")||params->HasKey("framePath")){
    Complete(owner,false,"Native worker debugger requires one actual dedicated-worker target");return;
  }
  const auto control=params->GetType("workerControl")==VTYPE_DICTIONARY?params->GetDictionary("workerControl"):nullptr;
  if(!control||control->GetSize()!=2||control->GetType("owner")!=VTYPE_STRING||control->GetString("owner")!=handle_namespace_||control->GetType("generation")!=VTYPE_STRING||control->GetString("generation").length()>20){
    Complete(owner,false,"Native worker debugger requires exact native Inspector owner and attachment generation");return;
  }
  const auto target=params->GetString("worker").ToString();
  const auto reply=[this,owner,generation](bool ok,const auto&json,const auto&error){if(operations_.IsCurrent(owner,generation))Complete(owner,ok,error,json);};
  try{
    NativeDeveloperWorkersEnsure();
    if(params->GetString("action")=="open"){
      const auto actual=worker_targets_.find(target);
      if(actual==worker_targets_.end()||actual->second.kind!="worker"||control->GetString("generation")!=std::to_string(actual->second.generation)){
        Complete(owner,false,"Native worker debugger supports only this Source's current dedicated-worker attachment");return;
      }
      const auto attachment=NativeWorkerDebuggerAttachment(actual->second.session);
      suitecut::DeveloperDebuggerWorkerDocuments::Pin pin;
      if(!attachment||!native_worker_debugger_documents_||!native_worker_debugger_documents_->Resolve(*attachment,pin)||!NativeWorkerDebuggerPinCurrent(pin)){
        Complete(owner,false,"Actual dedicated-worker Runtime unique-context metadata is unavailable or no longer current");return;
      }
      native_worker_developer_debuggers_->Open(owner,pin,params,deadline,reply);
    }else native_worker_developer_debuggers_->Handle(owner,target,handle_namespace_,control->GetString("generation").ToString(),params,deadline,reply);
  }catch(const std::bad_alloc&){
    NativeDebuggerRequestClose("Native worker debugger admission allocation failed");
    if(operations_.IsCurrent(owner,generation))Complete(owner,false,"Native worker debugger admission allocation failed");
  }
}
void NativeDeveloperWorkersChanged(){if(native_worker_developer_debuggers_)native_worker_developer_debuggers_->Changed();}
void NativeDeveloperWorkerChannelClosed(const std::string&session,uint64_t observer,uint64_t generation){
  if(native_worker_developer_debuggers_)native_worker_developer_debuggers_->ChannelClosed(handle_namespace_,observer,session,generation);
}
