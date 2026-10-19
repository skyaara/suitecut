// SPDX-License-Identifier: MIT
// Included inside Source and the controlled Source-wrapper test host.
// Persistent groups must never enter DevTools()'s SelectorWaits temporary group
// bookkeeping or its logical-operation continuation suppression.
suitecut::SnapshotReferenceDispatch reference_dispatch_;
void ReferenceSubmit(int owner,uint64_t generation,const std::string& session,
    const std::string& method,Dict params,std::function<bool()> before_submit,
    std::function<void(bool,bool,const std::string&)> done){
  const auto reject=[&done]{done(false,false,"{}");};
  if(!browser_||!operations_.IsCurrent(owner,generation)||
     operations_.IsExpired(owner,generation,Now())||
     (!session.empty()&&!sessions_.contains(session))){reject();return;}
  auto physical=reference_dispatch_.Begin();
  if(!physical){reject();return;}
  const int tool=AllocateDevToolsId();
  if(!tool){reject();return;}
  struct Settlement {
    std::shared_ptr<void> physical;
    std::function<void(bool,bool,const std::string&)> done;
    bool settled=false;
    void Finish(bool submitted,bool ok,const std::string& json){
      if(settled)return;
      settled=true;
      // Release the dispatch slot after the real consumer has settled its
      // registry install/use lease, even when that starts another command.
      done(submitted,ok,json);done={};physical.reset();
    }
  };
  auto settlement=std::make_shared<Settlement>();settlement->physical=std::move(physical);settlement->done=std::move(done);
  // Register before CEF submission: a real ACK may be delivered synchronously.
  // Every actual ACK is delivered, including after cancel/retry. Consumers
  // settle physical registry ownership before checking logical continuation.
  const auto [position,inserted]=devtools_.emplace(tool,DevToolsCall{owner,
    [settlement](bool ok,const std::string& json){settlement->Finish(true,ok,json);},
    session,true,1048576,true,true});
  if(!inserted){settlement->Finish(false,false,"{}");return;}
  const auto rollback=[this,tool,settlement]{
    const auto found=devtools_.find(tool);if(found!=devtools_.end())devtools_.erase(found);
    settlement->Finish(false,false,"{}");
  };
  if(!operations_.IsCurrent(owner,generation)||operations_.IsExpired(owner,generation,Now())||!before_submit()){
    rollback();return;
  }
  // A false submission result is explicit no-submit; a synchronous ACK that
  // already settled wins and cannot be overwritten by rollback.
  if(!SubmitDevTools(tool,method,params,session))rollback();
  WakeDeadlines();
}
