// SPDX-License-Identifier: MIT
// Source-owned media propagation uses actual renderer sessions. Physical calls
// outlive logical cancellation and retire only on reply or target destruction.
Dict native_media_params_;
size_t native_media_physical_=0;
struct NativeMediaQueued {int owner;std::string session;Dict params;DevToolsCallback callback;};
std::deque<NativeMediaQueued> native_media_queue_;
bool native_media_queue_task_=false;
void NativeMediaWake(){
  if(native_media_queue_task_||native_media_queue_.empty()||native_media_physical_>=64||closing_page_)return;
  native_media_queue_task_=true;
  CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this)]{
    self->native_media_queue_task_=false;
    for(size_t count=0;count<64&&!self->native_media_queue_.empty()&&self->native_media_physical_<64&&!self->closing_page_;++count){
      auto request=std::move(self->native_media_queue_.front());self->native_media_queue_.pop_front();
      self->NativeMediaRequest(request.owner,request.session,request.params,std::move(request.callback));
    }
    self->NativeMediaWake();
  }));
}
struct NativeMediaLatch {
  bool settled=false;std::function<void()> release;
  void Settle(){if(!settled){settled=true;release();}}
  ~NativeMediaLatch(){Settle();}
};
void NativeMediaRequest(int owner,const std::string& session,Dict params,DevToolsCallback callback){
  if(!browser_||closing_page_){callback(false,"Native media page is closed");return;}
  if(native_media_physical_>=64){
    if(native_media_queue_.size()>=1024){callback(false,"Native media command queue capacity exceeded");return;}
    native_media_queue_.push_back({owner,session,params->Copy(false),std::move(callback)});return;
  }
  const int id=AllocateDevToolsId();
  if(!id){callback(false,"Native media physical command capacity exceeded");return;}
  ++native_media_physical_;
  auto latch=std::make_shared<NativeMediaLatch>();latch->release=[this]{--native_media_physical_;NativeMediaWake();};
  devtools_.emplace(id,DevToolsCall{owner,[latch,callback=std::move(callback)](bool ok,const std::string& json)mutable{
    latch->Settle();callback(ok,json);
  },session,true,65536,true});
  if(!SubmitDevTools(id,"Emulation.setEmulatedMedia",params,session)){
    auto found=devtools_.find(id);
    if(found!=devtools_.end()){auto callback=std::move(found->second.callback);devtools_.erase(found);callback(false,"Native media command dispatch failed");}
  }
}
struct NativeMediaWork {
  int owner=0;Dict params;std::vector<std::string> sessions;size_t next=0;
  bool root_applied=false;SuiteCutArtifactController::Callback done;
};
void NativeMediaStep(const std::shared_ptr<NativeMediaWork>& work){
  if(!browser_||closing_page_){work->done(false,work->root_applied?"{\"rootApplied\":true}":"{}");return;}
  while(work->next<work->sessions.size()){
    const auto session=work->sessions[work->next++];
    if(!session.empty()&&!sessions_.contains(session))continue;
    NativeMediaRequest(work->owner,session,work->params,[this,work,session](bool ok,const std::string&){
      if(ok&&session.empty()){native_media_params_=work->params->Copy(false);work->root_applied=true;}
      if(!ok){
        // A detached child no longer has author execution to configure.
        if(!session.empty()&&!sessions_.contains(session)&&!closing_page_){NativeMediaStep(work);return;}
        work->done(false,work->root_applied?"{\"rootApplied\":true}":"{}");return;
      }
      NativeMediaStep(work);
    });
    return;
  }
  work->done(true,"{\"rootApplied\":true}");
}
void NativeMediaApply(int owner,Dict params,SuiteCutArtifactController::Callback done){
  auto work=std::make_shared<NativeMediaWork>();work->owner=owner;work->params=params->Copy(false);work->done=std::move(done);
  work->sessions.push_back("");
  for(const auto& [session,frame]:sessions_)work->sessions.push_back(session);
  NativeMediaStep(work);
}
void NativeMediaChild(const std::string& session,SuiteCutArtifactController::Callback done){
  if(!native_media_params_){done(true,"");return;}
  NativeMediaRequest(0,session,native_media_params_->Copy(false),std::move(done));
}
void NativeMediaClear(){
  auto queued=std::move(native_media_queue_);native_media_queue_.clear();
  for(auto& request:queued)request.callback(false,"Native media page is closed");
}
