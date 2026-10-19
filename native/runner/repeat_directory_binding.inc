// SPDX-License-Identifier: MIT
// Included inside the original Runner coordinator. No native actor is created
// during this compact selection phase. The original RunState owns every budget.
static sr::NativeRepeatRole NativeRepeatRole(const std::string& role){
  if(role=="primary")return sr::NativeRepeatRole::Primary;
  if(role=="dependency")return sr::NativeRepeatRole::Dependency;
  if(role=="teardown")return sr::NativeRepeatRole::Teardown;
  throw std::runtime_error("Native repeat role has no selected original project");
}
std::vector<std::string> NativeRepeatRoles(const Metadata& value)const{
  if(value.projects.empty())return {"primary"};
  std::vector<sr::ProjectTestDeclaration> declarations;for(const auto& test:value.tests)declarations.push_back({test.id,value.project_tests.at(test.id)});
  auto selection=options_.selection;selection.shard_current=selection.shard_total=1;
  return sr::SelectProjectTests(sr::ProjectGraph(value.projects),declarations,selection,options_.projects,options_.no_dependencies,value.focused,sr::ProjectSuiteFocus(value.projects.size(),value.suites,value.suite_modifiers)).roles;
}
void RecordNativeRepeatConfiguration(const Metadata& value){
  if(parallel_actor_||report_.repeat_configuration||(!options_.repeat_each&&!value.repeat_declarations.Explicit()))return;
  if(options_.directory_admission&&options_.directory_admission->collection)return;
  sr::RepeatConfigurationReport record;record.root=value.repeat_declarations.RootDeclaration();record.primary_override=options_.repeat_each;record.project_declarations=value.repeat_declarations.ProjectDeclarations();
  if(options_.directory_admission){
    const auto& a=*options_.directory_admission;
    if(a.repeat_selection){
      if(!a.repeat_slice)throw std::runtime_error("Native repeat report lost actual admitted slice");
      const auto& unit=a.repeat_selection->Catalog().Units().at(a.repeat_selection->Slices().at(*a.repeat_slice).unit);
      const auto counts=value.repeat_declarations.Resolve(a.project,unit.role,options_.repeat_each);
      record.resolved.push_back({a.project?*a.project+1:0,unit.role,counts});
      record.ValidateAdmission(a.project?*a.project+1:0,value.projects.size(),unit.role,unit.counts);
    }else {
      // A non-repeated directory source has one actual project admission.
      if(!a.project_role)throw std::runtime_error("Native repeat configuration lost outer admitted project role");
      record.resolved.push_back({a.project?*a.project+1:0,*a.project_role,value.repeat_declarations.Resolve(a.project,*a.project_role,options_.repeat_each)});
    }
  }else {
    const auto roles=NativeRepeatRoles(value);
    for(size_t project=0;project<roles.size();++project)if(roles[project]!="notSelected"){
      std::optional<size_t> local;if(!value.projects.empty())local=project;const auto role=NativeRepeatRole(roles[project]);
      record.resolved.push_back({local?*local+1:0,role,value.repeat_declarations.Resolve(local,role,options_.repeat_each)});
    }
  }
  record.Validate();Charge(record.MetadataBytes());report_.repeat_configuration=std::move(record);
}
bool NeedsNativeRepeatCoordinator(const Metadata& value)const{
  if(options_.directory_admission||parallel_actor_)return false;
  if(!options_.repeat_each&&!value.repeat_declarations.Explicit())return false;
  const auto roles=NativeRepeatRoles(value);
  for(size_t project=0;project<roles.size();++project)if(roles[project]!="notSelected"){
    std::optional<size_t> local;if(!value.projects.empty())local=project;
    if(value.repeat_declarations.Resolve(local,NativeRepeatRole(roles[project]),options_.repeat_each).executions>1)return true;
  }
  return false;
}
static std::optional<size_t> NativeLocalProject(const sr::MultiFileCatalog& catalog,size_t project,size_t file){
  const auto& original=catalog.Files().at(file).catalog;
  if(project==catalog.AnonymousProject()){if(!original.projects.empty())throw std::logic_error("Native anonymous repeat lost its actual file");return std::nullopt;}
  const auto& name=catalog.Graph().Declarations().at(project).name;
  for(size_t local=0;local<original.projects.size();++local)if(original.projects[local].name==name)return local;
  throw std::logic_error("Native repeat global project has no original local owner");
}
bool DirectoryNeedsNativeRepeats(const sr::MultiFileCatalog& catalog,const sr::MultiFileSelection& eligible){
  for(size_t project=0;project<eligible.project_tests.size();++project)if(eligible.projects.selected[project])
    for(size_t file=0;file<catalog.Files().size();++file){
      ThrowIfStopped();const auto& original=catalog.Files()[file].catalog;
      bool present=project==catalog.AnonymousProject()?original.projects.empty():std::any_of(original.projects.begin(),original.projects.end(),[&](const auto& p){return p.name==catalog.Graph().Declarations()[project].name;});
      if(present&&original.repeat_declarations.Resolve(NativeLocalProject(catalog,project,file),NativeRepeatRole(eligible.roles[project]),options_.repeat_each).executions>1)return true;
    }
  return false;
}
void SelectNativeDirectoryRepeats(sr::DirectoryReport& facts,const sr::MultiFileCatalog& catalog,const sr::MultiFileSelection& eligible){
  std::vector<sr::NativeRepeatUnit> units;std::vector<std::vector<size_t>> grouping;
  std::vector<std::unique_ptr<sr::NativeTestGroupTree>> groups;groups.reserve(catalog.Files().size());
  std::vector<std::unique_ptr<sr::NativeRepeatProjectionIndex<sr::TestListingCatalog::Test>>> projections;projections.reserve(catalog.Files().size());
  for(const auto& file:catalog.Files()){
    std::vector<uint64_t> ids;for(const auto& test:file.catalog.tests)ids.push_back(test.id);
    groups.push_back(std::make_unique<sr::NativeTestGroupTree>(std::move(ids),file.catalog.suites,file.catalog.members,file.catalog.suite_modes));
    projections.push_back(std::make_unique<sr::NativeRepeatProjectionIndex<sr::TestListingCatalog::Test>>(file.catalog.tests,[this]{ThrowIfStopped();}));
  }
  for(size_t project=0;project<eligible.project_tests.size();++project)if(eligible.projects.selected[project])
    for(size_t file=0;file<catalog.Files().size();++file){
      ThrowIfStopped();const auto& original=catalog.Files()[file];
      const bool present=project==catalog.AnonymousProject()?original.catalog.projects.empty():std::any_of(original.catalog.projects.begin(),original.catalog.projects.end(),[&](const auto& p){return p.name==catalog.Graph().Declarations()[project].name;});
      if(!present)continue;
      const auto local=NativeLocalProject(catalog,project,file);const auto ids=catalog.LocalTests(eligible,project,file);
      const auto role=NativeRepeatRole(eligible.roles[project]);
      const auto counts=original.catalog.repeat_declarations.Resolve(local,role,options_.repeat_each);
      bool parallel=original.catalog.fully_parallel.value_or(false);if(local){const auto mode=original.catalog.project_parallel.find(*local);if(mode!=original.catalog.project_parallel.end())parallel=mode->second;}
      const auto resolved_groups=groups[file]->Resolve(ids,parallel);auto projection=projections[file]->Project(resolved_groups,ids);
      bool declared_empty=false;if(local)declared_empty=std::none_of(original.catalog.project_tests.begin(),original.catalog.project_tests.end(),[&](const auto& member){return member.second==*local;});
      units.push_back({original.file.id,project+1,role,counts,std::move(projection.declarations),declared_empty,sizeof(sr::DirectoryAdmission)});grouping.push_back(std::move(projection.group_sizes));
    }
  const auto repeat_catalog=std::make_shared<const sr::NativeRepeatCatalog>(std::move(units),sr::DirectoryReport::kSegments,4096,0);
  uint64_t containers=repeat_catalog->OwnedMetadataBytes();for(const auto& unit:repeat_catalog->Units())containers=sr::repeat_detail::Add(containers,sr::repeat_detail::Add(sizeof(std::vector<sr::NativeRepeatTest>),sr::repeat_detail::Multiply(unit.declarations->capacity(),sizeof(sr::NativeRepeatTest))));
  Charge(static_cast<size_t>(containers));
  const sr::NativeRepeatSelectionLimits limits{sr::DirectoryReport::kSegments,128,67108864};
  auto& usage=shared_->repeat_planning_usage;
  if(usage.slices||usage.source_reports)throw std::runtime_error("Native repeat global selection was already accepted");
  usage.metadata_bytes=shared_->metadata_charge;
  auto primary=sr::SelectNativeRepeatPrimary(*repeat_catalog,grouping,options_.selection.shard_current,options_.selection.shard_total,limits,usage,[this]{ThrowIfStopped();});
  if(!primary.unsharded_logical_tests&&!options_.pass_with_no_tests)throw std::runtime_error("No tests found");
  std::vector<bool> roots(eligible.projects.selected.size());
  for(const auto& selected:primary.slices)roots.at(repeat_catalog->Units().at(selected.unit).project-1)=true;
  if(!primary.unsharded_logical_tests&&options_.pass_with_no_tests)for(size_t p=0;p<roots.size();++p)roots[p]=eligible.roles[p]=="primary"&&eligible.projects.selected[p];
  facts.selection={{catalog.Graph().Closure(roots,options_.no_dependencies),options_.no_dependencies},std::vector<std::vector<uint64_t>>(roots.size()),std::vector<std::string>(roots.size(),"notSelected")};
  std::set<uint64_t> selected_projects;
  for(size_t p=0;p<roots.size();++p)if(facts.selection.projects.selected[p]){facts.selection.roles[p]=roots[p]?"primary":eligible.roles[p];selected_projects.insert(p+1);}
  sr::AppendNativeRepeatSupport(*repeat_catalog,selected_projects,primary.slices,limits,usage,[this]{ThrowIfStopped();});
  auto accepted_selection=std::make_shared<const sr::NativeRepeatSelection>(repeat_catalog,std::move(primary.slices),limits,usage);
  uint64_t retained=sizeof(sr::NativeRepeatSelection)+accepted_selection->Slices().capacity()*sizeof(sr::NativeSelectedRepeatSlice);std::set<const std::vector<uint64_t>*> charged;
  for(const auto& slice:accepted_selection->Slices())if(charged.insert(slice.local_ids.get()).second)retained=sr::repeat_detail::Add(retained,sizeof(std::vector<uint64_t>)+slice.local_ids->capacity()*sizeof(uint64_t));
  // Planning reserves projected future attempt/error space once for the entire
  // run. Charge only actually retained container storage here; genuine later
  // reports consume the existing actual metadata ledger through Charge.
  Charge(static_cast<size_t>(retained));
  for(const auto& test:catalog.Tests())for(const auto& selected:accepted_selection->Slices()){
    const auto& unit=repeat_catalog->Units().at(selected.unit);if(unit.file!=catalog.Files().at(test.file).file.id||unit.project!=test.project+1)continue;
    if(std::find(selected.local_ids->begin(),selected.local_ids->end(),test.local_id)!=selected.local_ids->end()){facts.selection.project_tests[test.project].push_back(test.selection_id);break;}
  }
  auto accepted_groups=std::make_shared<const sr::NativeRepeatGroupPartition>(accepted_selection,facts.catalog,limits,usage,[this]{ThrowIfStopped();});
  Charge(static_cast<size_t>(accepted_groups->OwnedMetadataBytes()));
  // Publish the immutable logical/group selection only after every bounded
  // construction and retained-storage charge succeeds. Failure before this
  // point has no selected actor/source receipts, and remains unresolved.
  facts.repeat_selection=std::move(accepted_selection);facts.repeat_groups=std::move(accepted_groups);
  facts.selection_resolved=true;facts.empty_shard=primary.unsharded_logical_tests&&std::none_of(roots.begin(),roots.end(),[](bool value){return value;});
}
