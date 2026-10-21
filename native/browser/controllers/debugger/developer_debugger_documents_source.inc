// SPDX-License-Identifier: MIT
// Source binding for accepted observer/default-context/iframe facts. Central
// Source hooks are composed separately; this file activates no public frame API.
uint64_t native_frame_debugger_observer_=0;
bool native_frame_debugger_registering_=false,native_frame_debugger_registration_traffic_=false;
std::unique_ptr<suitecut::FrameDebuggerDocuments>native_frame_debugger_documents_;
suitecut::FrameDebuggerInitialTrees native_frame_debugger_trees_;
std::shared_ptr<suitecut::FrameDebuggerScopes>native_frame_debugger_scopes_;

void NativeFrameDebuggerMetadataFailure(const std::string&error){
  if(native_frame_debugger_documents_)native_frame_debugger_documents_->Unavailable();
  native_frame_debugger_trees_.Unavailable();
  NativeDeveloperChanged();
  NativeDebuggerRequestClose(error);
}
bool NativeFrameDebuggerRegistrationEvent(const std::string&method){
  if(!native_frame_debugger_registering_)return false;
  if(method=="Runtime.executionContextCreated"||method=="Runtime.executionContextDestroyed"||method=="Runtime.executionContextsCleared"||
     method=="Page.frameAttached"||method=="Page.frameNavigated"||method=="Page.frameDetached"||
     method=="Target.attachedToTarget"||method=="Target.detachedFromTarget"){
    native_frame_debugger_registration_traffic_=true;return true;
  }
  return false;
}
bool NativeFrameDebuggerPinCurrent(const suitecut::FrameDebuggerScopes::Pin&pin)const{
  if(closing_page_||native_debugger_closing_requested_||!native_frame_debugger_documents_||
     !native_frame_debugger_trees_.Available()||!native_frame_debugger_documents_->Current(pin))return false;
  const auto frame=frame_states_.find(pin.frame);
  return frame!=frame_states_.end()&&frame->second.session==pin.channel.session&&frame->second.loader==pin.loader&&
    frame->second.context==pin.context&&frame->second.context_identity==pin.unique_context&&pin.root==(pin.frame==main_frame_id_)&&
    (!pin.channel.session.empty()||!renderer_failed_);
}
bool NativeFrameDebuggerObserved(){
  native_frame_debugger_registering_=false;
  if(!observer_||native_frame_debugger_registration_traffic_||native_frame_debugger_documents_||native_frame_debugger_observer_==UINT64_MAX){NativeFrameDebuggerMetadataFailure("Native debugger observer registration failed or delivered default-document metadata before its ledger was initialized");return false;}
  try{
    const suitecut::FrameDebuggerScopes::Host host{handle_namespace_,++native_frame_debugger_observer_};
    native_frame_debugger_documents_=std::make_unique<suitecut::FrameDebuggerDocuments>(host);
    suitecut::FrameDebuggerScopes::Hooks hooks;
    hooks.now=[]{return Now();};hooks.current=[this](const auto&pin){return NativeFrameDebuggerPinCurrent(pin);};
    hooks.lease_present=[this](uint64_t lease){return native_debugger_domains_&&native_debugger_domains_->HasLease(lease);};
    // Kit A admits only the existing root controller. Frame bindings will add
    // their exact captured-pin consumers here, rather than a latest-token map.
    hooks.cleanup=[this](auto,const auto&pin){if(pin.root)NativeDeveloperChanged();NativeDeveloperFrameChanged(pin);};
    hooks.close_channel=[this](const auto&,const std::string&error){NativeDebuggerRequestClose(error);};
    hooks.wake=[this]{WakeDeadlines();};
    native_frame_debugger_scopes_=suitecut::FrameDebuggerScopes::Create(host,std::move(hooks));
    if(!native_frame_debugger_documents_->Available()||!native_frame_debugger_scopes_){NativeFrameDebuggerMetadataFailure("Native debugger observed-document scope capacity unavailable");return false;}
    return true;
  }catch(const std::bad_alloc&){NativeFrameDebuggerMetadataFailure("Native debugger observed-document allocation failed");return false;}
}
uint64_t NativeFrameDebuggerAttachment(const std::string&session)const{return native_frame_debugger_documents_?native_frame_debugger_documents_->AttachmentFor(session):0;}
bool NativeFrameDebuggerTreePrepare(int command,const std::string&session){
  try{
    std::string error;
    if(!native_frame_debugger_trees_.Prepare(command,session,NativeFrameDebuggerAttachment(session),error)){NativeFrameDebuggerMetadataFailure(error);return false;}
    return true;
  }catch(const std::bad_alloc&){NativeFrameDebuggerMetadataFailure("Native debugger initial-tree preparation allocation failed");return false;}
}
bool NativeFrameDebuggerBeforeSend(int command,const std::string&session,bool tracked,uint64_t expected_attachment){
  if(!tracked){
    if(!native_frame_debugger_trees_.Contains(command))return true;
    NativeFrameDebuggerMetadataFailure("Native debugger prepared initial-tree command lost its sender ownership marker");return false;
  }
  if(!expected_attachment){NativeFrameDebuggerMetadataFailure("Native debugger initial-tree queued command/session identity disagrees");return false;}
  if(native_frame_debugger_trees_.Contains(command)&&native_frame_debugger_trees_.ExpectedAttachment(command,session)!=expected_attachment){NativeFrameDebuggerMetadataFailure("Native debugger initial-tree sender disagrees with the exact original session/attachment");return false;}
  // This initial query was queued for an old channel incarnation which really
  // closed/replaced. Do not send it to the newer attachment or treat a skipped
  // query as an actual reply; its old Source registry was drained at closure.
  if(NativeFrameDebuggerAttachment(session)!=expected_attachment)return false;
  if(native_frame_debugger_trees_.Dispatching(command,session,expected_attachment))return true;
  NativeFrameDebuggerMetadataFailure("Native debugger initial-tree actual dispatch has no original command/channel observation");return false;
}
struct NativeFrameDebuggerTreeReceipt{
  Source*source;int command;const std::string&session;uint64_t attachment;
  ~NativeFrameDebuggerTreeReceipt(){source->native_frame_debugger_trees_.Finished(command,session,attachment);}
};
bool NativeFrameDebuggerAttached(const std::string&session,const std::string&parent,const std::string&frame){
  if(!native_frame_debugger_documents_){if(native_frame_debugger_registering_)native_frame_debugger_registration_traffic_=true;return false;}
  try{std::string error;if(!native_frame_debugger_documents_->Attached(session,parent,frame,error)){NativeFrameDebuggerMetadataFailure(error);return false;}return true;}
  catch(const std::bad_alloc&){NativeFrameDebuggerMetadataFailure("Native debugger accepted attachment allocation failed");return false;}
}
suitecut::FrameDebuggerDocuments::Snapshot NativeFrameDebuggerSnapshot(const FrameState&frame)const{return {frame.session,frame.id,frame.loader,frame.context_identity,frame.context};}
void NativeFrameDebuggerDefaultObserved(const FrameState&frame){
  if(!native_frame_debugger_documents_){if(native_frame_debugger_registering_)native_frame_debugger_registration_traffic_=true;return;}
  try{
    std::string error;const auto actual=NativeFrameDebuggerSnapshot(frame);
    if(!native_frame_debugger_documents_->DefaultCreated(actual,error)||
       !native_frame_debugger_trees_.DefaultObserved(frame.session,NativeFrameDebuggerAttachment(frame.session),frame.id,native_frame_debugger_documents_->DocumentStamp(frame.id),error)){
      NativeFrameDebuggerMetadataFailure(error);return;
    }
    WakeDeadlines();
  }catch(const std::bad_alloc&){NativeFrameDebuggerMetadataFailure("Native debugger actual default-context observation allocation failed");}
}
void NativeFrameDebuggerMainObserved(){
  if(!native_frame_debugger_documents_){if(native_frame_debugger_registering_)native_frame_debugger_registration_traffic_=true;return;}
  if(main_frame_id_.empty())return;
  try{std::string error;if(!native_frame_debugger_documents_->MainFrame(main_frame_id_,error))NativeFrameDebuggerMetadataFailure(error);else WakeDeadlines();}
  catch(const std::bad_alloc&){NativeFrameDebuggerMetadataFailure("Native debugger root identity observation allocation failed");}
}
void NativeFrameDebuggerInitialAssociated(int command,const std::string&frame_id,const std::string&session){
  if(!command||!native_frame_debugger_documents_)return;
  const auto frame=frame_states_.find(frame_id);
  if(frame==frame_states_.end()||frame->second.session!=session||frame->second.context<=0||frame->second.context_identity.empty())return;
  const auto document=native_frame_debugger_trees_.InitialDocument(command,session,NativeFrameDebuggerAttachment(session),frame_id);
  if(!document)return;
  try{
    std::string error;
    // A stale exact document receipt is an ineligible association, not an
    // instruction to adopt the latest serial or manufacture a context.
    native_frame_debugger_documents_->InitialFrameAssociated(NativeFrameDebuggerSnapshot(frame->second),document,error);
    if(!native_frame_debugger_documents_->Available())NativeFrameDebuggerMetadataFailure(error);
    WakeDeadlines();
  }catch(const std::bad_alloc&){NativeFrameDebuggerMetadataFailure("Native debugger initial frame association allocation failed");}
}
void NativeFrameDebuggerFrameInvalidated(const std::string&frame_id,const std::string&session){
  if(!native_frame_debugger_documents_){if(native_frame_debugger_registering_)native_frame_debugger_registration_traffic_=true;return;}
  try{
    std::string error;const auto attachment=NativeFrameDebuggerAttachment(session);
    if(!native_frame_debugger_trees_.FrameInvalidated(session,attachment,frame_id,error)){NativeFrameDebuggerMetadataFailure(error);return;}
    native_frame_debugger_documents_->FrameInvalidated(frame_id);
    if(native_frame_debugger_scopes_)native_frame_debugger_scopes_->Tick();
  }catch(const std::bad_alloc&){NativeFrameDebuggerMetadataFailure("Native debugger document invalidation allocation failed");}
}
void NativeFrameDebuggerContextsCleared(const std::string&session){
  if(!native_frame_debugger_documents_){if(native_frame_debugger_registering_)native_frame_debugger_registration_traffic_=true;return;}
  native_frame_debugger_trees_.ContextsCleared(session,NativeFrameDebuggerAttachment(session));
  native_frame_debugger_documents_->ContextsCleared(session);
  if(native_frame_debugger_scopes_)native_frame_debugger_scopes_->Tick();
}
void NativeFrameDebuggerRootRealmInvalidated(){
  // Called at actual CEF renderer death before the existing exact realm hook.
  // Do not run cleanup yet: RealmDestroyed must mark its V8 groups first.
  if(!native_frame_debugger_documents_)return;
  native_frame_debugger_trees_.ContextsCleared("",NativeFrameDebuggerAttachment(""));
  native_frame_debugger_documents_->ContextsCleared("");
}
void NativeFrameDebuggerDetached(const std::string&session,uint64_t attachment){
  if(!attachment||!native_frame_debugger_documents_||!native_frame_debugger_scopes_)return;
  const suitecut::FrameDebuggerScopes::Channel channel{{handle_namespace_,native_frame_debugger_observer_},session};
  // Called after the existing actual session arbiter/Source callback drain.
  native_frame_debugger_trees_.Detached(session,attachment);
  native_frame_debugger_documents_->Detached(session,attachment);
  NativeDeveloperFrameChannelClosed(channel,attachment);
  native_frame_debugger_scopes_->ChannelClosed(channel,attachment);
}
void NativeFrameDebuggerClosed(){
  native_frame_debugger_trees_.Closed();
  if(native_frame_debugger_documents_)native_frame_debugger_documents_->Closed();
  if(native_frame_debugger_scopes_)native_frame_debugger_scopes_->HostClosed({handle_namespace_,native_frame_debugger_observer_});
}
