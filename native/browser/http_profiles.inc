// SPDX-License-Identifier: MIT
// Private native profile lifecycle. Logical command cancellation never retires
// actual cookie/broker/utility ownership; closure waits their native ACKs.
void SuiteCutHTTPControls::ScopeCommandStart(int owner,int context_id,const std::string&action,Dict params,Reply reply,bool initialize,double deadline){
  const auto found=contexts_.find(context_id);
  if(owner<1||found==contexts_.end()||found->second.closing||scope_commands_.contains(owner)||scope_commands_.size()>=64||!hooks_.context_command){reply(false,nullptr,"Native HTTP context command is unavailable or at capacity");return;}
  const bool snapshot_export=found->second.owned&&action=="storageState";
  if(snapshot_export){
    for(const char*flag:{"indexedDB","opfs","credentials"})if(params->HasKey(flag)&&params->GetType(flag)!=VTYPE_BOOL){reply(false,nullptr,"Invalid native API storage export option");return;}
    // Optional real authenticator capture still uses its native context owner.
    // Ordinary API export needs no utility browser or origin recapture.
    if(!params->GetBool("credentials")){
      std::string error;std::vector<suitecut::NativeAPICookieJar::Cookie> cookies;
      if(!found->second.jar||!found->second.jar->Cookies({},JarClock(deadline),cookies,error)){reply(false,nullptr,error.empty()?"Native independent API cookie jar is unavailable":error);return;}
      auto output=CefDictionaryValue::Create();output->SetList("cookies",suitecut::NativeAPIJarCookies(cookies));
      if(!PublishOrigins(context_id,output,params->GetBool("indexedDB"),params->GetBool("opfs"),deadline,error)){reply(false,nullptr,error);return;}
      reply(true,output,"");return;
    }
  }
  const auto generation=++generation_;scope_commands_.emplace(owner,ScopeCommand{context_id,generation,std::move(reply),deadline,initialize,{}});found->second.physical_owners.insert(owner);hooks_.wake();
  const bool indexed_db=params->GetBool("indexedDB"),opfs=params->GetBool("opfs");
  const auto weak=weak_from_this();hooks_.context_command(owner,found->second.browser_context,action,params,[weak,owner,generation,initialize,action,indexed_db,opfs](bool ok,Dict result,const std::string&error){
    const auto self=weak.lock();if(!self)return;const auto command=self->scope_commands_.find(owner);if(command==self->scope_commands_.end()||command->second.generation!=generation)return;
    const int context=command->second.context;
    auto finish=[weak,owner,generation,initialize,action,indexed_db,opfs](bool ok,Dict result,const std::string&failure){
      const auto self=weak.lock();if(!self)return;const auto command=self->scope_commands_.find(owner);if(command==self->scope_commands_.end()||command->second.generation!=generation)return;
      const int context=command->second.context;const auto found=self->contexts_.find(context);std::string error=failure;
      if(found==self->contexts_.end()||found->second.closing){ok=false;error="Native API storage context has been closed";}
      if(ok&&initialize&&found->second.owned){
        const double creation=found->second.creation_deadline,original=command->second.deadline;
        const double deadline=creation&&original?std::min(creation,original):creation?creation:original;
        if(!command->second.origins){ok=false;error="Native API constructor origins were not validated";}
        else if(!suitecut::APIOriginsSnapshot::Current({self->hooks_.now,deadline},error))ok=false;
        else found->second.origins=std::move(command->second.origins);
      }
      if(ok&&action=="storageState"&&found->second.owned)ok=self->PublishOrigins(context,result,indexed_db,opfs,command->second.deadline,error);
      if(ok&&initialize)found->second.initializing=false;
      auto reply=std::move(command->second.reply);self->scope_commands_.erase(command);reply(ok,ok?result:nullptr,error);self->FinishDispose(context);
    };
    if(ok&&result&&(action=="cookies"||action=="storageState")){auto value=CefValue::Create();value->SetDictionary(result->Copy(false));const auto retained=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString().size();self->OrderCookies(context,result->GetList("cookies"),[finish=std::move(finish),result](CefRefPtr<CefListValue>ordered,const std::string&error){if(!error.empty()){finish(false,nullptr,error);return;}result->SetList("cookies",suitecut::HTTPPublicationCookies(ordered));finish(true,result,"");},retained);}
    else finish(ok,result,error);
  });
}
void SuiteCutHTTPControls::BeginDispose(int context_id,Reply reply,int owner){
  const auto found=contexts_.find(context_id);if(found==contexts_.end()){if(reply)reply(true,CefDictionaryValue::Create(),"");return;}
  if(reply){if(found->second.dispose_replies.size()>=64){reply(false,nullptr,"Native HTTP disposal waiter capacity exceeded");return;}found->second.dispose_replies.emplace_back(owner,std::move(reply));}
  const bool first=!found->second.closing;found->second.closing=true;found->second.initializing=false;
  if(first){
    std::vector<int>active;for(const auto&[id,state]:active_)if(state->context==context_id)active.push_back(id);for(const int id:active)Stop(id,"Target page, context or browser has been closed (API request context disposed)");
    std::vector<int>commands;for(const auto&[id,command]:scope_commands_)if(command.context==context_id)commands.push_back(id);
    for(const int id:commands){const auto pending=scope_commands_.find(id);if(pending==scope_commands_.end())continue;auto done=std::move(pending->second.reply);scope_commands_.erase(pending);if(hooks_.cancel_context_command)hooks_.cancel_context_command(id);done(false,nullptr,"Target page, context or browser has been closed (API request context disposed)");}
    commands.clear();for(const auto&[id,query]:storage_queries_)if(query.context==context_id)commands.push_back(id);for(const int id:commands)StopStorage(id,"Target page, context or browser has been closed (API request context disposed)",true);
    for(auto it=responses_.begin();it!=responses_.end();)if(it->second.context==context_id){body_bytes_-=it->second.body.size();it=responses_.erase(it);}else ++it;
    for(auto it=uploads_.begin();it!=uploads_.end();)if(it->second.context==context_id){upload_bytes_-=it->second.bytes.size();it=uploads_.erase(it);}else ++it;
    if(tracing_)tracing_->Closed(context_id);
  }
  const auto current=contexts_.find(context_id);if(current==contexts_.end())return;
  if(current->second.owned&&!current->second.actually_closed&&!current->second.close_started){
    current->second.close_started=true;const int profile=current->second.browser_context;const auto weak=weak_from_this();
    if(hooks_.close_context)hooks_.close_context(profile,suitecut::BindNativeLocatorHandlerAsync(Reply{[weak,context_id](bool ok,Dict,const std::string&error){if(const auto self=weak.lock()){const auto found=self->contexts_.find(context_id);if(found==self->contexts_.end())return;found->second.actually_closed=true;if(!ok)found->second.close_error=error;self->FinishDispose(context_id);}}}));
    else{current->second.close_error="Native HTTP profile close channel is unavailable";return;}
  }
  FinishDispose(context_id);hooks_.wake();
}
void SuiteCutHTTPControls::FinishDispose(int context_id){
  const auto found=contexts_.find(context_id);if(found==contexts_.end()||!found->second.closing)return;
  auto&context=found->second;
  for(auto it=context.physical_owners.begin();it!=context.physical_owners.end();)if(!hooks_.physical_owner||!hooks_.physical_owner(*it))it=context.physical_owners.erase(it);else ++it;
  if((tracing_&&!tracing_->ContextReady(context_id))||context.physical_visits||!context.physical_owners.empty()||(context.owned&&!context.actually_closed))return;
  const auto profile=context.browser_context;const auto error=context.close_error;auto replies=std::move(context.dispose_replies);contexts_.erase(found);
  if(contexts_.empty()){transport_.ResetIdle();}for(auto&[owner,reply]:replies)if(reply)reply(error.empty(),CefDictionaryValue::Create(),error);
  if(hooks_.settled_context)hooks_.settled_context(profile);
}
void SuiteCutHTTPControls::ContextClosing(int context){std::vector<int>ids;for(const auto&[id,profile]:contexts_)if(profile.browser_context==context)ids.push_back(id);for(const int id:ids)BeginDispose(id);}
bool SuiteCutHTTPControls::ContextReady(int context)const{for(const auto&[id,profile]:contexts_)if(profile.browser_context==context){if(profile.physical_visits||(tracing_&&!tracing_->ContextReady(id)))return false;for(const int owner:profile.physical_owners)if(hooks_.physical_owner&&hooks_.physical_owner(owner))return false;}return true;}
size_t SuiteCutHTTPControls::PhysicalProfiles()const{size_t count=0;for(const auto&[id,profile]:contexts_)if(profile.owned||profile.physical_visits||!profile.physical_owners.empty()||(profile.closing&&tracing_&&!tracing_->ContextReady(id)))++count;return count;}
bool SuiteCutHTTPControls::HasProfileWork()const{for(const auto&[id,profile]:contexts_)if(profile.closing||profile.initializing||profile.physical_visits||!profile.physical_owners.empty())return true;return false;}
void SuiteCutHTTPControls::ProfileTick(double now){
  std::vector<int>ids;for(const auto&[id,context]:contexts_)ids.push_back(id);
  for(const int id:ids){const auto found=contexts_.find(id);if(found==contexts_.end())continue;
    if(found->second.initializing&&found->second.creation_deadline&&now>=found->second.creation_deadline){found->second.close_error="Native API original creation deadline exceeded";BeginDispose(id);continue;}
    for(auto it=found->second.physical_owners.begin();it!=found->second.physical_owners.end();)if(!hooks_.physical_owner||!hooks_.physical_owner(*it))it=found->second.physical_owners.erase(it);else ++it;
    FinishDispose(id);
  }
}

void SuiteCutHTTPControls::OrderCookies(int context_id,CefRefPtr<CefListValue>cookies,std::function<void(CefRefPtr<CefListValue>,const std::string&)>done,size_t retained_bytes){
  const auto found=contexts_.find(context_id);if(found==contexts_.end()||found->second.closing||!cookies){done(nullptr,"Native HTTP cookie context is closed");return;}
  if(!found->second.owned||found->second.jar){done(cookies,"");return;}
  if(pending_cookie_visits_>=128){done(nullptr,"Native HTTP creation visitor capacity exceeded");return;}
  auto reservation=std::make_shared<suitecut::HTTPCookieResultReservation>(metadata_bytes_);
  auto value=CefValue::Create();value->SetList(cookies->Copy());const size_t cookie_bytes=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString().size();
  // The caller's result and the visitor's detached cookie list can both remain
  // alive after logical cancellation. Charge both until the actual callback.
  if(cookie_bytes>33554432||retained_bytes>33554432-cookie_bytes||!reservation->Reserve(retained_bytes+cookie_bytes)){done(nullptr,"Native HTTP physical cookie metadata budget exceeded");return;}
  cookies=cookies->Copy();
  ++pending_cookie_visits_;++found->second.physical_visits;hooks_.wake();const auto weak=weak_from_this();
  CefRefPtr<suitecut::HTTPCookieCreationVisitor>visitor=new suitecut::HTTPCookieCreationVisitor([](std::function<void()>task){CefPostTask(TID_UI,new HTTPTask(std::move(task)));},[weak,context_id,cookies,reservation,done=std::move(done)](suitecut::HTTPCookieCreationVisitor::Times times,const std::string&error)mutable{
    const auto self=weak.lock();if(!self)return;--self->pending_cookie_visits_;const auto found=self->contexts_.find(context_id);const int profile=found==self->contexts_.end()?0:found->second.browser_context;if(found!=self->contexts_.end())--found->second.physical_visits;
    if(!error.empty()){done(nullptr,error);self->FinishDispose(context_id);if(self->hooks_.settled_context&&profile)self->hooks_.settled_context(profile);return;}
    auto ordered=suitecut::HTTPOrderCreationCookies(cookies,times);done(ordered,"");self->FinishDispose(context_id);if(self->hooks_.settled_context&&profile)self->hooks_.settled_context(profile);
  });
  const auto manager=found->second.cef->GetCookieManager(nullptr);if(!manager||!manager->VisitAllCookies(visitor))visitor->Reject();
}
