// SPDX-License-Identifier: MIT
// Included inside NetworkControls. Native pause/snapshot ownership stays here.
  void MatchResult(Dict config,Done done){
      if(!config->HasKey("callbackScope")||config->GetType("routeId")!=VTYPE_STRING||config->GetType("registrationId")!=VTYPE_STRING||config->GetType("callbackToken")!=VTYPE_STRING){done(false,"Invalid native URL matcher result identity","");return;}
      uint64_t token=0;const auto text=config->GetString("callbackToken").ToString();if(text.size()>20||config->GetString("routeId").length()>128||config->GetString("registrationId").length()>128){done(false,"Native matcher identity exceeds its bounds","");return;}const auto parsed=std::from_chars(text.data(),text.data()+text.size(),token);
      const bool invalid=config->GetType("invalid")==VTYPE_BOOL&&config->GetBool("invalid");
      if(!token||text.size()>20||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||(invalid?config->HasKey("matched"):config->GetType("matched")!=VTYPE_BOOL)){done(false,"Invalid native URL matcher result","");return;}
      NativeRouteMatcherSelections::Ticket ticket;ticket.id=config->GetString("routeId");ticket.registration=config->GetString("registrationId");ticket.scope=callback_scope_;ticket.token=token;
      bool skipped=false;const bool accepted=matchers_->Result(ticket,invalid?std::optional<bool>{}:std::optional<bool>{config->GetBool("matched")},now_(),&skipped);
      done(accepted,accepted?"":"Native URL matcher result is stale, expired or lacks its actual callback receipt",accepted?(skipped?"{\"skipped\":true}":"{}"):"");return;
  }
  void EmitMatcher(const NativeRouteMatcherSelections::Ticket&ticket,Dict actual){
    if(disposed_)return;
    if(sequence_>=9007199254740991ULL){failure_("Native network publication sequence exhausted");return;}
    auto event=CefDictionaryValue::Create();event->SetString("event","routematch");event->SetDouble("sequence",double(++sequence_));
    auto matcher=CefDictionaryValue::Create();matcher->SetString("id",ticket.id);matcher->SetString("registrationId",ticket.registration);matcher->SetString("url",ticket.url);matcher->SetString("callbackScope",std::to_string(ticket.scope));matcher->SetString("callbackToken",std::to_string(ticket.token));event->SetDictionary("matcher",matcher);
    if(auto worker=actual->GetDictionary("suitecutWorkerInfo"))event->SetDictionary("workerInfo",worker->Copy(false));
    event->SetBool("contextWorker",actual->GetBool("suitecutContextWorker"));
    emit_(event);wake_();
  }
  void PauseReady(Dict data,const std::string&session,NativeRouteRegistry::Plan plan,double deadline){
    if(disposed_||authentication_->failed())return;
    auto candidate=routes_.Peek(plan);
    if(!candidate||!candidate->host_matcher){auto invocation=routes_.Next(plan);AdmitPause(data,session,std::move(plan),deadline,std::move(invocation));return;}
    const auto bytes=NativeNetworkPreflights::RetainedBytes(data);
    if(!bytes||paused_.size()+rejected_.size()+matchers_->pending()>=1024){RejectOversizedPause(data,session,"Native URL matcher pause capacity exceeded");return;}
    auto actual=data->Copy(false);const auto request=actual->GetDictionary("request");if(!request)return;
    const bool accepted=matchers_->Select(std::move(plan),request->GetString("url"),deadline,now_(),*bytes,
      [this,actual,session,deadline](auto invocation,auto plan,const std::string&error,NativeRouteMatcherSelections::Failure failure){
        if(disposed_)return;
        if(failure==NativeRouteMatcherSelections::Failure::OwnerClosed)return;
        if(!error.empty()){if(auto parent=RouteRequest(actual->GetString("networkId"),session)){parent->route_selection_complete=true;parent->route_selection_failed=true;}ResolvePreflights();RejectOversizedPause(actual,session,error,failure!=NativeRouteMatcherSelections::Failure::InvalidResult);return;}
        AdmitPause(actual,session,std::move(plan),deadline,std::move(invocation));
      },[this,actual](const auto&ticket){EmitMatcher(ticket,actual);},session);
    if(!accepted)RejectOversizedPause(actual,session,"Native URL matcher selection metadata capacity exceeded");
    wake_();
  }
  struct MatcherOperation {std::string ticket,route;int operation=0;uint64_t generation=0;};
  std::map<std::string,std::shared_ptr<MatcherOperation>>matcher_operations_;
  void CancelMatcherOperation(int operation){
    const auto found=std::find_if(matcher_operations_.begin(),matcher_operations_.end(),[&](const auto&entry){return entry.second->operation==operation;});if(found==matcher_operations_.end())return;
    auto actual=found->second;matcher_operations_.erase(found);matchers_->Cancel(actual->ticket);
    const auto pause=paused_.find(actual->route);if(pause==paused_.end())return;pause->second.matching=false;
    auto params=CefDictionaryValue::Create();params->SetString("requestId",pause->second.fetch_id);params->SetString("errorReason","Aborted");
    FinishPause(actual->route,"Fetch.failRequest",params,[](bool,const std::string&,const std::string&){});
  }
  void SelectFallback(const std::string&id,Done done,int operation,double original_deadline,uint64_t generation){
    auto found=paused_.find(id);if(found==paused_.end()){done(false,"Native fallback pause is no longer available","");return;}
    auto&pause=found->second;
    const double deadline=original_deadline>0?std::min(original_deadline,pause.deadline):pause.deadline;
    done=[this,deadline,reply=std::move(done)](bool ok,const std::string&error,const std::string&json){if(ok&&now_()>=deadline)reply(false,"Native fallback exceeded its original operation deadline","");else reply(ok,error,json);};
    auto candidate=routes_.Peek(pause.plan);
    if(!candidate||!candidate->host_matcher){auto invocation=routes_.Next(pause.plan);FallbackReady(id,std::move(invocation),std::move(done),deadline,operation,generation);return;}
    if(operation<0||(operation>0&&!generation)||matcher_operations_.contains(id)||matcher_operations_.size()>=128){done(false,"Native host matcher fallback requires its actual bounded operation","");return;}
    auto actual=std::make_shared<MatcherOperation>();actual->route=id;actual->operation=operation;actual->generation=generation;
    matcher_operations_.emplace(id,actual);pause.matching=true;
    auto projection=CefDictionaryValue::Create();
    if(auto worker=pause.request.info->GetDictionary("workerInfo"))projection->SetDictionary("suitecutWorkerInfo",worker->Copy(false));
    projection->SetBool("suitecutContextWorker",pause.request.info->GetBool("contextWorker"));
    const auto retained=NativeNetworkPreflights::RetainedBytes(projection);if(!retained){matcher_operations_.erase(id);pause.matching=false;done(false,"Native matcher worker projection exceeds its metadata bound","");return;}
    const bool accepted=matchers_->Select(pause.plan,pause.original_url,deadline,now_(),*retained+512,
      [this,actual,done,deadline](auto invocation,auto plan,const std::string&error,NativeRouteMatcherSelections::Failure failure){
        const auto current=matcher_operations_.find(actual->route);if(current==matcher_operations_.end()||current->second!=actual)return;
        matcher_operations_.erase(current);auto found=paused_.find(actual->route);
        if(found==paused_.end()){done(false,"Native fallback pause is no longer available","");return;}
        auto&pause=found->second;pause.matching=false;pause.plan=std::move(plan);
        if(failure==NativeRouteMatcherSelections::Failure::OwnerClosed){done(false,error,"");return;}
        if(!error.empty()){
          auto params=CefDictionaryValue::Create();params->SetString("requestId",pause.fetch_id);params->SetString("errorReason",failure==NativeRouteMatcherSelections::Failure::Deadline?"TimedOut":"Failed");
          FinishPause(actual->route,"Fetch.failRequest",params,[done,error](bool ok,const std::string&reason,const std::string&){done(false,ok?error:error+"; actual rejection failed: "+reason,"");});return;
        }
        FallbackReady(actual->route,std::move(invocation),done,deadline,actual->operation,actual->generation);
      },[this,actual,projection](const auto&ticket){actual->ticket=ticket.id;EmitMatcher(ticket,projection);},pause.session);
    if(!accepted){matcher_operations_.erase(id);pause.matching=false;done(false,"Native fallback matcher capacity exceeded; request remains owned for retry","");}
    wake_();
  }
  void FallbackReady(const std::string&id,std::optional<NativeRouteRegistry::Invocation>invocation,Done done,double deadline=0,int operation=0,uint64_t generation=0){
    const auto found=paused_.find(id);if(found==paused_.end()){done(false,"Native fallback pause is no longer available","");return;}auto&pause=found->second;
        if(invocation) {
          const auto next_id=std::to_string(++route_serial_);auto next_pause=std::move(pause);GlobalRouteCallbackLeases().Revoke(callback_scope_,id);CancelRouteFetches(id);paused_.erase(found);
          upload_bytes_-=next_pause.body.size();next_pause.body.clear();next_pause.replay_policy.reset();next_pause.replay_done={};next_pause.replay_deadline=0;next_pause.replay_operation=0;next_pause.replay_generation=0;next_pause.fetch_upload_deadline=0;
          if(har_replays_.contains(invocation->registration)){paused_.emplace(next_id,std::move(next_pause));ReplayPaused(next_id,invocation->registration,std::move(done),deadline,operation,generation);wake_();return;}
          if(!next_pause.public_prepared){PrepareBody(next_pause.request.info,next_pause.deadline);next_pause.public_prepared=true;const auto metadata=(next_pause.fetch_body?next_pause.fetch_body->capacity():0)+next_pause.plan.bytes()+next_pause.original_url.capacity()+Json(next_pause.request.info).size()+Json(next_pause.overrides).size()+(next_pause.har_info?Json(next_pause.har_info).size():0);if(metadata>16777216-(pause_metadata_bytes_-next_pause.metadata_bytes)){pause_metadata_bytes_-=next_pause.metadata_bytes;auto rejected=CefDictionaryValue::Create();rejected->SetString("requestId",next_pause.fetch_id);RejectOversizedPause(rejected,next_pause.session,"Native HAR fallback request metadata budget exceeded",true,std::move(next_pause.har_work));done(false,"Native HAR fallback request metadata budget exceeded","");return;}pause_metadata_bytes_=pause_metadata_bytes_-next_pause.metadata_bytes+metadata;next_pause.metadata_bytes=metadata;}
          auto event=RouteEvent(next_id,next_pause,*invocation);
          if(!event || Json(event).size()>1048576){if(event){const auto owner=event->GetDictionary("route");uint64_t token=0;const auto nonce=owner->GetString("callbackToken").ToString();const auto parsed=std::from_chars(nonce.data(),nonce.data()+nonce.size(),token);if(parsed.ec==std::errc{} && parsed.ptr==nonce.data()+nonce.size())GlobalRouteCallbackLeases().Finish(token,callback_scope_,next_id);}pause_metadata_bytes_-=next_pause.metadata_bytes;auto rejected=CefDictionaryValue::Create();rejected->SetString("requestId",next_pause.fetch_id);RejectOversizedPause(rejected,next_pause.session,"Native fallback request metadata exceeds 1 MiB",true,std::move(next_pause.har_work));done(false,"Native fallback request metadata exceeds 1 MiB","");return;}
          paused_.emplace(next_id,std::move(next_pause));Publish(event);wake_();done(true,"","{}");return;
        }

    auto params=pause.overrides->Copy(false);params->SetString("requestId",pause.fetch_id);FinishPause(id,"Fetch.continueRequest",params,std::move(done),deadline);
  }
  void FinishPause(const std::string&id,const std::string&method,Dict params,Done done,double deadline=0){
    const auto found=paused_.find(id);if(found==paused_.end()){done(false,"Native route is already handled or unavailable","");return;}auto&pause=found->second;
    const auto fetch_id=pause.fetch_id,session=pause.session;auto work=pause.har_work;auto policy=pause.replay_policy;const double actual_deadline=deadline>0?std::min(deadline,pause.deadline):pause.deadline;GlobalRouteCallbackLeases().Revoke(callback_scope_,id);upload_bytes_-=pause.body.size();pause_metadata_bytes_-=pause.metadata_bytes;CancelRouteFetches(id);paused_.erase(found);
    SendControl(session,method,params,[this,fetch_id,session,work,policy,actual_deadline,done=std::move(done)](bool ok,const std::string& error)mutable{
      policy.reset();
      if(work&&ok)work->TerminalACK();
      if(work&&!ok)work->Failed(error);
      if(!ok && !disposed_) {
        if(work&&!session.empty()&&!sessions_.contains(session))work->ActualChannelClosed();
        else{
          auto abort=CefDictionaryValue::Create();abort->SetString("requestId",fetch_id);abort->SetString("errorReason","Failed");
          if(!work)send_(session,"Fetch.failRequest",abort,[](bool,const std::string&){});
          else SendControl(session,"Fetch.failRequest",abort,[this,work](bool clean,const std::string&reason){if(clean)work->TerminalACK();else{work->Failed(reason);if(!disposed_)failure_("Actual HAR Fetch cleanup rejected: "+reason);}},now_()+1000);
        }
      }
      if(ok&&now_()>=actual_deadline)done(false,"Native route exceeded its original operation deadline","");else done(ok,ok?"":error,"{}");
      if(!disposed_ && routes_.empty() && !matchers_->pending() && paused_.empty())ConfigureAll([this](bool success,const std::string& reason,const std::string&){if(!success && !disposed_)failure_("Could not disable exhausted native routes: "+reason);});
    },actual_deadline);
  }
