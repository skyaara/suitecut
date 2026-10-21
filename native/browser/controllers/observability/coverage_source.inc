// SPDX-License-Identifier: MIT
// Included inside actual Source. Coverage callbacks use the existing retained
// native command registry and observer-safe deferred Inspector dispatch.
suitecut::CoverageScriptInventory native_coverage_inventory_;
std::shared_ptr<suitecut::CoverageTargets> native_coverage_;
std::set<int> native_coverage_calls_;
std::map<std::string,std::pair<std::string,bool>> native_coverage_registered_scripts_;
bool NativeCoverageMarkNativeScript(const std::string& session,int context,const std::string& script){
  if(!native_coverage_inventory_.MarkNative(session,context,script))return false;
  if(native_coverage_)native_coverage_->NativeScript(session,context,script);
  return true;
}
void NativeCoverageScripts(){
  native_coverage_registered_scripts_.clear();if(!native_coverage_){return;}
  auto info=engine_.contexts()->BrowserExtraInfo(page_id_);auto snapshot=info?info->GetDictionary("suitecutInitScripts"):nullptr;if(!snapshot)return;
  for(const char* category:{"scripts","bindings"}){auto scripts=snapshot->GetList(category);if(!scripts)continue;for(size_t i=0;i<scripts->GetSize();++i){auto entry=scripts->GetDictionary(i);if(!entry||entry->GetType("id")!=VTYPE_INT||entry->GetType("source")!=VTYPE_STRING)continue;const bool author=std::string(category)=="scripts";const auto name=std::string(author?"suitecut:init-script:":"suitecut:context-binding:")+std::to_string(entry->GetInt("id"));native_coverage_registered_scripts_[name]={suitecut::CoverageControls::SourceHash(entry->GetString("source").ToString()),author};}}
}
bool NativeCoverageDocumentWorker(const std::string& id)const{
  const auto found=worker_targets_.find(id);if(found==worker_targets_.end()||found->second.kind!="worker"||found->second.context_destroyed)return false;
  const auto* target=&found->second;for(size_t depth=0;depth<1024;++depth){
    if(target->kind!="worker")return false;
    if(target->parent_session.empty()||sessions_.contains(target->parent_session))return true;
    const auto parent=worker_sessions_.find(target->parent_session);if(parent==worker_sessions_.end())return false;
    const auto owner=worker_targets_.find(parent->second);if(owner==worker_targets_.end())return false;target=&owner->second;
  }return false;
}
void NativeCoverageEnsure(){
  if(native_coverage_){return;}suitecut::CoverageTargets::Hooks hooks;hooks.now=[]{return Now();};hooks.wake=[this]{WakeDeadlines();};
  hooks.debugger_acquire=[this](const std::string& session,double deadline,suitecut::CoverageControls::ProtocolReply reply){return NativeDebuggerCoverageAcquire(session,deadline,std::move(reply));};
  hooks.debugger_release=[this](const std::string& session,double deadline,suitecut::CoverageControls::ProtocolReply reply){return NativeDebuggerCoverageRelease(session,deadline,std::move(reply));};
  hooks.author_context=[this](const std::string& session,int context){if(context<=0)return false;for(const auto& [id,frame]:frame_states_)if(frame.session==session&&frame.context==context)return true;const auto worker=worker_sessions_.find(session);if(worker==worker_sessions_.end())return false;const auto target=worker_targets_.find(worker->second);return target!=worker_targets_.end()&&NativeCoverageDocumentWorker(worker->second)&&target->second.context==context;};
  hooks.script_inventory=[this](const std::string& session,double deadline,std::vector<Dict>& output,std::string& error){return native_coverage_inventory_.Snapshot(session,output,error,deadline,Now,true);};
  hooks.script_url=[this](Dict params,std::string& url){
    if(params->GetType("hasSourceURL")!=VTYPE_BOOL||params->GetBool("hasSourceURL"))return true;
    const auto found=native_coverage_registered_scripts_.find(params->GetString("embedderName").ToString());if(found==native_coverage_registered_scripts_.end()||params->GetString("url")!=found->first||params->GetString("hash")!=found->second.first)return true;
    if(found->second.second){url.clear();return true;}return false;
  };
  // Failure may arrive inside an Inspector observer. Post teardown and retain
  // every accepted coverage callback until actual ACK or OnBeforeClose.
  hooks.close_channel=[this](const std::string&,const std::string& cause){NetworkFatal(cause);};
  hooks.send=[this](const std::string& session,const std::string& method,Dict params,double deadline,suitecut::CoverageControls::ProtocolReply reply){
    if(method=="DOM.enable"||method=="DOM.disable")return NativeCoverageDOM(session,method,deadline,std::move(reply));
    if(!browser_||closing_page_||native_coverage_calls_.size()>=32)return false;
    const int assigned=AllocateDevToolsId();if(!assigned)return false;
    native_coverage_calls_.insert(assigned);
    const auto callback=[this,assigned,session,reply=std::move(reply)](bool ok,const std::string& json){
      native_coverage_calls_.erase(assigned);auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      const auto cause=!ok&&result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():ok?std::string():json;
      // A trusted Inspector session-not-found error also attests genuine
      // channel retirement; domain/method errors never imply this boundary.
      if(!ok&&!session.empty()&&cause=="Session with given id not found.")NativeTargetSessionDetached(session);
      reply(ok,result,cause);
    };
    // owner0 denotes an actual controller-owned command. Retained=true keeps it
    // registered across public operation cancellation; only ACK/close retires it.
    devtools_.emplace(assigned,DevToolsCall{0,callback,session,true,8*1048576,true,true});
    if(!SubmitDevTools(assigned,method,params,session)){
      const auto found=devtools_.find(assigned);if(found!=devtools_.end()){auto done=std::move(found->second.callback);devtools_.erase(found);done(false,"{\"message\":\"Actual native coverage dispatch failed\"}");}
    }
    WakeDeadlines();return true;
  };
  native_coverage_=std::make_shared<suitecut::CoverageTargets>(std::move(hooks));
  // Dedicated workers retain actual target/session/context identity. Shared
  // and service workers belong to the context broker, outside Page coverage.
  worker_coverage_attach_=[this](const std::string& session,const std::string& target,const std::string& kind,double deadline,std::function<void(bool,const std::string&)> ready){if(kind!="worker"||!NativeCoverageDocumentWorker(target)){ready(true,"");return;}native_coverage_->WorkerAttached(session,target,deadline,std::move(ready));};
  worker_coverage_lost_=[this](const std::string& session,const std::string& error){if(native_coverage_)native_coverage_->Lost(session,error);};
  worker_coverage_retains_=[this](int id){return native_coverage_calls_.contains(id);};
  for(const auto& [session,target]:sessions_)native_coverage_->Attached(session,target,Now()+30000,{});
  for(const auto& [id,target]:worker_targets_)if(NativeCoverageDocumentWorker(id))native_coverage_->WorkerAttached(target.session,id,target.resumed?Now()+30000:target.deadline,{});
}
void NativeCoverageCommand(int owner,Dict params){
  if(params->HasKey("frameId")){Complete(owner,false,"Native coverage belongs to an actual page, not a frame facade");return;}
  const auto action=params->GetString("action").ToString();if((action=="startJS"||action=="startCSS")&&sessions_.size()>=32){Complete(owner,false,"Native coverage target capacity exceeded (root plus31 actual iframe targets)");return;}
  NativeCoverageEnsure();NativeCoverageScripts();const auto operation=operations_.Get(owner);if(!operation){return;}
  // A worker observed before lazy controller creation may still be waiting
  // for Runtime/Network setup. Install its coverage gate before any ACK can
  // resume author code, and bind it to this admitted recording afterwards.
  std::vector<std::pair<std::string,uint64_t>> paused;
  if(action=="startJS"&&params->GetBool("includeWorkers"))for(auto& [id,target]:worker_targets_)if(NativeCoverageDocumentWorker(id)&&!target.resumed){target.coverage_ready=false;paused.emplace_back(id,target.generation);}
  native_coverage_->Handle(owner,params,operation->deadline_ms,[this,owner](bool ok,const std::string& error,const std::string& json){Complete(owner,ok,error,json);});
  for(const auto& [id,generation]:paused)WorkerCoverageBootstrap(id,generation);
}
void NativeCoverageChild(const std::string& session,const std::string& frame,double deadline,std::function<void(bool,const std::string&)> ready){
  // This is a genuine Source child-bootstrap branch. Author execution remains
  // paused until any active JS/CSS recording owns its actual domain ACKs.
  if(!native_coverage_){ready(true,"");return;}
  native_coverage_->Attached(session,frame,deadline,std::move(ready));
}
void NativeCoverageDetached(const std::string& session){
  native_coverage_inventory_.Detached(session);
  if(session.empty()||!native_coverage_)return;
  // The actual session-close error can precede its detach event. Retire the
  // worker metadata first so a closed bootstrap cannot report a live failure.
  WorkerDetached(session);
  native_coverage_->Detached(session);
  std::vector<DevToolsCallback> callbacks;
  for(auto it=devtools_.begin();it!=devtools_.end();)if(it->second.session==session&&native_coverage_calls_.contains(it->first)){callbacks.push_back(std::move(it->second.callback));it=devtools_.erase(it);}else ++it;
  for(auto& callback:callbacks)callback(false,"{\"message\":\"Actual native coverage target session detached\"}");
}
bool NativeCoverageEvent(const std::string& method,Dict params,const std::string& session){
  native_coverage_inventory_.Observe(method,params,session);
  return native_coverage_&&native_coverage_->Event(method,params,session);
}
void NativeCoverageRendererTerminated(){if(native_coverage_)native_coverage_->RendererTerminated();}
void NativeCoverageClosed(){
  native_coverage_inventory_.Clear();
  if(native_coverage_){native_coverage_->Closed();}native_coverage_registered_scripts_.clear();
  // OnBeforeClose is the genuine channel-retirement boundary. These commands
  // cannot receive another browser ACK after this callback.
  for(const int id:native_coverage_calls_){devtools_.erase(id);}native_coverage_calls_.clear();
}
