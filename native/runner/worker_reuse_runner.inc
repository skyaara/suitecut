// SPDX-License-Identifier: MIT
// Included in Runner's private scope. This coordinator retains each actual
// actor across logical groups. Existing fresh-worker/project paths are separate.
  static bool EligibleWorkerReuse(const Metadata& value,const sr::FileWorkGroups& groups){
    if(!value.projects.empty()||value.fully_parallel!=true||!value.suites.empty()||!value.hooks.empty()||!value.nested_hooks.empty()||!value.skipped.empty()||HasResourceLocks(value))return false;
    for(const auto& test:value.tests)if(test.retries||test.expectedFailure)return false;
    for(const auto& group:groups.Groups())if(group.mode!=sr::WorkGroupMode::Parallel||group.suite||group.tests.size()!=1)return false;
    return groups.Groups().size()>1;
  }
  sr::WorkerReusePool::GroupFence ReusableFence()const{
    sr::WorkerReusePool::GroupFence fence;
    fence.raw_callbacks=body_physical_?1:0;fence.steps=steps_.Physical();fence.step_callbacks=steps_.IdleReady()&&callback_retries_.IdleReady()?0:1;
    fence.soft_pending=soft_.Physical();fence.soft_contexts=soft_.IdleReady()?0:1;
    fence.file_jobs=attachments_.Physical();fence.file_callbacks=attachments_.IdleReady()?0:1;fence.output_jobs=outputs_.Physical();
    fence.control_open=host_&&!host_->Exited()&&!host_->ControlClosed()&&!host_control_failed_&&!physical_kill_requested_;
    fence.clean_outcomes=!fixture_mutation_failed_&&!artifact_cleanup_failed_&&!skip_callback_failed_&&!runtime_skip_&&!runtime_expected_failure_&&plan_&&plan_->Done()&&plan_->UnexpectedTestFailures()==0;
    fence.clean_outcomes&=!soft_hard_failure_&&!soft_cleanup_failed_&&!soft_infrastructure_failed_&&!callback_infrastructure_failed_;
    if(plan_)for(const auto& result:plan_->Results())fence.clean_outcomes&=!soft_.FailedAttempt(result.attempt.generation);
    fence.fixtures_ready=owners_&&owners_->IdleReady()&&(!fixture_runtime_||fixture_runtime_->IdleReady())&&!SourcesPhysical();
    if(owners_){const auto state=owners_->State();fence.test_fixtures=state.testFixtures;owners_->ForEachReadyWorkerLease([&](uint64_t token,uint64_t test,uint64_t generation){if(fence.retained.count>=fence.retained.values.size())throw std::length_error("Native reusable fixture snapshot capacity exceeded");fence.retained.values[fence.retained.count++]={token,test,generation};});}
    return fence;
  }
  void CompleteReusableGroup(){
    if(!reuse_borrow_||!reuse_pool_)return;
    const auto& record=reuse_pool_->Borrows()[reuse_borrow_->generation-1];if(record.outcome)return;
    sr::WorkerReusePool::Completion evidence;evidence.all_done=plan_&&plan_->Done();evidence.global_stopped=StopRequested();evidence.native_failure=fixture_mutation_failed_||artifact_cleanup_failed_||host_control_failed_||skip_callback_failed_||soft_cleanup_failed_||soft_infrastructure_failed_||callback_infrastructure_failed_;
    if(plan_)for(const auto& result:plan_->Results()){evidence.all_expected&=result.expected;evidence.actual_interrupted|=result.outcome==sr::Outcome::Interrupted;evidence.unexpected_failure|=!result.expected&&result.outcome!=sr::Outcome::Interrupted;}
    const auto outcome=sr::WorkerReusePool::CompletionOutcome(evidence);
    if(!reuse_pool_->Complete(*reuse_borrow_,outcome))throw std::runtime_error("Native reusable group completion lost exact live borrow");
    if(outcome!=sr::WorkerReusePool::Outcome::Passed||runtime_skip_||runtime_expected_failure_)reuse_pool_->Drain();
  }
  void RetireReusableHost(){
    if(!reuse_pool_||!reuse_borrow_||!host_||!host_->Exited()||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical()||(!soft_.IdleReady()||!callback_retries_.IdleReady()))throw std::runtime_error("Native reusable host lacks actual process/file/descendant retirement");
    CompleteReusableGroup();const auto exit=*host_->Exited();const auto now=sr::DedicatedRealMilliseconds();
    if(!reuse_pool_->PrimaryExited(reuse_borrow_->host,{exit.code,exit.signaled},now))throw std::runtime_error("Native reusable primary waitpid receipt rejected");
    sr::WorkerReusePool::RetirementFence fence;fence.soft_pending=soft_.Physical();fence.soft_contexts=soft_.IdleReady()?0:1;fence.steps=steps_.Physical();fence.step_callbacks=steps_.IdleReady()&&callback_retries_.IdleReady()?0:1;fence.file_jobs=attachments_.Physical();fence.file_callbacks=attachments_.IdleReady()?0:1;fence.output_jobs=outputs_.Physical();fence.worker_fixtures=owners_?owners_->State().physical:0;fence.descendants_retired=!DescendantsPhysical();
    if(!reuse_pool_->Retired(reuse_borrow_->host,fence,now))throw std::runtime_error("Native reusable worker remains physically charged after primary exit");
    project_worker_pid_=0;
  }
  void AppendReusableRecords(){
    if((!soft_.IdleReady()||!callback_retries_.IdleReady()))throw std::runtime_error("Native reusable soft history still owns callback work");
    CollectAttachmentViews();CollectCallbackRetries();
    const auto& soft=soft_.Records();report_.soft_assertions.insert(report_.soft_assertions.end(),soft.begin()+static_cast<std::ptrdiff_t>(reuse_soft_cursor_),soft.end());reuse_soft_cursor_=soft.size();
    const auto& steps=steps_.Records();const auto& files=attachments_.Records();const auto& declarations=attachments_.Declarations();const auto outputs=outputs_.Records();
    report_.steps.insert(report_.steps.end(),steps.begin()+static_cast<std::ptrdiff_t>(reuse_step_cursor_),steps.end());reuse_step_cursor_=steps.size();
    report_.attachments.insert(report_.attachments.end(),files.begin()+static_cast<std::ptrdiff_t>(reuse_file_cursor_),files.end());reuse_file_cursor_=files.size();
    report_.attachment_declarations.insert(report_.attachment_declarations.end(),declarations.begin()+static_cast<std::ptrdiff_t>(reuse_declaration_cursor_),declarations.end());reuse_declaration_cursor_=declarations.size();
    report_.outputs.insert(report_.outputs.end(),outputs.begin()+static_cast<std::ptrdiff_t>(reuse_output_cursor_),outputs.end());reuse_output_cursor_=outputs.size();
  }
  sr::NativeTask<> RunReusableGroup(){
    const auto errors=local_errors_;std::optional<std::string> caught;
    try{
      ThrowIfStopped();
      if(!host_)co_await StartWorker();
      else if(!metadata_)ConfigureWorker(*catalog_,owners_!=nullptr);
      co_await ExecuteProject(true);CompleteReusableGroup();
      if(local_errors_!=errors)reuse_pool_->Drain();
    }catch(const std::exception& error){caught=error.what();Global(Quote(*caught));if(reuse_borrow_)CompleteReusableGroup();reuse_pool_->Drain();}
    if(caught||StopRequested()||reuse_pool_->Draining())co_await CloseWorker(caught.has_value());
    if(metadata_)MarkUnrun(StopRequested()?StopReason():"Native reused group stopped before selected declaration admission");
    AppendReusableRecords();
  }
  void ReusableIdlePacket(const std::vector<std::string>& f,double now){
    Size(f,10);if(!reuse_pool_||!reuse_request_||!reuse_borrow_||reuse_idle_ack_observed_)throw std::runtime_error("Native idle ACK lacks an actual pending host request");
    const auto& owner=*reuse_borrow_;
    if(Integer(f[1])!=owner.group.file.id||Integer(f[2])!=owner.group.index+1||Integer(f[3])!=owner.group.generation||Integer(f[4])!=owner.host.lease||Integer(f[5])!=owner.generation||Integer(f[6])!=reuse_request_->request||Integer(f[8])!=owner.host.worker||Integer(f[9])!=static_cast<uint64_t>(owner.host.pid))throw std::runtime_error("Native idle ACK has foreign actual worker ownership");
    if(StopRequested())reuse_pool_->Stop();else reuse_pool_->ExpireIdle(now);
    const sr::WorkerReusePool::IdleACK ack{owner,Integer(f[6]),static_cast<size_t>(Integer(f[7],64))};
    if(!reuse_pool_->Acknowledge(ack,now)&&!reuse_pool_->ObserveCanceledIdleACK(ack,now))throw std::runtime_error("Native idle ACK is stale, expired or already observed");
    reuse_idle_ack_observed_=true;
  }
  sr::NativeTask<> AwaitReusableIdle(){
    const auto now=sr::DedicatedRealMilliseconds();auto end=now+1000;if(const auto original=suite_deadline_.OriginalDeadline())end=std::min(end,*original);
    if(SourcesPhysical()){
      const auto& borrow=*reuse_borrow_;
      const sr::RetainedSourceWorkWire::Borrow source_borrow{borrow.host.lease,borrow.generation,borrow.host.worker,borrow.host.pid,source_host_.start};
      if(!sources_->BeginAcceptedIdle(source_borrow,end,now)){reuse_pool_->Drain();co_return;}
      while(!sources_->AcceptedIdleReady(sr::DedicatedRealMilliseconds())){
        if(StopRequested()||sources_->AcceptedIdleRefused()||sr::DedicatedRealMilliseconds()>=end){reuse_pool_->Drain();co_return;}
        co_await Pump();if(host_->Exited())throw std::runtime_error("Native Source host exited before accepted-work observation");
      }
      // The process-level barrier is genuine evidence, but it does not attest
      // every CEF descendant. Keep unknown-child eligibility fail-closed until
      // the independent native launch/cohort membership ledger is installed.
      reuse_pool_->Drain();co_return;
    }
    reuse_request_=reuse_pool_->RequestIdle(*reuse_borrow_,ReusableFence(),now,end);reuse_idle_ack_observed_=false;
    if(!reuse_request_){reuse_pool_->Drain();co_return;}
    const auto& owner=*reuse_borrow_;Send("WORKER_IDLE "+std::to_string(owner.group.file.id)+" "+std::to_string(owner.group.index+1)+" "+std::to_string(owner.group.generation)+" "+std::to_string(owner.host.lease)+" "+std::to_string(owner.generation)+" "+std::to_string(reuse_request_->request)+" "+std::to_string(reuse_request_->fixtures));
    while(!reuse_idle_ack_observed_&&!reuse_pool_->Draining()){
      if(StopRequested()){reuse_pool_->Stop();break;}
      if(reuse_pool_->ExpireIdle(sr::DedicatedRealMilliseconds()))break;
      co_await Pump();if(host_->Exited())throw std::runtime_error("Native idle worker exited before its readiness receipt");
    }
  }
  int RunReusableFileGroups(const sr::FileWorkGroups& groups,const std::vector<uint64_t>& ids,const std::vector<uint64_t>& selected){
    const sr::WorkerReusePool::Key key{groups.File(),1};sr::WorkerReusePool pool(key,options_.workers,groups.Groups().size(),suite_deadline_.OriginalDeadline());
    enum class Phase {RunningGroup,WaitingCohort,WaitingACK,WaitingInventory,Idle,Closing};
    struct Slot {
      std::unique_ptr<Runner> owned;Runner* actor=nullptr;std::optional<sr::NativeTask<>> task;Phase phase=Phase::RunningGroup;
      explicit Slot(Runner& value):actor(&value){}
      explicit Slot(std::unique_ptr<Runner> value):owned(std::move(value)),actor(owned.get()){}
    };
    std::vector<std::unique_ptr<Slot>> slots;std::unique_ptr<Slot> staged;size_t next=0;uint64_t generation=0;
    const auto bind=[&](Runner& actor){actor.parallel_actor_=true;actor.catalog_=catalog_;actor.reuse_pool_=&pool;actor.reuse_plan_=&groups;actor.reuse_key_=key;};
    const auto admit=[&](Slot& slot,bool reused){
      auto& actor=*slot.actor;actor.current_file_group_=sr::FileGroupPool::Admission{groups.File(),next++,++generation};actor.metadata_.reset();actor.plan_.reset();actor.failure_accounted_=0;actor.reuse_request_.reset();actor.reuse_idle_ack_observed_=false;
      if(reused){const auto borrow=pool.Reuse(actor.reuse_borrow_->host,key,*actor.current_file_group_);if(!borrow)throw std::runtime_error("Native idle host lease lost exact compatible group admission");actor.reuse_borrow_=*borrow;}
      else if(actor.host_){actor.reuse_borrow_=pool.Started(key,*actor.current_file_group_,actor.worker_index_,actor.host_->Pid());if(!actor.reuse_borrow_)throw std::runtime_error("Native catalog host reuse admission rejected");actor.project_worker_pid_=actor.host_->Pid();}
      slot.task.emplace(actor.RunReusableGroup());slot.phase=Phase::RunningGroup;
    };
    try{
      slots.reserve(options_.workers);bind(*this);slots.push_back(std::make_unique<Slot>(*this));admit(*slots.back(),false);
      std::vector<pollfd> fds;
      for(;;){
        if(StopRequested())pool.Stop();
        if(!pool.Draining()&&!StopRequested()&&shared_->descendants.CanEnroll())while(next<groups.Groups().size()&&slots.size()<options_.workers){
          SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("actor-staging",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);
          auto actor=std::make_unique<Runner>(options_,shared_);actor->options_.fail_on_flaky_tests=false;bind(*actor);staged=std::make_unique<Slot>(std::move(actor));admit(*staged,false);slots.push_back(std::move(staged));
        }
        for(auto it=slots.begin();it!=slots.end();){auto& slot=**it;auto& actor=*slot.actor;
          if(slot.task){if(!slot.task->Done())slot.task->Resume(actor.continuation_);if(slot.task->Done()){
            slot.task->Result();slot.task.reset();
            if(slot.phase==Phase::Closing||!actor.host_){actor.AppendReusableRecords();it=slots.erase(it);continue;}
            slot.phase=slot.phase==Phase::WaitingACK?Phase::WaitingInventory:Phase::WaitingCohort;
          }}++it;
        }
        TickDescendants();
        if(pool.Draining()||StopRequested()||next==groups.Groups().size()){
          if(next==groups.Groups().size()&&std::none_of(slots.begin(),slots.end(),[](const auto& slot){return slot->phase==Phase::RunningGroup;}))pool.Drain();
          if(pool.Draining()||StopRequested())for(auto& slot:slots)if(!slot->task&&slot->actor->host_){slot->task.emplace(slot->actor->CloseWorker());slot->phase=Phase::Closing;}
        }
        if(slots.empty()){
          if(StopRequested()||next==groups.Groups().size())break;
          if(pool.Physical())throw std::runtime_error("Native reusable cohort lost actual host owners");
          continue;
        }
        if(!pool.Draining()&&!StopRequested()){
          const bool quiescent=std::none_of(slots.begin(),slots.end(),[](const auto& slot){return slot->phase==Phase::RunningGroup||slot->phase==Phase::Closing;});
          if(quiescent&&next<groups.Groups().size()){
            for(auto& slot:slots)if(slot->phase==Phase::WaitingCohort){slot->task.emplace(slot->actor->AwaitReusableIdle());slot->phase=Phase::WaitingACK;}
            if(const auto probe=pool.InventoryProbe()){
              const auto inventory=sr::ObserveWorkerReuseInventory(*probe);std::array<sr::WorkerReusePool::CohortFence,8> fences{};size_t count=0;
              for(size_t i=0;i<probe->count;++i){const auto found=std::find_if(slots.begin(),slots.end(),[&](const auto& slot){return slot->actor->reuse_borrow_->host==probe->hosts[i];});if(found==slots.end())throw std::logic_error("Native reuse inventory lost exact live actor");fences[count++]={*(*found)->actor->reuse_borrow_,(*found)->actor->ReusableFence()};}
              if(!pool.ConfirmInventory(inventory,{fences.data(),count},sr::DedicatedRealMilliseconds()))pool.Drain();
              else for(auto& slot:slots){slot->phase=Phase::Idle;if(next<groups.Groups().size())admit(*slot,true);}
            }
          }
        }
        pool.ExpireIdle(sr::DedicatedRealMilliseconds());
        if(!slots.empty()){fds.clear();double wake=sr::DedicatedRealMilliseconds()+10;if(const auto idle=pool.NextIdleDeadline())wake=std::min(wake,*idle);for(const auto& slot:slots){slot->actor->Descriptors(fds);if(slot->task)wake=std::min(wake,slot->actor->continuation_.wake);}WaitDescriptors(fds,wake);}
      }
      if(pool.Physical()||shared_->active_workers||shared_->descendants.Active()||shared_->file_budget.physical||shared_->step_budget.physical||shared_->soft_budget.physical||shared_->source_leases.Outstanding())throw std::runtime_error("Native reuse report lacks actual final host/file/domain retirement");
      report_.worker_reuse.hosts.assign(pool.Hosts().begin(),pool.Hosts().end());report_.worker_reuse.borrows.assign(pool.Borrows().begin(),pool.Borrows().end());
      bool failed=false,interrupted=false,not_run=false;std::set<uint64_t> reported;for(const auto& row:report_.attempts)reported.insert(row.test_id);for(const auto& row:report_.unrun)reported.insert(row.test_id);for(const auto& row:report_.runtime_skipped)reported.insert(row.test_id);
      for(size_t i=0;i<groups.Groups().size();++i){const auto& definition=groups.Groups()[i];sr::ReportFileWorkGroup row;row.id=i+1;row.file_id=groups.File().id;row.mode=definition.mode;row.tests=definition.tests;
        const auto borrow=std::find_if(pool.Borrows().begin(),pool.Borrows().end(),[&](const auto& record){return record.borrow.group.index==i;});
        if(borrow==pool.Borrows().end()){row.status="notRun";row.receipt="notAdmitted";not_run=true;}
        else{row.generation=borrow->borrow.group.generation;row.workers={borrow->borrow.host.worker};if(!borrow->outcome)throw std::runtime_error("Native reused group has no genuine terminal outcome");row.status=*borrow->outcome==sr::WorkerReusePool::Outcome::Passed?"passed":*borrow->outcome==sr::WorkerReusePool::Outcome::Failed?"failed":"interrupted";row.receipt=borrow->receipt==sr::WorkerReusePool::Receipt::IdleTransferred?"idleTransferred":borrow->receipt==sr::WorkerReusePool::Receipt::PhysicalRetired?"physicalRetired":"active";failed|=row.status=="failed";interrupted|=row.status=="interrupted";}
        for(const auto id:row.tests)if(reported.insert(id).second){const auto reason=Quote(StopRequested()?StopReason():"Native group was not admitted after actual worker retirement");Charge(catalog_->titles.at(id).size()+reason.size()+128);report_.unrun.push_back({id,catalog_->titles.at(id),reason});}
        report_.file_work_groups.push_back(std::move(row));
      }
      const auto opened=std::chrono::duration<double,std::milli>(shared_->suite_file->Opened().time_since_epoch()).count();const auto retired=std::chrono::duration<double,std::milli>(shared_->suite_file->Retire().time_since_epoch()).count();report_.files.push_back({groups.File(),shared_->suite_file->Path(),failed?"failed":interrupted?"interrupted":not_run?"notRun":"passed",ids,selected,opened,retired});
      if(StopRequested()&&!shared_->stop_recorded){Global(Quote(StopReason()));shared_->stop_recorded=true;}
      reuse_pool_=nullptr;reuse_plan_=nullptr;return Publish();
    }catch(...){const auto original=std::current_exception();pool.Stop();EmergencyDrain();if(pool.Physical())throw std::runtime_error("Native emergency reuse cleanup retained live host leases");reuse_pool_=nullptr;reuse_plan_=nullptr;std::rethrow_exception(original);}
  }
