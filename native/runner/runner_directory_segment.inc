// SPDX-License-Identifier: MIT
// Included inside Runner. Existing native ledgers remain shared by all files.
std::unique_ptr<sr::SuiteFileOwner>& FileOwner(){return options_.directory_admission?directory_file_:shared_->suite_file;}
void ValidateDirectorySegment(const Metadata& value){
  if(!options_.directory_admission)return;
  const auto& a=*options_.directory_admission;
  if(!FileOwner()||FileOwner()->Token()!=a.file||FileOwner()->Path()!=a.path||(!a.collection&&(!a.protocol||value.signature!=*a.protocol)))throw std::runtime_error("Native directory collected file/catalog identity changed");
  FileOwner()->Validate();
  DirectoryHostMetadata();
  if(a.collection)return;
  if(a.project){if(*a.project>=value.projects.size()||value.project_tests.size()!=value.tests.size())throw std::runtime_error("Native directory project identity rejected");}
  else if(!value.projects.empty()||!value.project_tests.empty())throw std::runtime_error("Native anonymous directory actor received project metadata");
  std::set<uint64_t> available;for(const auto& t:value.tests)if(!a.project||value.project_tests.at(t.id)==*a.project)available.insert(t.id);
  for(const auto id:a.tests)if(!available.contains(id))throw std::runtime_error("Native directory selected ID has no actual declaration");
}
void ConfigureDirectorySegment(const Metadata& value,bool reused){
  if(reused||parallel_actor_||project_plan_)throw std::runtime_error("Native directory actor cannot reselect/borrow another owner");
  ValidateDirectorySegment(value);const auto& a=*options_.directory_admission;
  Send("DIRECTORY_FILE_CONTEXT "+std::to_string(a.file.id)+" "+Hex(sr::listing_detail::Quote(a.path)));
  if(a.project)SendProjectContext(value,*a.project);
  BindNativeRepeatActor(value);
  if(!catalog_)catalog_=value;
}

// No IO or author callback can be admitted after this coroutine result. The
// actor, original task frame, reports and exact file pin remain cohort-owned
// until the shared global publication fence is closed.
int DirectoryCompletedResult(){
  if(!options_.directory_admission||!options_.directory_admission->defer_publication||host_||DescendantsPhysical()||!attachments_.IdleReady()||!steps_.IdleReady()||(!soft_.IdleReady()||!callback_retries_.IdleReady())||outputs_.Physical()||SourcesPhysical())throw std::runtime_error("Native deferred directory result lacks exact physical actor retirement");
  const auto summary=sr::report_detail::Validate(report_);
  return interrupt_.Signal()?128+interrupt_.Signal():!report_.global_errors_json.empty()||summary.unexpected?1:0;
}
void PrepareDirectoryFile(){
  const auto& a=*options_.directory_admission;directory_file_=std::make_unique<sr::SuiteFileOwner>(a.file.id,a.path);
  if(directory_file_->Token()!=a.file)throw std::runtime_error("Native directory file identity changed before host admission");
}
void DirectoryHostStarted(int64_t pid){if(!options_.directory_admission){if(repeat_collection_timing_)repeat_collection_timing_->HostStarted(worker_index_,pid,sr::DedicatedRealMilliseconds());return;}const auto& a=*options_.directory_admission;if(a.timing)a.timing->HostStarted(worker_index_,pid,sr::DedicatedRealMilliseconds());if(a.actual_host_started)a.actual_host_started(pid);}
void DirectoryHostMetadata(){if(options_.directory_admission&&options_.directory_admission->timing&&host_)options_.directory_admission->timing->Metadata(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());}
void DirectoryHostClosing(){if(!options_.directory_admission&&repeat_collection_timing_&&host_)repeat_collection_timing_->Closing(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());if(options_.directory_admission&&options_.directory_admission->timing&&host_)options_.directory_admission->timing->Closing(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());}
void DirectoryHostCaptured(){if(!options_.directory_admission&&repeat_collection_timing_&&host_)repeat_collection_timing_->Captured(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());if(options_.directory_admission&&options_.directory_admission->timing&&host_)options_.directory_admission->timing->Captured(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());}


// A sibling's later stop cannot rewrite an already-completed native file. This
// reads actual plan/failure/retirement facts and never clears an authored error.
bool DirectoryCompletedExpected()const{
  if(!options_.directory_admission||!plan_||!metadata_||!plan_->Done()||!report_.unrun.empty()||!report_.global_errors_json.empty()||host_||DescendantsPhysical()||!attachments_.IdleReady()||!steps_.IdleReady()||(!soft_.IdleReady()||!callback_retries_.IdleReady())||outputs_.Physical()||SourcesPhysical()||callback_infrastructure_failed_||artifact_cleanup_failed_||host_control_failed_||close_failure_)return false;
  for(const auto& result:plan_->Results())if(!plan_->FinalExpected(result.attempt.test.id))return false;
  return true;
}
