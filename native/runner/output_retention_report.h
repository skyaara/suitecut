// SPDX-License-Identifier: MIT
#pragma once
#include "output_retention_policy.h"
#include "test_attachments.h"
#include <algorithm>
#include <set>
namespace suitecut::runner {
inline const char* OutputRetentionStatusName(OutputRetentionStatus status){
 switch(status){case OutputRetentionStatus::Kept:return "kept";case OutputRetentionStatus::Removed:return "removed";case OutputRetentionStatus::AlreadyAbsent:return "alreadyAbsent";case OutputRetentionStatus::ForeignPreserved:return "foreignPreserved";case OutputRetentionStatus::Failed:return "failed";case OutputRetentionStatus::Pending:break;}
 throw std::invalid_argument("Native output retention lacks a terminal receipt");
}
inline const char* OutputRetentionAvailability(OutputRetentionStatus status){
 switch(status){case OutputRetentionStatus::Kept:return "notObserved";case OutputRetentionStatus::Removed:case OutputRetentionStatus::AlreadyAbsent:return "originalLocationAbsent";case OutputRetentionStatus::ForeignPreserved:return "foreignAtOriginalLocation";case OutputRetentionStatus::Failed:return "unknownAfterFailure";case OutputRetentionStatus::Pending:break;}
 throw std::invalid_argument("Native output retention availability is still pending");
}
template<class Report>size_t ValidateOutputRetentionReports(const Report& report){
 if(report.output_retentions.size()>8192)throw std::invalid_argument("Native output retention report capacity exceeded");
 if(!report.output_preservation){if(!report.output_retentions.empty())throw std::invalid_argument("Native retention rows lack an activated run policy");return 0;}
 (void)PreserveOutputName(*report.output_preservation);
 std::set<uint64_t> ids;size_t charge=0;
 for(const auto& row:report.output_retentions){
  if(row.policy!=*report.output_preservation)throw std::invalid_argument("Native retention row changed activated run policy");
  (void)OutputRetentionStatusName(row.status);(void)OutputRetentionAvailability(row.status);
  const auto source=std::find_if(report.outputs.begin(),report.outputs.end(),[&](const auto& output){return output.id==row.output_id;});
  if(!row.output_id||!ids.insert(row.output_id).second||source==report.outputs.end()||row.owner!=row.outcome.owner||row.path!=source->path)throw std::invalid_argument("Native output retention has no exact original output");
  ValidateRetentionSource(*source,row.outcome);const bool kept=PreserveNativeOutput(row.policy,row.outcome);
  if((row.receipt&&(!std::isfinite(*row.receipt)||(*row.receipt)<row.decided))||!std::isfinite(row.decided)||row.decided<*source->assigned||(kept!=(row.status==OutputRetentionStatus::Kept))||row.error.size()>4096)
    throw std::invalid_argument("Native output retention decision/clock metadata rejected");
  const auto attempt=std::find_if(report.attempts.begin(),report.attempts.end(),[&](const auto& value){return value.test_id==row.owner.test_id&&value.generation==row.owner.generation&&value.worker_index==row.owner.worker_index&&value.pid==row.owner.pid;});
  if(attempt==report.attempts.end()||attempt->retry!=row.outcome.retry||attempt->status!=row.outcome.status||attempt->expected_status!=row.outcome.expected_status||attempt->expected!=row.outcome.expected)
    throw std::invalid_argument("Native output retention changed actual final attempt facts");
  if(kept){
   if(row.helper_pid||row.helper_exit!=-1||row.helper_signaled||row.deadline||row.physical_received||!row.receipt||*row.receipt!=row.decided||!row.error.empty()||row.removed_files||row.removed_directories||row.removed_symlinks)
     throw std::invalid_argument("Kept native output fabricated filesystem/availability receipts");
  }else{
   if(!std::isfinite(row.deadline)||row.deadline<=row.decided||row.deadline-row.decided>kOutputRetentionCleanupMaximum)throw std::invalid_argument("Native output retention cleanup deadline rejected");
   if(row.helper_pid){
    if(row.helper_pid<=0||row.helper_pid>INT32_MAX||row.helper_pid==row.owner.pid||row.helper_exit<0||(row.helper_signaled&&row.helper_exit==0)||!row.physical_received||!std::isfinite(*row.physical_received)||*row.physical_received<row.decided||
       (row.receipt&&(!std::isfinite(*row.receipt)||(*row.receipt)<row.decided||(*row.receipt)>(*row.physical_received))))throw std::invalid_argument("Native output retention lacks actual helper physical retirement");
   }else if(row.helper_exit!=-1||row.helper_signaled||row.physical_received||row.status!=OutputRetentionStatus::Failed||!row.receipt)throw std::invalid_argument("Unadmitted output retention invented a helper outcome");
   if(row.status==OutputRetentionStatus::Failed){if(row.error.empty())throw std::invalid_argument("Failed native output retention has no actual cause");}
   else if(!row.receipt||!row.physical_received||*row.physical_received>=row.deadline||row.helper_signaled||row.helper_exit||!row.error.empty()||!row.removed_files||!row.removed_directories||!row.removed_symlinks)
     throw std::invalid_argument("Native output retention success lacks complete timely native receipts");
   if(row.removed_files||row.removed_directories||row.removed_symlinks){
    if(!row.removed_files||!row.removed_directories||!row.removed_symlinks||!row.helper_pid||!row.receipt||*row.removed_files>16384||*row.removed_directories>16384||*row.removed_symlinks>16384||*row.removed_files+*row.removed_directories+*row.removed_symlinks>16384||
       ((row.status==OutputRetentionStatus::AlreadyAbsent||row.status==OutputRetentionStatus::ForeignPreserved)&&(*row.removed_files||*row.removed_directories||*row.removed_symlinks))||
       (row.status==OutputRetentionStatus::Removed&&!*row.removed_directories))throw std::invalid_argument("Native output retention removal counts rejected");
   }
  }
  (void)AttachmentPathJSON(row.path);(void)AttachmentPathJSON(row.error);
  const auto bytes=row.path.size()+row.error.size()+row.outcome.status.size()+row.outcome.expected_status.size()+1024;
  if(bytes>64*1024*1024-charge)throw std::invalid_argument("Native output retention report bytes exceed bound");
  charge+=bytes;
 }
 for(const auto& output:report.outputs)if(output.status==OutputDirectoryStatus::Ready&&output.assigned&&!ids.contains(output.id))throw std::invalid_argument("Native output preservation omitted an applicable assigned directory");
 return charge;
}
template<class Writer>void PutOutputRetentionReports(Writer& file,const std::vector<OutputRetentionRecord>& records){
 file.Put("[");bool first=true;
 for(const auto& row:records){if(!first)file.Put(",");first=false;
  file.Put("{\"outputId\":\"");file.NumberValue(row.output_id);file.Put("\",\"testId\":\"");file.NumberValue(row.owner.test_id);file.Put("\",\"generation\":\"");file.NumberValue(row.owner.generation);file.Put("\",\"workerIndex\":");file.NumberValue(row.owner.worker_index);file.Put(",\"pid\":");file.NumberValue(row.owner.pid);
  file.Put(",\"policy\":\"");file.Put(PreserveOutputName(row.policy));file.Put("\",\"status\":\"");file.Put(OutputRetentionStatusName(row.status));file.Put("\",\"availability\":\"");file.Put(OutputRetentionAvailability(row.status));file.Put("\",\"originalPath\":");file.Put(AttachmentPathJSON(row.path));
  file.Put(",\"retry\":");file.NumberValue(row.outcome.retry);file.Put(",\"attemptStatus\":\"");file.Put(row.outcome.status);file.Put("\",\"expectedStatus\":\"");file.Put(row.outcome.expected_status);file.Put("\",\"expected\":");file.Put(row.outcome.expected?"true":"false");file.Put(",\"decisionMonotonicMs\":");file.NumberValue(row.decided);
  if(row.deadline){file.Put(",\"cleanupDeadlineMonotonicMs\":");file.NumberValue(row.deadline);}
  if(row.receipt){file.Put(",\"receiptMonotonicMs\":");file.NumberValue(*row.receipt);}
  if(row.helper_pid){file.Put(",\"helperPid\":");file.NumberValue(row.helper_pid);file.Put(",\"helperExitCode\":");file.NumberValue(row.helper_exit);file.Put(",\"helperSignaled\":");file.Put(row.helper_signaled?"true":"false");}
  if(row.physical_received){file.Put(",\"physicalReceiptMonotonicMs\":");file.NumberValue(*row.physical_received);}
  if(row.removed_files){file.Put(",\"observedRemovedFiles\":");file.NumberValue(*row.removed_files);file.Put(",\"observedRemovedDirectories\":");file.NumberValue(*row.removed_directories);file.Put(",\"observedRemovedSymlinks\":");file.NumberValue(*row.removed_symlinks);}
  if(!row.error.empty()){file.Put(",\"error\":");file.Put(AttachmentPathJSON(row.error));}file.Put("}");
 }
 file.Put("]");
}
class OutputRetentionJSONWriter {
 public:
  void Put(std::string_view value){if(value.size()>16*1024*1024-text_.size())throw std::length_error("Native output retention JSON section exceeds bound");text_.append(value);}
  template<class Number>void NumberValue(Number value){Put(std::to_string(value));}
  std::string Take(){return std::move(text_);}
 private:std::string text_;
};
inline std::string OutputRetentionJSON(const std::vector<OutputRetentionRecord>& records){OutputRetentionJSONWriter writer;PutOutputRetentionReports(writer,records);return writer.Take();}
} // namespace suitecut::runner
