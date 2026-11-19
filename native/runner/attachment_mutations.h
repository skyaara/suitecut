// SPDX-License-Identifier: MIT
#pragma once
#include "attachment_declarations.h"
#include <type_traits>

namespace suitecut::runner {
// Accepted declaration visibility only. This object has no process/file/job
// callbacks, so withdrawal cannot impersonate a physical completion receipt.
class AttachmentMutations {
 public:
  enum class Kind { Declaration, Artifact };
  struct Reference {
    Kind kind=Kind::Declaration;uint64_t id=0;
    bool operator==(const Reference&)const=default;
  };
  using Slot=std::optional<Reference>;
  struct Entry {Reference reference;uint64_t request=0;StepOwner owner;uint64_t step_id=0;};
  struct Change {
    uint64_t revision=0,request=0;StepOwner owner;uint64_t step_id=0;
    double accepted=0;std::vector<Slot> slots;uint64_t publication_artifact=0;
  };
  struct Rejection {
    uint64_t request=0,expected_revision=0,observed_revision=0;StepOwner owner;
    uint64_t step_id=0;double received=0;std::string error_json;
  };
  explicit AttachmentMutations(const StepOwner& actual):attempt_(actual){ValidateOwner(actual);}
  AttachmentMutations(const AttachmentMutations&)=delete;
  AttachmentMutations& operator=(const AttachmentMutations&)=delete;
  AttachmentMutations(AttachmentMutations&&)=delete;
  AttachmentMutations& operator=(AttachmentMutations&&)=delete;

  void Open(const StepOwner& actual,double deadline,double now){
    Time(now);ValidateAttempt(actual);
    if(!std::isfinite(deadline)||deadline<=now||FindContext(actual))
      throw std::invalid_argument("Native attachment mutation context rejected");
    Context next{actual,deadline,now,true,false};
    contexts_.push_back(std::move(next));
  }
  void Amend(const StepOwner& actual,double deadline,double now){
    auto& context=Current(actual,now);
    if(!std::isfinite(deadline)||deadline<0)throw std::invalid_argument("Native attachment mutation deadline rejected");
    context.deadline=deadline;
  }
  void Close(const StepOwner& actual){Owned(actual).open=false;}
  void Retire(const StepOwner& actual){
    auto& context=Owned(actual);
    if(context.open)throw std::invalid_argument("Native attachment mutation callback is still open");
    // Keep an owner tombstone: retiring cannot reopen this exact callback.
    // No writer, failure, artifact, or immutable declaration is retired here.
    context.retired=true;
  }

  Reference AdmitDeclaration(const AttachmentDeclarations& native,uint64_t id){
    const auto* record=native.Find(id);
    if(!record||record->receipt==AttachmentDeclarations::Receipt::Rejected)
      throw std::invalid_argument("Native attachment mutation reference has no accepted declaration");
    return Admit({{Kind::Declaration,id},record->request,record->owner,0});
  }
  Reference AdmitArtifact(const std::vector<AttachmentRecord>& native,uint64_t id){
    for(const auto& record:native)if(record.id==id){
      ValidateNativeAttachment(record);
      return Admit({{Kind::Artifact,id},record.request,record.owner,record.step_id});
    }
    throw std::invalid_argument("Native attachment mutation reference has no committed artifact");
  }

  // The host may propose a complete sparse array, but only this native ingress
  // can accept its order. Registering a native reference alone never publishes
  // it in the visible projection. Actual append/publication uses Apply as well.
  uint64_t Apply(const StepOwner& actual,uint64_t request,uint64_t expected_revision,
                 const std::vector<Slot>& proposed,double now,const StepRecord* actual_step=nullptr){
    return Commit(actual,request,expected_revision,proposed,now,actual_step,nullptr);
  }
  uint64_t Publish(const StepOwner& actual,uint64_t request,uint64_t expected_revision,
                   const std::vector<AttachmentRecord>& native,uint64_t artifact_id,double original_deadline,double now){
    const AttachmentRecord* artifact=nullptr;
    for(const auto& record:native)if(record.id==artifact_id){artifact=&record;break;}
    if(!artifact||artifact->owner!=actual)
      throw std::invalid_argument("Native attachment publication view has no actual owned artifact");
    ValidateNativeAttachment(*artifact);
    if(!std::isfinite(original_deadline)||now>=original_deadline||artifact->received>=original_deadline)
      throw std::invalid_argument("Native attachment publication view ticket original deadline expired");
    for(const auto& change:changes_)if(change.publication_artifact==artifact_id)
      throw std::invalid_argument("Native attachment publication view receipt is duplicate");
    const Reference reference{Kind::Artifact,artifact_id};
    if(!Find(reference))AdmitArtifact(native,artifact_id);
    auto proposed=Projection();proposed.push_back(reference);
    return Commit(actual,request,expected_revision,proposed,now,nullptr,artifact);
  }
  // Native packet handling records a genuine admission failure even when the
  // author's synchronous array operation has no Promise to observe. This is
  // never an accepted projection, writer cancellation or replacement Error.
  void Reject(const StepOwner& actual,uint64_t request,uint64_t expected_revision,
              const std::string& error_json,double now,uint64_t actual_step_id=0){
    Time(now);const auto& context=Owned(actual);
    if(!request||!JsonString(error_json))
      throw std::invalid_argument("Native attachment mutation rejection receipt rejected");
    if(now<context.opened||(!changes_.empty()&&now<changes_.back().accepted))
      throw std::invalid_argument("Native attachment mutation rejection time precedes admission");
    for(const auto& old:changes_)if(old.request==request)
      throw std::invalid_argument("Native attachment mutation request replay rejected");
    for(const auto& old:rejections_)if(old.request==request)
      throw std::invalid_argument("Native attachment mutation request replay rejected");
    Rejection next{request,expected_revision,Revision(),actual,actual_step_id,now,error_json};
    static_assert(std::is_nothrow_move_constructible_v<Rejection>);
    rejections_.push_back(std::move(next));
  }
  uint64_t Revision()const{return changes_.empty()?0:changes_.back().revision;}
  const std::vector<Slot>& Projection()const{
    static const std::vector<Slot> empty;
    return changes_.empty()?empty:changes_.back().slots;
  }
  const std::vector<Change>& Changes()const{return changes_;}
  const std::vector<Rejection>& Rejections()const{return rejections_;}
  const std::vector<Entry>& References()const{return entries_;}
  const StepOwner& AttemptOwner()const{return attempt_;}
  bool PublishedArtifact(uint64_t id)const{
    for(const auto& change:changes_)if(change.publication_artifact==id)return true;
    return false;
  }
  bool SameAttempt(const StepOwner& owner)const{
    return owner.test_id==attempt_.test_id&&owner.attempt_generation==attempt_.attempt_generation&&
           owner.worker_index==attempt_.worker_index&&owner.pid==attempt_.pid;
  }
  const Entry* Find(Reference reference)const{
    for(const auto& entry:entries_)if(entry.reference==reference)return &entry;
    return nullptr;
  }
 private:
  uint64_t Commit(const StepOwner& actual,uint64_t request,uint64_t expected_revision,
                 const std::vector<Slot>& proposed,double now,const StepRecord* actual_step,const AttachmentRecord* publication){
    if(publication){
      Time(now);const auto& context=Owned(actual);
      if(now<context.opened||now>=context.deadline||publication->received>now||publication->received>=context.deadline||
         (!changes_.empty()&&now<changes_.back().accepted))
        throw std::invalid_argument("Native attachment publication view original deadline expired");
    }else (void)Current(actual,now);
    if(!request||expected_revision!=Revision()||Revision()==UINT64_MAX)
      throw std::invalid_argument("Native attachment mutation revision rejected");
    for(const auto& old:changes_)if(old.request==request)
      throw std::invalid_argument("Native attachment mutation request replay rejected");
    for(const auto& old:rejections_)if(old.request==request)
      throw std::invalid_argument("Native attachment mutation request replay rejected");
    if(actual_step&&(actual_step->owner!=actual||!actual_step->id||
       actual_step->receipt!=StepReceipt::Pending||actual_step->status!=StepStatus::Running||
       actual_step->callback_received||!std::isfinite(actual_step->deadline)||now>=actual_step->deadline))
      throw std::invalid_argument("Native attachment mutation step is stale or foreign");
    for(const auto& slot:proposed)if(slot){
      if(!Find(*slot))throw std::invalid_argument("Native attachment mutation contains an unadmitted reference");
      if(slot->kind==Kind::Artifact&&!(publication&&publication->id==slot->id)&&!PublishedArtifact(slot->id))
        throw std::invalid_argument("Native attachment mutation artifact has no accepted publication view");
    }
    // Every validation precedes commit; push_back leaves the history unchanged if it throws.
    Change next{Revision()+1,request,actual,publication?publication->step_id:actual_step?actual_step->id:0,now,proposed,publication?publication->id:0};
    static_assert(std::is_nothrow_move_constructible_v<Change>);
    changes_.push_back(std::move(next));
    return Revision();
  }
  struct Context {StepOwner owner;double deadline=0,opened=0;bool open=false,retired=false;};
  static void Time(double now){if(!std::isfinite(now)||now<0)throw std::invalid_argument("Invalid native attachment mutation time");}
  static void ValidateOwner(const StepOwner& owner){
    if(!owner.test_id||!owner.attempt_generation||!owner.scope||!owner.scope_generation||
       !owner.worker_index||owner.pid<=0||owner.pid>INT32_MAX||
       (owner.callback_kind!="body"&&owner.callback_kind!="beforeEach"&&owner.callback_kind!="afterEach"&&
        owner.callback_kind!="beforeAll"&&owner.callback_kind!="afterAll")||
       (owner.callback_kind!="body"&&!owner.callback_token))
      throw std::invalid_argument("Invalid native attachment mutation owner");
  }
  void ValidateAttempt(const StepOwner& owner)const{
    ValidateOwner(owner);
    if(!SameAttempt(owner))
      throw std::invalid_argument("Native attachment mutation belongs to another actual attempt");
  }
  Context* FindContext(const StepOwner& owner){for(auto& context:contexts_)if(context.owner==owner)return &context;return nullptr;}
  Context& Owned(const StepOwner& owner){
    ValidateAttempt(owner);auto* context=FindContext(owner);
    if(!context||context->retired)throw std::invalid_argument("Native attachment mutation callback is stale or foreign");
    return *context;
  }
  Context& Current(const StepOwner& owner,double now){
    Time(now);auto& context=Owned(owner);
    if(!context.open||now<context.opened||now>=context.deadline||
       (!changes_.empty()&&now<changes_.back().accepted))
      throw std::invalid_argument("Native attachment mutation original deadline expired or callback closed");
    return context;
  }
  Reference Admit(Entry next){
    ValidateAttempt(next.owner);
    if(!next.reference.id||!next.request||Find(next.reference))
      throw std::invalid_argument("Native attachment mutation admission is duplicate");
    const auto reference=next.reference;entries_.push_back(std::move(next));return reference;
  }
  StepOwner attempt_;
  std::vector<Entry> entries_;std::vector<Context> contexts_;std::vector<Change> changes_;std::vector<Rejection> rejections_;
};
} // namespace suitecut::runner
