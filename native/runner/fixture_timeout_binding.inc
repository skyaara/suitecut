// SPDX-License-Identifier: MIT
// Included in Runner's private scope. All timestamps/owners originate here;
// host packets carry only exact ACK requests and real fixture receipts.
  double FixtureNow()const{const auto current=fixture_event_time_.Current();return current?*current:sr::DedicatedRealMilliseconds();}
  template<class Callback> decltype(auto) FixtureTransition(Callback&& callback){const auto now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);return callback(now);}
  void TickHookWorker(){FixtureTransition([&](double now){hook_worker_->Tick(now);});}
  const Metadata& FixtureClockMetadata()const{if(metadata_)return *metadata_;if(catalog_)return *catalog_;throw std::runtime_error("Native fixture clock lacks actual pinned catalog");}
  uint64_t FixtureDefaultTimeout()const{
    const auto& metadata=FixtureClockMetadata();
    const auto project=current_group_?std::optional<size_t>(current_group_->project):current_project_?std::optional<size_t>(current_project_->index):options_.directory_admission?options_.directory_admission->project:std::nullopt;
    if(project&&*project>=metadata.projects.size())throw std::runtime_error("Native fixture default project admission rejected");
    return metadata.default_timeouts.Default(project);
  }
  static std::string FixtureControlWire(const std::string& text){
    static constexpr char hex[]="0123456789abcdef";std::string wire;wire.reserve(text.size()*4);
    for(const auto c:text){const auto byte=static_cast<unsigned char>(c);if(byte>=128)throw std::invalid_argument("Native fixture control text must be fixed ASCII");wire+=hex[byte>>4];wire+=hex[byte&15];wire+="00";}return wire.empty()?"-":wire;
  }
  sr::FixtureClockOwner FixtureSlotOwner(uint64_t token,uint64_t scope,uint64_t generation)const{
    const auto found=fixture_slot_owners_.find(token);
    if(found==fixture_slot_owners_.end()||found->second.scope!=scope||found->second.generation!=generation||
        found->second.worker!=worker_index_||!host_||found->second.pid!=host_->Pid())
      throw std::runtime_error("Native fixture clock receipt has no exact actual lease");
    return found->second;
  }
  std::shared_ptr<sr::FixtureChargedClock> FixtureCharge(uint64_t scope,uint64_t generation)const{
    return hook_worker_?hook_worker_->FixtureChargedClockFor(scope,generation):owners_?owners_->ActiveChargedClock():nullptr;
  }
  std::optional<sr::FixtureSourceSetupClock> FixtureSetupAdmission(uint64_t token,uint64_t scope,uint64_t generation,double now){
    if(!fixture_runtime_)return std::nullopt;
    return sr::InspectFixtureSourceSetupClock(*fixture_runtime_,FixtureSlotOwner(token,scope,generation),now,suite_deadline_.OriginalDeadline());
  }
  std::optional<double> FixtureSlotDeadline(uint64_t token,uint64_t scope,uint64_t generation,double now){
    if(!fixture_runtime_)return std::nullopt;
    return fixture_runtime_->SlotDeadline(FixtureSlotOwner(token,scope,generation),now,suite_deadline_.OriginalDeadline());
  }
  std::optional<double> FixtureRetirementDeadline(uint64_t token){
    if(!fixture_runtime_)return std::nullopt;
    const auto found=fixture_slot_owners_.find(token);if(found==fixture_slot_owners_.end())return std::nullopt;
    const auto* record=fixture_runtime_->Current(found->second);
    if(!record||!record->independent||record->phase==sr::FixtureClockPhase::Ready)return std::nullopt;
    if(const auto drain=fixture_runtime_->DrainDeadline(found->second))return drain;
    const auto now=FixtureNow();return fixture_runtime_->SlotDeadline(found->second,now,std::nullopt).value_or(now+3153600000000.0);
  }
  void RefreshFixtureBudget(double now){
    if(owners_)if(const auto deadline=owners_->ActiveDeadline(now))deadline_=*deadline;
    if(hook_worker_){hook_worker_->RefreshFixtureBudget(now);hook_group_deadline_=hook_worker_->ActiveDeadline().value_or(hook_group_deadline_);}
  }
  void TickFixtureClocks(double now){
    sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
    if(!fixture_runtime_)return;
    if(StopRequested()){
      const auto cap=StopTime().value_or(now)+1000;
      if(now>=cap&&fixture_runtime_->Physical())KillHost();
    }
    fixture_runtime_->Tick(now);RefreshFixtureBudget(now);
  }
  void RecordAttemptWall(){if(!active_||report_.attempts.empty()||report_.attempts.back().generation!=active_->generation)throw std::runtime_error("Native attempt wall observation lacks current report owner");report_.attempts.back().wall_duration_ms=std::max(0.0,sr::DedicatedRealMilliseconds()-attempt_started_);}
  void ApplyFixtureMutationOutcome(){if(!fixture_mutation_failed_)return;finish_ok_=false;if(actual_error_.empty()&&!timeout_&&!interrupted_)actual_error_=fixture_mutation_error_;}
  double AttemptChargedDuration(double now)const{
    // BeforeAll can fail before any associated Test timing ledger is admitted.
    // Its independent phase elapsed is not this uninvoked test's duration;
    // RecordAttemptWall separately retains the genuine elapsed observation.
    return sr::FixtureResultDuration(attempt_timing_.get(),now);
  }
  void FixtureCommandClock(const std::vector<std::string>& fields,double now){
    if(!fixture_runtime_||fields.size()<5)throw std::runtime_error("Native fixture command lacks clock controller");
    const auto token=Integer(fields[1]),scope=Integer(fields[2]),generation=Integer(fields[3]);
    const auto& metadata=FixtureClockMetadata();
    if(fields[0]=="SETUP"){
      const auto definition=std::find_if(metadata.fixtures.begin(),metadata.fixtures.end(),[&](const auto& value){return value.name==fields[4];});
      if(definition==metadata.fixtures.end()||fixture_slot_owners_.contains(token))throw std::runtime_error("Native fixture clock has no selected declaration");
      uint64_t declaration=static_cast<uint64_t>(definition-metadata.fixtures.begin())+1;
      if(metadata.fixture_catalog){const auto& versions=metadata.fixture_catalog->Versions();const auto version=std::find_if(versions.begin(),versions.end(),[&](const auto& value){return value.active&&value.key==definition->name;});if(version==versions.end())throw std::runtime_error("Native fixture clock has no actual catalog version");declaration=version->id;}
      const sr::FixtureClockOwner owner{token,scope,generation,declaration,worker_index_,host_->Pid(),definition->scope==sr::Scope::Worker};
      fixture_slot_owners_.emplace(token,owner);
      try{if(!fixture_runtime_->Begin(owner,definition->timeout,FixtureDefaultTimeout(),FixtureCharge(scope,generation),now))throw std::runtime_error("Native fixture timeout slot admission rejected");}
      catch(...){fixture_slot_owners_.erase(token);throw;}
      const auto* record=fixture_runtime_->Current(owner);
      if(!host_->Send("FIXTURE_CLOCK "+fields[1]+" "+fields[2]+" "+fields[3]+" setup "+std::to_string(record->timeout)))throw std::runtime_error("Native fixture setup clock sidecar control failed");
    }else{
      const auto owner=FixtureSlotOwner(token,scope,generation);
      if(fields[0]=="TEARDOWN"){
        const auto charged=FixtureCharge(scope,generation);
        if(!fixture_runtime_->Teardown(owner,charged?charged->Timeout():FixtureDefaultTimeout(),charged,now))throw std::runtime_error("Native fixture teardown slot transition rejected");
        if(!host_->Send("FIXTURE_CLOCK "+fields[1]+" "+fields[2]+" "+fields[3]+" teardown "+std::to_string(fixture_runtime_->Current(owner)->timeout)))throw std::runtime_error("Native fixture teardown clock sidecar control failed");
      }else{
        (void)fixture_runtime_->Cancel(owner,now,now+1000);
        if(!host_->Send("FIXTURE_ABORT "+fields[1]+" "+fields[2]+" "+fields[3]+" "+FixtureControlWire("Native fixture callback canceled or original deadline expired")))throw std::runtime_error("Native fixture abort sidecar control failed");
      }
    }
    RefreshFixtureBudget(now);
  }
  void ConfigureFixtureClocks(){
    if(fixture_runtime_&&fixture_runtime_->Physical())throw std::runtime_error("Native fresh fixture controller lacks actual prior callback retirement");
    fixture_slot_owners_.clear();
    fixture_runtime_=std::make_unique<sr::FixtureTimeoutRuntime>(sr::FixtureTimeoutRuntime::Callbacks{
      [this](const auto& record,double now){
        sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
        const auto& metadata=FixtureClockMetadata();
        const auto definition=std::find_if(metadata.fixtures.begin(),metadata.fixtures.end(),[&](const auto& value){
          if(metadata.fixture_catalog)return metadata.fixture_catalog->Get(record.owner.declaration).key==value.name;
          return static_cast<uint64_t>(&value-metadata.fixtures.data())+1==record.owner.declaration;
        });
        if(definition==metadata.fixtures.end())throw std::runtime_error("Native fixture expiry lost its declaration");
        const auto public_name=metadata.fixture_catalog?metadata.fixture_catalog->Get(record.owner.declaration).name:definition->name;
        const auto title=definition->title_json.value_or(Quote(public_name));
        auto error=std::string("\"Fixture \\\"")+title.substr(1,title.size()-2)+"\\\" timeout of "+std::to_string(record.timeout)+"ms exceeded during "+(record.slot==sr::FixtureClockPhase::Setup?"setup.":"teardown.");
        if(const auto excerpt=metadata.fixture_sources.Excerpt(record.owner.declaration)){const auto text=Quote(*excerpt);error+="\\n"+text.substr(1,text.size()-2);}
        error+='\"';if(!sr::JsonString(error,8192))throw std::runtime_error("Native fixture timeout diagnostic capacity exceeded");
        if(closing_worker_)Global(error);else{timeout_=true;if(actual_error_.empty())actual_error_=error;}
        if(!host_->Send("FIXTURE_ABORT "+std::to_string(record.owner.token)+" "+std::to_string(record.owner.scope)+" "+std::to_string(record.owner.generation)+" "+FixtureControlWire("Native fixture original slot deadline expired")))throw std::runtime_error("Native expired fixture signal control failed");
        if(hook_worker_){if(!hook_worker_->FixtureExpired(record.owner.token,record.owner.scope,record.owner.generation,error,now))throw std::runtime_error("Native expired fixture lost its actual hook lease");}
        else if(owners_&&!owners_->FixtureExpired(record.owner.token,record.owner.scope,record.owner.generation,error,now))throw std::runtime_error("Native expired fixture lost its actual flat lease");
      },[this](const auto&){Global(Quote("Native fixture original physical cancellation grace expired"));KillHost();}});
  }
  bool FixtureTimeoutPacket(const std::vector<std::string>& fields,double now){
    if(fields.empty()||fields[0]!="FIXTURE_TIMEOUT")return false;
    Size(fields,6);const auto request=Integer(fields[1]);const auto owner=FixtureSlotOwner(Integer(fields[2]),Integer(fields[3]),Integer(fields[4]));
    const auto timeout=Integer(fields[5],120000);
    const auto accepted=(!physical_kill_requested_)?fixture_runtime_->Amend(owner,request,timeout,now):std::nullopt;
    if(!accepted){fixture_mutation_failed_=true;if(fixture_mutation_error_.empty())fixture_mutation_error_=Quote("Native fixture timeout mutation rejected by actual owner, original clock or request fence");}
    RefreshFixtureBudget(now);
    const auto error=FixtureControlWire("Native fixture timeout mutation rejected by actual owner, original clock or request fence");
    Send("FIXTURE_TIMEOUT_RESULT "+fields[1]+" "+fields[2]+" "+fields[3]+" "+fields[4]+" "+(accepted?"1 "+std::to_string(accepted->timeout):"0 "+error));
    return true;
  }
  void RememberFlatFixtureError(uint64_t token,uint64_t scope,uint64_t generation,const std::string& error){
    const auto owner=FixtureSlotOwner(token,scope,generation);
    if(active_&&!finished_&&!closing_worker_&&scope==active_->test.id&&generation==active_->generation){
      if(actual_error_.empty()){actual_error_=error;originating_step_=0;}
      else{if(attempt_secondary_errors_.size()>=256)throw std::length_error("Native attempt secondary error capacity256 exceeded");Charge(error.size()+128);attempt_secondary_errors_.push_back({{{active_->test.id,active_->generation,worker_index_,host_->Pid()},scope,generation},error,0,true});}
    }else if(owner.worker_fixture&&closing_worker_)Global(error);
    else throw std::runtime_error("Native raw fixture failure lacks current actual attempt or worker retirement owner");
  }
  bool FixtureReadyClock(uint64_t token,uint64_t scope,uint64_t generation,double now){
    const auto accepted=fixture_runtime_->Ready(FixtureSlotOwner(token,scope,generation),now);
    RefreshFixtureBudget(now);return accepted;
  }
  void FixtureSettledClock(uint64_t token,uint64_t scope,uint64_t generation,double now){
    if(!fixture_runtime_->Settled(FixtureSlotOwner(token,scope,generation),now))throw std::runtime_error("Native raw fixture receipt has no actual clock owner");
    fixture_slot_owners_.erase(token);RefreshFixtureBudget(now);
  }
  void FixtureWorkerExited(double now){
    if(fixture_runtime_&&fixture_runtime_->Physical()){
      if(!host_||!host_->Exited()||!fixture_runtime_->ActualWorkerExited(worker_index_,host_->Pid(),now))throw std::runtime_error("Native fixture clock retirement lacks exact actual waitpid");
      fixture_slot_owners_.clear();
    }
  }
