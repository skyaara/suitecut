// SPDX-License-Identifier: MIT
// Included inside Source. Registrations are resident policy; only a dispatched
// author callback owns an Engine lease. Page closure is not its RAW receipt.
  struct LocatorHandlerRegistration {
    uint64_t id=0;std::string client;Dict target;uint64_t remaining=0;
    bool limited=false,no_wait=false,enabled=true;std::function<bool()> references_current;
  };
  struct LocatorHandlerWork {
    enum class Phase { Observing, Prepared, Calling, Draining, Hidden };
    int owner=0;uint64_t generation=0,cursor=0,token=0;double deadline=0,next=0;
    Phase phase=Phase::Observing;std::shared_ptr<LocatorHandlerRegistration> handler;
    std::function<void()> resume;bool raw=false,front_pending=false,ok=false,aborted=false,busy=false;
    std::string error;std::set<uint64_t> operations;std::set<int> close_pages,close_contexts;uint64_t next_operation=0;
  };
  std::map<uint64_t,std::shared_ptr<LocatorHandlerRegistration>> locator_handlers_;
  std::map<int,std::shared_ptr<LocatorHandlerWork>> locator_handler_work_;
  std::shared_ptr<LocatorHandlerWork> locator_handler_active_;
  std::map<uint64_t,std::vector<int>> locator_handler_removals_;
  uint64_t locator_handler_next_=0;
  std::pair<int,uint64_t> locator_handler_resuming_{};

  bool LocatorHandlerCurrent(const std::shared_ptr<LocatorHandlerWork>& work)const {
    const auto found=locator_handler_work_.find(work->owner);
    return found!=locator_handler_work_.end()&&found->second==work;
  }
  bool LocatorHandlerPaused(int id,uint64_t generation)const {
    const auto found=locator_handler_work_.find(id);
    return found!=locator_handler_work_.end()&&found->second->generation==generation&&
      (found->second->phase==LocatorHandlerWork::Phase::Calling||found->second->phase==LocatorHandlerWork::Phase::Draining);
  }
  bool LocatorHandlerLogicalReady()const {
    // Exempt only exact logical paused parents. Physical Inspector/IO/input
    // ownership is still checked by the unchanged AcceptedWorkReady predicate.
    size_t paused=0;
    for(const auto&[id,work]:locator_handler_work_)if(LocatorHandlerPaused(id,work->generation)) {
      if(operations_.IsCurrent(id,work->generation))++paused;
    }
    if(operations_.Size()!=paused)return false;
    for(const auto&[id,retry]:context_retries_){(void)retry;const auto op=operations_.Get(id);if(!op||!LocatorHandlerPaused(id,op->generation))return false;}
    for(const int id:context_retry_queued_){const auto op=operations_.Get(id);if(!op||!LocatorHandlerPaused(id,op->generation))return false;}
    if(selector_waits_) {
      size_t states=0;
      for(const auto&[id,work]:locator_handler_work_)if(LocatorHandlerPaused(id,work->generation)&&selector_waits_->Contains(id)) {
        if(!selector_waits_->Ready(id)||!selector_waits_->Retrying(id))return false;
        ++states;
      }
      if(selector_waits_->Size()!=states)return false;
    }
    return true;
  }
  bool LocatorHandlerNativeReady(uint64_t invocation)const {
    if(selector_waits_&&!selector_waits_->LocatorHandlerReady(invocation))return false;
    for(const auto&[id,work]:input_work_){(void)id;if(work->native_origin.locator_handler_invocation==invocation)return false;}
    return true;
  }
  bool LocatorHandlerNeedsSourceTick()const {
    for(const auto&[id,work]:locator_handler_work_){(void)id;if(work->phase!=LocatorHandlerWork::Phase::Calling&&work->phase!=LocatorHandlerWork::Phase::Draining)return true;}
    return false;
  }
  void LocatorHandlerEvent(const std::shared_ptr<LocatorHandlerWork>& work,const char* stage) {
    auto event=Header("actionstage");event->SetInt("id",work->owner);
    event->SetDouble("generation",double(work->generation));event->SetDouble("token",double(work->token));
    event->SetDouble("handlerId",double(work->handler->id));event->SetString("clientToken",work->handler->client);
    event->SetString("stage",stage);
    if(std::string_view(stage)=="locatorHandlerRetired")event->SetBool("removed",!work->handler->enabled||(work->handler->limited&&!work->handler->remaining));
    SendPacket(event);
  }
  void LocatorHandlerRegister(int id,Dict message) {
    auto target=message->GetDictionary("target");uint64_t times=0;
    const auto client=message->GetString("clientToken").ToString();
    if(!target||client.empty()||client.size()>128||target->HasKey("nativeRootHandle")||
       target->GetType("query")!=VTYPE_LIST||target->GetType("strict")!=VTYPE_BOOL||
       (message->HasKey("times")&&!ActionHookInteger(message,"times",times))||
       (message->HasKey("noWaitAfter")&&message->GetType("noWaitAfter")!=VTYPE_BOOL)||locator_handlers_.size()>=64||
       locator_handler_next_==SuiteCutActionPauseRegistry::kMaxWireInteger) {
      writer_.Reply(id,false,"Invalid native locator handler or registration capacity exceeded");return;
    }
    target=target->Copy(false);
    if(target->HasKey("nativeHandle")) {
      if(target->GetType("nativeHandle")!=VTYPE_INT||target->GetInt("nativeHandle")<1||
         (target->GetList("framePath")&&target->GetList("framePath")->GetSize())) {
        writer_.Reply(id,false,"Invalid native locator handler fixed node");return;
      }
      const auto found=handles_.find(target->GetInt("nativeHandle"));
      if(found==handles_.end()||found->second.node_id.empty()) {
        writer_.Reply(id,false,"Native locator handler fixed node is unavailable");return;
      }
      const auto frame=target->GetString("frame").ToString();
      if(!frame.empty()&&frame!=found->second.frame_id) {
        writer_.Reply(id,false,"Native locator handler fixed node belongs to another frame");return;
      }
      target->SetString("frame",found->second.frame_id);
    }
    if(const auto frame=target->GetString("frame").ToString();!frame.empty()&&!frame_states_.contains(frame)) {
      writer_.Reply(id,false,"Native locator handler frame is unavailable");return;
    }
    std::string reference_error;auto references=LocatorHandlerReferences(target,reference_error);
    if(!references){writer_.Reply(id,false,reference_error);return;}
    auto registration=std::make_shared<LocatorHandlerRegistration>();registration->id=++locator_handler_next_;
    registration->references_current=std::move(references);
    registration->client=client;registration->target=target->Copy(false);registration->limited=message->HasKey("times");
    registration->remaining=times;registration->no_wait=message->GetBool("noWaitAfter");
    locator_handlers_.emplace(registration->id,registration);
    const auto json="{\"handlerId\":"+std::to_string(registration->id)+"}";
    writer_.Reply(id,true,"",std::vector<uint8_t>(json.begin(),json.end()));
  }
  void LocatorHandlerRemovalReply(int id,bool ok,const std::string& error="") {Complete(id,ok,error);}
  void LocatorHandlerCancelRemoval(int id) {
    for(auto it=locator_handler_removals_.begin();it!=locator_handler_removals_.end();) {
      auto& ids=it->second;ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
      if(ids.empty())it=locator_handler_removals_.erase(it);else ++it;
    }
  }
  void LocatorHandlerRemove(int id,Dict message) {
    uint64_t handler=0;
    if(!ActionHookInteger(message,"handlerId",handler)){LocatorHandlerRemovalReply(id,false,"Invalid native locator handler identity");return;}
    const auto found=locator_handlers_.find(handler);
    if(found==locator_handlers_.end()||found->second->client!=message->GetString("clientToken").ToString()) {LocatorHandlerRemovalReply(id,false,"Native locator handler is stale");return;}
    found->second->enabled=false;
    const auto active=locator_handler_active_;
    if(active&&active->handler==found->second) {
      uint64_t invocation=0;
      if(ActionHookInteger(message,"invocationToken",invocation)&&invocation==active->token) {
        LocatorHandlerRemovalReply(id,false,"A locator handler cannot await its own removal; registration disabled");return;
      }
      size_t pending=0;for(const auto&[key,ids]:locator_handler_removals_){(void)key;pending+=ids.size();}
      if(pending>=64){LocatorHandlerRemovalReply(id,false,"Native locator handler removal capacity exceeded; registration disabled");return;}
      locator_handler_removals_[handler].push_back(id);return;
    }
    locator_handlers_.erase(found);LocatorHandlerRemovalReply(id,true);
  }
  bool LocatorHandlerBefore(int id,uint64_t generation,Dict config,std::function<void()> resume) {
    if(locator_handler_resuming_==std::pair{id,generation}) {locator_handler_resuming_={};return false;}
    if(locator_handler_work_.contains(id))return true;
    if(locator_handlers_.empty()||config->GetBool("force")||locator_handler_active_)return false;
    if(locator_handler_work_.size()>=64){Complete(id,false,"Native locator handler action capacity exceeded");return true;}
    const auto op=operations_.Get(id);if(!op||op->generation!=generation)return true;
    auto work=std::make_shared<LocatorHandlerWork>();work->owner=id;work->generation=generation;work->deadline=op->deadline_ms;work->resume=std::move(resume);
    locator_handler_work_.emplace(id,work);WakeDeadlines();LocatorHandlerObserve(work);return true;
  }
  void LocatorHandlerResume(const std::shared_ptr<LocatorHandlerWork>& work) {
    if(!LocatorHandlerCurrent(work))return;
    auto resume=std::move(work->resume);locator_handler_work_.erase(work->owner);
    if(!operations_.IsCurrent(work->owner,work->generation)||!resume)return;
    if(selector_waits_)selector_waits_->Resume(work->owner);
    locator_handler_resuming_={work->owner,work->generation};resume();locator_handler_resuming_={};
  }
  void LocatorHandlerObserve(const std::shared_ptr<LocatorHandlerWork>& work) {
    if(!LocatorHandlerCurrent(work)||work->busy||!operations_.IsCurrent(work->owner,work->generation))return;
    if(work->phase!=LocatorHandlerWork::Phase::Hidden) {
      auto found=locator_handlers_.upper_bound(work->cursor);
      while(found!=locator_handlers_.end()&&(!found->second->enabled||(found->second->limited&&!found->second->remaining)))++found;
      if(found==locator_handlers_.end()){LocatorHandlerResume(work);return;}
      work->handler=found->second;work->cursor=found->first;
    }
    const auto handler=work->handler;work->busy=true;
    if(handler->references_current&&!handler->references_current()){LocatorHandlerObserved(work,false);return;}
    auto observe=[this,work](Dict target) {
      if(!LocatorHandlerCurrent(work))return;
      const auto frame=FrameId(target);
      if(!frame_states_.contains(frame)){LocatorHandlerObserved(work,false);return;}
      if(selector_waits_)selector_waits_->Resume(work->owner);
      const std::string visibility="if(nodes.length>1&&"+std::string(target->GetBool("strict")?"true":"false")+")throw new Error('Strict locator handler matched multiple elements');const e=nodes[0];if(!e||!e.isConnected||e.nodeType!==1)return false;const r=e.getBoundingClientRect(),s=getComputedStyle(e);return r.width>0&&r.height>0&&s.display!=='none'&&s.visibility!=='hidden'&&s.visibility!=='collapse';";
      auto observed=[this,work](bool ok,const std::string& json) {
          if(!LocatorHandlerCurrent(work))return;
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto value=result?result->GetDictionary("result"):nullptr;
          if(!ok||!result||result->HasKey("exceptionDetails")||!value||value->GetType("value")!=VTYPE_BOOL) {
            Complete(work->owner,false,"Native locator handler visibility observation failed");return;
          }
          LocatorHandlerObserved(work,value->GetBool("value"));
        };
      if(target->HasKey("nativeHandle")) {
        // Resolve the exact retained node in its current native utility world.
        // Disposal/navigation never falls back to a query for a replacement.
        if(!handles_.contains(target->GetInt("nativeHandle"))){LocatorHandlerObserved(work,false);return;}
        EvaluateFixedNode(work->owner,target,"(()=>{const nodes=[this];"+visibility+"})()",std::move(observed));
      } else QueryInspect(work->owner,target,visibility,true,true,std::move(observed));
    };
    auto target=handler->target->Copy(false);
    if(!frame_states_.contains(FrameId(target))){LocatorHandlerObserved(work,false);return;}
    const auto path=target->GetList("framePath");
    if(path&&path->GetSize())ResolveFramePath(work->owner,work->generation,target,0,FrameId(target),std::move(observe),[this,work]{LocatorHandlerObserved(work,false);});
    else observe(target);
  }
  void LocatorHandlerObserved(const std::shared_ptr<LocatorHandlerWork>& work,bool visible) {
    if(!LocatorHandlerCurrent(work))return;
    work->busy=false;
    if(work->phase==LocatorHandlerWork::Phase::Hidden) {
      if(visible){work->next=Now()+20;WakeDeadlines();return;}
      work->phase=LocatorHandlerWork::Phase::Observing;LocatorHandlerObserve(work);return;
    }
    if(!visible||!work->handler->enabled){LocatorHandlerObserve(work);return;}
    work->phase=LocatorHandlerWork::Phase::Prepared;
    if(selector_waits_)selector_waits_->Pause(work->owner);
    WakeDeadlines();
  }
  void LocatorHandlerCancel(int id) {
    const auto found=locator_handler_work_.find(id);if(found==locator_handler_work_.end())return;
    const auto work=found->second;work->resume={};
    if(work==locator_handler_active_) {
      if(!work->aborted){work->aborted=true;LocatorHandlerEvent(work,"locatorHandlerAbort");}
    } else locator_handler_work_.erase(found);
  }
  bool LocatorHandlerRaw(Dict message,bool front_only) {
    const auto work=locator_handler_active_;uint64_t generation=0,token=0,handler=0;
    if(!work||message->GetInt("target")!=work->owner||!ActionHookInteger(message,"generation",generation)||generation!=work->generation||
       !ActionHookInteger(message,"token",token)||token!=work->token||!ActionHookInteger(message,"handlerId",handler)||handler!=work->handler->id||message->GetString("clientToken")!=work->handler->client)return false;
    if(front_only){if(!work->raw||!work->front_pending||!work->operations.empty())return false;work->front_pending=false;}
    else {
      std::string authored_error;
      if(work->raw||(!message->GetBool("ok")&&!suitecut::InboundString(message,"error",authored_error,8192))||message->GetBool("operationsPending")!=!work->operations.empty()||message->GetType("ok")!=VTYPE_BOOL||message->GetType("operationsPending")!=VTYPE_BOOL)return false;
      work->raw=true;work->ok=message->GetBool("ok");work->error=suitecut::InboundDisplay(authored_error);work->front_pending=message->GetBool("operationsPending");
      work->phase=LocatorHandlerWork::Phase::Draining;
    }
    engine_.WakeLocatorHandlers();return true;
  }
  bool LocatorHandlerOperationControl(int reply,Dict message,bool end) {
    const auto work=locator_handler_active_;uint64_t generation=0,token=0,handler=0,operation=0;
    if(!work||message->GetInt("target")!=work->owner||!ActionHookInteger(message,"generation",generation)||generation!=work->generation||
       !ActionHookInteger(message,"token",token)||token!=work->token||!ActionHookInteger(message,"handlerId",handler)||handler!=work->handler->id||message->GetString("clientToken")!=work->handler->client)return false;
    if(end) {
      if(!ActionHookInteger(message,"operationToken",operation)||!work->operations.erase(operation))return false;
      writer_.Reply(reply,true);return true;
    }
    if(work->raw||work->aborted||(work->deadline&&Now()>=work->deadline)||work->operations.size()>=128||work->next_operation==SuiteCutActionPauseRegistry::kMaxWireInteger)return false;
    operation=++work->next_operation;work->operations.insert(operation);
    const auto json="{\"operationToken\":"+std::to_string(operation)+"}";
    writer_.Reply(reply,true,"",std::vector<uint8_t>(json.begin(),json.end()));return true;
  }
  bool LocatorHandlerOrigin(Dict origin,double& deadline)const {
    const auto work=locator_handler_active_;uint64_t generation=0,token=0;
    uint64_t operation=0;
    if(!work||work->aborted||!ActionHookInteger(origin,"operationToken",operation)||!work->operations.contains(operation)||origin->GetInt("target")!=work->owner||
       !ActionHookInteger(origin,"generation",generation)||generation!=work->generation||
       !ActionHookInteger(origin,"token",token)||token!=work->token||
       (work->deadline&&Now()>=work->deadline))return false;
    deadline=work->deadline;return true;
  }
  void LocatorHandlerTick() {
    std::vector<std::shared_ptr<LocatorHandlerWork>> pending;
    for(const auto&[id,work]:locator_handler_work_){(void)id;pending.push_back(work);}
    for(const auto&work:pending) {
      if(!LocatorHandlerCurrent(work))continue;
      if(!operations_.IsCurrent(work->owner,work->generation)||(work->deadline&&Now()>=work->deadline)) {
        if(operations_.IsCurrent(work->owner,work->generation))Complete(work->owner,false,"Native locator handler original action deadline exceeded");
        else LocatorHandlerCancel(work->owner);
        continue;
      }
      if(work->phase==LocatorHandlerWork::Phase::Prepared&&locator_handler_active_&&locator_handler_active_!=work){LocatorHandlerResume(work);continue;}
      if(work->phase==LocatorHandlerWork::Phase::Prepared&&!locator_handler_active_) {
        if(selector_waits_&&selector_waits_->Contains(work->owner)&&!selector_waits_->Ready(work->owner))continue;
        bool physical=false;for(const auto&[call,entry]:devtools_){(void)call;if(entry.owner==work->owner){physical=true;break;}}
        if(physical)continue;
        if(!work->handler->enabled){work->phase=LocatorHandlerWork::Phase::Observing;LocatorHandlerObserve(work);continue;}
        const auto token=engine_.RetainLocatorHandler(page_id_,this);
        if(!token){Complete(work->owner,false,"Native locator handler invocation capacity exceeded");continue;}
        work->token=token;work->phase=LocatorHandlerWork::Phase::Calling;locator_handler_active_=work;
        if(work->handler->limited)--work->handler->remaining;
        LocatorHandlerEvent(work,"locatorHandlerRun");
      } else if(work->phase==LocatorHandlerWork::Phase::Hidden&&!work->busy&&Now()>=work->next)LocatorHandlerObserve(work);
    }
  }
  void LocatorHandlerRetire() {
    const auto work=locator_handler_active_;if(!work||!work->raw||work->front_pending)return;
    LocatorHandlerEvent(work,"locatorHandlerRetired");locator_handler_active_.reset();
    const auto removal=locator_handler_removals_.find(work->handler->id);
    if(removal!=locator_handler_removals_.end()){auto ids=std::move(removal->second);locator_handler_removals_.erase(removal);for(const int id:ids)LocatorHandlerRemovalReply(id,true);}
    if(!work->handler->enabled||(work->handler->limited&&!work->handler->remaining))locator_handlers_.erase(work->handler->id);
    if(!operations_.IsCurrent(work->owner,work->generation)){locator_handler_work_.erase(work->owner);return;}
    if(!work->ok){const auto error=work->error.empty()?"Native locator handler callback failed":work->error;locator_handler_work_.erase(work->owner);Complete(work->owner,false,error);return;}
    work->token=0;work->raw=false;
    work->phase=work->handler->no_wait?LocatorHandlerWork::Phase::Observing:LocatorHandlerWork::Phase::Hidden;
    if(selector_waits_)selector_waits_->Resume(work->owner);
    LocatorHandlerObserve(work);WakeDeadlines();
  }
