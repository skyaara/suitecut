// SPDX-License-Identifier: MIT
// Included after Source is complete. The UI thread owns these real Engine
// checks. CEF state/timers/listeners are resident state, not automation work.
bool Engine::AcceptedWorkReady(bool viewer)const{
  if(!locator_handler_leases_.empty()||closing_||operations_.Size()||!inspector_work_->ReceiptLaneReady()||
     !suitecut::NativeTaskBarrierReady(AcceptedSourceTicket(),viewer)||!authentication_failures_.empty()||
     authentication_.pending()||authentication_.physical()||geolocation_.pending()||
     network_policy_.pending()||network_headers_.pending()||
     websocket_routes_->Physical()||websocket_routes_->NeedsTick()||context_routes_->physical()||context_routes_->pending()||context_unroute_all_->pending()||
     context_har_->physical()||context_har_->pending()||har_files_->Physical()||har_recording_->Physical()||
     suitecut::GlobalRouteCallbackLeases().size()||suitecut::GlobalHARWorkLeases().physical()||
     !contexts_->AcceptedWorkReady()||!workers_->AcceptedWorkReady()||!utilities_->AcceptedWorkReady()||
     !http_->AcceptedWorkReady()||downloads_->NeedsTick()||recordings_->PhysicalCount()||context_video_->Physical()||
     traces_->PhysicalCount()||trace_preview_->PhysicalCount()||trace_sources_->PhysicalCount()||
     trace_background_.PhysicalCount()||trace_background_.Failures()||trace_physical_||trace_dom_physical_||
     screenshot_inputs_->NeedsTick()||screenshot_comparisons_->NeedsTick()||
     suitecut::NativeRequestBodyReads::Physical()||suitecut::NativePrivateResponseSession::Waiting()||
     suitecut::NativePrivateResponseSession::Physical()||suitecut::NativeResponseStreams::Physical())return false;
#if defined(OS_LINUX)
  if(streaming_->PhysicalCount())return false;
#endif
  for(const auto&[page,source]:sources_){(void)page;if(!source->AcceptedWorkReady(viewer))return false;}
  return true;
}
#if defined(__linux__)
void Engine::BindAcceptedWork(suitecut::runner::RetainedSourceEndpoint* endpoint){
  if(!endpoint||!endpoint->Enabled())return;
  if(native_work_endpoint_||!endpoint->PrepareAcceptedWork())throw std::runtime_error("Native Engine work endpoint could not bind actual startup");
  native_work_endpoint_=endpoint;
  native_work_wake_=std::make_unique<suitecut::AcceptedEngineWorkWake>(endpoint->WorkDescriptor(),[this,alive=alive_](auto token){
    return CefPostTask(TID_UI,new Task([this,alive,token=std::move(token)]{if(*alive)PumpAcceptedWork();},suitecut::AcceptedTaskWork::Kind::BarrierCoordinator));
  });
}
uint64_t Engine::AcceptedSourceTicket()const noexcept{return native_work_endpoint_?native_work_endpoint_->Ticket():0;}
double Engine::AcceptedWorkNow(){
  // The Runner's DedicatedRealMilliseconds uses this same native steady-clock
  // epoch. Browser Now() has a different process-relative origin and MUST NOT
  // be compared with the unchanged Runner-issued absolute deadline.
  return std::chrono::duration<double,std::milli>(Clock::now().time_since_epoch()).count();
}
void Engine::PostAcceptedWork(){
  if(native_work_posted_||closing_||!native_work_wake_)return;
  native_work_posted_=true;
  auto token=native_work_wake_->RetainPost();
  if(!CefPostDelayedTask(TID_UI,new Task([this,alive=alive_,token=std::move(token)]{if(*alive){native_work_posted_=false;PumpAcceptedWork();}},suitecut::AcceptedTaskWork::Kind::BarrierCoordinator),10)){
    native_work_posted_=false;throw std::runtime_error("CEF refused actual Engine work coordinator");
  }
}
void Engine::PumpAcceptedWork(){
  if(!native_work_endpoint_||closing_)return;
  using Wire=suitecut::runner::RetainedSourceWorkWire;
  if(native_work_wake_->Failed())throw std::runtime_error("Native Engine work descriptor watcher failed");
  // A bounded drain; socket readiness schedules another actual UI turn if more
  // real messages remain. No browser callback runs on the kernel watcher.
  for(size_t count=0;count<16;++count){
    auto message=native_work_endpoint_->ReceiveAcceptedWork();if(!message)break;
    switch(message->kind){
      case Wire::Kind::Install:
        if(native_work_installed_||native_work_)throw std::runtime_error("Native Engine work install replayed");
        native_work_installed_=true;
        if(!native_work_endpoint_->WriteAcceptedCapability())throw std::runtime_error("Native Engine capability publication failed");
        break;
      case Wire::Kind::Begin:
        if(!native_work_installed_||native_work_||message->challenge.nonce<=native_work_nonce_||message->challenge.borrow_generation<native_work_generation_||AcceptedWorkNow()>=message->challenge.deadline)throw std::runtime_error("Native Engine work challenge stale/expired");
        native_work_nonce_=message->challenge.nonce;
        native_work_.emplace();native_work_->challenge=std::move(message->challenge);
        break;
      case Wire::Kind::Observed:
        if(!native_work_||!native_work_->cuts||*native_work_->cuts!=message->cuts||native_work_->observed||native_work_->granted)throw std::runtime_error("Native Engine stdout observation foreign/replayed");
        native_work_->observed=true;
        break;
      case Wire::Kind::Resume:
        if(!native_work_||!native_work_->granted||message->resume.nonce!=native_work_->challenge.nonce||message->resume.generation<=native_work_->challenge.borrow_generation)throw std::runtime_error("Native Engine work resume has no exact transferred borrow");
        if(native_work_->resume_generation)throw std::runtime_error("Native Engine work resume replayed");
        native_work_->resume_generation=message->resume.generation;
        break;
      default:throw std::runtime_error("Native Engine work message direction invalid");
    }
  }
  if(!native_work_)return;
  auto& work=*native_work_;
  if(work.granted){
    // Recheck actual used native lanes before accepting a new borrow. Native
    // work triggered by resident state remains worker-owned, not physically0.
    if(!work.resume_generation)return;
    if(!AcceptedWorkReady()){PostAcceptedWork();return;}
    const auto generation=work.resume_generation;
    if(!native_work_endpoint_->WriteAcceptedResume({work.challenge.ticket,work.challenge.nonce,generation}))throw std::runtime_error("Native Engine resumed-borrow receipt failed");
    native_work_generation_=generation;native_work_.reset();return;
  }
  if(AcceptedWorkNow()>=work.challenge.deadline){
    // Expiration is a refusal. Keep all raw/file/callback owners and let the
    // original Runner cleanup own actual close/exit; never fabricate an ACK.
    if(!work.refused){work.refused=true;if(!native_work_endpoint_->WriteAcceptedUnsupported(work.challenge.nonce,1))throw std::runtime_error("Native Engine expiry refusal publication failed");if(work.command){const std::string result="{\"acceptedWork\":false,\"reason\":\"originalDeadline\"}";writer_.Reply(work.command,true,"",std::vector<uint8_t>(result.begin(),result.end()));}}
    return;
  }
  if(!work.command||work.refused)return; // Separate socket never substitutes for original stdin.
  const auto unsupported=writer_.PublicationAudit().Unattested();
  if(unsupported){
    work.refused=true;if(!native_work_endpoint_->WriteAcceptedUnsupported(work.challenge.nonce,unsupported<<8))throw std::runtime_error("Native Engine unattested-lane refusal publication failed");
    const std::string result="{\"acceptedWork\":false,\"reason\":\"unattestedProducer\"}";writer_.Reply(work.command,true,"",std::vector<uint8_t>(result.begin(),result.end()));return;
  }
  if(!AcceptedWorkReady()||!reader_work_->Through(work.reader)){PostAcceptedWork();return;}
  if(!work.cuts){
    if(!writer_.QueueWorkWatermark(reader_work_,work.reader,work.challenge.nonce))throw std::runtime_error("Native Engine original stdout cutoff could not queue");
    const auto identity=writer_.WorkWatermarkIdentity(work.challenge.nonce);if(!identity)throw std::runtime_error("Native Engine issued cutoff disappeared");
    work.cuts=Wire::Cuts{work.challenge.ticket,work.challenge.nonce,identity->first,identity->second};
    if(!native_work_endpoint_->WriteAcceptedCuts(Wire::Kind::Cut,*work.cuts))throw std::runtime_error("Native Engine cutoff publication failed");
  }
  // The actual OBSERVED datagram wakes the UI through the descriptor watcher.
  // Polling here adds latency and retains an unnecessary coordinator task.
  if(!work.observed)return;
  if(!writer_.WorkWatermarkWritten(work.challenge.nonce,work.cuts->reader,work.cuts->writer)){PostAcceptedWork();return;}
  // Same actual UI turn, after original stdout's full write and authenticated
  // original decoder observation. Logical registry erase alone never qualifies.
  if(!AcceptedWorkReady()){PostAcceptedWork();return;}
  if(!writer_.RetireWorkWatermark(work.challenge.nonce,work.cuts->reader,work.cuts->writer))throw std::runtime_error("Native Engine original watermark retirement refused");
  if(!native_work_endpoint_->WriteAcceptedCuts(Wire::Kind::Acknowledged,*work.cuts))throw std::runtime_error("Native Engine accepted-work acknowledgement failed");
  work.granted=true;writer_.Reply(work.command,true);
}
void Engine::OriginalAcceptedWorkBarrier(int id,Dict message){
  using Wire=suitecut::runner::RetainedSourceWorkWire;
  if(native_work_endpoint_)PumpAcceptedWork(); // Native Begin may precede stdin on another kernel channel.
  if(!native_work_||!native_work_installed_||native_work_->command||native_work_->granted||native_work_->refused){writer_.Reply(id,false,"Native Engine original barrier has no issued challenge");return;}
  const auto& challenge=native_work_->challenge;
  for(const char* key:{"sourceTicket","barrier","borrowLease","borrowGeneration","nativeDeadline"})if(message->GetType(key)!=VTYPE_STRING){writer_.Reply(id,false,"Native Engine original barrier identity malformed");return;}
  const auto packet="SOURCE_WORK_BEGIN 1 "+message->GetString("sourceTicket").ToString()+" "+message->GetString("barrier").ToString()+" "+message->GetString("borrowLease").ToString()+" "+message->GetString("borrowGeneration").ToString()+" "+message->GetString("nativeDeadline").ToString()+"\n";
  if(Wire::Decode(packet).challenge!=challenge){writer_.Reply(id,false,"Native Engine original barrier differs from issued native challenge");return;}
  const auto origin=suitecut::CurrentNativeWorkOrigin();
  if(origin.kind!=suitecut::NativeWorkOrigin::Kind::Command||origin.source_lease!=AcceptedSourceTicket()){writer_.Reply(id,false,"Native Engine barrier lacks original reader admission");return;}
  native_work_->command=id;native_work_->reader=reader_work_->Cut();
  if(!native_work_->reader.nonce)throw std::runtime_error("Native Engine original reader cutoff exhausted");
  // Route/accepted reader frames must really return before the cutoff can pass.
  PostAcceptedWork();
}
#endif
