// SPDX-License-Identifier: MIT
#pragma once
#include "test_attachments.h"
#include <map>
#include <set>
#include <tuple>
#include <utility>
namespace suitecut::runner {
// Metadata ledger only. RunnerAttachments remains the owner of actual processes,
// file bytes, cancellation and writer/rollback fences. Its real callback ingress
// supplies Open/Close/Amend; a metadata retirement is never a process receipt.
class AttachmentDeclarations {
 public:
  enum class Source { Absent, Body, PathReference };
  enum class Receipt { Pending, ReferenceAccepted, Committed, Failed, Canceled, Rejected };
  struct Record {
    uint64_t id=0,request=0;StepOwner owner;
    std::string name_json,type_json;Source source=Source::Absent;
    std::optional<std::string> author_path_json;
    std::optional<uint64_t> expected_bytes;
    double declared=0;std::optional<double> received;
    Receipt receipt=Receipt::Pending;
    uint64_t artifact_id=0,cancellation_request=0;
    std::string error_json;
    AttachmentRetryProvenance callback_retry{};
    bool retry_failure_disposed=false;
  };
  struct Budget {uint64_t sequence=0;};
  explicit AttachmentDeclarations(Budget* budget=nullptr):budget_(budget){}
  void Open(const StepOwner& owner,double deadline,double now){
    Time(now);if(!Valid(owner)||!std::isfinite(deadline)||deadline<=now||contexts_.contains(Key(owner)))throw std::invalid_argument("Native append callback context rejected");
    contexts_.emplace(Key(owner),Context{owner,deadline,true});
  }
  void Close(const StepOwner& owner){auto& context=Owned(owner);context.open=false;}
  void Amend(const StepOwner& owner,double deadline,double now){
    Time(now);auto& context=Owned(owner);if(!context.open||now>=context.deadline||!std::isfinite(deadline)||deadline<0)throw std::invalid_argument("Native append callback amendment rejected");context.deadline=deadline;
  }
  uint64_t Declare(const StepOwner& owner,uint64_t request,const std::string& name_json,const std::string& type_json,
                   Source source,const std::optional<std::string>& path,const std::optional<uint64_t>& bytes,double now,const std::optional<std::string>& rejection=std::nullopt,const AttachmentRetryProvenance& callback_retry={}){
    Time(now);auto& context=Owned(owner);
    if(!callback_retry.Empty()&&!callback_retry.Complete())throw std::invalid_argument("Native append retry identity is incomplete");
    if(((!context.open||now>=context.deadline)&&!rejection)||!request||!JsonString(name_json)||!JsonString(type_json)||
       (source!=Source::Absent&&source!=Source::Body&&source!=Source::PathReference)||
       (source==Source::PathReference&&!path)||(source==Source::Absent&&path)||(source==Source::Body)!=bytes.has_value()||
       (path&&!JsonString(*path))||(rejection&&!JsonString(*rejection)))throw std::invalid_argument("Native append declaration metadata or original deadline rejected");
    if(requests_.contains({request,owner.worker_index,owner.pid}))throw std::invalid_argument("Native append request already belongs to this actual worker");
    if(sequence_==UINT64_MAX||(budget_&&budget_->sequence==UINT64_MAX))throw std::runtime_error("Native append declaration sequence exhausted");
    Record record;record.id=budget_?budget_->sequence+1:sequence_+1;record.request=request;record.owner=owner;record.name_json=name_json;record.type_json=type_json;record.source=source;record.author_path_json=path;record.expected_bytes=bytes;record.declared=now;record.callback_retry=callback_retry;
    if(rejection){record.receipt=Receipt::Rejected;record.received=now;record.error_json=*rejection;}else if(source!=Source::Body){record.receipt=Receipt::ReferenceAccepted;record.received=now;}
    const bool pending=record.receipt==Receipt::Pending,failed=record.receipt==Receipt::Rejected;records_.push_back(std::move(record));const auto id=records_.back().id;
    try{indices_.emplace(id,records_.size()-1);requests_.insert({request,owner.worker_index,owner.pid});if(pending)++pending_[PendingKey(owner)];else if(failed)failures_.insert(PendingKey(owner));}
    catch(...){indices_.erase(id);requests_.erase({request,owner.worker_index,owner.pid});records_.pop_back();throw;}
    ++sequence_;if(budget_)++budget_->sequence;
    return id;
  }
  void BindArtifact(const StepOwner& owner,uint64_t id,uint64_t artifact){
    auto& record=Pending(owner,id);if(artifact&&artifacts_.contains(artifact))throw std::invalid_argument("Native append artifact already belongs to a declaration");if(!artifact||record.artifact_id)throw std::invalid_argument("Native append artifact admission is duplicate or empty");artifacts_.insert(artifact);record.artifact_id=artifact;
  }
  void Committed(const StepOwner& owner,uint64_t id,const AttachmentRecord& artifact){
    ValidateNativeAttachment(artifact);auto& record=Pending(owner,id);
    if(!record.artifact_id||record.artifact_id!=artifact.id||artifact.request!=record.request||artifact.owner!=owner||
       artifact.callback_retry!=record.callback_retry||artifact.name_json!=record.name_json||artifact.content_type_json!=record.type_json||artifact.source_kind=="path"||
       artifact.bytes!=*record.expected_bytes||artifact.started<record.declared)throw std::invalid_argument("Native append publication has no exact committed body artifact");
    Settle(record,Receipt::Committed);record.received=artifact.received;
  }
  void Failed(const StepOwner& owner,uint64_t id,const std::string& error_json,double now,bool retry_failure_disposed=false){
    Time(now);auto& record=Pending(owner,id);if((retry_failure_disposed&&!record.callback_retry.Complete())||now<record.declared||!JsonString(error_json))throw std::invalid_argument("Native append failure receipt rejected");
    if(!retry_failure_disposed)failures_.insert(PendingKey(owner));
    record.error_json=error_json;record.received=now;record.retry_failure_disposed=retry_failure_disposed;Settle(record,Receipt::Failed);
  }
  void Canceled(const StepOwner& owner,uint64_t id,uint64_t actual_request,double now){
    Time(now);auto& record=Pending(owner,id);if(!actual_request||now<record.declared)throw std::invalid_argument("Native append cancellation has no accepted request");record.cancellation_request=actual_request;record.received=now;Settle(record,Receipt::Canceled);
  }
  void RetireContext(const StepOwner& owner){
    (void)Owned(owner);if(pending_.contains(PendingKey(owner)))throw std::runtime_error("Native append context still has pending declaration metadata");contexts_.erase(Key(owner));
  }
  // A Failed (not retry-disposed) or Rejected declaration of this exact owner.
  bool HasUndisposedFailure(const StepOwner& owner)const{return failures_.contains(PendingKey(owner));}
  const Record* Find(uint64_t id)const{const auto found=indices_.find(id);return found==indices_.end()?nullptr:&records_[found->second];}
  const std::vector<Record>& Records()const{return records_;}
 private:
  struct Context {StepOwner owner;double deadline;bool open;};
  static void Time(double now){if(!std::isfinite(now)||now<0)throw std::invalid_argument("Invalid native append observation time");}
  static bool Valid(const StepOwner& owner){return owner.test_id&&owner.attempt_generation&&owner.scope&&owner.scope_generation&&owner.worker_index&&owner.pid>0&&owner.pid<=INT32_MAX&&(owner.callback_kind=="body"||owner.callback_kind=="beforeEach"||owner.callback_kind=="afterEach"||owner.callback_kind=="beforeAll"||owner.callback_kind=="afterAll")&&(owner.callback_kind=="body"||owner.callback_token);}
  static std::string Key(const StepOwner& owner){return std::to_string(owner.callback_token)+":"+std::to_string(owner.scope)+":"+std::to_string(owner.scope_generation);}
  Context& Owned(const StepOwner& owner){const auto found=contexts_.find(Key(owner));if(found==contexts_.end()||found->second.owner!=owner)throw std::invalid_argument("Native append callback context is stale or foreign");return found->second;}
  Record& Pending(const StepOwner& owner,uint64_t id){const auto found=indices_.find(id);if(found==indices_.end())throw std::invalid_argument("Native append body receipt has no declaration");auto& record=records_[found->second];if(record.owner!=owner||record.source!=Source::Body||record.receipt!=Receipt::Pending)throw std::invalid_argument("Native append body receipt is foreign, stale or duplicate");return record;}
  // Exact owner identity (StepOwner::Key omits callback_kind) of pending metadata.
  using OwnerKey=std::pair<decltype(StepOwner{}.Key()),std::string>;
  static OwnerKey PendingKey(const StepOwner& owner){return {owner.Key(),owner.callback_kind};}
  void Settle(Record& record,Receipt receipt){record.receipt=receipt;const auto found=pending_.find(PendingKey(record.owner));if(found!=pending_.end()&&!--found->second)pending_.erase(found);}
  Budget* budget_;std::map<std::string,Context> contexts_;std::vector<Record> records_;uint64_t sequence_=0;
  // Run-lifetime ledger indexes: id, (request, worker, pid), bound artifacts, pending owners,
  // owners with an undisposed failure.
  std::map<uint64_t,size_t> indices_;std::set<std::tuple<uint64_t,unsigned,int64_t>> requests_;std::set<uint64_t> artifacts_;std::map<OwnerKey,size_t> pending_;std::set<OwnerKey> failures_;
};
} // namespace suitecut::runner
