// SPDX-License-Identifier: MIT
// Included inside actual Source. Factory and coverage share this one actual
// per-session domain owner. No callbacks retire at logical public cancellation.
std::shared_ptr<suitecut::DebuggerDomains> native_debugger_domains_;
struct NativeDebuggerPhysicalCall {std::string session;double retirement=0;bool closing=false;};
std::map<int,NativeDebuggerPhysicalCall> native_debugger_calls_;
bool native_debugger_closing_requested_=false;
std::map<std::string,uint64_t> native_coverage_debugger_leases_;
uint64_t native_debugger_next_lease_=0;
std::function<double(const std::string&,Dict)> native_debugger_pause_deadline_;
std::function<void(const std::string&,Dict,const std::string&)> native_debugger_event_observer_;
std::function<void(uint64_t,Dict,const std::string&)> native_debugger_pause_expired_;
uint64_t NativeDebuggerLease(){return native_debugger_next_lease_>=9007199254740991ULL?0:++native_debugger_next_lease_;}
bool NativeDebuggerSend(const std::string& session,const std::string& method,Dict params,double deadline,suitecut::DebuggerDomains::Reply reply){
  if(!browser_||closing_page_||native_debugger_closing_requested_||native_debugger_calls_.size()>=64||!reply||!std::isfinite(deadline)||deadline<0||(deadline>0&&Now()>=deadline))return false;
  const int assigned=AllocateDevToolsId();if(!assigned)return false;
  native_debugger_calls_.emplace(assigned,NativeDebuggerPhysicalCall{session,(deadline>0?deadline:Now()+30000)+30000,false});
  auto callback=[this,assigned,session,reply=std::move(reply)](bool ok,const std::string& json){
    native_debugger_calls_.erase(assigned);
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto data=parsed?parsed->GetDictionary():nullptr;
    const auto error=!ok&&data&&data->GetType("message")==VTYPE_STRING?data->GetString("message").ToString():ok?std::string():json;
    if(!ok&&!session.empty()&&error=="Session with given id not found.")NativeTargetSessionDetached(session);
    reply(ok,data,error);
  };
  // These actual retained callbacks survive canceled bootstrap/public owners.
  // Typed scalar projection applies only to genuine protocol-owned metadata.
  devtools_.emplace(assigned,DevToolsCall{0,callback,session,true,8*1048576,true,true});
  if(!SubmitDevTools(assigned,method,params,session)){
    const auto found=devtools_.find(assigned);if(found!=devtools_.end()){auto done=std::move(found->second.callback);devtools_.erase(found);done(false,"{\"message\":\"Native shared Debugger dispatch was not accepted\"}");}
  }
  WakeDeadlines();return true;
}
void NativeDebuggerRawSend(int owner,uint64_t raw_receipt,const std::string&session,const std::string&method,Dict params,double deadline,DevToolsCallback reply){
  // Raw commands use the same real Source registry but must settle their
  // reserved arbiter receipt even when dispatch is definitively refused.
  if(!browser_||closing_page_||native_debugger_closing_requested_||!std::isfinite(deadline)||deadline<0||(deadline>0&&Now()>=deadline)){
    if(native_debugger_domains_){native_debugger_domains_->EndRaw(raw_receipt,false,false);}
    reply(false,"{\"message\":\"Native raw Debugger dispatch was not admitted\"}");return;
  }
  const int assigned=AllocateDevToolsId();
  if(!assigned){if(native_debugger_domains_)native_debugger_domains_->EndRaw(raw_receipt,false,false);reply(false,"{\"message\":\"Native raw Debugger command identity exhausted\"}");return;}
  DevToolsCallback actual_reply=[this,raw_receipt,reply=std::move(reply)](bool ok,const std::string&json){if(native_debugger_domains_)native_debugger_domains_->EndRaw(raw_receipt,ok);reply(ok,json);};
  devtools_.emplace(assigned,DevToolsCall{owner,std::move(actual_reply),session,true,kMaxResponseBytes,true,false});
  if(!SubmitDevTools(assigned,method,params,session)){
    const auto found=devtools_.find(assigned);
    if(found!=devtools_.end()){if(native_debugger_domains_)native_debugger_domains_->EndRaw(raw_receipt,false,false);auto actual=std::move(found->second.callback);devtools_.erase(found);actual(false,"{\"message\":\"Native raw Debugger dispatch was not accepted\"}");}
  }
  WakeDeadlines();
}
void NativeDebuggerRequestClose(const std::string&error){
  if(native_debugger_closing_requested_)return;
  native_debugger_closing_requested_=true;
  NetworkFatal(error);
}
void NativeDebuggerTick(){
  if(native_debugger_domains_)native_debugger_domains_->Tick(Now());
  bool close=false;
  for(auto&[id,call]:native_debugger_calls_)if(!call.closing&&Now()>=call.retirement){call.closing=true;close=true;}
  // The watchdog requests actual closure; accepted callbacks and factory Holds
  // remain physically reserved until their real ACK or actual channel death.
  if(close)NativeDebuggerRequestClose("Native shared Debugger command exceeded its physical settlement grace");
}
bool NativeDebuggerNeedsTick()const{
  return(native_debugger_domains_&&native_debugger_domains_->NeedsTick())||std::any_of(native_debugger_calls_.begin(),native_debugger_calls_.end(),[](const auto&entry){return !entry.second.closing;});
}
std::shared_ptr<suitecut::DebuggerDomains> NativeDebuggerDomains(){
  if(native_debugger_domains_)return native_debugger_domains_;
  suitecut::DebuggerDomains::Hooks hooks;hooks.now=[]{return Now();};hooks.wake=[this]{WakeDeadlines();};
  hooks.send=[this](const std::string& session,const std::string& method,Dict params,double deadline,suitecut::DebuggerDomains::Reply reply){return NativeDebuggerSend(session,method,params,deadline,std::move(reply));};
  // An owned factory pause requires the actual document bootstrap's original
  // budget; no default timer or prior navigation budget may authorize capture.
  hooks.pause_deadline=[this](const std::string& session,Dict params){return native_debugger_pause_deadline_?native_debugger_pause_deadline_(session,params):-1;};
  hooks.pause_expired=[this](uint64_t lease,Dict pause,const std::string& error){if(native_debugger_pause_expired_)native_debugger_pause_expired_(lease,pause,error);else NativeDebuggerRequestClose(error);};
  hooks.close_channel=[this](const std::string&,const std::string& error){NativeDebuggerRequestClose(error);};
  native_debugger_domains_=std::make_shared<suitecut::DebuggerDomains>(std::move(hooks));return native_debugger_domains_;
}
bool NativeDebuggerCoverageAcquire(const std::string& session,double deadline,suitecut::CoverageControls::ProtocolReply reply){
  if(native_coverage_debugger_leases_.contains(session)){reply(false,nullptr,"Native coverage Debugger lease is already pending");return true;}
  const auto lease=NativeDebuggerLease();if(!lease)return false;
  native_coverage_debugger_leases_[session]=lease;
  NativeDebuggerDomains()->Acquire(session,lease,suitecut::DebuggerDomains::Kind::Coverage,deadline,[this,session,lease,reply=std::move(reply)](bool ok,const std::string& error){
    if(!ok){
      // Acquire's deadline is a logical failure. Its shared Debugger command
      // can still be executing: retain coverage's physical startup receipt
      // until the failed lease has actually drained and restored domain policy.
      NativeDebuggerDomains()->Release(lease,Now()+30000,[this,session,lease,error,reply=std::move(reply)](bool retired,const std::string&failure){
        const auto found=native_coverage_debugger_leases_.find(session);if(found!=native_coverage_debugger_leases_.end()&&found->second==lease)native_coverage_debugger_leases_.erase(found);
        if(!retired&&!closing_page_)NetworkFatal(failure);
        reply(false,nullptr,error);
      });return;
    }
    reply(true,nullptr,error);
  });return true;
}
bool NativeDebuggerCoverageRelease(const std::string& session,double deadline,suitecut::CoverageControls::ProtocolReply reply){
  const auto found=native_coverage_debugger_leases_.find(session);if(found==native_coverage_debugger_leases_.end()||!native_debugger_domains_){reply(false,nullptr,"Actual native coverage Debugger lease is unavailable");return true;}
  const auto lease=found->second;
  native_debugger_domains_->Release(lease,deadline,[this,session,lease,reply=std::move(reply)](bool ok,const std::string& error){
    const auto found=native_coverage_debugger_leases_.find(session);if(found!=native_coverage_debugger_leases_.end()&&found->second==lease)native_coverage_debugger_leases_.erase(found);
    reply(ok,nullptr,error);
  });return true;
}
bool NativeDebuggerEvent(const std::string& method,Dict params,const std::string& session){if(native_debugger_event_observer_&&method.starts_with("Debugger."))native_debugger_event_observer_(method,params,session);return native_debugger_domains_&&native_debugger_domains_->Event(method,params,session);}
void NativeDebuggerDetached(const std::string& session){
  if(native_debugger_domains_)native_debugger_domains_->Detached(session);
  native_coverage_debugger_leases_.erase(session);
  std::vector<DevToolsCallback> callbacks;
  for(auto it=devtools_.begin();it!=devtools_.end();)if(it->second.session==session&&native_debugger_calls_.contains(it->first)){callbacks.push_back(std::move(it->second.callback));it=devtools_.erase(it);}else ++it;
  for(auto& callback:callbacks)callback(false,"{\"message\":\"Actual native shared Debugger session detached\"}");
}
void NativeDebuggerClosed(){
  if(native_debugger_domains_){native_debugger_domains_->Closed();}native_coverage_debugger_leases_.clear();
  for(const auto&[id,call]:native_debugger_calls_){devtools_.erase(id);}native_debugger_calls_.clear();
}
