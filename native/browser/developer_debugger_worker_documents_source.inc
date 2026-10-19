// SPDX-License-Identifier: MIT
// Source-only exact attachment/Runtime observation adapter. Worker debugger
// commands consume these pins; the context broker has no access to this owner.
std::unique_ptr<suitecut::DeveloperDebuggerWorkerDocuments>native_worker_debugger_documents_;
const char*native_worker_debugger_metadata_error_=nullptr;
void NativeWorkerDebuggerMetadataUnavailable(const char*error)noexcept{
  native_worker_debugger_metadata_error_=error;
  if(native_worker_debugger_documents_)native_worker_debugger_documents_->Unavailable();
}
void NativeWorkerDebuggerObserved(){
  if(!observer_||!native_frame_debugger_observer_||native_frame_debugger_registering_||native_frame_debugger_registration_traffic_||native_worker_debugger_documents_){
    NativeWorkerDebuggerMetadataUnavailable("Actual worker debugger observer metadata is unavailable");return;
  }
  try{
    native_worker_debugger_documents_=std::make_unique<suitecut::DeveloperDebuggerWorkerDocuments>(suitecut::DeveloperDebuggerWorkerDocuments::Host{handle_namespace_,native_frame_debugger_observer_});
    if(!native_worker_debugger_documents_->Available())NativeWorkerDebuggerMetadataUnavailable("Actual worker debugger observer identity is invalid");
  }catch(const std::bad_alloc&){NativeWorkerDebuggerMetadataUnavailable("Native worker debugger observer metadata allocation failed");}
}
std::optional<suitecut::DeveloperDebuggerWorkerDocuments::Attachment>NativeWorkerDebuggerAttachment(const std::string&session)const{
  const auto mapped=worker_sessions_.find(session);if(mapped==worker_sessions_.end())return std::nullopt;
  const auto found=worker_targets_.find(mapped->second);
  if(found==worker_targets_.end()||found->second.kind!="worker"||found->second.session!=session||!found->second.generation||!native_frame_debugger_observer_)return std::nullopt;
  const auto&target=found->second;
  return suitecut::DeveloperDebuggerWorkerDocuments::Attachment{{handle_namespace_,native_frame_debugger_observer_},target.id,session,target.parent_session,target.generation};
}
void NativeWorkerDebuggerAttached(Dict actual,uint64_t before){
  if(!native_worker_debugger_documents_||!native_worker_debugger_documents_->Available())return;
  try{
    const auto info=actual?actual->GetDictionary("targetInfo"):nullptr;
    if(!info||info->GetType("type")!=VTYPE_STRING||info->GetString("type")!="worker"||info->GetType("targetId")!=VTYPE_STRING||actual->GetType("sessionId")!=VTYPE_STRING)return;
    const auto attachment=NativeWorkerDebuggerAttachment(actual->GetString("sessionId").ToString());
    // WorkerAttached returns handled=true on validation failure too. Only the
    // actually admitted map entry/new generation authenticates this receipt.
    if(!attachment||attachment->generation<=before||info->GetString("targetId")!=attachment->target)return;
    std::string error;
    if(!native_worker_debugger_documents_->Attached(*attachment,"worker",error))NativeWorkerDebuggerMetadataUnavailable("Actual dedicated-worker debugger attachment metadata was rejected");
  }catch(const std::bad_alloc&){NativeWorkerDebuggerMetadataUnavailable("Native worker debugger attachment metadata allocation failed");}
}
void NativeWorkerDebuggerRuntime(const std::string&method,Dict actual,const std::string&session){
  if(!native_worker_debugger_documents_||!native_worker_debugger_documents_->Available()||!actual)return;
  if(method!="Runtime.executionContextCreated"&&method!="Runtime.executionContextDestroyed"&&method!="Runtime.executionContextsCleared")return;
  try{
    const auto attachment=NativeWorkerDebuggerAttachment(session);if(!attachment)return;
    if(method=="Runtime.executionContextsCleared"){native_worker_debugger_documents_->ContextsCleared(*attachment);return;}
    if(method=="Runtime.executionContextDestroyed"){
      if(actual->GetType("executionContextId")==VTYPE_INT)native_worker_debugger_documents_->ContextDestroyed(*attachment,actual->GetInt("executionContextId"));
      return;
    }
    const auto context=actual->GetDictionary("context");const auto aux=context?context->GetDictionary("auxData"):nullptr;
    if(aux&&aux->HasKey("isDefault")){
      if(aux->GetType("isDefault")!=VTYPE_BOOL){NativeWorkerDebuggerMetadataUnavailable("Actual dedicated-worker default-context metadata is invalid");return;}
      if(!aux->GetBool("isDefault"))return;
    }
    // Match the existing actual worker-global context acceptance, adding the
    // genuine uniqueId requirement. Never synthesize it from a numeric ID.
    if(!context||context->GetType("id")!=VTYPE_INT||context->GetType("uniqueId")!=VTYPE_STRING){NativeWorkerDebuggerMetadataUnavailable("Actual dedicated-worker Runtime unique-context metadata is missing");return;}
    std::string error;
    if(!native_worker_debugger_documents_->ContextCreated(*attachment,context->GetInt("id"),context->GetString("uniqueId").ToString(),error))NativeWorkerDebuggerMetadataUnavailable("Actual dedicated-worker Runtime identity is invalid");
  }catch(const std::bad_alloc&){NativeWorkerDebuggerMetadataUnavailable("Native worker debugger Runtime metadata allocation failed");}
}
bool NativeWorkerDebuggerPinCurrent(const suitecut::DeveloperDebuggerWorkerDocuments::Pin&pin)const{
  if(closing_page_||native_debugger_closing_requested_||!native_worker_debugger_documents_||!native_worker_debugger_documents_->Current(pin))return false;
  const auto target=worker_targets_.find(pin.attachment.target);
  const auto mapped=worker_sessions_.find(pin.attachment.session);
  return target!=worker_targets_.end()&&mapped!=worker_sessions_.end()&&mapped->second==pin.attachment.target&&target->second.published&&!target->second.bootstrap_failed&&
    !target->second.context_destroyed&&target->second.context==pin.context&&target->second.generation==pin.attachment.generation&&target->second.session==pin.attachment.session&&
    pin.attachment.host.source==handle_namespace_&&pin.attachment.host.observer==native_frame_debugger_observer_;
}
struct NativeWorkerDebuggerChannelReceipt{uint64_t observer=0,generation=0;};
NativeWorkerDebuggerChannelReceipt NativeWorkerDebuggerChannel(const std::string&session)const noexcept{
  const auto mapped=worker_sessions_.find(session);if(mapped==worker_sessions_.end())return {};
  const auto target=worker_targets_.find(mapped->second);
  if(target==worker_targets_.end()||target->second.kind!="worker"||target->second.session!=session)return {};
  return {native_frame_debugger_observer_,target->second.generation};
}
void NativeWorkerDebuggerChannelDrained(const std::string&session,NativeWorkerDebuggerChannelReceipt actual)noexcept{
  // Called only after the existing exact real worker session callback/domain
  // drain. No strings/containers are allocated to preserve this receipt.
  if(native_worker_debugger_documents_)native_worker_debugger_documents_->ChannelDrained(handle_namespace_,actual.observer,session,actual.generation);
}
void NativeWorkerDebuggerClosed(){
  // Actual Source OnBeforeClose, after the shared domain/accepted callback drain.
  if(native_worker_debugger_documents_)native_worker_debugger_documents_->ObserverClosed(handle_namespace_,native_frame_debugger_observer_);
}
