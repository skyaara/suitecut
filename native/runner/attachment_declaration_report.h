// SPDX-License-Identifier: MIT
#pragma once
#include "attachment_declarations.h"
#include "attachment_callback_report.h"
#include <set>
#include <algorithm>
namespace suitecut::runner {
inline const char* AttachmentDeclarationSource(AttachmentDeclarations::Source source){
  switch(source){case AttachmentDeclarations::Source::Absent:return "absent";case AttachmentDeclarations::Source::Body:return "body";case AttachmentDeclarations::Source::PathReference:return "pathReference";}
  throw std::invalid_argument("Invalid native attachment declaration source");
}
inline const char* AttachmentDeclarationReceipt(AttachmentDeclarations::Receipt receipt){
  switch(receipt){case AttachmentDeclarations::Receipt::ReferenceAccepted:return "referenceAccepted";case AttachmentDeclarations::Receipt::Committed:return "committed";case AttachmentDeclarations::Receipt::Failed:return "failed";case AttachmentDeclarations::Receipt::Canceled:return "canceled";case AttachmentDeclarations::Receipt::Rejected:return "rejected";case AttachmentDeclarations::Receipt::Pending:break;}
  throw std::invalid_argument("Native attachment declaration lacks terminal native receipt");
}
template<class Report>size_t ValidateAttachmentDeclarationReports(const Report& report){
  if(report.attachment_declarations.size()>AttachmentDeclarations::kRecords)throw std::invalid_argument("Native attachment declaration report capacity exceeded");
  size_t charge=0;std::set<uint64_t> ids,artifacts;std::set<std::pair<unsigned,uint64_t>> requests;
  for(const auto& declaration:report.attachment_declarations){
    const auto& owner=declaration.owner;(void)AttachmentDeclarationSource(declaration.source);(void)AttachmentDeclarationReceipt(declaration.receipt);
    if((declaration.retry_failure_disposed&&(!declaration.callback_retry.Complete()||declaration.receipt!=AttachmentDeclarations::Receipt::Failed))||!declaration.id||!declaration.request||!ids.insert(declaration.id).second||!requests.emplace(owner.worker_index,declaration.request).second||
       !owner.test_id||!owner.attempt_generation||!owner.scope||!owner.scope_generation||!owner.worker_index||owner.pid<=0||owner.pid>INT32_MAX||
       (owner.callback_kind!="body"&&owner.callback_kind!="beforeEach"&&owner.callback_kind!="afterEach"&&owner.callback_kind!="beforeAll"&&owner.callback_kind!="afterAll")||(owner.callback_kind!="body"&&!owner.callback_token)||
       !JsonString(declaration.name_json,4096)||!JsonString(declaration.type_json,1024)||!std::isfinite(declaration.declared)||declaration.declared<0||!declaration.received||!std::isfinite(*declaration.received)||*declaration.received<declaration.declared||
       (declaration.source==AttachmentDeclarations::Source::PathReference&&!declaration.author_path_json)||(declaration.source==AttachmentDeclarations::Source::Absent&&declaration.author_path_json)||
       (declaration.author_path_json&&!JsonString(*declaration.author_path_json,8192))||
       (declaration.source==AttachmentDeclarations::Source::Body)!=declaration.expected_bytes.has_value()||(declaration.expected_bytes&&*declaration.expected_bytes>16*1024*1024))throw std::invalid_argument("Invalid native attachment declaration report metadata");
    if(std::none_of(report.attempts.begin(),report.attempts.end(),[&](const auto& attempt){return attempt.test_id==owner.test_id&&attempt.generation==owner.attempt_generation&&attempt.worker_index==owner.worker_index&&attempt.pid==owner.pid;})||
       std::none_of(report.workers.begin(),report.workers.end(),[&](const auto& worker){return worker.worker_index==owner.worker_index&&worker.pid==owner.pid&&worker.exit_code>=0;}))throw std::invalid_argument("Native attachment declaration has no actual attempt/worker");
    if(declaration.receipt==AttachmentDeclarations::Receipt::ReferenceAccepted){
      if(declaration.source==AttachmentDeclarations::Source::Body||declaration.artifact_id||!declaration.error_json.empty()||declaration.cancellation_request||*declaration.received!=declaration.declared)throw std::invalid_argument("Native declared reference fabricated file or error receipts");
    }else if(declaration.receipt==AttachmentDeclarations::Receipt::Rejected){
      if(declaration.artifact_id||declaration.cancellation_request||!JsonString(declaration.error_json,8192)||*declaration.received!=declaration.declared)throw std::invalid_argument("Native rejected declaration fabricated file ownership or acceptance");
    }else{
      if(declaration.source!=AttachmentDeclarations::Source::Body)throw std::invalid_argument("Native non-body declaration fabricated a file outcome");
      const auto artifact=std::find_if(report.attachments.begin(),report.attachments.end(),[&](const auto& value){return value.id==declaration.artifact_id;});
      if(declaration.receipt==AttachmentDeclarations::Receipt::Committed){
        if(!declaration.artifact_id||!artifacts.insert(declaration.artifact_id).second||artifact==report.attachments.end()||artifact->owner!=owner||artifact->callback_retry!=declaration.callback_retry||artifact->request!=declaration.request||artifact->name_json!=declaration.name_json||artifact->content_type_json!=declaration.type_json||artifact->source_kind=="path"||artifact->bytes!=*declaration.expected_bytes||artifact->started<declaration.declared||artifact->received!=*declaration.received||!declaration.error_json.empty()||declaration.cancellation_request)throw std::invalid_argument("Native append body publication has no actual matching committed artifact");
      }else{
        if(declaration.artifact_id&&artifact!=report.attachments.end())throw std::invalid_argument("Native failed append cannot hide a successful file artifact");
        if(declaration.receipt==AttachmentDeclarations::Receipt::Failed&&(!JsonString(declaration.error_json,8192)||declaration.cancellation_request))throw std::invalid_argument("Native append failure has no actual error record");
        if(declaration.receipt==AttachmentDeclarations::Receipt::Canceled&&(!declaration.cancellation_request||!declaration.error_json.empty()))throw std::invalid_argument("Native append cancellation has no accepted cause record");
      }
    }
    ValidateAttachmentCallbackRetry(report,owner,declaration.callback_retry,0,declaration.declared,*declaration.received);
    const auto bytes=declaration.name_json.size()+declaration.type_json.size()+(declaration.author_path_json?declaration.author_path_json->size():0)+declaration.error_json.size()+768;
    if(bytes>AttachmentDeclarations::kMetadata-charge)throw std::invalid_argument("Native append report metadata capacity exceeded");
    charge+=bytes;
  }
  return charge;
}
template<class Writer>void PutAttachmentDeclarationReports(Writer& file,const std::vector<AttachmentDeclarations::Record>& records){
  file.Put("[");bool first=true;
  for(const auto& record:records){if(!first)file.Put(",");first=false;
    const auto& owner=record.owner;
    file.Put("{\"id\":\"");file.NumberValue(record.id);file.Put("\",\"requestId\":\"");file.NumberValue(record.request);file.Put("\",\"testId\":\"");file.NumberValue(owner.test_id);file.Put("\",\"generation\":\"");file.NumberValue(owner.attempt_generation);file.Put("\",\"callbackToken\":\"");file.NumberValue(owner.callback_token);file.Put("\",\"scopeId\":\"");file.NumberValue(owner.scope);file.Put("\",\"scopeGeneration\":\"");file.NumberValue(owner.scope_generation);file.Put("\",\"workerIndex\":");file.NumberValue(owner.worker_index);file.Put(",\"pid\":");file.NumberValue(owner.pid);file.Put(",\"callbackKind\":\"");file.Put(owner.callback_kind);file.Put("\",\"name\":");file.Put(record.name_json);file.Put(",\"contentType\":");file.Put(record.type_json);file.Put(",\"sourceKind\":\"");file.Put(AttachmentDeclarationSource(record.source));file.Put("\",\"receipt\":\"");file.Put(AttachmentDeclarationReceipt(record.receipt));file.Put("\",\"declaredMonotonicMs\":");file.NumberValue(record.declared);file.Put(",\"terminalMonotonicMs\":");file.NumberValue(*record.received);
    if(record.author_path_json){file.Put(",\"declaredPath\":");file.Put(*record.author_path_json);}
    if(record.expected_bytes){file.Put(",\"declaredByteLength\":");file.NumberValue(*record.expected_bytes);}
    if(record.receipt==AttachmentDeclarations::Receipt::Committed){file.Put(",\"attachmentId\":\"");file.NumberValue(record.artifact_id);file.Put("\"");}
    if(record.receipt==AttachmentDeclarations::Receipt::Failed||record.receipt==AttachmentDeclarations::Receipt::Rejected){file.Put(",\"error\":");file.Put(record.error_json);}
    if(record.cancellation_request){file.Put(",\"cancellationRequestId\":\"");file.NumberValue(record.cancellation_request);file.Put("\"");}
    PutAttachmentCallbackRetry(file,record.callback_retry);file.Put("}");
  }
  file.Put("]");
}
} // namespace suitecut::runner
