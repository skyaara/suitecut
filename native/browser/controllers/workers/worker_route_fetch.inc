// SPDX-License-Identifier: MIT
// Included inside the real context worker broker Client. The HTTP controller
// independently checks this exact CEF/context pair against the current registry.
bool WorkerRouteFetchSessionOwned(const std::string& interception,const std::string& request)const{
  const auto* session=&request;
  for(size_t depth=0;depth<kWorkerTargets;++depth){
    const auto mapped=worker_sessions_.find(*session);if(mapped==worker_sessions_.end())return false;
    const auto target=worker_targets_.find(mapped->second);if(target==worker_targets_.end()||target->second.session!=*session||target->second.context_destroyed)return false;
    if(*session==interception)return true;
    session=&target->second.parent_session;if(session->empty())return interception.empty();
  }
  return false;
}
void WorkerRouteFetchRuntimeEvent(const std::string& method,Dict data,const std::string& session){
  if(method!="Runtime.executionContextDestroyed"&&method!="Runtime.executionContextsCleared")return;
  const auto mapped=worker_sessions_.find(session);if(mapped==worker_sessions_.end())return;
  const auto target=worker_targets_.find(mapped->second);if(target==worker_targets_.end()||target->second.session!=session)return;
  if(method=="Runtime.executionContextDestroyed"&&data->GetInt("executionContextId")!=target->second.context)return;
  if(network_)network_->RequestSessionContextDestroyed(session);
}
void ConfigureWorkerRouteFetch(){
  network_->SetRouteFetch(
    [this](int operation,const std::string& interception,const std::string& session,Dict request,Dict options,double deadline,std::shared_ptr<void> lease,std::function<bool(bool,Dict,const std::string&)> done){
      const auto owner=owner_.lock();const auto mapped=worker_sessions_.find(session);
      const auto target=mapped==worker_sessions_.end()?worker_targets_.end():worker_targets_.find(mapped->second);
      const auto worker=request?request->GetDictionary("workerInfo"):nullptr;const auto control=worker?worker->GetDictionary("control"):nullptr;
      if(!ready_||chromium_context_.empty()||closing_page_||(network_fatal_&&network_fatal_->pending())||!owner||!owner->hooks_.route_fetch||!owner->hooks_.cancel_route_fetch||!request_context_||
         !WorkerRouteFetchSessionOwned(interception,session)||session.empty()||target==worker_targets_.end()||target->second.session!=session||target->second.context_destroyed||target->second.context<=0||
         !worker||worker->GetString("id")!=target->second.id||worker->GetString("type")!=target->second.kind||
         !control||control->GetString("owner")!=handle_namespace_||control->GetString("generation").ToString()!=std::to_string(target->second.generation)){
        done(false,nullptr,"Native route fetch actual worker/context/session owner is unavailable");return;
      }
      const auto target_id=target->second.id;const auto generation=target->second.generation;const auto execution_context=target->second.context;const auto expected_context=request_context_;
      owner->hooks_.route_fetch(operation,context_,request_context_,request,options,deadline,std::move(lease),
        [this,keep=RetainWorkerRouteFetchOwner(),target_id,session,generation,execution_context,expected_context,done=std::move(done)](bool ok,Dict result,const std::string& error){
          const auto current=worker_targets_.find(target_id);const auto mapped=worker_sessions_.find(session);
          if(closing_page_||!ready_||request_context_!=expected_context||mapped==worker_sessions_.end()||mapped->second!=target_id||current==worker_targets_.end()||current->second.session!=session||current->second.generation!=generation||current->second.context_destroyed||current->second.context!=execution_context){
            done(false,nullptr,"Native route fetch actual worker execution-context owner was retired");return false;
          }
          return done(ok,result,error);
        });
    },
    [this](int operation){if(const auto owner=owner_.lock();owner&&owner->hooks_.cancel_route_fetch)owner->hooks_.cancel_route_fetch(operation);});
}
