// SPDX-License-Identifier: MIT
// Included after NativeContextRoutes, inside namespace suitecut. A prepared
// removal owns all allocations, but has not deactivated even one real policy.
class NativeContextRoutes::RemovalReservation {
 public:
  RemovalReservation(const RemovalReservation&)=delete;
  RemovalReservation&operator=(const RemovalReservation&)=delete;
 private:
  friend class NativeContextRoutes;
  RemovalReservation()=default;
  struct Change {
    uint64_t generation=0;bool busy=false;
    std::deque<Task> before,after;
    std::map<std::string,Route> claims;
  };
  const NativeContextRoutes*manager=nullptr;
  std::shared_ptr<bool>alive,policy_owner;
  int operation=0,context=0;uint64_t serial=0,generation=0;
  double deadline=0;bool committed=false,dispatched=false;
  std::map<std::string,Route>routes;
  std::vector<std::string>order,owners;
  size_t bytes=0;
  std::map<std::string,Change>changes;
  std::map<int,Batch>batch_node;
};
inline std::unique_ptr<NativeContextRoutes::RemovalReservation> NativeContextRoutes::PrepareRemoveAll(int operation,int context,double deadline,Result reply){
  const auto state=states_.find(context);
  if(!Admit(operation,state,deadline,reply)||!Room(context,state->second.routes.size()))return {};
  try{
    auto prepared=std::unique_ptr<RemovalReservation>(new RemovalReservation);
    prepared->manager=this;prepared->alive=alive_;prepared->operation=operation;prepared->context=context;
    prepared->serial=serial_;prepared->generation=serial_+1;prepared->deadline=deadline;
    prepared->policy_owner=state->second.policy_owner;prepared->routes=state->second.routes;prepared->order=state->second.order;prepared->bytes=state->second.bytes;
    Batch batch;batch.generation=prepared->generation;batch.context=context;batch.mutation=Mutation::Remove;batch.deadline=deadline;batch.reply=std::move(reply);
    for(const auto&[id,route]:prepared->routes){(void)route;batch.ids.insert(id);}
    for(const auto&[owner,target]:targets_)if(target.context==context){
      RemovalReservation::Change change;change.generation=target.generation;change.busy=target.busy;
      change.before=target.queue;change.after=target.queue;change.claims=target.claims;
      for(const auto&id:prepared->order){const auto&route=prepared->routes.at(id);if(!Claims(target,route))continue;
        change.after.push_back({Mutation::Remove,route,operation,deadline,prepared->generation});batch.pending.insert({owner,id});
      }
      if(change.after.size()!=change.before.size())prepared->owners.push_back(owner);
      prepared->changes.emplace(owner,std::move(change));
    }
    prepared->batch_node.emplace(operation,std::move(batch));
    return prepared;
  }catch(const std::bad_alloc&){return {};}
}
inline bool NativeContextRoutes::RemovalCurrent(const RemovalReservation&prepared)const{
  if(prepared.manager!=this||prepared.alive!=alive_||!*alive_||closed_||prepared.committed||prepared.serial!=serial_||
     (prepared.deadline&&now_()>=prepared.deadline)||batches_.size()>=32||batches_.contains(prepared.operation)||waits_.Contains(prepared.operation))return false;
  const auto state=states_.find(prepared.context);
  if(state==states_.end()||state->second.policy_owner!=prepared.policy_owner||state->second.operation||state->second.routes!=prepared.routes||state->second.order!=prepared.order||state->second.bytes!=prepared.bytes)return false;
  size_t count=0;
  for(const auto&[owner,target]:targets_)if(target.context==prepared.context){
    ++count;const auto found=prepared.changes.find(owner);if(found==prepared.changes.end())return false;
    const auto&change=found->second;
    if(target.generation!=change.generation||target.busy!=change.busy||target.claims!=change.claims||target.queue.size()!=change.before.size())return false;
    for(size_t index=0;index<target.queue.size();++index){const auto&a=target.queue[index];const auto&b=change.before[index];
      if(a.mutation!=b.mutation||a.route!=b.route||a.batch!=b.batch||a.deadline!=b.deadline||a.generation!=b.generation)return false;
    }
  }
  return count==prepared.changes.size();
}
inline bool NativeContextRoutes::CommitRemoval(RemovalReservation&prepared){
  // Only the friend two-family coordinator calls this, immediately after
  // validating BOTH tickets and without any callback or allocating operation.
  // Rechecking a moving clock here could split an otherwise atomic commit.
  if(prepared.manager!=this||prepared.committed)return false;
  auto&state=states_.at(prepared.context);
  for(const auto&[id,route]:prepared.routes){(void)id;route->Remove();}
  state.routes.clear();state.order.clear();state.bytes=0;state.operation=prepared.operation;
  for(auto&[owner,change]:prepared.changes)targets_.at(owner).queue.swap(change.after);
  batches_.insert(prepared.batch_node.extract(prepared.operation));serial_=prepared.generation;prepared.committed=true;
  return true;
}
inline void NativeContextRoutes::DispatchRemoval(RemovalReservation&prepared){
  if(prepared.manager!=this||prepared.alive!=alive_||!*alive_||!prepared.committed||prepared.dispatched)return;
  prepared.dispatched=true;const auto batch=batches_.find(prepared.operation);
  if(batch!=batches_.end()&&batch->second.generation==prepared.generation){Start(prepared.operation,prepared.owners);return;}
  auto alive=alive_;for(const auto&owner:prepared.owners){if(!*alive)break;Pump(owner);}
}
