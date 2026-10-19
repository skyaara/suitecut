// SPDX-License-Identifier: MIT
// Exact active callback capabilities survive page removal, without making page
// close wait on a user function which may itself be awaiting that close.
uint64_t Engine::RetainLocatorHandler(int page,CefRefPtr<Source> source) {
  if(!source||locator_handler_leases_.size()>=64||locator_handler_leases_.contains(page)||locator_handler_invocation_==SuiteCutActionPauseRegistry::kMaxWireInteger)return 0;
  const auto token=suitecut::NextNativeLocatorHandlerInvocation(SuiteCutActionPauseRegistry::kMaxWireInteger);if(!token)return 0;
  locator_handler_leases_.emplace(page,std::move(source));locator_handler_invocation_=token;WakeLocatorHandlers();return token;
}
void Engine::WakeLocatorHandlers(){WakeTrace();}
bool Engine::LocatorHandlerAdmission(Dict message,double& deadline)const {
  const auto origin=message->GetDictionary("locatorHandlerOrigin");
  if(!origin||origin->GetType("page")!=VTYPE_INT)return false;
  const auto source=locator_handler_leases_.find(origin->GetInt("page"));
  return source!=locator_handler_leases_.end()&&source->second->LocatorHandlerOrigin(origin,deadline);
}
bool Engine::LocatorHandlerControl(int id,const std::string& command,Dict message) {
  const bool operation=command=="locatorHandlerOperationBegin"||command=="locatorHandlerOperationEnd";
  if(!operation&&command!="locatorHandlerRaw"&&command!="locatorHandlerOperationsRetired")return false;
  const auto page=message->GetInt("pageId");const auto source=locator_handler_leases_.find(page);
  if(operation){
    if(source==locator_handler_leases_.end()||!source->second->LocatorHandlerOperationControl(id,message,command=="locatorHandlerOperationEnd"))writer_.Reply(id,false,"Native locator handler operation capability is stale, expired or full");
    return true;
  }
  if(source==locator_handler_leases_.end()||!source->second->LocatorHandlerRaw(message,command=="locatorHandlerOperationsRetired"))
    writer_.Reply(id,false,"Native locator handler receipt is stale or invalid");
  else writer_.Reply(id,true);
  return true;
}
void Engine::TickLocatorHandlers() {
  if(locator_handler_leases_.empty())return;
  // Snapshot bounded capabilities: actual retirement can synchronously resume
  // an action and admit another handler. Never iterate a mutated owner map.
  std::vector<std::pair<int,CefRefPtr<Source>>> ready;
  for(const auto&[page,source]:locator_handler_leases_) {
    source->LocatorHandlerTick();
    const auto work=source->locator_handler_active_;
    if(work&&work->raw&&!work->front_pending)ready.emplace_back(page,source);
  }
  if(ready.empty())return;
  // Each exact callback drains independently. Unrelated logical waiters are
  // not part of its ownership; no global idle result is claimed here.
  // Remove the capability before resume so a newly dispatched invocation gets
  // a distinct lease; the local CefRefPtr still protects this actual Source.
  for(const auto&[page,source]:ready) {
    const auto found=locator_handler_leases_.find(page);
    if(found==locator_handler_leases_.end()||found->second!=source||!LocatorHandlerDescendantsReady(source->locator_handler_active_->token))continue;
    locator_handler_leases_.erase(found);source->LocatorHandlerRetire();
  }
}
void Engine::LocatorHandlerChannelClosed() {
  // Called only by the reader's actual EOF/framing termination, not by page or
  // renderer close. No author RAW/RETIRED callback receipt is manufactured.
  for(const auto&[page,source]:locator_handler_leases_) {
    (void)page;
    std::vector<int> removals;
    for(const auto&[handler,ids]:source->locator_handler_removals_){(void)handler;removals.insert(removals.end(),ids.begin(),ids.end());}
    source->locator_handler_removals_.clear();
    for(const int id:removals)source->Complete(id,false,"Native author channel closed before locator handler retirement");
    source->locator_handler_active_.reset();source->locator_handler_work_.clear();
  }
  locator_handler_leases_.clear();
}

bool Engine::TrackLocatorHandlerCommand(int id,int page,uint64_t generation) {
  const auto origin=suitecut::CurrentNativeWorkOrigin();
  if(!origin.locator_handler_invocation)return true;
  if(locator_handler_commands_.size()>=suitecut::AcceptedTaskWork::kPending)return false;
  return locator_handler_commands_.emplace(id,LocatorHandlerCommand{origin,page,generation}).second;
}
void Engine::ForgetLocatorHandlerCommand(int id){locator_handler_commands_.erase(id);}
suitecut::NativeWorkOrigin Engine::LocatorHandlerCommandOrigin(int id,int page,uint64_t generation)const {
  const auto found=locator_handler_commands_.find(id);
  if(found!=locator_handler_commands_.end()&&found->second.page==page&&found->second.generation==generation)return found->second.origin;
  auto origin=suitecut::CurrentNativeWorkOrigin();
  // A callback completing/canceling an unrelated operation cannot lend that
  // operation its invocation identity. Existing reader provenance stays intact.
  origin.locator_handler_invocation=0;return origin;
}
bool Engine::LocatorHandlerDescendantsReady(uint64_t invocation)const {
  if(!invocation||!suitecut::NativeLocatorHandlerAsyncWork::Ready(invocation)||!inspector_work_->LocatorHandlerReady(invocation)||!suitecut::NativeLocatorHandlerTasksReady(invocation))return false;
  for(const auto&[id,command]:locator_handler_commands_){(void)id;if(command.origin.locator_handler_invocation==invocation)return false;}
  for(const auto&[page,source]:sources_){(void)page;if(!source->LocatorHandlerNativeReady(invocation))return false;}
  for(const auto&[page,source]:locator_handler_leases_) {
    if(!sources_.contains(page)&&!source->LocatorHandlerNativeReady(invocation))return false;
    const auto work=source->locator_handler_active_;
    if(work&&work->token==invocation)for(const int closed:work->close_pages)if(contexts_->PageClosePending(closed))return false;
    if(work&&work->token==invocation)for(const int closed:work->close_contexts)if(contexts_->ContextClosePending(closed))return false;
  }
  return true;
}
