// SPDX-License-Identifier: MIT
// Included privately inside the single native Runner coordinator.
void DirectoryPhysicalFence()const{
  if(shared_->active_workers||shared_->descendants.Active()||shared_->file_budget.physical||shared_->file_budget.publications.pending||shared_->step_budget.physical||shared_->soft_budget.physical||shared_->source_leases.Outstanding())throw std::runtime_error("Native directory cannot advance with actual host/file/callback/Source work");
}
int RunDirectory(bool collected_single=false){
  auto facts=std::make_shared<sr::DirectoryReport>();facts->original_start=sr::DedicatedRealMilliseconds();
  // The suite clock was constructed before discovery. Retain that exact origin.
  facts->coordinator_pid=::getpid();facts->original_start=shared_->directory_start;facts->global_timeout=options_.global_timeout;facts->workers=options_.workers;facts->max_failures=options_.max_failures;facts->listing=options_.list;facts->pass_with_no_tests=options_.pass_with_no_tests;
  std::vector<sr::ReportMergeInput> inputs;std::vector<sr::DiscoveredSuite> files;std::vector<sr::CollectedSuiteFile> collected;std::vector<std::string> protected_inputs{options_.host,options_.node};
  // Strip directory-only declarations once, before creating any per-file
  // actor. Discovery inventory and compiled path filters are not copied/rebuilt
  // per file. Preserve other native options so later bounded policies inherit.
  auto child_template=options_;child_template.fail_on_flaky_tests=false;if(options_.terminal_reporter!=sr::NativeTerminalReporter::Console)child_template.terminal_reporter=sr::NativeTerminalReporter::None;child_template.discovery.reset();child_template.workers=1;child_template.list=false;child_template.pass_with_no_tests=true;child_template.projects.clear();child_template.no_dependencies=false;child_template.selection={};child_template.junit.clear();child_template.html.clear();child_template.test_directory=options_.discovery?options_.discovery->directory:options_.test_directory;
  std::string scratch;std::shared_ptr<const sr::MultiFileCatalog> catalog;std::unique_ptr<sr::MultiFilePlan> native_plan;
  const auto error=[&](const std::exception& e){if(facts->errors.size()>=256)throw std::runtime_error("Native directory error capacity exceeded");facts->errors.push_back(Quote(e.what()));};
  try{
    DirectoryPhysicalFence();
    if(collected_single){
      if(!shared_->suite_file||!catalog_||!repeat_collection_timing_)throw std::logic_error("Native repeat promotion lost genuine first import/file/timing receipts");
      files.push_back({std::filesystem::path(options_.suite).filename().string(),std::move(shared_->suite_file)});
      auto declarations=ListingCatalog(*catalog_);sr::DirectoryCollectionReport receipt;
      receipt.file=0;receipt.identity=files[0].owner->Token();receipt.path=files[0].owner->Path();receipt.observed=true;
      receipt.protocol=declarations.protocol;receipt.timing=repeat_collection_timing_;receipt.workers=report_.workers;receipt.errors=report_.global_errors_json;
      // Retain the already-real collection before stop/quota/namespace policy
      // can fail. Transfer only after the facts row has accepted all allocations.
      facts->collections.push_back(std::move(receipt));report_.workers.clear();
      for(const auto& worker:facts->collections.back().workers)shared_->directory_worker_bytes+=worker.stdout_bytes.size()+worker.stderr_bytes.size();
      if(declarations.tests.size()>4096-shared_->directory_declarations||declarations.protocol.size()>16777216-shared_->directory_protocol_bytes)throw std::runtime_error("Native repeat initial collection exceeds original global metadata bounds");
      shared_->directory_declarations+=declarations.tests.size();shared_->directory_protocol_bytes+=declarations.protocol.size();
      facts->resource_locks_serialize_files=HasResourceLocks(*catalog_);
      collected.push_back({files[0].owner->Token(),files[0].owner->Path(),std::move(declarations),options_.selection.catalog_eligible,options_.selection.eligible});
    }
    else files=sr::SuiteDiscovery::Collect(*options_.discovery,[&]{ThrowIfStopped();});
    for(const auto& file:files)protected_inputs.push_back(file.owner->Path());
    for(const auto* output:{&options_.report,&options_.junit,&options_.html})if(!output->empty())sr::CheckTestListingOutput(*output,protected_inputs);
    if(!files.empty()){
      // Fresh native-owned report namespace; committed artifacts remain there.
      std::string pattern=(std::filesystem::path(options_.report).parent_path()/".suitecut-directory-XXXXXX").string();std::vector<char> bytes(pattern.begin(),pattern.end());bytes.push_back(0);const auto result=::mkdtemp(bytes.data());if(!result)throw std::system_error(errno,std::generic_category(),"Native directory report namespace creation failed");scratch=result;
      for(size_t f=0;f<files.size();++f){
        ThrowIfStopped();files[f].owner->Validate();if(collected_single)continue;
        auto a=std::make_shared<sr::DirectoryAdmission>();a->file=files[f].owner->Token();a->path=files[f].owner->Path();a->collection=true;a->timing=std::make_shared<sr::DirectoryActorTiming>(sr::DedicatedRealMilliseconds());
        auto child_options=child_template;child_options.directory_admission=a;child_options.suite=a->path;child_options.output_directory.clear();child_options.preserve_output.reset();child_options.report=scratch+"/collection-"+std::to_string(f)+".json";
        Runner actor(std::move(child_options),shared_);const auto status=actor.Run();DirectoryPhysicalFence();a->timing->Complete(sr::DedicatedRealMilliseconds());
        if(actor.catalog_&&HasResourceLocks(*actor.catalog_))facts->resource_locks_serialize_files=true;
        sr::DirectoryCollectionReport receipt;receipt.file=f;receipt.identity=a->file;receipt.path=a->path;if(actor.directory_catalog_)receipt.protocol=actor.directory_catalog_->protocol;receipt.observed=actor.directory_catalog_.has_value();receipt.timing=a->timing;receipt.workers=actor.report_.workers;receipt.errors=actor.report_.global_errors_json;facts->collections.push_back(std::move(receipt));
        if(status||!actor.directory_catalog_)throw std::runtime_error("Native directory collection failed for "+a->path);
        sr::CollectedSuiteFile file{a->file,a->path,std::move(*actor.directory_catalog_),actor.options_.selection.catalog_eligible,actor.options_.selection.eligible};collected.push_back(std::move(file));
      }
      catalog=std::make_shared<const sr::MultiFileCatalog>(std::move(collected));facts->catalog=catalog;for(auto& receipt:facts->collections)std::string{}.swap(receipt.protocol);
      auto before_shard=options_.selection;before_shard.shard_current=before_shard.shard_total=1;
      const auto eligible=catalog->Select(before_shard,options_.projects,options_.no_dependencies);
      const auto primary_count=[&](const sr::MultiFileSelection& s){size_t n=0;for(size_t p=0;p<s.project_tests.size();++p)if(s.roles[p]=="primary")n+=s.project_tests[p].size();return n;};
      if(!primary_count(eligible)&&!options_.pass_with_no_tests)throw std::runtime_error("No tests found");
      if(DirectoryNeedsNativeRepeats(*catalog,eligible))SelectNativeDirectoryRepeats(*facts,*catalog,eligible);
      else {facts->selection=catalog->Select(options_.selection,options_.projects,options_.no_dependencies);facts->selection_resolved=true;facts->empty_shard=primary_count(eligible)&&!primary_count(facts->selection);}
      facts->active_file_limit=facts->resource_locks_serialize_files?1:options_.workers;
      native_plan=std::make_unique<sr::MultiFilePlan>(*catalog,facts->selection,facts->active_file_limit,options_.max_failures,false,facts->repeat_selection,facts->repeat_groups);auto& plan=*native_plan;
      if(!options_.list&&plan.Segments().size()>128){const std::string why="Native directory actual source capacity128 exceeded before callbacks";plan.Stop(why);facts->errors.push_back(Quote(why));}
      if(!options_.list){
        RunDirectoryCohorts(plan,*catalog,files,scratch,child_template,*facts,inputs);
      }
    }else {facts->selection_resolved=false;if(!options_.pass_with_no_tests)throw std::runtime_error("No tests found");}
  }catch(const std::exception& e){error(e);if(native_plan){native_plan->Stop(e.what());if(std::any_of(native_plan->Segments().begin(),native_plan->Segments().end(),[](const auto& row){return row.state==sr::MultiFilePlan::State::Running;})){DirectoryPhysicalFence();throw std::runtime_error(std::string("Native directory actor failed after real emergency drain; report publication refused: ")+e.what());}}}
  if(native_plan&&!options_.list){facts->project_states=native_plan->Projects();for(size_t i=0;i<native_plan->Segments().size();++i){const auto& s=native_plan->Segments()[i];if(std::any_of(facts->segments.begin(),facts->segments.end(),[&](const auto& row){return row.segment==i;}))continue;if(s.state==sr::MultiFilePlan::State::NotRun||s.state==sr::MultiFilePlan::State::Failed){sr::DirectorySegmentReport row;row.repeat_slice=s.repeat_slice;row.repeat_group=s.repeat_group;row.segment=i;row.file=s.file;row.project=s.project;row.selected_local_ids=s.local_tests;row.state=s.state;row.reason=s.reason;facts->segments.push_back(std::move(row));}}}
  DirectoryPhysicalFence();
  for(auto& file:files){file.owner->Validate();file.owner->Retire();}
  if(!facts->selection_resolved&&facts->errors.empty()&&options_.pass_with_no_tests)facts->empty_shard=true;
  facts->attachment_declarations=shared_->file_budget.declarations.records;facts->attachment_bytes=shared_->file_budget.reserved;facts->step_records=shared_->step_budget.records;facts->soft_records=shared_->soft_budget.records;facts->output_records=shared_->output_budget.records;facts->source_records=shared_->source_leases.Records().size();facts->physical_workers=shared_->active_workers;facts->physical_io=shared_->file_budget.physical;facts->physical_steps=shared_->step_budget.physical;facts->physical_soft=shared_->soft_budget.physical;facts->physical_sources=shared_->source_leases.Outstanding();facts->publication_pending=shared_->file_budget.publications.pending;
  for(const auto* output:{&options_.report,&options_.junit,&options_.html})if(!output->empty())sr::CheckTestListingOutput(*output,protected_inputs);
  sr::ReportMergeOptions merge_options;merge_options.fail_on_flaky_tests=options_.fail_on_flaky_tests;
  auto merged=sr::MergedRunnerReport::ReadDirectory(inputs,facts,merge_options);
  sr::NativeReportPublication publication;
  merged.WriteJSON(options_.report,TerminalPublication(publication));if(!options_.junit.empty())merged.WriteJUnit(options_.junit);if(!options_.html.empty())merged.WriteHTML(options_.html);
  if(options_.terminal_reporter==sr::NativeTerminalReporter::Console)merged.WritePolicyConsole(std::cout);
  const int actual_exit=interrupt_.Signal()?128+interrupt_.Signal():merged.RunFailed()?1:0;
  EmitTerminalReport(publication,actual_exit);return actual_exit;
}
