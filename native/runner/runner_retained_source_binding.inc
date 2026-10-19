// SPDX-License-Identifier: MIT
// External review draft, included in Runner private scope. No Engine idle or
// worker-reuse eligibility is granted by registration/startup/shutdown labels.
  bool SourcesPhysical()const noexcept{return sources_&&sources_->Physical();}
  void SourceSetupClosed(std::span<const std::string> fields){
    if(sources_&&fields.size()>=4)sources_->SetupClosed(Integer(fields[1]),Integer(fields[2]),Integer(fields[3]));
  }
  void SourceSetup(const std::string& line,double now){
    if(!sources_||!metadata_||!metadata_->source_capability)return;
    const auto fields=Fields(line);if(fields.size()!=5&&fields.size()!=7)throw std::runtime_error("Native Source setup requires the original actual fixture packet");
    const auto test=fields.size()==7?Integer(fields[6]):Integer(fields[2]);
    // Hook-only beforeAll/afterAll acquisitions have genuine associated test0.
    // Do not substitute the runner's neighboring active attempt for that0.
    auto callback_scope=sr::RetainedSourceLeases::SetupScope::Test;
    if(fields.size()==7&&fields[5]!="test"){
      if(fields[5]=="beforeAll")callback_scope=sr::RetainedSourceLeases::SetupScope::BeforeAll;
      else if(fields[5]=="afterAll")callback_scope=sr::RetainedSourceLeases::SetupScope::AfterAll;
      else throw std::runtime_error("Native Source setup has no actual native hook scope");
      if(test)throw std::runtime_error("Native Source all-hook setup cannot infer neighboring test ownership");
    }else if(!test||!active_||test!=active_->test.id)throw std::runtime_error("Native Source setup differs from actual associated test");
    const auto definition=std::find_if(metadata_->fixtures.begin(),metadata_->fixtures.end(),[&](const auto& value){return value.name==fields[4];});
    if(definition==metadata_->fixtures.end())throw std::runtime_error("Native Source setup has no native selected fixture definition");
    uint64_t declaration=static_cast<uint64_t>(definition-metadata_->fixtures.begin())+1;
    auto origin=sr::FixtureOptionCatalog::Origin::Callback;
    if(metadata_->fixture_catalog){
      const auto& versions=metadata_->fixture_catalog->Versions();const auto version=std::find_if(versions.begin(),versions.end(),[&](const auto& value){return value.active&&value.key==fields[4];});
      if(version==versions.end()||!metadata_->fixture_values)throw std::runtime_error("Native Source setup has no actual catalog version/origin");
      declaration=version->id;origin=metadata_->fixture_values->Get(declaration).origin;
    }
    const auto slot=FixtureSetupAdmission(Integer(fields[1]),Integer(fields[2]),Integer(fields[3]),now);if(!slot)return;
    auto end=sr::FixtureSourceSetupDeadline(*slot);
    if(const auto original=suite_deadline_.OriginalDeadline())end=std::min(end,*original);
    // Optional child registration cannot create a new author budget or reject
    // the original fixture's existing bounded cleanup admission.
    if(now>=end)return;
    const sr::RunnerRetainedSources::Selection selection{worker_index_,declaration,Integer(fields[1]),Integer(fields[2]),test,Integer(fields[3]),fields[4],definition->scope,origin,end,callback_scope};
    if(const auto sidecar=sources_->Setup(selection,now))if(!host_||!host_->Send(*sidecar))throw std::runtime_error("Native Source setup sidecar lost actual host control");
  }
  void AppendSourceReceipt(){
    if(!sources_||source_reported_||!sources_->Receiver().Registration())return;
    const auto& receiver=sources_->Receiver();const auto& ticket=*receiver.Registration();
    if(!source_retired_||SourcesPhysical()||!receiver.ChannelDrained()||ticket.id>shared_->source_leases.Records().size())throw std::runtime_error("Native Source report lacks exact completed reservation");
    const auto& record=shared_->source_leases.Records()[ticket.id-1];
    if(record.ticket!=ticket||record.state!=sr::RetainedSourceLeases::State::Retired)throw std::runtime_error("Native Source report has stale physical retirement");
    sr::ReportRetainedSource row{ticket,record.executable,receiver.ProcessIdentity(),std::nullopt,receiver.ExitObserved(),receiver.ChannelDrained(),record.retired_batch};
    if(receiver.Boundary())row.shutdown=sr::ReportSourceShutdown{receiver.Boundary()->current_parent,receiver.Boundary()->planned_code};
    if(receiver.Work()){const auto& work=*receiver.Work();row.accepted_work=sr::ReportSourceAcceptedWork{work.challenge,work.cuts,work.observed,work.acknowledged,work.canceled,work.late_acknowledged,work.refusal,work.observed_at,work.acknowledged_at};if(const auto* iteration=sources_->PendingCallbackIterationOwner())row.accepted_work->callback_iteration=sr::ReportSourceCallbackIteration{iteration->callback,iteration->request,iteration->outer_step,iteration->iteration_step,iteration->assertion,iteration->invocation};}
    sr::ValidateRetainedSource(row);Charge(row.accepted_work?(row.accepted_work->callback_iteration?1024:768):384);report_.retained_sources.push_back(row);source_reported_=true;
  }
  sr::NativeTask<> RetireSources(){
    if(!sources_)co_return;
    if(source_retired_){if(SourcesPhysical()||!host_||!host_->Exited()||host_->Pid()!=source_host_.pid||worker_index_!=source_host_.worker||DescendantsPhysical()||!sources_->ChannelDrained())throw std::runtime_error("Native resumed Source cleanup lost its exact prior physical owner");AppendSourceReceipt();co_return;}
    // No registration can exist before actual binding/capability. A failed
    // bootstrap still waits original host/domain retirement before descriptors
    // are destroyed, without inventing a Source registration or exit receipt.
    if(!source_bound_){if(SourcesPhysical()||!host_||!host_->Exited()||DescendantsPhysical())throw std::runtime_error("Native unbound Source transport still has actual process owners");sources_.reset();co_return;}
    if(!source_retire_end_)source_retire_end_=sr::DedicatedRealMilliseconds()+6000;
    auto end=*source_retire_end_;if(shared_->emergency_time)end=std::min(end,*shared_->emergency_time+6000);
    for(;;){
      const auto now=sr::DedicatedRealMilliseconds();sources_->Drain(now);
      const sr::RetainedSourceLeases::HostRetirement receipt{source_host_,descendant_generation_,host_&&host_->Exited().has_value(),sources_->ChannelDrained(),!DescendantsPhysical()};
      if(sources_->RetireDrained(receipt,now)){source_retired_=true;break;}
      if(now>=end)throw std::runtime_error("Native Source physical channel/pidfd/domain retirement remains unconfirmed after fixed cleanup grace");
      co_await continuation_.Yield(now+5);
    }
    AppendSourceReceipt();
    if(sources_->DrainFailure()&&!source_cleanup_reported_){
      source_cleanup_reported_=true;artifact_cleanup_failed_=true;
      try{std::rethrow_exception(sources_->DrainFailure());}
      catch(const std::exception& error){Global(Quote(std::string("Actual Source cleanup transport failure: ")+error.what()));}
      catch(...){Global(Quote("Actual Source cleanup transport failure: nonstandard native exception"));}
    }
  }
