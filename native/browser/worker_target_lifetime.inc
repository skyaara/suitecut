// SPDX-License-Identifier: MIT
// Included inside the real Source. Inspector channel loss and author target
// destruction are separate native receipts, bound to this actual owner.
std::map<std::string,WorkerTarget> native_detached_workers_;
bool native_worker_discovery_started_=false,native_worker_discovery_ready_=false;
void NativeWorkerTargetDiscoveryStart(){
  if(native_worker_discovery_started_||closing_page_)return;
  native_worker_discovery_started_=true;
  worker_channel_detached_=[this](const WorkerTarget& target){
    if(!target.published&&!target.observed)return false;
    if(!native_detached_workers_.contains(target.id)&&native_detached_workers_.size()>=1024){NetworkFatal("Native detached worker lifetime capacity exceeded");return false;}
    native_detached_workers_[target.id]=target;return true;
  };
  auto params=CefDictionaryValue::Create();params->SetBool("discover",true);
  NativeNetworkSend("","Target.setDiscoverTargets",params,
    [self=CefRefPtr<Source>(this)](bool ok,const std::string&error){
      if(self->closing_page_)return;
      if(!ok){self->NetworkFatal(error.empty()?"Actual native worker target discovery failed":error);return;}
      self->native_worker_discovery_ready_=true;self->MaybeReady();
    },Now()+30000);
}
void NativeWorkerCloseWaiters(const WorkerTarget& target){
  std::vector<std::pair<int,uint64_t>> waiting;
  for(const auto&[owner,work]:worker_work_)if(work.wait_close&&work.worker==target.id)waiting.emplace_back(owner,work.generation);
  for(const auto&[owner,generation]:waiting)if(operations_.IsCurrent(owner,generation)){
    if(operations_.IsExpired(owner,generation,Now()))Complete(owner,false,"Native worker original close deadline expired");
    else Complete(owner,true);
  }
}
bool NativeWorkerTargetDestroyed(Dict actual){
  if(!native_worker_discovery_started_||!actual||actual->GetType("targetId")!=VTYPE_STRING)return false;
  const auto id=actual->GetString("targetId").ToString();if(id.empty()||id.size()>128)return false;
  if(const auto current=worker_targets_.find(id);current!=worker_targets_.end()){
    // This exact owned target was destroyed, independently of Inspector detach.
    // Capture its actual session before the existing physical teardown mutates it.
    current->second.context_destroyed=true;const auto session=current->second.session;
    NativeTargetSessionDetached(session);return true;
  }
  const auto previous=native_detached_workers_.find(id);if(previous==native_detached_workers_.end())return false;
  const auto target=previous->second;native_detached_workers_.erase(previous);
  WorkerPublish(target,"close");NativeWorkerCloseWaiters(target);return true;
}
void NativeWorkerReattached(Dict actual,uint64_t before){
  auto info=actual?actual->GetDictionary("targetInfo"):nullptr;if(!info)return;
  const auto id=info->GetString("targetId").ToString(),session=actual->GetString("sessionId").ToString();
  const auto current=worker_targets_.find(id),previous=native_detached_workers_.find(id);
  if(current==worker_targets_.end()||previous==native_detached_workers_.end()||current->second.generation<=before||current->second.session!=session||previous->second.generation>=current->second.generation)return;
  // The logical target was already genuinely observed on its prior attachment.
  current->second.observed=current->second.observed||previous->second.published||previous->second.observed;
  native_detached_workers_.erase(previous);
}
void NativeWorkerTargetOwnerClosed(){
  auto retired=std::move(native_detached_workers_);native_detached_workers_.clear();worker_channel_detached_={};
  for(const auto&[id,target]:retired)if(target.kind=="worker"){
    // Actual Source::OnBeforeClose proves document-owned dedicated execution
    // ended. Shared/service execution has a separate profile-owned lifetime.
    WorkerPublish(target,"close");NativeWorkerCloseWaiters(target);
  }
}
