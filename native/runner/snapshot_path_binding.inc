// SPDX-License-Identifier: MIT
// Included inside Runner. All facts come from the native-owned metadata catalog;
// no output directory, retry generation or author-computed identity is a root.
  bool SnapshotDeclaration(Metadata&value,const std::vector<std::string>&fields){
    if(fields.empty()||fields[0]!="SNAPSHOT_CONFIG")return false;
    Size(fields,4);const auto owner=Integer(fields[1],128);
    value.snapshot_paths.Declare(owner,value.projects.size(),fields[2],sr::DecodeHex(fields[3],32768));
    return true;
  }
  std::set<uint64_t> snapshot_contexts_;
  void SendSnapshotContext(uint64_t id){
    if(!metadata_||!metadata_->titles.contains(id)||!active_||active_->test.id!=id)
      throw std::runtime_error("Native snapshot plan lacks actual admitted test ownership");
    if(snapshot_contexts_.contains(id))return;
    const auto&value=*metadata_;
    const auto project=value.projects.empty()?std::nullopt:std::optional<size_t>(value.project_tests.at(id));
    const auto settings=value.snapshot_paths.Select(project,value.projects.size());
    const auto components=value.grep_components.find(id);
    if(components==value.grep_components.end()||components->second.empty())
      throw std::runtime_error("Native snapshot path lacks actual title component declarations");
    std::string wire;std::optional<std::string>directory;
    try{
        sr::NativeSnapshotFacts facts;facts.module=options_.suite;
        directory=sr::NativeSnapshotPaths::Directory(facts,settings).snapshot_directory;
        if(project)facts.project_name=value.projects.at(*project).name;
        for(const auto&part:components->second)
          facts.title_path.push_back(sr::NativeSnapshotPaths::NeedsTitle(settings)?sr::NativeSnapshotPaths::FilesystemText(part.title):std::string{});
        wire=sr::NativeSnapshotPaths::Serialize(sr::NativeSnapshotPaths::Bind(facts,settings,true));
        if(wire.size()>30000)throw std::length_error("Native snapshot token plan exceeds callback packet bound");
      }catch(const std::invalid_argument&){
        wire=sr::NativeSnapshotPaths::Unsupported("Native snapshot path is unavailable for this actual declaration or filesystem encoding",directory,true);
      }catch(const std::length_error&){
        wire=sr::NativeSnapshotPaths::Unsupported("Native snapshot path exceeds the native token or filesystem bound",directory,true);
      }
      Charge(wire.size());Send("TEST_SNAPSHOT_CONTEXT "+std::to_string(id)+" "+Hex(wire));
      snapshot_contexts_.insert(id);
  }
