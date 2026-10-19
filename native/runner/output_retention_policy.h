// SPDX-License-Identifier: MIT
#pragma once
#include "output_directory_record.h"
#include <cmath>
#include <stdexcept>
#include <string_view>
namespace suitecut::runner {
inline constexpr double kOutputRetentionCleanupMaximum=30000;
enum class PreserveOutput {Always,Never,FailuresOnly};
inline PreserveOutput NativePreserveOutput(std::string_view value){
 if(value=="always")return PreserveOutput::Always;
 if(value=="never")return PreserveOutput::Never;
 if(value=="failures-only")return PreserveOutput::FailuresOnly;
 throw std::invalid_argument("Native preserveOutput must be always, never or failures-only");
}
inline const char* PreserveOutputName(PreserveOutput policy){
 switch(policy){case PreserveOutput::Always:return "always";case PreserveOutput::Never:return "never";case PreserveOutput::FailuresOnly:return "failures-only";}
 throw std::invalid_argument("Invalid native output preservation policy");
}
// These facts are supplied only by the native final-attempt report, including
// hook/fixture teardown. A retry is a separate owner/generation, never a lookup
// of the final retry's status. No JavaScript status/counter controls retention.
struct OutputRetentionOutcome {
 OutputDirectoryOwner owner;unsigned retry=0;std::string status,expected_status;bool expected=false;
};
inline bool PreserveNativeOutput(PreserveOutput policy,const OutputRetentionOutcome& outcome){
 if(!outcome.owner.test_id||!outcome.owner.generation||!outcome.owner.worker_index||outcome.owner.pid<=0||outcome.owner.pid>INT32_MAX||outcome.retry>100||
    (outcome.status!="passed"&&outcome.status!="failed"&&outcome.status!="timedOut"&&outcome.status!="skipped"&&outcome.status!="interrupted")||
    (outcome.expected_status!="passed"&&outcome.expected_status!="failed"&&outcome.expected_status!="skipped"))
   throw std::invalid_argument("Native output retention lacks exact final attempt facts");
 (void)PreserveOutputName(policy);
 return policy==PreserveOutput::Always||(policy==PreserveOutput::FailuresOnly&&!outcome.expected);
}
enum class OutputRetentionStatus {Kept,Pending,Removed,AlreadyAbsent,ForeignPreserved,Failed};
// A retained-by-policy decision performs no stat, so it does NOT assert current
// availability. Removed/absent concern the original location, not inode death
// (a user may have hardlinks/renames elsewhere). Creation history is immutable.
struct OutputRetentionRecord {
 uint64_t output_id=0;OutputDirectoryOwner owner;PreserveOutput policy=PreserveOutput::Always;
 OutputRetentionOutcome outcome;OutputRetentionStatus status=OutputRetentionStatus::Pending;
 std::string path,error;double decided=0,deadline=0;std::optional<double> receipt,physical_received;
 int64_t helper_pid=0;bool helper_signaled=false;int helper_exit=-1;
 std::optional<uint64_t> removed_files,removed_directories,removed_symlinks;
};
inline void ValidateRetentionSource(const OutputDirectoryRecord& output,const OutputRetentionOutcome& outcome){
 if(output.owner!=outcome.owner||!output.id||output.status!=OutputDirectoryStatus::Ready||!output.assigned||!output.created||!output.writer_received||!output.received||
    !output.inode||!output.parent_inode||output.writer_pid<=0||output.writer_pid>INT32_MAX||output.writer_pid==outcome.owner.pid||output.writer_exit||output.writer_signaled||!output.ready_receipt||output.rollback_pid||
    !output.error.empty()||!output.cleanup_error.empty()||!std::isfinite(output.started)||output.started<0||!std::isfinite(output.deadline)||output.deadline<=output.started||
    !std::isfinite(*output.created)||!std::isfinite(*output.writer_received)||!std::isfinite(*output.received)||!std::isfinite(*output.assigned)||
    *output.created<output.started||*output.writer_received<*output.created||*output.received<*output.writer_received||*output.assigned<*output.received||*output.assigned>=output.deadline)
   throw std::invalid_argument("Native output retention requires an actually assigned native directory");
}
} // namespace suitecut::runner
