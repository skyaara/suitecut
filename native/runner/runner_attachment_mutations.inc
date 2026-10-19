// SPDX-License-Identifier: MIT
// Included in RunnerAttachments private scope. Native ledgers supply every
// reference and publication ticket; host packets can only propose visibility.
using MutationKey=std::tuple<uint64_t,uint64_t,unsigned,int64_t>;
static MutationKey MutationAttempt(const StepOwner& owner){return {owner.test_id,owner.attempt_generation,owner.worker_index,owner.pid};}
AttachmentMutations* MutationFind(const StepOwner& owner)const{
  const auto found=mutation_journals_.find(MutationAttempt(owner));
  return found==mutation_journals_.end()?nullptr:found->second.get();
}
AttachmentMutations& MutationJournal(const StepOwner& owner,double now){
  if(auto* journal=MutationFind(owner))return *journal;
  auto next=std::make_unique<AttachmentMutations>(owner,budget_?&budget_->mutations:&mutation_budget_);
  for(const auto& [key,callback]:callbacks_){(void)key;
    if(next->SameAttempt(callback.owner)&&!callback.exited){
      next->Open(callback.owner,callback.deadline,callback.opened);
      if(callback.closed)next->Close(callback.owner);
    }
  }
  (void)now;
  auto* actual=next.get();mutation_journals_.emplace(MutationAttempt(owner),std::move(next));return *actual;
}
void MutationOpen(const StepOwner& owner,double deadline,double now){if(auto* journal=MutationFind(owner))journal->Open(owner,deadline,now);}
void MutationClose(const StepOwner& owner){if(auto* journal=MutationFind(owner))journal->Close(owner);}
void MutationAmend(const StepOwner& owner,double deadline,double now){if(auto* journal=MutationFind(owner))journal->Amend(owner,deadline,now);}
void MutationRetire(const StepOwner& owner){if(auto* journal=MutationFind(owner))journal->Retire(owner);}
void MutationFailure(const StepOwner& owner,uint64_t step,const std::string& error_json){
  const auto callback=callbacks_.find(Key(owner));
  if(callback==callbacks_.end()||callback->second.owner!=owner)
    throw std::runtime_error("Native attachment mutation failure lost its actual callback");
  if(!callback->second.mutation_error)callback->second.mutation_error=error_json;
  if(step)for(auto& [id,fence]:step_fences_)if(fence.owner==owner&&Within(step,id)&&!fence.error)fence.error=error_json;
}
AttachmentViewReceipts& MutationReceipts(){
  if(!mutation_receipts_)mutation_receipts_=std::make_unique<AttachmentViewReceipts>(AttachmentViewReceipts::Hooks{
    [this](const auto& record){MutationFailure(record.owner,record.step_id,MessageJSON("Native attachment publication-view original deadline expired"));},
    [this](const auto& record){
      if(hooks_.close_owner)hooks_.close_owner(record.owner,"Native attachment publication-view ACK cleanup deadline expired");
      else hooks_.failure("Native attachment publication-view close hook is unavailable; receipt retained");
    }},budget_?&budget_->publications:&mutation_publication_budget_);
  return *mutation_receipts_;
}
bool MutationPending(const StepOwner& owner)const{return mutation_receipts_&&mutation_receipts_->PendingOwner(owner);}
bool MutationPendingStep(const StepOwner& owner,uint64_t id)const{
  return mutation_receipts_&&mutation_receipts_->PendingStep(owner,id,[this](uint64_t child,uint64_t parent){return Within(child,parent);});
}
std::vector<AttachmentMutations::Slot> MutationSlots(AttachmentMutations& journal,const std::string& wire){
  std::vector<AttachmentMutations::Slot> slots;if(wire=="-")return slots;
  if(wire.size()>8192)throw std::runtime_error("Native attachment mutation slot packet exceeds capacity");
  size_t start=0;
  for(;;){
    if(slots.size()>=AttachmentMutations::kSlots)throw std::runtime_error("Native attachment mutation slot count exceeds capacity");
    const auto end=wire.find(',',start);const auto part=wire.substr(start,end==std::string::npos?end:end-start);
    if(part=="h")slots.push_back(std::nullopt);
    else{
      if(part.size()<2||(part[0]!='d'&&part[0]!='a'))throw std::runtime_error("Invalid native attachment mutation reference");
      const auto id=attachment_files::UInt(part.substr(1));if(!id)throw std::runtime_error("Empty native attachment mutation reference");
      AttachmentMutations::Reference reference;
      if(part[0]=='d'){
        const AttachmentDeclarations::Record* actual=nullptr;
        for(const auto& record:declarations_.Records())if(record.request==id&&journal.SameAttempt(record.owner)){
          if(actual)throw std::runtime_error("Native attachment declaration request identity is ambiguous");
          actual=&record;
        }
        if(!actual)throw std::runtime_error("Native attachment mutation has no actual declaration request");
        reference={AttachmentMutations::Kind::Declaration,actual->id};
        if(!journal.Find(reference))journal.AdmitDeclaration(declarations_,actual->id);
      }else{
        reference={AttachmentMutations::Kind::Artifact,id};
        if(!journal.Find(reference))journal.AdmitArtifact(records_,id);
      }
      slots.push_back(reference);
    }
    if(end==std::string::npos)break;
    start=end+1;
  }
  return slots;
}
bool MutationPacket(const std::vector<std::string>& fields,double now,bool stopped){
  if(fields.empty()||fields[0]!="ATTACH_MUTATE")return false;
  if(fields.size()!=10&&fields.size()!=13)throw std::runtime_error("Invalid native attachment mutation packet");
  const auto request=attachment_files::UInt(fields[1]),token=attachment_files::UInt(fields[2]),scope=attachment_files::UInt(fields[3]),
             generation=attachment_files::UInt(fields[4]),expected=attachment_files::UInt(fields[5]),step=attachment_files::UInt(fields[6]),ticket=attachment_files::UInt(fields[8]);
  if(!request||nonce_==UINT64_MAX||request!=++nonce_)throw std::runtime_error("Native attachment mutation request nonce is stale");
  const auto callback=callbacks_.find(Key(token,scope,generation));
  if(callback==callbacks_.end()||callback->second.exited)throw std::runtime_error("Native attachment mutation has no actual callback");
  const auto owner=callback->second.owner;
  const bool publication=fields[7]=="P";
  if((fields[7]!="S"&&!publication)||(publication?(!ticket||step):ticket!=0))throw std::runtime_error("Invalid native attachment mutation mode");
  uint64_t artifact=0;
  if(publication){
    artifact=attachment_files::UInt(fields[9]);
    if(!mutation_receipts_)throw std::runtime_error("Native attachment publication has no issued view ticket");
    (void)mutation_receipts_->Pending(ticket,owner,artifact);
  }
  const auto retry=ReadRetry(fields,10,owner,step,now,publication);
  if(publication&&mutation_receipts_->Pending(ticket,owner,artifact).callback_retry!=retry)throw std::runtime_error("Native attachment publication changed its admitted retry identity");
  auto& journal=MutationJournal(owner,now);std::optional<std::string> error;uint64_t observed=journal.Revision();
  const auto* actual_step=step&&hooks_.step?hooks_.step(step):nullptr;
  try{
    if(stopped&&!publication)throw std::runtime_error("Native attachment mutation admission stopped");
    if(publication)observed=journal.Publish(owner,request,expected,records_,artifact,mutation_receipts_->Pending(ticket,owner,artifact).deadline,now);
    else{
      if(step&&!actual_step)throw std::runtime_error("Native attachment mutation step is unadmitted");
      const auto slots=MutationSlots(journal,fields[9]);
      observed=journal.Apply(owner,request,expected,slots,now,actual_step);
    }
  }catch(const std::exception& failure){
    const auto original_step=publication?mutation_receipts_->Pending(ticket,owner,artifact).step_id:actual_step&&actual_step->owner==owner?actual_step->id:0;
    error=MessageJSON(failure.what());journal.Reject(owner,request,expected,*error,now,original_step);
    MutationFailure(owner,original_step,*error);RetryFailure(owner,retry,*error,true);
  }
  if(publication)mutation_receipts_->Acknowledge(ticket,owner,artifact,request,!error,now);
  hooks_.send("ATTACH_MUTATION_RESULT "+fields[1]+" "+fields[2]+" "+fields[3]+" "+fields[4]+" "+std::to_string(observed)+" "+(error?"0 "+StepWire(*error)+(retry.Empty()?"":" I"):"1 -"));
  DrainSteps();Drain(now);return true;
}
// Fallback budgets outlive the journals whose destructors debit metadata.
// Production Runner supplies the separately owned shared run budgets.
AttachmentMutations::Budget mutation_budget_;
AttachmentViewReceipts::Budget mutation_publication_budget_;
std::map<MutationKey,std::unique_ptr<AttachmentMutations>> mutation_journals_;
std::unique_ptr<AttachmentViewReceipts> mutation_receipts_;
