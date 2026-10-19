// SPDX-License-Identifier: MIT
// Native cooperative cohorts retain each task/frame/actor through actual host,
// descendants, IO, callbacks and Source retirement. Publication then runs at the
// existing global physical fence, so output retention cannot race live actors.
void RunDirectoryCohorts(sr::MultiFilePlan& plan,const sr::MultiFileCatalog& catalog,
    const std::vector<sr::DiscoveredSuite>& files,const std::string& scratch,const Options& child_template,
    sr::DirectoryReport& facts,std::vector<sr::ReportMergeInput>& inputs){
  struct Slot {
    sr::MultiFilePlan::Admission admitted;std::shared_ptr<sr::DirectoryAdmission> admission;
    std::unique_ptr<Runner> actor;sr::NativeTask<int> task;bool started=false;int64_t first_pid=0;
    Slot(sr::MultiFilePlan::Admission owner,std::shared_ptr<sr::DirectoryAdmission> a,
        std::unique_ptr<Runner> runner,sr::MultiFilePlan& policy)
        :admitted(owner),admission(std::move(a)),actor(std::move(runner)),task(actor->RunSerial()){
      admission->actual_host_started=[this,&policy](int64_t pid){if(started)return;if(!policy.Started(admitted,pid))throw std::runtime_error("Native directory actual worker admission rejected");first_pid=pid;started=true;};
      actor->PrepareDirectoryFile();
    }
  };
  std::vector<std::unique_ptr<Slot>> cohort;cohort.reserve(facts.active_file_limit);
  try{
    std::vector<pollfd> fds;
    while(!plan.Done()){
      if(StopRequested())plan.Stop(StopReason());
      while(cohort.size()<facts.active_file_limit){
        const auto admitted=plan.Next();if(!admitted)break;
        const auto& definition=plan.Segments().at(admitted->segment);const auto& file=catalog.Files().at(admitted->file);files.at(admitted->file).owner->Validate();
        auto a=std::make_shared<sr::DirectoryAdmission>();a->file=file.file;a->path=file.path;a->protocol=std::shared_ptr<const std::string>(facts.catalog,&file.catalog.protocol);a->tests=definition.local_tests;a->project_role=NativeRepeatRole(facts.selection.roles.at(admitted->project));a->repeat_selection=facts.repeat_selection;a->repeat_slice=definition.repeat_slice;a->repeat_groups=facts.repeat_groups;a->repeat_group=definition.repeat_group;a->defer_publication=true;a->timing=std::make_shared<sr::DirectoryActorTiming>(sr::DedicatedRealMilliseconds());
        if(admitted->project!=catalog.AnonymousProject()){
          const auto& name=catalog.Graph().Declarations().at(admitted->project).name;
          for(size_t p=0;p<file.catalog.projects.size();++p)if(file.catalog.projects[p].name==name)a->project=p;
          if(!a->project)throw std::logic_error("Native directory lost actual project mapping");
        }
        auto child=child_template;child.directory_admission=a;child.suite=a->path;child.report=scratch+"/segment-"+std::to_string(admitted->segment)+".json";
        cohort.push_back(std::make_unique<Slot>(*admitted,std::move(a),std::make_unique<Runner>(std::move(child),shared_),plan));
      }
      if(cohort.empty())break;
      for(;;){
        bool pending=false;
        for(auto& slot:cohort)if(!slot->task.Done()){slot->task.Resume(slot->actor->continuation_);if(!slot->task.Done())pending=true;else slot->admission->timing->Complete(sr::DedicatedRealMilliseconds());}
        TickDescendants();
        if(!pending)break;
        fds.clear();double wake=sr::DedicatedRealMilliseconds()+10;
        for(const auto& slot:cohort)if(!slot->task.Done()){slot->actor->Descriptors(fds);wake=std::min(wake,slot->actor->continuation_.wake);}
        WaitDescriptors(fds,wake);
      }
      DirectoryPhysicalFence();
      for(auto& slot:cohort){
        (void)slot->task.Result();
        const auto status=slot->actor->Publish();const auto summary=sr::report_detail::Validate(slot->actor->report_);
        bool actual_interrupted=false;
        if(slot->actor->plan_)for(const auto& result:slot->actor->plan_->Results())actual_interrupted|=result.outcome==sr::Outcome::Interrupted;
        const bool completed_failure=(slot->actor->plan_&&slot->actor->plan_->UnexpectedTestFailures())||slot->actor->artifact_cleanup_failed_||slot->actor->host_control_failed_||slot->actor->close_failure_;
        const bool completed_expected=slot->actor->DirectoryCompletedExpected();
        const bool interrupted=actual_interrupted||(!completed_expected&&(interrupt_.Signal()||suite_deadline_.Expired()));
        const auto outcome=completed_failure?sr::MultiFilePlan::Outcome::Failed:completed_expected?sr::MultiFilePlan::Outcome::Passed:interrupted?sr::MultiFilePlan::Outcome::Interrupted:status||summary.did_not_run||!slot->actor->report_.global_errors_json.empty()?sr::MultiFilePlan::Outcome::Failed:sr::MultiFilePlan::Outcome::Passed;
        if(slot->started){if(!plan.Complete(slot->admitted,outcome,summary.unexpected)||!plan.PhysicalExit(slot->admitted,slot->first_pid))throw std::runtime_error("Native directory completion lacks exact original physical worker receipt");}
        else if(!plan.RefusedBeforeStart(slot->admitted,"Native directory actor stopped before actual host admission"))throw std::runtime_error("Native directory refusal lost original reservation");
        auto source="file-"+std::to_string(slot->admission->file.id)+"-project-"+std::to_string(slot->admitted.project+1);if(slot->admission->repeat_selection){const auto& repeat=slot->admission->repeat_selection->Slices().at(*slot->admission->repeat_slice);source+="-repeat-"+std::to_string(repeat.index);if(slot->admission->repeat_group)source+="-group-"+std::to_string(*slot->admission->repeat_group);}inputs.push_back({source,slot->actor->options_.report});
        sr::DirectorySegmentReport row;row.repeat_slice=slot->admission->repeat_slice;row.repeat_group=slot->admission->repeat_group;row.segment=slot->admitted.segment;row.file=slot->admitted.file;row.project=slot->admitted.project;row.generation=slot->admitted.generation;row.selected_local_ids=slot->admission->tests;row.source_id=source;row.native_retired=true;row.state=plan.Segments().at(slot->admitted.segment).state;row.timing=slot->admission->timing;facts.segments.push_back(std::move(row));
      }
      cohort.clear();
    }
  }catch(...){const auto original=std::current_exception();EmergencyDrain();std::rethrow_exception(original);}
  if(!plan.Done())plan.Stop(StopRequested()?StopReason():"Native directory coordinator could not admit remaining work");
  for(size_t i=0;i<plan.Segments().size();++i)if(plan.Segments()[i].state==sr::MultiFilePlan::State::NotRun){const auto& s=plan.Segments()[i];sr::DirectorySegmentReport row;row.repeat_slice=s.repeat_slice;row.repeat_group=s.repeat_group;row.segment=i;row.file=s.file;row.project=s.project;row.selected_local_ids=s.local_tests;row.state=s.state;row.reason=s.reason;facts.segments.push_back(std::move(row));}
}
