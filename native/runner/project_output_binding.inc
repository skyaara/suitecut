// SPDX-License-Identifier: MIT
// Included inside Runner. Only native admitted IDs choose an output root.
std::filesystem::path OutputFallbackRoot()const{
  return options_.output_directory.empty()?std::filesystem::path(options_.report).parent_path()/"test-results":std::filesystem::path(options_.output_directory);
}
void SendProjectContext(const Metadata& value,size_t actual_project){
  std::string packet="PROJECT_CONTEXT "+std::to_string(actual_project+1)+" "+Hex(value.projects.at(actual_project).name)+" "+Hex(value.project_metadata.at(actual_project));
  if(const auto* root=value.project_output_roots.Override(actual_project))packet+=" "+Hex(*root);
  Send(packet);
  const auto declared=value.repeat_declarations.Resolve(actual_project,sr::NativeRepeatRole::Primary).declared;
  Send("PROJECT_REPEAT_CONTEXT "+std::to_string(actual_project+1)+" "+std::to_string(declared));
}
std::filesystem::path ActualAttemptOutputRoot()const{
  if(!active_||!metadata_)throw std::runtime_error("Native project output selection lacks an actual attempt catalog");
  std::optional<size_t> project;
  const auto member=metadata_->project_tests.find(active_->test.id);
  if(metadata_->projects.empty()){
    if(member!=metadata_->project_tests.end())throw std::runtime_error("Native anonymous output attempt has forged project membership");
  }else{
    if(member==metadata_->project_tests.end()||member->second>=metadata_->projects.size())throw std::runtime_error("Native output attempt lacks actual project membership");
    project=member->second;
    if((options_.directory_admission&&options_.directory_admission->project!=project)||(current_group_&&current_group_->project!=*project)||(current_project_&&current_project_->index!=*project))throw std::runtime_error("Native project output root differs from admitted native project");
  }
  return metadata_->project_output_roots.Select(project,OutputFallbackRoot());
}
