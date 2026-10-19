// SPDX-License-Identifier: MIT
#pragma once
#include "attachment_mutations.h"
#include "attachment_callback_report.h"
#include "attachment_view_receipts.h"
#include <set>
#include <charconv>

namespace suitecut::runner {
struct AttachmentProjectionReport {
  StepOwner owner;
  std::vector<AttachmentMutations::Entry> references;
  std::vector<AttachmentMutations::Change> changes;
  std::vector<AttachmentMutations::Rejection> rejections;
  std::vector<AttachmentViewReceipts::Record> publications;
};
inline bool SameAttachmentAttempt(const StepOwner& left,const StepOwner& right){
  return left.test_id==right.test_id&&left.attempt_generation==right.attempt_generation&&
         left.worker_index==right.worker_index&&left.pid==right.pid;
}
inline AttachmentProjectionReport CaptureAttachmentProjection(const AttachmentMutations& native,const std::vector<AttachmentViewReceipts::Record>& receipts){
  AttachmentProjectionReport result{native.AttemptOwner(),native.References(),native.Changes(),native.Rejections(),{}};
  for(const auto& receipt:receipts)if(SameAttachmentAttempt(result.owner,receipt.owner))result.publications.push_back(receipt);
  return result;
}
inline bool ValidAttachmentProjectionOwner(const StepOwner& owner){
  return owner.test_id&&owner.attempt_generation&&owner.worker_index&&owner.pid>0&&owner.pid<=INT32_MAX&&
         owner.scope&&owner.scope_generation&&
         (owner.callback_kind=="body"||((owner.callback_kind=="beforeEach"||owner.callback_kind=="afterEach"||
          owner.callback_kind=="beforeAll"||owner.callback_kind=="afterAll")&&owner.callback_token));
}
inline size_t AttachmentProjectionMetadata(const AttachmentProjectionReport& view){
  size_t bytes=sizeof(view)+view.owner.callback_kind.capacity()+1+
    view.references.capacity()*sizeof(AttachmentMutations::Entry)+
    view.changes.capacity()*sizeof(AttachmentMutations::Change)+
    view.rejections.capacity()*sizeof(AttachmentMutations::Rejection)+view.publications.capacity()*sizeof(AttachmentViewReceipts::Record);
  for(const auto& entry:view.references)bytes+=entry.owner.callback_kind.capacity()+1;
  for(const auto& change:view.changes)bytes+=change.owner.callback_kind.capacity()+1+change.slots.capacity()*sizeof(AttachmentMutations::Slot);
  for(const auto& rejection:view.rejections)bytes+=rejection.owner.callback_kind.capacity()+rejection.error_json.capacity()+2;
  for(const auto& receipt:view.publications)bytes+=receipt.owner.callback_kind.capacity()+1;
  return bytes;
}
inline void UpsertAttachmentProjection(std::vector<AttachmentProjectionReport>& views,const AttachmentMutations& native,const std::vector<AttachmentViewReceipts::Record>& receipts){
  auto next=CaptureAttachmentProjection(native,receipts);size_t bytes=AttachmentProjectionMetadata(next);
  auto replaced=views.end();
  for(auto it=views.begin();it!=views.end();++it){
    if(SameAttachmentAttempt(it->owner,next.owner)){if(replaced!=views.end())throw std::invalid_argument("Duplicate native attachment projection report");replaced=it;}
    else bytes+=AttachmentProjectionMetadata(*it);
  }
  if(bytes>AttachmentMutations::kMetadata||(replaced==views.end()&&views.size()>=8192))
    throw std::runtime_error("Native attachment projection report metadata capacity exceeded");
  if(replaced==views.end())views.push_back(std::move(next));
  else *replaced=std::move(next);
}
template<class Report>size_t ValidateAttachmentProjections(const Report& report){
  if(report.attachment_views.size()>8192)throw std::invalid_argument("Native attachment view report count exceeded");
  size_t charge=0;std::set<uint64_t> tickets,publication_artifacts;std::set<std::tuple<uint64_t,uint64_t,unsigned,int64_t>> attempts;
  for(const auto& view:report.attachment_views){
    const auto& owner=view.owner;
    if(!ValidAttachmentProjectionOwner(owner)||!attempts.emplace(owner.test_id,owner.attempt_generation,owner.worker_index,owner.pid).second||
       view.publications.size()>AttachmentViewReceipts::kRecords||view.references.size()>AttachmentMutations::kReferences||view.changes.size()+view.rejections.size()>AttachmentMutations::kChanges)
      throw std::invalid_argument("Invalid native attachment view identity or capacity");
    if(std::none_of(report.attempts.begin(),report.attempts.end(),[&](const auto& actual){return actual.test_id==owner.test_id&&actual.generation==owner.attempt_generation&&actual.worker_index==owner.worker_index&&actual.pid==owner.pid;}))
      throw std::invalid_argument("Native attachment view has no actual attempt");
    std::set<std::pair<int,uint64_t>> refs;std::set<uint64_t> requests,published;
    for(const auto& entry:view.references){
      if(!entry.reference.id||!entry.request||!ValidAttachmentProjectionOwner(entry.owner)||!SameAttachmentAttempt(owner,entry.owner)||
         !refs.emplace(static_cast<int>(entry.reference.kind),entry.reference.id).second)
        throw std::invalid_argument("Invalid native attachment view reference identity");
      if(entry.reference.kind==AttachmentMutations::Kind::Declaration){
        const auto actual=std::find_if(report.attachment_declarations.begin(),report.attachment_declarations.end(),[&](const auto& value){return value.id==entry.reference.id;});
        if(actual==report.attachment_declarations.end()||actual->owner!=entry.owner||actual->request!=entry.request||entry.step_id||actual->receipt==AttachmentDeclarations::Receipt::Rejected)
          throw std::invalid_argument("Native attachment view reference lacks its actual admitted declaration");
      }else if(entry.reference.kind==AttachmentMutations::Kind::Artifact){
        const auto actual=std::find_if(report.attachments.begin(),report.attachments.end(),[&](const auto& value){return value.id==entry.reference.id;});
        if(actual==report.attachments.end()||actual->owner!=entry.owner||actual->request!=entry.request||actual->step_id!=entry.step_id)
          throw std::invalid_argument("Native attachment view reference lacks its actual committed artifact");
        ValidateNativeAttachment(*actual);
      }else throw std::invalid_argument("Invalid native attachment view reference kind");
    }
    double previous=0;uint64_t revision=0;
    for(const auto& change:view.changes){
      if(change.revision!=++revision||!change.request||!requests.insert(change.request).second||
         !ValidAttachmentProjectionOwner(change.owner)||!SameAttachmentAttempt(owner,change.owner)||
         !std::isfinite(change.accepted)||change.accepted<previous||change.slots.size()>AttachmentMutations::kSlots)
        throw std::invalid_argument("Invalid native attachment mutation report history");
      previous=change.accepted;
      if(change.publication_artifact){
        const auto artifact=std::find_if(report.attachments.begin(),report.attachments.end(),[&](const auto& value){return value.id==change.publication_artifact;});
        if(artifact==report.attachments.end()||artifact->owner!=change.owner||artifact->step_id!=change.step_id||artifact->received>change.accepted||
           !published.insert(change.publication_artifact).second||change.slots.empty()||
           change.slots.back()!=AttachmentMutations::Slot(AttachmentMutations::Reference{AttachmentMutations::Kind::Artifact,change.publication_artifact}))
          throw std::invalid_argument("Native attachment view publication has no exact native artifact receipt");
      }else if(change.step_id){
        const auto step=std::find_if(report.steps.begin(),report.steps.end(),[&](const auto& value){return value.id==change.step_id;});
        if(step==report.steps.end()||step->owner!=change.owner||step->started>change.accepted||change.accepted>=step->deadline||
           (step->ended&&*step->ended<change.accepted))throw std::invalid_argument("Native attachment mutation has no actual author step");
      }
      for(const auto& slot:change.slots)if(slot){
        if(!refs.contains({static_cast<int>(slot->kind),slot->id})||
           (slot->kind==AttachmentMutations::Kind::Artifact&&!published.contains(slot->id)))
          throw std::invalid_argument("Native attachment view projects an unadmitted or unpublished reference");
      }
    }
    for(const auto& rejected:view.rejections){
      if(!rejected.request||!requests.insert(rejected.request).second||!ValidAttachmentProjectionOwner(rejected.owner)||
         !SameAttachmentAttempt(owner,rejected.owner)||!JsonString(rejected.error_json,8192)||
         !std::isfinite(rejected.received)||rejected.received<0||rejected.observed_revision>revision||
         (rejected.observed_revision&&rejected.received<view.changes[rejected.observed_revision-1].accepted)||
         (rejected.observed_revision<revision&&rejected.received>view.changes[rejected.observed_revision].accepted))
        throw std::invalid_argument("Invalid native attachment mutation rejection history");
    }
    std::set<uint64_t> accepted_publications;
    for(const auto& receipt:view.publications){
      const auto actual=std::find_if(report.attachments.begin(),report.attachments.end(),[&](const auto& artifact){return artifact.id==receipt.artifact;});
      if(!receipt.ticket||!tickets.insert(receipt.ticket).second||tickets.size()>AttachmentViewReceipts::kRecords||
         !publication_artifacts.insert(receipt.artifact).second||actual==report.attachments.end()||actual->owner!=receipt.owner||
         actual->step_id!=receipt.step_id||actual->callback_retry!=receipt.callback_retry||!ValidAttachmentProjectionOwner(receipt.owner)||!SameAttachmentAttempt(owner,receipt.owner)||
         !std::isfinite(receipt.issued)||receipt.issued!=actual->received||!std::isfinite(receipt.deadline)||receipt.deadline<=receipt.issued||
         !std::isfinite(receipt.cleanup_deadline)||receipt.cleanup_deadline!=receipt.deadline+AttachmentViewReceipts::kCleanupGrace||
         !receipt.end||!receipt.received||!std::isfinite(*receipt.received)||*receipt.received<receipt.issued||
         (receipt.expired&&*receipt.received<receipt.deadline)||(receipt.close_requested&&(!receipt.expired||*receipt.received<receipt.cleanup_deadline)))
        throw std::invalid_argument("Invalid native attachment publication-view receipt");
      ValidateNativeAttachment(*actual);
      ValidateAttachmentCallbackRetry(report,receipt.owner,receipt.callback_retry,receipt.step_id,actual->started,*receipt.received);
      if(*receipt.end==AttachmentViewReceipts::End::Acknowledged){
        if(!receipt.mutation_request)throw std::invalid_argument("Native attachment publication ACK has no request");
        if(receipt.accepted){
          const auto change=std::find_if(view.changes.begin(),view.changes.end(),[&](const auto& value){return value.request==receipt.mutation_request;});
          if(receipt.expired||*receipt.received>=receipt.deadline||change==view.changes.end()||change->owner!=receipt.owner||
             change->publication_artifact!=receipt.artifact||change->step_id!=receipt.step_id||change->accepted!=*receipt.received)
            throw std::invalid_argument("Native attachment accepted publication lacks its exact view ACK");
          accepted_publications.insert(receipt.artifact);
        }else{
          const auto error=std::find_if(view.rejections.begin(),view.rejections.end(),[&](const auto& value){return value.request==receipt.mutation_request;});
          if(error==view.rejections.end()||error->owner!=receipt.owner||error->step_id!=receipt.step_id||error->received!=*receipt.received||published.contains(receipt.artifact))
            throw std::invalid_argument("Native attachment rejected publication lacks its exact native error");
        }
      }else if(*receipt.end==AttachmentViewReceipts::End::WorkerExited){
        if(receipt.mutation_request||receipt.accepted||published.contains(receipt.artifact)||
           std::none_of(report.workers.begin(),report.workers.end(),[&](const auto& worker){return worker.worker_index==receipt.owner.worker_index&&worker.pid==receipt.owner.pid&&worker.exit_code>=0;}))
          throw std::invalid_argument("Native attachment publication death has no actual reported host exit");
      }else throw std::invalid_argument("Invalid native attachment publication terminal receipt");
    }
    if(accepted_publications!=published)throw std::invalid_argument("Native attachment view publication has no issued accepted ticket");
    const auto bytes=AttachmentProjectionMetadata(view);
    if(bytes>AttachmentMutations::kMetadata-charge)throw std::invalid_argument("Native attachment projection report metadata exceeded");
    charge+=bytes;
  }
  return charge;
}
template<class Writer>void PutAttachmentProjectionOwner(Writer& file,const StepOwner& owner){
  file.Put("\"testId\":\"");file.NumberValue(owner.test_id);file.Put("\",\"generation\":\"");file.NumberValue(owner.attempt_generation);
  file.Put("\",\"workerIndex\":");file.NumberValue(owner.worker_index);file.Put(",\"pid\":");file.NumberValue(owner.pid);
  file.Put(",\"callbackToken\":\"");file.NumberValue(owner.callback_token);file.Put("\",\"scopeId\":\"");file.NumberValue(owner.scope);
  file.Put("\",\"scopeGeneration\":\"");file.NumberValue(owner.scope_generation);file.Put("\",\"callbackKind\":\"");file.Put(owner.callback_kind);file.Put("\"");
}
template<class Writer>void PutAttachmentProjectionSlots(Writer& file,const std::vector<AttachmentMutations::Slot>& slots){
  file.Put("[");bool first=true;for(const auto& slot:slots){if(!first)file.Put(",");first=false;
    if(!slot){file.Put("null");continue;}
    file.Put(slot->kind==AttachmentMutations::Kind::Declaration?"{\"kind\":\"declaration\",\"id\":\"":"{\"kind\":\"artifact\",\"id\":\"");file.NumberValue(slot->id);file.Put("\"}");
  }file.Put("]");
}
template<class Writer>void PutAttachmentProjections(Writer& file,const std::vector<AttachmentProjectionReport>& views){
  file.Put("[");bool first=true;for(const auto& view:views){if(!first)file.Put(",");first=false;
    file.Put("{");PutAttachmentProjectionOwner(file,view.owner);file.Put(",\"revision\":\"");file.NumberValue(view.changes.size());file.Put("\",\"slots\":");
    static const std::vector<AttachmentMutations::Slot> empty;
    PutAttachmentProjectionSlots(file,view.changes.empty()?empty:view.changes.back().slots);
    file.Put(",\"references\":[");bool first_reference=true;for(const auto& entry:view.references){if(!first_reference)file.Put(",");first_reference=false;
      file.Put("{");PutAttachmentProjectionOwner(file,entry.owner);file.Put(",\"kind\":\"");file.Put(entry.reference.kind==AttachmentMutations::Kind::Declaration?"declaration":"artifact");
      file.Put("\",\"id\":\"");file.NumberValue(entry.reference.id);file.Put("\",\"requestId\":\"");file.NumberValue(entry.request);file.Put("\",\"stepId\":\"");file.NumberValue(entry.step_id);file.Put("\"}");
    }
    file.Put("],\"changes\":[");bool first_change=true;for(const auto& change:view.changes){if(!first_change)file.Put(",");first_change=false;
      file.Put("{");PutAttachmentProjectionOwner(file,change.owner);file.Put(",\"requestId\":\"");file.NumberValue(change.request);file.Put("\",\"revision\":\"");file.NumberValue(change.revision);
      file.Put("\",\"stepId\":\"");file.NumberValue(change.step_id);file.Put("\",\"acceptedMonotonicMs\":");file.NumberValue(change.accepted);
      file.Put(",\"publicationArtifactId\":\"");file.NumberValue(change.publication_artifact);file.Put("\",\"slots\":");PutAttachmentProjectionSlots(file,change.slots);file.Put("}");
    }file.Put("],\"rejections\":[");bool first_rejection=true;for(const auto& rejected:view.rejections){if(!first_rejection)file.Put(",");first_rejection=false;
      file.Put("{");PutAttachmentProjectionOwner(file,rejected.owner);file.Put(",\"requestId\":\"");file.NumberValue(rejected.request);file.Put("\",\"expectedRevision\":\"");file.NumberValue(rejected.expected_revision);
      file.Put("\",\"observedRevision\":\"");file.NumberValue(rejected.observed_revision);file.Put("\",\"stepId\":\"");file.NumberValue(rejected.step_id);
      file.Put("\",\"receivedMonotonicMs\":");file.NumberValue(rejected.received);file.Put(",\"error\":");file.Put(rejected.error_json);file.Put("}");
    }file.Put("],\"publicationReceipts\":[");bool first_receipt=true;for(const auto& receipt:view.publications){if(!first_receipt)file.Put(",");first_receipt=false;
      file.Put("{");PutAttachmentProjectionOwner(file,receipt.owner);file.Put(",\"ticketId\":\"");file.NumberValue(receipt.ticket);file.Put("\",\"artifactId\":\"");file.NumberValue(receipt.artifact);
      file.Put("\",\"stepId\":\"");file.NumberValue(receipt.step_id);file.Put("\",\"requestId\":\"");file.NumberValue(receipt.mutation_request);
      file.Put("\",\"issuedMonotonicMs\":");file.NumberValue(receipt.issued);file.Put(",\"originalDeadlineMonotonicMs\":");file.NumberValue(receipt.deadline);
      file.Put(",\"cleanupDeadlineMonotonicMs\":");file.NumberValue(receipt.cleanup_deadline);file.Put(",\"receivedMonotonicMs\":");file.NumberValue(*receipt.received);
      file.Put(",\"expired\":");file.Put(receipt.expired?"true":"false");file.Put(",\"closeRequested\":");file.Put(receipt.close_requested?"true":"false");
      file.Put(",\"accepted\":");file.Put(receipt.accepted?"true":"false");file.Put(",\"end\":\"");file.Put(*receipt.end==AttachmentViewReceipts::End::Acknowledged?"acknowledged":"workerExited");file.Put("\"");PutAttachmentCallbackRetry(file,receipt.callback_retry);file.Put("}");
    }file.Put("]}");
  }file.Put("]");
}
class AttachmentProjectionJSONWriter {
 public:
  void Put(std::string_view value){if(value.size()>AttachmentMutations::kMetadata-text_.size())throw std::length_error("Native attachment projection JSON exceeds bound");text_+=value;}
  template<class Number>void NumberValue(Number value){char data[128];const auto result=std::to_chars(data,data+sizeof(data),value);if(result.ec!=std::errc{})throw std::runtime_error("Native attachment projection numeric encoding failed");Put(std::string_view(data,result.ptr));}
  std::string Take(){return std::move(text_);}
 private:std::string text_;
};
inline std::string AttachmentProjectionJSON(const std::vector<AttachmentProjectionReport>& views){AttachmentProjectionJSONWriter writer;PutAttachmentProjections(writer,views);return writer.Take();}
// Quote the bounded serialized projection as a JSON string for the existing
// lossless JSON-to-XML encoder. It is report content, never a filesystem path.
inline std::string AttachmentProjectionQuotedJSON(std::string_view original){
  if(original.size()>AttachmentMutations::kMetadata)throw std::length_error("Native attachment projection JSON exceeds bound");
  static constexpr char hex[]="0123456789abcdef";
  std::string value;value.reserve(original.size()+2);value+='"';
  for(const unsigned char c:original){
    if(c=='"'||c=='\\'){value+='\\';value+=static_cast<char>(c);}
    else if(c<32){value+="\\u00";value+=hex[c>>4];value+=hex[c&15];}
    else value+=static_cast<char>(c);
  }
  value+='"';
  if(!JsonString(value,AttachmentMutations::kMetadata))throw std::invalid_argument("Native attachment projection JSON is not bounded UTF8");
  return value;
}
} // namespace suitecut::runner
