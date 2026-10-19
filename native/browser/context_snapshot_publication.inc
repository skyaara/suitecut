// SPDX-License-Identifier: MIT
// Single UI owner for authoritative accepted snapshot order and real renderer
// receipts. The cache does not participate in author bootstrap in this slice.
double SuiteCutContextControls::SnapshotNow()const{return hooks_.operation_now?hooks_.operation_now()/1000:SuiteCutInitNow();}
double SuiteCutContextControls::SnapshotDeadline(int owner)const{
  const double budget=owner>0&&hooks_.operation_deadline?hooks_.operation_deadline(owner):0;
  return budget?budget/1000:0;
}
bool SuiteCutContextControls::CommitInitSnapshot(int context,std::string text,int owner){
  const auto found=contexts_.find(context);if(found==contexts_.end()||found->second.closing)return false;
  if(owner>0&&!snapshot_targets_.contains(owner)&&snapshot_targets_.size()>=64)return false;
  const auto before=snapshot_publications_.Current(context).version;
  std::shared_ptr<SnapshotTarget>target;
  bool inserted=false;
  if(owner>0){
    target=std::make_shared<SnapshotTarget>();target->context=context;
    for(const auto&[page,peer]:snapshot_peers_)if(peer.ready&&peer.pin.context==static_cast<uint64_t>(context))target->epochs.emplace(page,peer.pin.renderer_epoch);
    auto entry=snapshot_targets_.try_emplace(owner,target);inserted=entry.second;
  }
  bool accepted=false;
  try{
    accepted=snapshot_publications_.Commit(context,std::move(text),[this,context,cef=found->second.cef](std::shared_ptr<const std::string>bytes){return init_snapshot_store_->SetOwned(context,cef,std::move(bytes));});
  }catch(...){if(inserted)snapshot_targets_.erase(owner);throw;}
  if(accepted&&snapshot_publications_.Current(context).version!=before){
    if(target){target->version=snapshot_publications_.Current(context).version;snapshot_targets_.at(owner)=std::move(target);}
    SnapshotBroadcast(context,owner);
  }else if(inserted)snapshot_targets_.erase(owner);
  return accepted;
}
void SuiteCutContextControls::SnapshotFatal(int page,const std::string&reason){
  if(!snapshot_closing_.insert(page).second)return;
  CefPostTask(TID_UI,new ContextPolicyCloseTask([weak=weak_from_this(),page,reason]{if(auto self=weak.lock())self->BeginPageClose(page,reason);}));
}
bool SuiteCutContextControls::SnapshotSend(int page,int owner){
  auto peer=snapshot_peers_.find(page);const auto target=pages_.find(page);
  if(peer==snapshot_peers_.end()||!peer->second.ready||target==pages_.end()||target->second.closing||!target->second.browser)return false;
  const auto current=snapshot_publications_.Current(target->second.context);if(!current.bytes)return false;
  if(peer->second.sent>=current.version)return true;
  auto frame=target->second.browser->GetMainFrame();
  if(!frame||!frame->IsValid()||frame->GetIdentifier().ToString()!=peer->second.route)return false;
  const auto now=SnapshotNow();const auto original=SnapshotDeadline(owner);const double deadline=owner>0?original:now+30;
  if(owner>0&&original&&!hooks_.operation_now)return false;
  auto pin=snapshot_receipts_.Reserve(peer->second.pin,current.version,current.bytes->size(),deadline,now);if(!pin)return false;
  auto message=suitecut::InitSnapshotMessage::Build(*pin,*current.bytes);
  if(!message||!snapshot_receipts_.Submitted(pin->transfer,SnapshotNow())){snapshot_receipts_.Refused(pin->transfer);return false;}
  // Native Submit has a void result and explicitly does not guarantee delivery.
  // This physical reservation survives every logical cancellation/timeout.
  snapshot_flights_.emplace(pin->transfer,SnapshotFlight{*pin,page,owner,peer->second.request});peer->second.sent=current.version;
  frame->SendProcessMessage(PID_RENDERER,message);SnapshotWake();return true;
}
void SuiteCutContextControls::SnapshotBroadcast(int context,int owner){
  std::vector<int>targets;for(const auto&[page,peer]:snapshot_peers_)if(peer.ready&&peer.pin.context==static_cast<uint64_t>(context))targets.push_back(page);
  for(const int page:targets)if(!SnapshotSend(page,owner))SnapshotFatal(page,"Native shared snapshot publication admission failed");
}
SuiteCutContextControls::Reply SuiteCutContextControls::SnapshotReply(int owner,int context,Reply reply){
  const auto generation=hooks_.operation_generation?hooks_.operation_generation(owner):0;const auto deadline=SnapshotDeadline(owner);
  return [weak=weak_from_this(),owner,context,generation,deadline,reply=std::move(reply)](bool ok,Dict result,const std::string&error)mutable{
    const auto self=weak.lock();if(!self){reply(false,nullptr,"Native snapshot owner closed");return;}
    if(generation&&(!self->hooks_.operation_current||!self->hooks_.operation_current(owner,generation))){reply(false,nullptr,"Native snapshot original command is no longer current");return;}
    if(!ok){self->snapshot_targets_.erase(owner);reply(false,result,error);return;}
    if(deadline&&self->SnapshotNow()>=deadline){self->snapshot_targets_.erase(owner);reply(false,nullptr,"Native snapshot original command timed out");return;}
    self->SnapshotAwait(owner,context,generation,deadline,result,std::move(reply));
  };
}
void SuiteCutContextControls::SnapshotAwait(int owner,int context,uint64_t generation,double deadline,Dict result,Reply reply){
  if(owner<=0||snapshot_waits_.contains(owner)||snapshot_waits_.size()>=64){reply(false,nullptr,"Native shared snapshot waiter capacity exceeded");return;}
  const auto expected=snapshot_targets_.find(owner);
  if(expected==snapshot_targets_.end()){reply(true,result,"");return;}
  const auto target=expected->second;snapshot_targets_.erase(expected);
  if(target->context!=context){reply(false,nullptr,"Native snapshot original context identity changed");return;}
  SnapshotWait wait;wait.context=context;wait.version=target->version;wait.epochs=std::move(target->epochs);wait.generation=generation;wait.deadline=deadline;wait.result=result;wait.reply=std::move(reply);
  snapshot_waits_.emplace(owner,std::move(wait));SnapshotCheckWaiters();SnapshotWake();
}
void SuiteCutContextControls::SnapshotCheckWaiters(){
  struct ReplyNow {Reply reply;bool ok;Dict result;std::string error;};std::vector<ReplyNow>replies;
  const auto now=SnapshotNow();
  for(auto it=snapshot_waits_.begin();it!=snapshot_waits_.end();){
    auto&wait=it->second;std::string error;bool complete=true;
    const auto context=contexts_.find(wait.context);
    if(wait.generation&&(!hooks_.operation_current||!hooks_.operation_current(it->first,wait.generation)))error="Native snapshot original command is no longer current";
    else if(context==contexts_.end()||context->second.closing)error="Native shared snapshot context closed";
    else if(wait.deadline&&now>=wait.deadline)error="Native snapshot original command timed out";
    if(error.empty())for(const auto&[page,epoch]:wait.epochs){
      const auto peer=snapshot_peers_.find(page);
      if(peer==snapshot_peers_.end()||!peer->second.ready||peer->second.pin.renderer_epoch!=epoch){error="Native shared snapshot receiver changed before installation ACK";break;}
      if(peer->second.sent<wait.version){error="Native shared snapshot was not submitted to admitted receiver";break;}
      if(peer->second.installed<wait.version)complete=false;
    }
    if(!error.empty()||complete){replies.push_back({std::move(wait.reply),error.empty(),wait.result,std::move(error)});it=snapshot_waits_.erase(it);}else ++it;
  }
  for(auto&reply:replies)reply.reply(reply.ok,reply.result,reply.error);
}
bool SuiteCutContextControls::SnapshotMessage(int page,CefRefPtr<CefFrame>frame,CefProcessId sender,CefRefPtr<CefProcessMessage>message){
  if(sender!=PID_RENDERER||!message)return false;
  const auto name=message->GetName();
  if(name!=suitecut::InitSnapshotControl::kRequest&&name!=suitecut::InitSnapshotControl::kReady&&name!=suitecut::InitSnapshotControl::kAck&&name!=suitecut::InitSnapshotControl::kReleased&&name!=suitecut::InitSnapshotControl::kClosed)return false;
  const auto target=pages_.find(page);
  const auto sender_browser=frame?frame->GetBrowser():CefRefPtr<CefBrowser>{};
  if(target==pages_.end()||!target->second.browser||!sender_browser||sender_browser->GetIdentifier()!=target->second.browser->GetIdentifier())return true;
  if(name==suitecut::InitSnapshotControl::kAck||name==suitecut::InitSnapshotControl::kReleased){
    suitecut::InitSnapshotPin pin;bool installed=false;if(!suitecut::InitSnapshotControl::Receipt(message,pin,installed))return true;
    auto flight=snapshot_flights_.find(pin.transfer);
    if(flight==snapshot_flights_.end()||flight->second.page!=page||flight->second.pin!=pin||pin.owner.browser!=static_cast<uint64_t>(target->second.browser->GetIdentifier()))return true;
    if(name==suitecut::InitSnapshotControl::kReleased){
      if(!snapshot_receipts_.Released(pin))return true;
      flight->second.released=true;++snapshot_release_acks_;
    }else{
      auto ack=snapshot_receipts_.Acknowledge(pin,installed,SnapshotNow());if(!ack)return true;
      ++snapshot_install_acks_;flight->second.acknowledged=true;flight->second.installed=installed;
      auto peer=snapshot_peers_.find(page);
      if(installed&&peer!=snapshot_peers_.end()&&peer->second.pin==pin.owner){
        peer->second.installed=std::max(peer->second.installed,pin.version);
        snapshot_receipts_.RetireSuperseded(pin.owner,peer->second.installed,SnapshotNow());
      }
      if(!installed)SnapshotFatal(page,"Native renderer rejected shared snapshot region");
    }
    if(flight->second.acknowledged&&(!flight->second.installed||flight->second.released))snapshot_flights_.erase(flight);
    SnapshotCheckWaiters();SnapshotWake();return true;
  }
  int context=0;uint64_t epoch=0,request=0;std::string channel;
  if(!suitecut::InitSnapshotControl::Admission(message,context,epoch,request,channel)||context!=target->second.context)return true;
  if(name==suitecut::InitSnapshotControl::kRequest){
    if(epoch!=1)return true;
    auto actual=target->second.browser->GetMainFrame();
    if(!frame->IsMain()||!frame->IsValid()||!actual||actual->GetIdentifier()!=frame->GetIdentifier().ToString()||channel!=frame->GetIdentifier().ToString()||snapshot_next_epoch_==std::numeric_limits<uint64_t>::max())return true;
    auto peer=snapshot_peers_.find(page);
    if(peer==snapshot_peers_.end()||peer->second.request!=request||peer->second.pin.frame!=channel){
      if(snapshot_peers_.size()>=64&&peer==snapshot_peers_.end())return true;
      SnapshotPeer next;next.pin={static_cast<uint64_t>(context),static_cast<uint64_t>(target->second.browser->GetIdentifier()),++snapshot_next_epoch_,frame->GetIdentifier().ToString()};next.request=request;next.route=frame->GetIdentifier().ToString();
      snapshot_peers_.insert_or_assign(page,std::move(next));peer=snapshot_peers_.find(page);
    }
    frame->SendProcessMessage(PID_RENDERER,suitecut::InitSnapshotControl::Admission(suitecut::InitSnapshotControl::kAdmission,context,peer->second.pin.renderer_epoch,request,peer->second.pin.frame));
    SnapshotCheckWaiters();return true;
  }
  auto peer=snapshot_peers_.find(page);
  const bool current=peer!=snapshot_peers_.end()&&peer->second.pin.renderer_epoch==epoch&&peer->second.request==request&&peer->second.pin.frame==channel;
  if(name==suitecut::InitSnapshotControl::kClosed){
    // A true old renderer-browser instance may close after the new incarnation
    // is admitted. Its tombstone cannot remove the new peer or its receipts.
    bool known=current;
    for(const auto&[id,flight]:snapshot_flights_)if(flight.page==page&&flight.request==request&&flight.pin.owner.renderer_epoch==epoch&&flight.pin.owner.frame==channel)known=true;
    if(!known)return true;
    const auto browser=static_cast<uint64_t>(target->second.browser->GetIdentifier());
    snapshot_receipts_.RendererGone(browser,epoch);
    for(auto it=snapshot_flights_.begin();it!=snapshot_flights_.end();)if(it->second.pin.owner.browser==browser&&it->second.pin.owner.renderer_epoch==epoch)it=snapshot_flights_.erase(it);else ++it;
    if(current){snapshot_peers_.erase(peer);}
    SnapshotCheckWaiters();return true;
  }
  if(!current)return true;
  const auto actual_frame=target->second.browser->GetMainFrame();
  if(!frame->IsMain()||!frame->IsValid()||!actual_frame||actual_frame->GetIdentifier().ToString()!=frame->GetIdentifier().ToString())return true;
  peer->second.route=frame->GetIdentifier().ToString();peer->second.ready=true;if(!SnapshotSend(page))SnapshotFatal(page,"Native current snapshot receiver admission failed");SnapshotCheckWaiters();return true;
}
void SuiteCutContextControls::SnapshotRendererTerminated(int page){
  const auto peer=snapshot_peers_.find(page);if(peer==snapshot_peers_.end())return;
  const auto owner=peer->second.pin;snapshot_receipts_.RendererGone(owner.browser,owner.renderer_epoch);
  for(auto it=snapshot_flights_.begin();it!=snapshot_flights_.end();)if(it->second.pin.owner.browser==owner.browser&&it->second.pin.owner.renderer_epoch==owner.renderer_epoch)it=snapshot_flights_.erase(it);else ++it;
  snapshot_peers_.erase(peer);SnapshotCheckWaiters();
}
void SuiteCutContextControls::SnapshotPageClosed(int page){
  snapshot_closing_.erase(page);
  std::vector<suitecut::InitSnapshotOwner>owners;
  for(const auto&[id,flight]:snapshot_flights_)if(flight.page==page)owners.push_back(flight.pin.owner);
  for(const auto&owner:owners)snapshot_receipts_.RendererGone(owner.browser,owner.renderer_epoch);
  for(auto it=snapshot_flights_.begin();it!=snapshot_flights_.end();)if(it->second.page==page)it=snapshot_flights_.erase(it);else ++it;
  snapshot_peers_.erase(page);SnapshotCheckWaiters();
}
void SuiteCutContextControls::SnapshotWake(){
  const bool deadlines=std::any_of(snapshot_waits_.begin(),snapshot_waits_.end(),[](const auto&entry){return entry.second.deadline!=0;});
  if(snapshot_timer_||(!snapshot_receipts_.NeedsTick()&&!deadlines))return;
  snapshot_timer_=true;CefPostDelayedTask(TID_UI,new SuiteCutInitDeadlineTask([weak=weak_from_this()]{if(auto self=weak.lock())self->SnapshotTick();}),10);
}
void SuiteCutContextControls::SnapshotTick(){
  snapshot_timer_=false;
  for(const auto&event:snapshot_receipts_.Tick(SnapshotNow()))if(event.status==suitecut::InitSnapshotReceipts::Status::CloseRequired){
    auto flight=snapshot_flights_.find(event.pin.transfer);if(flight!=snapshot_flights_.end())SnapshotFatal(flight->second.page,"Native shared snapshot physical retirement exceeded cleanup grace");
  }
  SnapshotCheckWaiters();SnapshotWake();
}
void SuiteCutContextControls::SnapshotDiagnostics(Dict result)const{
  auto publications=CefListValue::Create();
  for(const auto&[context,value]:contexts_){
    const auto current=snapshot_publications_.Current(context);if(!current.version)continue;
    auto item=CefDictionaryValue::Create();item->SetInt("contextId",context);item->SetString("version",std::to_string(current.version));item->SetDouble("bytes",static_cast<double>(current.bytes->size()));publications->SetDictionary(publications->GetSize(),item);
  }
  auto peers=CefListValue::Create();
  for(const auto&[page,peer]:snapshot_peers_){
    auto item=CefDictionaryValue::Create();item->SetInt("pageId",page);item->SetInt("contextId",static_cast<int>(peer.pin.context));item->SetInt("browserId",static_cast<int>(peer.pin.browser));item->SetString("rendererEpoch",std::to_string(peer.pin.renderer_epoch));item->SetString("channelFrame",peer.pin.frame);item->SetString("routeFrame",peer.route);item->SetString("submittedVersion",std::to_string(peer.sent));item->SetString("acknowledgedVersion",std::to_string(peer.installed));item->SetBool("ready",peer.ready);peers->SetDictionary(peers->GetSize(),item);
  }
  result->SetList("nativeSnapshotPublications",publications);result->SetList("nativeSnapshotPeers",peers);
  result->SetString("nativeSnapshotInstallAcks",std::to_string(snapshot_install_acks_));result->SetString("nativeSnapshotReleaseAcks",std::to_string(snapshot_release_acks_));
  result->SetInt("nativeSnapshotPublicationContexts",static_cast<int>(snapshot_publications_.Contexts()));result->SetDouble("nativeSnapshotPublicationBytes",static_cast<double>(snapshot_publications_.Bytes()));result->SetInt("nativeSnapshotPublicationBodies",static_cast<int>(snapshot_publications_.Bodies()));
  result->SetInt("nativeSnapshotReceiverPeers",static_cast<int>(snapshot_peers_.size()));result->SetInt("nativeSnapshotPhysicalRegions",static_cast<int>(snapshot_receipts_.Physical()));result->SetDouble("nativeSnapshotPhysicalBytes",static_cast<double>(snapshot_receipts_.Bytes()));result->SetInt("nativeSnapshotWaiters",static_cast<int>(snapshot_waits_.size()));
}
