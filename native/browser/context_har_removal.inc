// SPDX-License-Identifier: MIT
// Included after NativeContextHARRoutes, inside namespace suitecut.
class NativeContextHARRoutes::RemovalReservation {
 public:
  RemovalReservation(const RemovalReservation&)=delete;
  RemovalReservation&operator=(const RemovalReservation&)=delete;
 private:
  friend class NativeContextHARRoutes;
  RemovalReservation()=default;
  struct Change {
    uint64_t generation=0;bool busy=false;
    std::deque<std::string>queue;
    std::map<std::string,NativeHARRouteRegistration::Policy>claims;
    std::deque<RemoveTask>before,after;
  };
  const NativeContextHARRoutes*manager=nullptr;std::shared_ptr<bool>alive,policy_owner;
  int operation=0,context=0;uint64_t serial=0,generation=0;double deadline=0;
  bool committed=false,dispatched=false;
  std::map<std::string,Route>routes;std::vector<std::string>order,owners;size_t bytes=0;
  std::map<std::string,Change>changes;std::map<int,RemovalBatch>batch_node;
};
inline std::unique_ptr<NativeContextHARRoutes::RemovalReservation> NativeContextHARRoutes::PrepareRemoveAll(int owner,int context,double deadline,Result done){
  const auto state=states_.find(context);
  if(closed_||serial_>=9007199254740991ULL||owner<1||!done||!std::isfinite(deadline)||deadline<0||
     (deadline&&now_&&now_()>=deadline)||state==states_.end()||state->second.owner||batches_.size()+removals_.size()>=32||batches_.contains(owner)||removals_.contains(owner))return {};
  try{
    auto prepared=std::unique_ptr<RemovalReservation>(new RemovalReservation);
    prepared->manager=this;prepared->alive=alive_;prepared->operation=owner;prepared->context=context;
    prepared->serial=serial_;prepared->generation=serial_+1;prepared->deadline=deadline;
    prepared->policy_owner=state->second.policy_owner;prepared->routes=state->second.routes;prepared->order=state->second.order;prepared->bytes=state->second.bytes;
    RemovalBatch batch;batch.generation=prepared->generation;batch.context=context;batch.deadline=deadline;batch.reply=std::move(done);
    for(const auto&[name,target]:targets_)if(target.context==context){
      RemovalReservation::Change change;change.generation=target.generation;change.busy=target.busy;
      change.queue=target.queue;change.claims=target.claims;change.before=target.removals;change.after=target.removals;
      for(const auto&id:prepared->order){const auto&route=prepared->routes.at(id);const auto claim=target.claims.find(id);
        if(claim==target.claims.end()||claim->second!=route.policy)continue;
        if(change.after.size()>=128)return {};
        change.after.push_back({route,owner,prepared->generation,deadline});batch.pending.insert({name,id});
      }
      if(change.after.size()!=change.before.size())prepared->owners.push_back(name);
      prepared->changes.emplace(name,std::move(change));
    }
    prepared->batch_node.emplace(owner,std::move(batch));return prepared;
  }catch(const std::bad_alloc&){return {};}
}
inline bool NativeContextHARRoutes::RemovalCurrent(const RemovalReservation&prepared)const{
  if(prepared.manager!=this||prepared.alive!=alive_||!*alive_||closed_||prepared.committed||prepared.serial!=serial_||
     (prepared.deadline&&now_&&now_()>=prepared.deadline)||batches_.size()+removals_.size()>=32||batches_.contains(prepared.operation)||removals_.contains(prepared.operation))return false;
  const auto state=states_.find(prepared.context);
  if(state==states_.end()||state->second.policy_owner!=prepared.policy_owner||state->second.owner||state->second.order!=prepared.order||state->second.bytes!=prepared.bytes||state->second.routes.size()!=prepared.routes.size())return false;
  for(const auto&[id,route]:prepared.routes){const auto found=state->second.routes.find(id);if(found==state->second.routes.end()||found->second.policy!=route.policy)return false;}
  size_t count=0;
  for(const auto&[owner,target]:targets_)if(target.context==prepared.context){
    ++count;const auto found=prepared.changes.find(owner);if(found==prepared.changes.end())return false;const auto&change=found->second;
    if(target.generation!=change.generation||target.busy!=change.busy||target.queue!=change.queue||target.claims!=change.claims||target.removals.size()!=change.before.size())return false;
    for(size_t index=0;index<target.removals.size();++index){const auto&a=target.removals[index];const auto&b=change.before[index];
      if(a.route.policy!=b.route.policy||a.route.id!=b.route.id||a.batch!=b.batch||a.generation!=b.generation||a.deadline!=b.deadline)return false;
    }
  }
  return count==prepared.changes.size();
}
inline bool NativeContextHARRoutes::CommitRemoval(RemovalReservation&prepared){
  // Only the friend two-family coordinator calls this, immediately after
  // validating BOTH tickets and without any callback or allocating operation.
  // Rechecking a moving clock here could split an otherwise atomic commit.
  if(prepared.manager!=this||prepared.committed)return false;
  auto&state=states_.at(prepared.context);
  for(const auto&[id,route]:prepared.routes){(void)id;ReleaseArchive(route.archive);}
  state.routes.clear();state.order.clear();state.bytes=0;state.owner=prepared.operation;
  for(auto&[owner,change]:prepared.changes)targets_.at(owner).removals.swap(change.after);
  removals_.insert(prepared.batch_node.extract(prepared.operation));serial_=prepared.generation;prepared.committed=true;return true;
}
inline void NativeContextHARRoutes::DispatchRemoval(RemovalReservation&prepared){
  if(prepared.manager!=this||prepared.alive!=alive_||!*alive_||!prepared.committed||prepared.dispatched)return;
  prepared.dispatched=true;const auto batch=removals_.find(prepared.operation);
  if(batch!=removals_.end()&&batch->second.generation==prepared.generation&&batch->second.pending.empty()){FinishRemoval(prepared.operation,prepared.generation,true,"");return;}
  auto alive=alive_;for(const auto&owner:prepared.owners){if(!*alive)break;Pump(owner);}
}
inline bool NativeContextHARRoutes::PumpRemoval(const std::string&owner){
  const auto found=targets_.find(owner);if(found==targets_.end()||found->second.removals.empty())return false;
  auto&target=found->second;auto task=std::move(target.removals.front());target.removals.pop_front();target.busy=true;
  const auto generation=target.generation;const int context=target.context;auto apply=target.apply;
  Call(owner,context,std::move(apply),Mutation::Remove,task.route,task.deadline,[this,alive=alive_,owner,generation,task](bool ok,const std::string&error){
    if(!*alive)return;
    const auto target=targets_.find(owner);if(target==targets_.end()||target->second.generation!=generation)return;
    target->second.busy=false;const auto claim=target->second.claims.find(task.route.id);
    if(claim!=target->second.claims.end()&&claim->second==task.route.policy)target->second.claims.erase(claim);
    const auto batch=removals_.find(task.batch);
    if(batch!=removals_.end()&&batch->second.generation==task.generation){batch->second.pending.erase({owner,task.route.id});if(!ok||batch->second.pending.empty())FinishRemoval(task.batch,task.generation,ok,error);}
    if(!*alive)return;
    const auto current=targets_.find(owner);if(current==targets_.end()||current->second.generation!=generation)return;
    if(!ok){auto ready=std::move(current->second.ready);if(ready)ready(false,error);else failure_(owner,error);return;}
    Pump(owner);
  });return true;
}
inline void NativeContextHARRoutes::FinishRemoval(int owner,uint64_t generation,bool ok,const std::string&error){
  const auto found=removals_.find(owner);if(found==removals_.end()||found->second.generation!=generation)return;
  auto batch=std::move(found->second);removals_.erase(found);
  const auto state=states_.find(batch.context);if(state!=states_.end()&&state->second.owner==owner)state->second.owner=0;
  if(batch.deadline&&now_&&now_()>=batch.deadline){ok=false;batch.reply(false,"Native context HAR removal original deadline exceeded");return;}
  batch.reply(ok,error);
}
