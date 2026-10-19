// SPDX-License-Identifier: MIT
// Included in Runner. Existing actors keep actual process/file/Source owners.
  double CallbackRetryWake(double wake)const{if(const auto next=callback_retries_.NextWake())return std::min(wake,*next);return wake;}
  void CallbackRetryHard(const sr::StepOwner&,const std::string& error){callback_infrastructure_failed_=true;if(callback_error_.empty())callback_error_=error;finish_ok_=false;}
  void ApplyCallbackRetryOutcome(){if(callback_infrastructure_failed_){finish_ok_=false;if(actual_error_.empty())actual_error_=callback_error_.empty()?Quote("Native callback retry infrastructure failed"):callback_error_;}}
  sr::RunnerCallbackRetries::Hooks CallbackRetryHooks(){
    return{
      [this](const auto& packet){
        if(host_&&!host_->Exited()&&!host_->ControlClosed()){Send(packet);return;}
        if(packet.starts_with("TOPASS_RUN ")||packet.starts_with("TOPASS_ADMITTED "))throw std::runtime_error("Native callback retry dispatch lost actual host control ownership");
      },
      [this]{return shared_->step_budget.sequence==UINT64_MAX?0:shared_->step_budget.sequence+1;},
      [this](const auto& owner,uint64_t parent,uint64_t assertion,uint64_t invocation,std::optional<double> cap,double now){return steps_.BeginCallbackIteration(owner,parent,assertion,invocation,cap,now);},
      [this](const auto& owner,uint64_t id,uint64_t invocation,bool ok,const auto& error,uint64_t source,auto forward,double now){steps_.SettleCallbackIteration(owner,id,invocation,ok,error,source,std::move(forward),now);},
      [this](const auto& owner,uint64_t id,const auto& error,double now){steps_.CancelCallbackIteration(owner,id,error,now);},
      [this](const auto& owner,const auto& retry,auto forward,double now){soft_.FenceStep(owner,retry.iteration_step,[this,owner,retry,forward=std::move(forward)]{attachments_.FenceRetry(owner,retry,std::move(forward),FixtureNow());},now);},
      [this](const auto& owner,uint64_t request,uint64_t assertion,uint64_t invocation,uint64_t step,uint64_t ticket,double end,double now){return CallbackRetrySourceFence(owner,request,assertion,invocation,step,ticket,end,now);},
      [this](uint64_t id){return steps_.Find(id);},
      [this](const auto& owner,const auto& error){CallbackRetryHard(owner,error);},
      [this](const auto& error){callback_infrastructure_failed_=true;if(callback_error_.empty())callback_error_=Quote(error);finish_ok_=false;Global(Quote(error));KillHost();},
      [this]{return FixtureNow();},
      [this](const auto& owner){return owner.worker_index!=worker_index_||!host_||owner.pid!=host_->Pid()||host_control_failed_||artifact_cleanup_failed_||fixture_mutation_failed_||soft_infrastructure_failed_||(owners_&&owners_->InfrastructureFailure())||callback_infrastructure_failed_||physical_kill_requested_;}
    };
  }
  void CallbackRetryStepFence(const sr::StepOwner& owner,uint64_t id,std::function<void(const std::optional<std::string>&)> forward,double now){
    if(callback_retries_.OwnsIteration(owner,id)){callback_retries_.FenceIteration(owner,id,std::move(forward),now);return;}
    callback_retries_.FenceStep(owner,id,[this,owner,id,forward=std::move(forward)](const auto& retry_failure){
      soft_.FenceStep(owner,id,[this,owner,id,forward,retry_failure]{attachments_.FenceStep(owner,id,[forward,retry_failure](const auto& file_failure){forward(retry_failure?retry_failure:file_failure);},FixtureNow());},FixtureNow());
    },now);
  }
  sr::RunnerCallbackRetries::SourceFence CallbackRetrySourceFence(const sr::StepOwner& owner,uint64_t request,uint64_t assertion,uint64_t invocation,uint64_t step,uint64_t ticket,double end,double now){
    using Result=sr::RunnerCallbackRetries::SourceFence;
    if(!sources_||!source_bound_||!sources_->Receiver().Registration()||sources_->Receiver().Registration()->id!=ticket||!sources_->Receiver().AdmissionActive())return Result::Failed;
    const auto* record=steps_.Find(step);if(!record||!record->callback_iteration||record->owner!=owner)return Result::Failed;
    const sr::RunnerRetainedSources::CallbackIterationOwner actual{owner,request,record->parent,step,assertion,invocation};
    if(!callback_retries_.HasSourceFenceOwner(owner,request,assertion,invocation,step,record->parent))return Result::Failed;
    if(callback_source_owner_&&*callback_source_owner_!=actual)return Result::Pending;
    if(!callback_source_owner_){callback_source_owner_=actual;if(!sources_->BeginCallbackIterationFence(actual,end,now))return Result::Failed;}
    if(sources_->CallbackIterationFenceRefused(actual))return Result::Failed;
    if(!sources_->CallbackIterationFenceReady(actual,now))return Result::Pending;
    const auto receipt=sources_->CallbackIterationFenceReceipt(actual,now);if(!receipt)throw std::runtime_error("Native callback Source ready lost its exact receipt");
    steps_.SetCallbackSourceFence(owner,step,*receipt);
    if(!sources_->FinishCallbackIterationFence(actual,now))throw std::runtime_error("Native callback Source retirement lost actual resume receipts");
    callback_source_owner_.reset();return Result::Ready;
  }
  void CallbackRetryAmend(uint64_t token,uint64_t scope,uint64_t generation,double deadline,double now){
    if(const auto* owner=steps_.CallbackOwner(token,scope,generation))callback_retries_.Amend(*owner,sr::SoftCallbackDeadline(owner->callback_kind,deadline,suite_deadline_.OriginalDeadline(),hook_stop_cleanup_deadline_?std::make_optional(hook_stop_cleanup_deadline_):std::nullopt),now);
  }
  void CallbackRetryCancel(uint64_t token,uint64_t scope,uint64_t generation,double now){if(const auto* owner=steps_.CallbackOwner(token,scope,generation))callback_retries_.Cancel(*owner,now);}
  void CallbackRetryPublicationFence(){
    if(shared_->callback_publication_failure)std::rethrow_exception(shared_->callback_publication_failure);
    try{callback_retries_.ThrowIfUnpublishable();}
    catch(...){shared_->callback_publication_failure=std::current_exception();throw;}
  }
  void CollectCallbackRetries(){
    CallbackRetryPublicationFence();
    if(!callback_retries_.IdleReady())throw std::runtime_error("Native callback retry publication retains actual callback work");
    const auto& rows=callback_retries_.Records();
    if(callback_record_cursor_>rows.size())throw std::runtime_error("Native callback retry history cursor changed");
    report_.callback_assertions.insert(report_.callback_assertions.end(),rows.begin()+static_cast<std::ptrdiff_t>(callback_record_cursor_),rows.end());callback_record_cursor_=rows.size();
  }
