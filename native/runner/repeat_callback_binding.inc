// SPDX-License-Identifier: MIT
// Included inside Runner. Legacy BODY/CALL ABI implicitly means repeat index0
// and original local public IDs; only explicit selected repeat actors send this
// marker and require a native-issued mapping before the original callback.
  std::set<uint64_t> repeat_contexts_;
  void BindNativeRepeatActor(const Metadata& value){
    if(!options_.directory_admission||!options_.directory_admission->repeat_selection)return;
    const auto& a=*options_.directory_admission;
    if(!a.repeat_slice)throw std::runtime_error("Native repeated actor lost exact selected slice");
    const auto& selection=*a.repeat_selection;const auto& slice=selection.Slices().at(*a.repeat_slice);const auto& unit=selection.Catalog().Units().at(slice.unit);
    const auto* ids=slice.local_ids.get();
    if(a.repeat_groups){
      if(!a.repeat_group||&a.repeat_groups->Selection()!=a.repeat_selection.get())throw std::runtime_error("Native repeated group lost exact selected partition");
      const auto& group=a.repeat_groups->Groups().at(*a.repeat_group);
      if(group.slice!=*a.repeat_slice||group.index!=*a.repeat_group)throw std::runtime_error("Native repeated group has foreign selected slice");
      ids=group.local_ids.get();
    }else if(a.repeat_group)throw std::runtime_error("Native repeated group lacks immutable partition");
    if(unit.file!=a.file.id||*ids!=a.tests||unit.counts!=value.repeat_declarations.Resolve(a.project,unit.role,options_.repeat_each))throw std::runtime_error("Native repeated actor lost original file/project/count admission");
    repeat_contexts_.clear();
    Send("REPEAT_ACTOR_CONTEXT "+std::to_string(unit.file)+" "+std::to_string(unit.project)+" "+std::to_string(a.project?*a.project+1:0)+" "+std::to_string(slice.index)+" "+std::to_string(unit.counts.declared)+" "+std::to_string(unit.counts.effective)+" "+std::to_string(unit.counts.executions));
  }
  void SendNativeRepeatTest(uint64_t id){
    if(!options_.directory_admission||!options_.directory_admission->repeat_selection)return;
    const auto& a=*options_.directory_admission;
    if(!active_||active_->test.id!=id||!metadata_||!a.repeat_slice)throw std::runtime_error("Native repeated callback has no actual selected attempt");
    if(repeat_contexts_.contains(id))return;
    const auto logical=a.repeat_selection->LogicalID(*a.repeat_slice,id);
    Send("TEST_REPEAT_CONTEXT "+std::to_string(id)+" "+Hex(logical));repeat_contexts_.insert(id);
  }
