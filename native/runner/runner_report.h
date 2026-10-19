// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include "json_text.h"
#include "fail_on_flaky_tests.h"
#include "modifier_annotations.h"
#include "test_steps.h"
#include "soft_assertion_report.h"
#include "callback_retry_report.h"
#include "test_attachments.h"
#include "attachment_callback_report.h"
#include "attachment_declaration_report.h"
#include "attachment_projection_report.h"
#include "output_directory_record.h"
#include "output_retention_report.h"
#include "project_report.h"
#include "work_group_report.h"
#include "file_group_report.h"
#include "worker_reuse_report.h"
#include "attempt_error_attribution.h"
#include "serial_group_report.h"
#include "resource_lock_report.h"
#include "retained_source_report.h"
#include "repeat_configuration_report.h"
#include "report_publication.h"

namespace suitecut::runner {
struct ReportAttempt {
  uint64_t test_id=0,generation=0;unsigned retry=0,worker_index=0;int64_t pid=0;
  std::string title_json="\"\"",error_json="\"\"",status;bool expected=false;double duration_ms=0;std::optional<std::string> skip_reason_json=std::nullopt;std::string expected_status={};std::optional<std::string> expected_failure_reason_json=std::nullopt;std::vector<ModifierAnnotation> annotations{};uint64_t originating_step=0,soft_error_id=0;std::vector<AttemptSecondaryError> secondary_errors{};std::optional<double> wall_duration_ms=std::nullopt;
};
struct ReportWorker {
  unsigned worker_index=0;int64_t pid=0;bool signaled=false;int exit_code=-1;
  std::string stdout_bytes,stderr_bytes;
};
struct ReportUnrun {
  uint64_t test_id=0;std::string title_json="\"\"",reason_json="\"\"";
};
struct ReportSkipped {
  uint64_t test_id=0;std::string title_json="\"\"",reason_json="\"\"";
};
struct ReportSkipCause {
  uint64_t scope_id=0,scope_generation=0,callback_id=0,callback_token=0,test_id=0,generation=0,suite_id=0;
  unsigned worker_index=0;int64_t pid=0;
  std::string reason_json="\"\"",status;std::optional<ModifierAnnotation> annotation=std::nullopt;
};
struct ReportRuntimeSkipped {uint64_t test_id=0,cause_id=0;std::string title_json="\"\"";};
struct RunnerReport {
  std::vector<SoftAssertionRecord> soft_assertions;std::vector<ReportAttempt> attempts;std::vector<ReportWorker> workers;
  std::vector<std::string> global_errors_json;
  std::vector<ReportUnrun> unrun;
  std::vector<ReportSkipped> skipped;
  std::vector<ReportSkipCause> skip_causes;std::vector<ReportRuntimeSkipped> runtime_skipped;std::vector<StepRecord> steps{};std::vector<AttachmentDeclarations::Record> attachment_declarations;std::vector<AttachmentRecord> attachments{};std::vector<AttachmentProjectionReport> attachment_views{};std::vector<ReportProject> projects{};std::vector<ReportSerialGroup> serial_groups{};std::vector<ReportResourceLockGroup> resource_locks{};std::vector<OutputDirectoryRecord> outputs{};std::optional<PreserveOutput> output_preservation;std::vector<OutputRetentionRecord> output_retentions{};std::vector<ReportWorkGroup> work_groups{};std::vector<ReportSuiteFile> files{};std::vector<ReportFileWorkGroup> file_work_groups{};std::vector<ReportFileResourceLocks> file_resource_locks{};ReportWorkerReuse worker_reuse{};std::vector<ReportRetainedSource> retained_sources{};std::optional<RepeatConfigurationReport> repeat_configuration;FailOnFlakyTestsPolicy run_policy;std::vector<CallbackAssertionRecord> callback_assertions;
};
inline constexpr size_t kRunnerReportStreamBytes=16*1024*1024;
namespace report_detail {
inline constexpr size_t kMetadata=64*1024*1024,kOutput=128*1024*1024,kRecords=8192;
struct Summary {
  std::map<uint64_t,std::vector<const ReportAttempt*>> tests;
  std::map<uint64_t,bool> final_expected,final_skipped,final_flaky;
  size_t expected=0,unexpected=0,skipped=0,flaky=0,failures=0,errors=0,did_not_run=0,declared_skipped=0,runtime_skipped=0;double duration_ms=0;
  size_t total_tests()const{return tests.size()+did_not_run+declared_skipped+runtime_skipped;}
};
inline bool Status(std::string_view status){return status=="passed"||status=="failed"||status=="timedOut"||status=="interrupted"||status=="skipped";}
inline Summary Validate(const RunnerReport& report){
  if(report.attempts.size()>kRecords||report.workers.size()>kRecords||report.global_errors_json.size()>256||report.unrun.size()>kRecords||report.skipped.size()>kRecords||report.skip_causes.size()>kRecords||report.runtime_skipped.size()>kRecords)throw std::invalid_argument("Native runner report record capacity exceeded");
  ValidateProjectReports(report.projects);
  Summary summary;size_t metadata=0,streams=0;std::set<uint64_t> generations;std::map<unsigned,int64_t> workers;
  const auto charge=[&](size_t bytes){if(bytes>kMetadata-metadata)throw std::invalid_argument("Native runner report metadata capacity exceeded");metadata+=bytes;};
  if(report.run_policy.enabled)charge(128);
  charge(ValidateRetainedSourceReports(report));
  if(report.repeat_configuration)charge(report.repeat_configuration->MetadataBytes());
  for(const auto& worker:report.workers){
    if(worker.pid<=0||worker.pid>std::numeric_limits<int32_t>::max()||worker.exit_code < -1||worker.exit_code>255||(worker.signaled&&worker.exit_code<1)||!workers.emplace(worker.worker_index,worker.pid).second)throw std::invalid_argument("Invalid native runner report worker identity or exit status");
    for(const auto* bytes:{&worker.stdout_bytes,&worker.stderr_bytes}){if(bytes->size()>kRunnerReportStreamBytes-streams)throw std::invalid_argument("Native runner report raw stream capacity exceeded");streams+=bytes->size();}
  }
  for(const auto& project:report.projects){
    charge(project.name_json.size()+project.metadata_json.size()+project.test_ids.size()*16+project.dependencies.size()*16+project.workers.size()*8+256);
    for(const auto worker:project.workers)if(!workers.contains(worker))throw std::invalid_argument("Native project references an unreported actual worker");
  }
  for(const auto& attempt:report.attempts){
    charge(attempt.title_json.size());charge(attempt.error_json.size());charge(288);
    ModifierAnnotations annotation_bounds;
    for(const auto& annotation:attempt.annotations){annotation_bounds.Append(annotation);charge(ModifierAnnotationJSON(annotation).size()+128);}
    if(!attempt.expected_status.empty()&&attempt.expected_status!="passed"&&attempt.expected_status!="failed"&&attempt.expected_status!="skipped")throw std::invalid_argument("Invalid native expected status");
    if(attempt.expected_failure_reason_json){if(!JsonString(*attempt.expected_failure_reason_json,8192))throw std::invalid_argument("Invalid native expected failure reason");charge(attempt.expected_failure_reason_json->size());}
    if(attempt.skip_reason_json){if(!JsonString(*attempt.skip_reason_json,8192))throw std::invalid_argument("Invalid native runtime skip reason");charge(attempt.skip_reason_json->size());}
    if(!attempt.test_id||!attempt.generation||!generations.insert(attempt.generation).second||attempt.retry>100||attempt.pid<=0||attempt.pid>std::numeric_limits<int32_t>::max()||!Status(attempt.status)||!std::isfinite(attempt.duration_ms)||attempt.duration_ms<0||!JsonString(attempt.title_json,4096)||!JsonString(attempt.error_json,8192))throw std::invalid_argument("Invalid native runner report attempt");
    if(attempt.secondary_errors.size()>256)throw std::length_error("Native attempt secondary error capacity exceeded");
    for(const auto& secondary:attempt.secondary_errors){const auto& owner=secondary.source.attempt;
      if(owner!=AttemptErrorOwner{attempt.test_id,attempt.generation,attempt.worker_index,attempt.pid}||!secondary.source.scope||!secondary.source.generation||!JsonString(secondary.error_json,8192)||(secondary.fixture_raw&&secondary.originating_step))throw std::invalid_argument("Native secondary error lacks actual attempt/phase ownership");
      charge(secondary.error_json.size()+128);
    }
    if(attempt.wall_duration_ms&&(!std::isfinite(*attempt.wall_duration_ms)||*attempt.wall_duration_ms<attempt.duration_ms))throw std::invalid_argument("Invalid native attempt wall duration");
    if(!report.projects.empty()){
      const auto project=ProjectForTest(report.projects,attempt.test_id);
      if(!project||std::find(project->workers.begin(),project->workers.end(),attempt.worker_index)==project->workers.end())throw std::invalid_argument("Native project attempt has no actual project worker membership");
    }
    const auto worker=workers.find(attempt.worker_index);if(worker!=workers.end()&&worker->second!=attempt.pid)throw std::invalid_argument("Native runner report worker/PID mismatch");
    auto& attempts=summary.tests[attempt.test_id];if(!attempts.empty()&&attempt.retry<=attempts.back()->retry)throw std::invalid_argument("Native runner report retry order is not increasing");attempts.push_back(&attempt);
    summary.duration_ms+=attempt.duration_ms;if(!std::isfinite(summary.duration_ms))throw std::invalid_argument("Native runner report duration exceeds finite capacity");
  }
  if(report.unrun.size()>kRecords-summary.tests.size())throw std::invalid_argument("Native runner report declaration capacity exceeded");
  std::set<uint64_t> unrun_ids;
  for(const auto& declaration:report.unrun){
    charge(declaration.title_json.size());charge(declaration.reason_json.size());charge(128);
    if(!declaration.test_id||summary.tests.contains(declaration.test_id)||!unrun_ids.insert(declaration.test_id).second||!JsonString(declaration.title_json,4096)||!JsonString(declaration.reason_json,8192))throw std::invalid_argument("Invalid native runner unrun declaration");
  }
  summary.did_not_run=report.unrun.size();
  if(report.skipped.size()>kRecords-summary.tests.size()-report.unrun.size())throw std::invalid_argument("Native runner skipped declaration capacity exceeded");
  for(const auto& declaration:report.skipped){
    charge(declaration.title_json.size());charge(declaration.reason_json.size());charge(128);
    if(!declaration.test_id||summary.tests.contains(declaration.test_id)||!unrun_ids.insert(declaration.test_id).second||!JsonString(declaration.title_json,4096)||!JsonString(declaration.reason_json,8192))throw std::invalid_argument("Invalid native runner skipped declaration");
  }
  std::map<uint64_t,const ReportSkipCause*> causes;
  for(const auto& cause:report.skip_causes){
    charge(cause.reason_json.size());charge(256);
    if(!cause.scope_id||!cause.scope_generation||!cause.callback_id||!cause.callback_token||!cause.test_id||!cause.generation||!JsonString(cause.reason_json,8192)||!Status(cause.status)||cause.status=="passed"||!causes.emplace(cause.scope_id,&cause).second)throw std::invalid_argument("Invalid native beforeAll skip cause");
    if(cause.annotation){ValidateModifierAnnotation(*cause.annotation);charge(ModifierAnnotationJSON(*cause.annotation).size()+128);if(cause.annotation->type!="skip"||cause.annotation->reason_json!=cause.reason_json)throw std::invalid_argument("Native beforeAll cause annotation mismatch");}
    const auto worker=workers.find(cause.worker_index);
    if(worker==workers.end()||worker->second!=cause.pid)throw std::invalid_argument("Native beforeAll skip cause has no actual worker");
    const auto associated=summary.tests.find(cause.test_id);
    if(associated==summary.tests.end()||std::none_of(associated->second.begin(),associated->second.end(),[&](const auto* attempt){return attempt->generation==cause.generation&&attempt->pid==cause.pid&&attempt->worker_index==cause.worker_index&&attempt->skip_reason_json&&*attempt->skip_reason_json==cause.reason_json&&(!cause.annotation||std::find(attempt->annotations.begin(),attempt->annotations.end(),*cause.annotation)!=attempt->annotations.end());}))throw std::invalid_argument("Native beforeAll skip cause has no actual associated attempt");
  }
  if(report.runtime_skipped.size()>kRecords-summary.tests.size()-report.unrun.size()-report.skipped.size())throw std::invalid_argument("Native runtime skipped declaration capacity exceeded");
  for(const auto& declaration:report.runtime_skipped){
    charge(declaration.title_json.size());charge(128);
    const auto cause=causes.find(declaration.cause_id);
    if(!declaration.test_id||summary.tests.contains(declaration.test_id)||!unrun_ids.insert(declaration.test_id).second||!JsonString(declaration.title_json,4096)||cause==causes.end()||cause->second->status!="skipped")throw std::invalid_argument("Invalid native runtime skipped declaration");
    const auto& attempts=summary.tests.at(cause->second->test_id);
    if(std::none_of(attempts.begin(),attempts.end(),[&](const auto* attempt){return attempt->generation==cause->second->generation&&attempt->status=="skipped"&&attempt->expected;}))throw std::invalid_argument("Native runtime skip propagation lacks a clean actual associated attempt");
  }
  charge(ValidateAttachmentDeclarationReports(report));
  charge(ValidateAttachmentProjections(report));
  if(report.steps.size()>TestSteps::kRecords)throw std::invalid_argument("Native step report capacity exceeded");
  std::map<uint64_t,const StepRecord*> step_ids;
  std::set<std::pair<unsigned,uint64_t>> declared_skip_requests;
  std::map<std::pair<unsigned,uint64_t>,LocalStepSkipCause> local_skip_causes;
  for(const auto& step:report.steps){
    if(!ValidStepBoxLocation(step.box,step.location))throw std::invalid_argument("Invalid native boxed-step report metadata");
    if(step.location&&!ValidStepLocation(*step.location))throw std::invalid_argument("Invalid native step report location");
    charge(StepBoxMetadata(step.box)+(step.location?step.location->file_json.size()+64:0)+step.title_json.size()+step.error_json.size()+step.soft_error_json.size()+step.receipt_error_json.size()+(step.attachment_error_json?step.attachment_error_json->size():0)+(step.test_skip?step.test_skip->reason_json.size():0)+(step.step_skip?step.step_skip->reason_json.size():0)+800);
    if(!step.id||(step.status!=StepStatus::Passed&&step.status!=StepStatus::Failed&&step.status!=StepStatus::TimedOut&&step.status!=StepStatus::Interrupted&&step.status!=StepStatus::Skipped)||(step.receipt!=StepReceipt::Settled&&step.receipt!=StepReceipt::WorkerExited&&step.receipt!=StepReceipt::NotInvoked)||!step.ended||!step.received||!std::isfinite(step.started)||!std::isfinite(step.deadline)||!std::isfinite(*step.ended)||!std::isfinite(*step.received)||step.started<0||step.started>*step.ended||*step.ended>*step.received||step.deadline<0||step.timeout_ms>120000||!std::isfinite(step.own_deadline)||step.own_deadline!=(step.timeout_ms?step.started+static_cast<double>(step.timeout_ms):std::numeric_limits<double>::max())||step.deadline>step.own_deadline||!JsonString(step.title_json,4096)||!JsonString(step.error_json,8192)||!JsonString(step.receipt_error_json,8192)||!step.depth||step.depth>TestSteps::kDepth||!step_ids.emplace(step.id,&step).second)throw std::invalid_argument("Invalid native step report record");
    if(!step.callback_received&&step.callback_ok)throw std::invalid_argument("Native step successful raw callback has no actual receipt");
    if(step.callback_received&&(!std::isfinite(*step.callback_received)||*step.callback_received<step.started||*step.callback_received>*step.received||step.receipt==StepReceipt::NotInvoked||(step.callback_ok&&step.receipt_error_json!="\"\"")))throw std::invalid_argument("Native step actual raw callback receipt mismatch");
    if(step.attachment_error_json&&(!JsonString(*step.attachment_error_json,8192)||step.status==StepStatus::Passed||step.receipt==StepReceipt::NotInvoked))throw std::invalid_argument("Native step attachment error provenance mismatch");
    const auto actual=summary.tests.find(step.owner.test_id);
    if(actual==summary.tests.end()||std::none_of(actual->second.begin(),actual->second.end(),[&](const auto* attempt){return attempt->generation==step.owner.attempt_generation&&attempt->worker_index==step.owner.worker_index&&attempt->pid==step.owner.pid;}))throw std::invalid_argument("Native step has no actual associated attempt");
    const auto process=workers.find(step.owner.worker_index);if(process==workers.end()||process->second!=step.owner.pid||!step.owner.scope||!step.owner.scope_generation)throw std::invalid_argument("Native step has no actual worker/callback owner");
    if((step.owner.callback_kind!="body"&&step.owner.callback_kind!="beforeEach"&&step.owner.callback_kind!="afterEach"&&step.owner.callback_kind!="beforeAll"&&step.owner.callback_kind!="afterAll")||(step.owner.callback_kind!="body"&&!step.owner.callback_token))throw std::invalid_argument("Invalid native step callback kind");
    if(step.receipt==StepReceipt::WorkerExited&&step.receipt_ok)throw std::invalid_argument("Native step worker exit cannot fabricate successful raw settlement");
    if(step.receipt_ok&&step.receipt_error_json!="\"\"")throw std::invalid_argument("Native successful step receipt has an error");
    if(step.status==StepStatus::Passed&&(step.receipt!=StepReceipt::Settled||!step.receipt_ok||step.error_json!="\"\""))throw std::invalid_argument("Native passed step has no actual successful receipt");
    if((step.status==StepStatus::Skipped&&!step.test_skip&&!step.step_skip&&!step.declared_skip_request)||(step.test_skip&&(!step.test_skip->request||!JsonString(step.test_skip->reason_json,8192)||(step.status==StepStatus::Skipped&&step.error_json!="\"\"")))||(step.receipt_test_skip&&(!step.test_skip||step.receipt_test_skip!=step.test_skip->request||step.receipt!=StepReceipt::Settled||!step.receipt_ok)))throw std::invalid_argument("Native skipped step cause/receipt mismatch");
    if((step.receipt==StepReceipt::NotInvoked)!=(step.declared_skip_request!=0))throw std::invalid_argument("Native non-invoked step lacks its actual skipped declaration request");
    if(step.declared_skip_request&&(step.status!=StepStatus::Skipped||step.receipt_ok||step.test_skip||step.step_skip||step.receipt_test_skip||step.receipt_step_skip||step.error_json!="\"\""||step.receipt_error_json!="\"\""||step.started!=*step.ended||step.started!=*step.received||!declared_skip_requests.emplace(step.owner.worker_index,step.declared_skip_request).second))throw std::invalid_argument("Native non-invoked step fabricated callback receipt/cause/timing");
    if((step.test_skip||step.step_skip)&&(step.status==StepStatus::Passed||step.status==StepStatus::Failed))throw std::invalid_argument("Native settled ordinary step cannot acquire a later skip cause");
    if(step.receipt_step_skip&&(!step.step_skip||step.receipt_step_skip!=step.step_skip->request||step.receipt!=StepReceipt::Settled||!step.receipt_ok||step.receipt_test_skip))throw std::invalid_argument("Native local-step control receipt cause mismatch");
    if(step.step_skip){if(step.status==StepStatus::Skipped&&step.error_json!="\"\"")throw std::invalid_argument("Native local skipped step has an invented logical error");const auto& cause=*step.step_skip;const auto [cause_record,inserted]=local_skip_causes.emplace(std::pair(step.owner.worker_index,cause.request),cause);if(!inserted&&cause_record->second!=cause)throw std::invalid_argument("Native local-step skip request changed source/cause");const auto source=step_ids.find(cause.source_step);if(!cause.request||!JsonString(cause.reason_json,8192)||(!cause.description_present&&cause.reason_json!="\"\"")||source==step_ids.end()||source->second->owner!=step.owner||!source->second->step_skip||*source->second->step_skip!=cause)throw std::invalid_argument("Native local-step skip source cause mismatch");const auto* ancestor=&step;while(ancestor->id!=cause.source_step&&ancestor->parent){const auto found=step_ids.find(ancestor->parent);if(found==step_ids.end())break;ancestor=found->second;}if(ancestor->id!=cause.source_step)throw std::invalid_argument("Native local-step skip source is not an actual ancestor");}
    if(step.parent){const auto parent=step_ids.find(step.parent);if(parent==step_ids.end()||parent->second->owner!=step.owner||parent->second->receipt==StepReceipt::NotInvoked||parent->second->depth+1!=step.depth||parent->second->started>step.started||*parent->second->ended<step.started)throw std::invalid_argument("Native step actual parent mismatch");}
    else if(step.depth!=1)throw std::invalid_argument("Native root step depth mismatch");
  }
  for(const auto& step:report.steps)if(step.originating_step){
    const auto found=step_ids.find(step.originating_step);
    if(found==step_ids.end()||found->second->owner!=step.owner||found->second->status!=StepStatus::Failed||!found->second->callback_received||found->second->callback_ok||!step.callback_received||step.callback_ok)throw std::invalid_argument("Native step originating failure has no actual raw source receipt");
    const auto* ancestor=found->second;while(ancestor->parent&&ancestor->parent!=step.id)ancestor=step_ids.at(ancestor->parent);
    if(ancestor->parent!=step.id)throw std::invalid_argument("Native step originating failure is not a real descendant");
  }
  for(const auto& attempt:report.attempts)if(attempt.originating_step){
    const auto found=step_ids.find(attempt.originating_step);
    if(found==step_ids.end()||found->second->owner.test_id!=attempt.test_id||found->second->owner.attempt_generation!=attempt.generation||found->second->owner.worker_index!=attempt.worker_index||found->second->owner.pid!=attempt.pid||found->second->status!=StepStatus::Failed||!found->second->callback_received||found->second->callback_ok||attempt.status=="passed"||attempt.status=="skipped")throw std::invalid_argument("Native attempt originating-step failure has foreign, stale or absent raw source");
  }

  for(const auto& attempt:report.attempts)for(const auto& secondary:attempt.secondary_errors)if(secondary.originating_step){
    const auto found=step_ids.find(secondary.originating_step);
    if(found==step_ids.end()||found->second->owner.test_id!=attempt.test_id||found->second->owner.attempt_generation!=attempt.generation||found->second->owner.worker_index!=attempt.worker_index||found->second->owner.pid!=attempt.pid||found->second->owner.scope!=secondary.source.scope||found->second->owner.scope_generation!=secondary.source.generation||found->second->status!=StepStatus::Failed||!found->second->callback_received||found->second->callback_ok)throw std::invalid_argument("Native secondary error originating-step receipt has foreign, stale or absent raw source");
  }
  if(report.attachments.size()>128)throw std::invalid_argument("Native attachment report capacity exceeded");
  size_t artifact_bytes=0;std::set<uint64_t> artifact_ids;std::set<std::pair<unsigned,uint64_t>> artifact_requests;std::set<std::string> artifact_paths;
  for(const auto& artifact:report.attachments){
    ValidateAttachmentCallbackRetry(report,artifact.owner,artifact.callback_retry,artifact.step_id,artifact.started,artifact.received);
    ValidateNativeAttachment(artifact);charge(artifact.name_json.size()+artifact.content_type_json.size()+artifact.path.size()+512);
    if(!artifact_ids.insert(artifact.id).second||!artifact_requests.emplace(artifact.owner.worker_index,artifact.request).second||!artifact_paths.insert(artifact.path).second||artifact.bytes>64*1024*1024-artifact_bytes)throw std::invalid_argument("Native attachment report duplicate/quota violation");
    if(artifact.step_id){const auto step=step_ids.find(artifact.step_id);if(step==step_ids.end()||step->second->owner!=artifact.owner||step->second->receipt==StepReceipt::NotInvoked||artifact.started<step->second->started||artifact.received>*step->second->received)throw std::invalid_argument("Native attachment has no actual step/fenced writer provenance");}
    artifact_bytes+=artifact.bytes;const auto actual=summary.tests.find(artifact.owner.test_id);const auto worker=workers.find(artifact.owner.worker_index);
    if(worker==workers.end()||worker->second!=artifact.owner.pid||actual==summary.tests.end()||std::none_of(actual->second.begin(),actual->second.end(),[&](const auto* attempt){return attempt->generation==artifact.owner.attempt_generation&&attempt->pid==artifact.owner.pid&&attempt->worker_index==artifact.owner.worker_index;}))throw std::invalid_argument("Native attachment has no actual associated attempt/worker");
  }
  if(report.outputs.size()>kRecords)throw std::invalid_argument("Native output directory report capacity exceeded");
  std::set<uint64_t> output_ids;std::set<uint64_t> output_generations;std::set<std::string> output_paths;
  for(const auto& output:report.outputs){
    const auto& owner=output.owner;charge(output.path.size()*6+output.error.size()*6+output.cleanup_error.size()*6+768);
    (void)AttachmentPathJSON(output.path);(void)AttachmentPathJSON(output.error);(void)AttachmentPathJSON(output.cleanup_error);
    const auto actual=summary.tests.find(owner.test_id);const auto worker=workers.find(owner.worker_index);
    if(!output.id||!owner.test_id||!owner.generation||!owner.worker_index||owner.pid<=0||!output_ids.insert(output.id).second||!output_generations.insert(owner.generation).second||!output_paths.insert(output.path).second||!std::filesystem::path(output.path).is_absolute()||output.path.find('\0')!=std::string::npos||worker==workers.end()||worker->second!=owner.pid||actual==summary.tests.end()||std::none_of(actual->second.begin(),actual->second.end(),[&](const auto* attempt){return attempt->generation==owner.generation&&attempt->pid==owner.pid&&attempt->worker_index==owner.worker_index;}))throw std::invalid_argument("Native output directory has no unique actual attempt/worker owner");
    if(output.status==OutputDirectoryStatus::Preparing||!std::isfinite(output.started)||output.started<0||!std::isfinite(output.deadline)||output.deadline<=output.started||!output.writer_received||!output.received||!std::isfinite(*output.writer_received)||!std::isfinite(*output.received)||*output.writer_received<output.started||*output.received<*output.writer_received||output.writer_pid<=0||output.writer_pid==owner.pid||output.writer_exit<0||(output.writer_signaled&&output.writer_exit==0))throw std::invalid_argument("Native output directory lacks actual writer retirement");
    if(output.created&&(!std::isfinite(*output.created)||*output.created<output.started||*output.created>*output.writer_received||!output.inode||!output.parent_inode))throw std::invalid_argument("Native output directory inode receipt mismatch");
    if(!output.created&&(output.inode||output.parent_inode||output.ready_receipt||output.rollback_pid))throw std::invalid_argument("Native output directory invented inode/rollback provenance");
    if(output.status==OutputDirectoryStatus::Ready&&(!output.created||!output.ready_receipt||output.writer_signaled||output.writer_exit||*output.received>=output.deadline||!output.error.empty()||!output.cleanup_error.empty()||output.rollback_pid))throw std::invalid_argument("Native ready output directory lacks successful real IO");
    if(output.assigned&&(output.status!=OutputDirectoryStatus::Ready||!std::isfinite(*output.assigned)||*output.assigned<*output.received||*output.assigned>=output.deadline))throw std::invalid_argument("Native output assignment lacks timely actual readiness ACK");
    if(output.rollback_pid&&(output.rollback_pid==owner.pid||output.rollback_pid==output.writer_pid||output.rollback_exit<0||(output.rollback_signaled&&output.rollback_exit==0)))throw std::invalid_argument("Native output rollback lacks actual distinct helper exit");
    if(output.created&&output.status!=OutputDirectoryStatus::Ready&&!output.rollback_receipt&&output.cleanup_error.empty())throw std::invalid_argument("Native failed output directory lacks actual rollback/failure receipt");
    if(output.foreign_preserved&&!output.rollback_receipt)throw std::invalid_argument("Native output foreign preservation lacks actual rollback receipt");
    if(output.rollback_receipt&&(!output.rollback_pid||output.rollback_signaled||output.rollback_exit))throw std::invalid_argument("Native output rollback receipt contradicts actual helper exit");
  }
  charge(ValidateOutputRetentionReports(report));
  charge(ValidateSoftAssertions(report));
  charge(ValidateCallbackAssertions(report,step_ids));
  summary.runtime_skipped=report.runtime_skipped.size();
  summary.declared_skipped=report.skipped.size();summary.skipped=report.skipped.size()+report.runtime_skipped.size();
  for(const auto& error:report.global_errors_json){charge(error.size());if(!JsonString(error,8192))throw std::invalid_argument("Invalid native runner report global error");}
  const auto serial_members=ValidateSerialGroupReports(report);
  for(const auto& [id,attempts]:summary.tests){
    const auto& last=*attempts.back();
    const bool serial=serial_members.contains(id);
    if(!serial){
      const bool skipped=last.status=="skipped"&&last.expected;
      const bool flaky=!skipped&&last.expected&&std::any_of(attempts.begin(),attempts.end()-1,[](const auto* attempt){return !attempt->expected;});
      summary.final_expected.emplace(id,last.expected);summary.final_skipped.emplace(id,skipped);summary.final_flaky.emplace(id,flaky);
      if(skipped)++summary.skipped;
      else if(last.expected){++summary.expected;if(flaky)++summary.flaky;}
      else{++summary.unexpected;if(last.status=="interrupted")++summary.errors;else ++summary.failures;}
      continue;
    }
    const bool interrupted=std::any_of(attempts.begin(),attempts.end(),[](const auto* a){return a->status=="interrupted";});
    const bool expected=!interrupted&&std::any_of(attempts.begin(),attempts.end(),[](const auto* a){return a->expected;});
    const bool flaky=expected&&std::any_of(attempts.begin(),attempts.end(),[](const auto* a){return !a->expected;});
    const bool skipped=expected&&!flaky&&std::all_of(attempts.begin(),attempts.end(),[](const auto* a){return a->status=="skipped";});
    summary.final_expected.emplace(id,expected);summary.final_skipped.emplace(id,skipped);summary.final_flaky.emplace(id,flaky);
    if(skipped)++summary.skipped;
    else if(expected){++summary.expected;if(flaky)++summary.flaky;}
    else{++summary.unexpected;if(interrupted||last.status=="interrupted")++summary.errors;else ++summary.failures;}
  }
  ValidateWorkGroupReports(report);
  ValidateWorkerReuseReports(report);
  ValidateFileGroupReports(report);
  for(const auto& file:report.files)charge(256+file.path.size()+file.tests.size()*16+file.selected.size()*16);
  charge(report.worker_reuse.hosts.size()*256+report.worker_reuse.borrows.size()*320);
  for(const auto& group:report.file_work_groups)charge(256+group.tests.size()*16+group.workers.size()*8);
  for(const auto& lock:report.file_resource_locks){charge(256);for(const auto& name:lock.names)charge(name.size()+32);}
  for(const auto& group:report.work_groups)charge(256+group.tests.size()*16+group.workers.size()*8);
  ValidateResourceLockReports(report);
  for(const auto& group:report.resource_locks){charge(128);for(const auto& name:group.names)charge(name.size()*3+32);}
  summary.errors+=report.global_errors_json.size();return summary;
}
class AtomicFile {
 public:
  explicit AtomicFile(const std::filesystem::path& path,NativeReportPublication* publication=nullptr):path_(path),publication_(publication){
    if(publication_&&publication_->Ready())throw std::invalid_argument("Native report publication handle is already occupied");
    const auto text=path.string();if(text.empty()||text.size()>4096||text.find('\0')!=std::string::npos||path.filename().empty())throw std::invalid_argument("Invalid native runner report path");
    const auto parent=path.parent_path().empty()?std::filesystem::path("."):path.parent_path();std::filesystem::create_directories(parent);
    for(unsigned retry=0;retry<64;++retry){
      static std::atomic<uint64_t> sequence{0};auto token=sequence.load(std::memory_order_relaxed);
      do{if(token==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Native runner report temporary identity exhausted");}while(!sequence.compare_exchange_weak(token,token+1,std::memory_order_relaxed));
      temporary_=parent/(path.filename().string()+".suitecut-report-"+std::to_string(getpid())+"-"+std::to_string(token)+".tmp");
      fd_=::open(temporary_.c_str(),(publication_?O_RDWR:O_WRONLY)|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
      if(fd_>=0)return;
      if(errno!=EEXIST)throw std::system_error(errno,std::generic_category(),"Native runner report temporary open");
    }
    throw std::runtime_error("Native runner report temporary file collision capacity exceeded");
  }
  AtomicFile(const AtomicFile&)=delete;AtomicFile& operator=(const AtomicFile&)=delete;
  ~AtomicFile(){if(fd_>=0)::close(fd_);if(!committed_&&!temporary_.empty())::unlink(temporary_.c_str());}
  void Put(std::string_view value){
    if(value.size()>kOutput-written_)throw std::length_error("Native runner report output capacity exceeded");
    written_+=value.size();
    while(!value.empty()){const auto count=::write(fd_,value.data(),value.size());if(count<0){if(errno==EINTR)continue;throw std::system_error(errno,std::generic_category(),"Native runner report write");}if(!count)throw std::runtime_error("Native runner report write made no progress");value.remove_prefix(static_cast<size_t>(count));}
  }
  template<class Number>void NumberValue(Number value){char data[128];const auto result=std::to_chars(data,data+sizeof(data),value);if(result.ec!=std::errc{})throw std::runtime_error("Native runner report number encoding failed");Put(std::string_view(data,result.ptr));}
  void DecimalValue(double value){
    char data[128];const auto result=std::to_chars(data,data+sizeof(data),value);if(result.ec!=std::errc{})throw std::runtime_error("Native runner report decimal encoding failed");
    const std::string text(data,result.ptr);const auto marker=text.find_first_of("eE");if(marker==std::string::npos){Put(text);return;}
    auto exponent_text=std::string_view(text).substr(marker+1);if(exponent_text.starts_with('+'))exponent_text.remove_prefix(1);int exponent=0;const auto parsed=std::from_chars(exponent_text.data(),exponent_text.data()+exponent_text.size(),exponent);if(parsed.ec!=std::errc{}||parsed.ptr!=exponent_text.data()+exponent_text.size()||exponent < -400||exponent>400)throw std::runtime_error("Native runner report decimal exponent is invalid");
    auto digits=text.substr(0,marker);if(digits.starts_with('-')){Put("-");digits.erase(0,1);}const auto dot=digits.find('.');const auto place=static_cast<int>(dot==std::string::npos?digits.size():dot)+exponent;if(dot!=std::string::npos)digits.erase(dot,1);
    if(place<=0){Put("0.");Put(std::string(static_cast<size_t>(-place),'0'));Put(digits);}else if(static_cast<size_t>(place)>=digits.size()){Put(digits);Put(std::string(static_cast<size_t>(place)-digits.size(),'0'));}else{Put(std::string_view(digits).substr(0,static_cast<size_t>(place)));Put(".");Put(std::string_view(digits).substr(static_cast<size_t>(place)));}
  }
  void Hex(std::string_view bytes){static constexpr char digits[]="0123456789abcdef";char buffer[4096];while(!bytes.empty()){const size_t count=std::min(bytes.size(),sizeof(buffer)/2);for(size_t i=0;i<count;++i){const auto byte=static_cast<unsigned char>(bytes[i]);buffer[2*i]=digits[byte>>4];buffer[2*i+1]=digits[byte&15];}Put(std::string_view(buffer,2*count));bytes.remove_prefix(count);}}
  void Commit(){
    if(::fsync(fd_)<0)throw std::system_error(errno,std::generic_category(),"Native runner report file sync");
    auto retained=publication_?NativeReportPublication::Duplicate(fd_,written_):NativeReportPublication{};
    const int fd=fd_;fd_=-1;if(::close(fd)<0)throw std::system_error(errno,std::generic_category(),"Native runner report file close");
    if(::rename(temporary_.c_str(),path_.c_str())<0)throw std::system_error(errno,std::generic_category(),"Native runner report atomic commit");
    committed_=true;
    const auto parent=path_.parent_path().empty()?std::filesystem::path("."):path_.parent_path();const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(directory<0)throw std::system_error(errno,std::generic_category(),"Native runner report committed directory open");
    const int result=::fsync(directory),saved=errno;::close(directory);if(result<0)throw std::system_error(saved,std::generic_category(),"Native runner report committed directory sync");
    if(publication_)*publication_=std::move(retained);
  }
 private:std::filesystem::path path_,temporary_;int fd_=-1;size_t written_=0;bool committed_=false;NativeReportPublication* publication_=nullptr;
};
struct XMLText {std::string text;size_t sanitized=0;};
inline void UTF8(std::string& target,uint32_t code){
  if(code<128)target.push_back(static_cast<char>(code));else if(code<2048){target.push_back(static_cast<char>(0xc0|(code>>6)));target.push_back(static_cast<char>(0x80|(code&63)));}
  else if(code<65536){target.push_back(static_cast<char>(0xe0|(code>>12)));target.push_back(static_cast<char>(0x80|((code>>6)&63)));target.push_back(static_cast<char>(0x80|(code&63)));}
  else{target.push_back(static_cast<char>(0xf0|(code>>18)));target.push_back(static_cast<char>(0x80|((code>>12)&63)));target.push_back(static_cast<char>(0x80|((code>>6)&63)));target.push_back(static_cast<char>(0x80|(code&63)));}
}
inline XMLText XML(std::string_view checked){
  XMLText result;
  for(size_t i=1;i+1<checked.size();++i){
    uint32_t code=static_cast<unsigned char>(checked[i]);
    if(code=='\\'){
      const char escape=checked[++i];
      if(escape=='u'){
        code=0;for(unsigned j=0;j<4;++j)code=(code<<4)|static_cast<unsigned>(HexDigit(checked[++i]));
        if(code>=0xd800&&code<=0xdbff&&i+6<checked.size()&&checked[i+1]=='\\'&&checked[i+2]=='u'){
          uint32_t low=0;for(unsigned j=3;j<=6;++j)low=(low<<4)|static_cast<unsigned>(HexDigit(checked[i+j]));
          if(low>=0xdc00&&low<=0xdfff){code=0x10000+((code-0xd800)<<10)+(low-0xdc00);i+=6;}
        }
      }else if(escape=='b')code=8;else if(escape=='f')code=12;else if(escape=='n')code=10;else if(escape=='r')code=13;else if(escape=='t')code=9;else code=static_cast<unsigned char>(escape);
    }else if(code>=128){unsigned count=0;if((code&0xe0)==0xc0){count=1;code&=31;}else if((code&0xf0)==0xe0){count=2;code&=15;}else{count=3;code&=7;}for(unsigned j=0;j<count;++j)code=(code<<6)|(static_cast<unsigned char>(checked[++i])&63);}
    if((code<32&&code!=9&&code!=10&&code!=13)||(code>=0xd800&&code<=0xdfff)||code==0xfffe||code==0xffff){code=0xfffd;++result.sanitized;}
    if(code=='&')result.text+="&amp;";else if(code=='<')result.text+="&lt;";else if(code=='>')result.text+="&gt;";else if(code=='\"')result.text+="&quot;";else if(code=='\'')result.text+="&apos;";else if(code==9)result.text+="&#9;";else if(code==10)result.text+="&#10;";else if(code==13)result.text+="&#13;";else UTF8(result.text,code);
  }
  return result;
}
inline void Property(AtomicFile& file,std::string_view name,std::string_view escaped){file.Put("<property name=\"");file.Put(name);file.Put("\" value=\"");file.Put(escaped);file.Put("\"/>");}
template<class Number>inline void NumericProperty(AtomicFile& file,std::string_view name,Number value){file.Put("<property name=\"");file.Put(name);file.Put("\" value=\"");file.NumberValue(value);file.Put("\"/>");}
inline void AnnotationProperties(AtomicFile& file,const std::string& prefix,const ModifierAnnotation& value){
  Property(file,prefix+"type",value.type);
  if(value.description_present){const auto text=XML(value.reason_json);Property(file,prefix+"description",text.text);NumericProperty(file,prefix+"descriptionXmlSanitizedUnits",text.sanitized);}
  if(value.location){const auto text=XML(value.location->file_json);Property(file,prefix+"location.file",text.text);NumericProperty(file,prefix+"location.fileXmlSanitizedUnits",text.sanitized);NumericProperty(file,prefix+"location.line",value.location->line);NumericProperty(file,prefix+"location.column",value.location->column);}
}
inline void PutBoxLocation(AtomicFile& file,const StepLocation& location){
  file.Put("{\"file\":");file.Put(location.file_json);file.Put(",\"line\":");file.NumberValue(location.line);file.Put(",\"column\":");file.NumberValue(location.column);file.Put(",\"origin\":\"callsite\"}");
}
inline void PutStepBox(AtomicFile& file,const StepRecord& step){
  if(!step.box.enabled)return;
  file.Put(",\"box\":true");
  if(step.box.callsite){file.Put(",\"boxCallsite\":");PutBoxLocation(file,*step.box.callsite);}
  if(step.box.caller){file.Put(",\"boxCaller\":");PutBoxLocation(file,*step.box.caller);}
  if(const auto* location=StepErrorAttribution(step)){
    file.Put(",\"errorAttribution\":{\"stepId\":\"");file.NumberValue(step.id);file.Put("\",\"kind\":\"");file.Put(StepErrorAttributionKind(step));file.Put("\",\"primary\":true,\"location\":");PutBoxLocation(file,*location);file.Put("}");
  }
}
inline void StepBoxProperties(AtomicFile& file,const StepRecord& step,const std::string& prefix){
  if(!step.box.enabled)return;
  Property(file,prefix+"box","true");
  const auto location=[&](const std::string& name,const StepLocation& value){const auto text=XML(value.file_json);Property(file,prefix+name+".file",text.text);NumericProperty(file,prefix+name+".fileXmlSanitizedUnits",text.sanitized);NumericProperty(file,prefix+name+".line",value.line);NumericProperty(file,prefix+name+".column",value.column);Property(file,prefix+name+".origin","callsite");};
  if(step.box.callsite)location("boxCallsite",*step.box.callsite);
  if(step.box.caller)location("boxCaller",*step.box.caller);
  if(const auto* actual=StepErrorAttribution(step)){location("errorAttribution",*actual);Property(file,prefix+"errorAttribution.kind",StepErrorAttributionKind(step));Property(file,prefix+"errorAttribution.primary","true");NumericProperty(file,prefix+"errorAttribution.stepId",step.id);}
}
using StepIndex=std::map<uint64_t,const StepRecord*>;
inline StepIndex IndexSteps(const std::vector<StepRecord>& steps){StepIndex index;for(const auto& step:steps)index.emplace(step.id,&step);return index;}
inline const StepRecord* AttemptBoxedSource(const ReportAttempt& attempt,const StepIndex& index){
  if(attempt.status!="failed")return nullptr;
  auto id=attempt.originating_step;
  for(size_t depth=0;id&&depth<TestSteps::kDepth;++depth){const auto found=index.find(id);if(found==index.end())return nullptr;const auto* step=found->second;if(StepErrorAttribution(*step))return step;id=step->originating_step;}
  return nullptr;
}
// JUnit presentation is separate from the exact raw attempt error property.
inline void PutAttemptDisplayedError(AtomicFile& file,const ReportAttempt& attempt,const StepIndex& index,const std::string& raw_xml){
  if(const auto* source=AttemptBoxedSource(attempt,index)){
    const auto* location=StepErrorAttribution(*source);const auto path=XML(location->file_json);
    file.Put("Primary boxed error location: ");file.Put(path.text);file.Put(":");file.NumberValue(location->line);file.Put(":");file.NumberValue(location->column);file.Put("\nOriginal error: ");
  }
  file.Put(raw_xml);
}
inline void PutAttemptSource(AtomicFile& file,const ReportAttempt& attempt,const StepIndex& index){
  if(!attempt.originating_step)return;
  file.Put(",\"originatingStepId\":\"");file.NumberValue(attempt.originating_step);file.Put("\"");
  if(const auto* step=AttemptBoxedSource(attempt,index)){
    file.Put(",\"errorAttribution\":{\"stepId\":\"");file.NumberValue(step->id);file.Put("\",\"kind\":\"");file.Put(StepErrorAttributionKind(*step));file.Put("\",\"primary\":true,\"location\":");PutBoxLocation(file,*StepErrorAttribution(*step));file.Put("}");
  }
}
inline void PutStep(AtomicFile& file,const StepRecord& step){
  // The native managed invocation is a real step; preserve its original ancestry.
  file.Put("{\"id\":\"");file.NumberValue(step.id);file.Put("\",\"parentId\":\"");file.NumberValue(step.parent);file.Put("\",\"testId\":\"");file.NumberValue(step.owner.test_id);file.Put("\",\"generation\":\"");file.NumberValue(step.owner.attempt_generation);file.Put("\",\"callbackToken\":\"");file.NumberValue(step.owner.callback_token);file.Put("\",\"scopeId\":\"");file.NumberValue(step.owner.scope);file.Put("\",\"scopeGeneration\":\"");file.NumberValue(step.owner.scope_generation);file.Put("\",\"workerIndex\":");file.NumberValue(step.owner.worker_index);file.Put(",\"pid\":");file.NumberValue(step.owner.pid);file.Put(",\"title\":");file.Put(step.title_json);if(step.callback_iteration){file.Put(",\"callbackAssertion\":{\"id\":\"");file.NumberValue(step.callback_iteration->assertion);file.Put("\",\"invocation\":\"");file.NumberValue(step.callback_iteration->invocation);file.Put("\"");if(step.source_fence){file.Put(",\"sourceFence\":");PutSourceCallbackIterationReceipt(file,*step.source_fence);}file.Put("}");}if(step.originating_step){file.Put(",\"originatingStepId\":\"");file.NumberValue(step.originating_step);file.Put("\"");}PutStepBox(file,step);if(step.location){file.Put(",\"location\":{\"file\":");file.Put(step.location->file_json);file.Put(",\"line\":");file.NumberValue(step.location->line);file.Put(",\"column\":");file.NumberValue(step.location->column);file.Put(",\"origin\":\"");file.Put(step.location->explicit_location?"explicit":"callsite");file.Put("\"}");}file.Put(",\"status\":\"");file.Put(StepStatusName(step.status));file.Put("\",\"callbackKind\":\"");file.Put(step.owner.callback_kind);file.Put("\",\"timeoutMs\":");file.NumberValue(step.timeout_ms);file.Put(",\"startedMonotonicMs\":");file.NumberValue(step.started);file.Put(",\"endedMonotonicMs\":");file.NumberValue(*step.ended);file.Put(",\"deadlineMonotonicMs\":");file.NumberValue(step.deadline);file.Put(",\"durationMs\":");file.NumberValue(*step.ended-step.started);file.Put(",\"receiptMonotonicMs\":");file.NumberValue(*step.received);if(step.receipt!=StepReceipt::NotInvoked){file.Put(",\"physicalDurationMs\":");file.NumberValue(*step.received-step.started);}file.Put(",\"receipt\":\"");file.Put(step.receipt==StepReceipt::Settled?"settled":step.receipt==StepReceipt::NotInvoked?"notInvoked":"workerExited");file.Put("\",\"receiptOK\":");file.Put(step.receipt_ok?"true":"false");file.Put(",\"error\":");file.Put(step.error_json);file.Put(",\"receiptError\":");file.Put(step.receipt_error_json);if(step.declared_skip_request){file.Put(",\"declaredSkipRequestId\":\"");file.NumberValue(step.declared_skip_request);file.Put("\",\"callbackInvoked\":false");}if(step.test_skip){file.Put(",\"testSkip\":{\"requestId\":\"");file.NumberValue(step.test_skip->request);file.Put("\",\"reason\":");file.Put(step.test_skip->reason_json);file.Put("}");}if(step.receipt_test_skip){file.Put(",\"receiptTestSkipRequestId\":\"");file.NumberValue(step.receipt_test_skip);file.Put("\"");}if(step.step_skip){file.Put(",\"stepSkip\":{\"requestId\":\"");file.NumberValue(step.step_skip->request);file.Put("\",\"sourceStepId\":\"");file.NumberValue(step.step_skip->source_step);file.Put("\",\"reason\":");file.Put(step.step_skip->reason_json);file.Put(",\"descriptionPresent\":");file.Put(step.step_skip->description_present?"true":"false");file.Put("}");}if(step.receipt_step_skip){file.Put(",\"receiptStepSkipRequestId\":\"");file.NumberValue(step.receipt_step_skip);file.Put("\"");}if(step.callback_received){file.Put(",\"callbackReceiptMonotonicMs\":");file.NumberValue(*step.callback_received);file.Put(",\"callbackReceiptOK\":");file.Put(step.callback_ok?"true":"false");}if(step.soft_error_id){file.Put(",\"softErrorId\":\"");file.NumberValue(step.soft_error_id);file.Put("\"");}if(step.attachment_error_json){file.Put(",\"attachmentError\":");file.Put(*step.attachment_error_json);}file.Put("}");
}
inline void StepProperties(AtomicFile& file,const StepRecord& step){
  const auto prefix="native.step"+std::to_string(step.id)+".";StepBoxProperties(file,step,prefix);if(step.originating_step)NumericProperty(file,prefix+"originatingStepId",step.originating_step);if(step.soft_error_id)NumericProperty(file,prefix+"softErrorId",step.soft_error_id);if(step.location){const auto text=XML(step.location->file_json);Property(file,prefix+"location.file",text.text);NumericProperty(file,prefix+"location.fileXmlSanitizedUnits",text.sanitized);NumericProperty(file,prefix+"location.line",step.location->line);NumericProperty(file,prefix+"location.column",step.location->column);Property(file,prefix+"location.origin",step.location->explicit_location?"explicit":"callsite");}if(step.callback_received){NumericProperty(file,prefix+"callbackReceiptMonotonicMs",*step.callback_received);Property(file,prefix+"callbackReceiptOK",step.callback_ok?"true":"false");}if(step.attachment_error_json){const auto error=XML(*step.attachment_error_json);Property(file,prefix+"attachmentError",error.text);NumericProperty(file,prefix+"attachmentErrorXmlSanitizedUnits",error.sanitized);}NumericProperty(file,prefix+"parentId",step.parent);NumericProperty(file,prefix+"testId",step.owner.test_id);NumericProperty(file,prefix+"generation",step.owner.attempt_generation);NumericProperty(file,prefix+"callbackToken",step.owner.callback_token);NumericProperty(file,prefix+"scopeId",step.owner.scope);NumericProperty(file,prefix+"scopeGeneration",step.owner.scope_generation);NumericProperty(file,prefix+"workerIndex",step.owner.worker_index);NumericProperty(file,prefix+"pid",step.owner.pid);Property(file,prefix+"callbackKind",step.owner.callback_kind);NumericProperty(file,prefix+"timeoutMs",step.timeout_ms);Property(file,prefix+"status",StepStatusName(step.status));NumericProperty(file,prefix+"startedMonotonicMs",step.started);NumericProperty(file,prefix+"endedMonotonicMs",*step.ended);NumericProperty(file,prefix+"durationMs",*step.ended-step.started);NumericProperty(file,prefix+"receiptMonotonicMs",*step.received);Property(file,prefix+"receipt",step.receipt==StepReceipt::Settled?"settled":step.receipt==StepReceipt::NotInvoked?"notInvoked":"workerExited");Property(file,prefix+"receiptOK",step.receipt_ok?"true":"false");if(step.declared_skip_request){NumericProperty(file,prefix+"declaredSkipRequestId",step.declared_skip_request);Property(file,prefix+"callbackInvoked","false");}if(step.test_skip){NumericProperty(file,prefix+"testSkipRequestId",step.test_skip->request);const auto reason=XML(step.test_skip->reason_json);Property(file,prefix+"testSkipReason",reason.text);NumericProperty(file,prefix+"testSkipReasonXmlSanitizedUnits",reason.sanitized);}if(step.receipt_test_skip)NumericProperty(file,prefix+"receiptTestSkipRequestId",step.receipt_test_skip);if(step.step_skip){NumericProperty(file,prefix+"stepSkipRequestId",step.step_skip->request);NumericProperty(file,prefix+"stepSkipSourceStepId",step.step_skip->source_step);Property(file,prefix+"stepSkipDescriptionPresent",step.step_skip->description_present?"true":"false");const auto reason=XML(step.step_skip->reason_json);Property(file,prefix+"stepSkipReason",reason.text);NumericProperty(file,prefix+"stepSkipReasonXmlSanitizedUnits",reason.sanitized);}if(step.receipt_step_skip)NumericProperty(file,prefix+"receiptStepSkipRequestId",step.receipt_step_skip);for(const auto&[name,value]:{std::pair<std::string,const std::string&>{"title",step.title_json},{"error",step.error_json},{"receiptError",step.receipt_error_json}}){const auto text=XML(value);Property(file,prefix+name,text.text);NumericProperty(file,prefix+name+"XmlSanitizedUnits",text.sanitized);}
}
inline void PutAttachment(AtomicFile& file,const AttachmentRecord& a){
  file.Put("{");bool first=true;const auto integer=[&](std::string_view name,uint64_t value,bool string=false){if(!first)file.Put(",");first=false;file.Put("\"");file.Put(name);file.Put("\":");if(string)file.Put("\"");file.NumberValue(value);if(string)file.Put("\"");};
  if(a.step_id)integer("stepId",a.step_id,true);
  integer("id",a.id,true);integer("requestId",a.request,true);integer("testId",a.owner.test_id,true);integer("generation",a.owner.attempt_generation,true);integer("callbackToken",a.owner.callback_token,true);integer("scopeId",a.owner.scope,true);integer("scopeGeneration",a.owner.scope_generation,true);integer("workerIndex",a.owner.worker_index);integer("pid",static_cast<uint64_t>(a.owner.pid));integer("writerPid",static_cast<uint64_t>(a.writer_pid));integer("writerExitCode",static_cast<uint64_t>(a.writer_exit));integer("byteLength",a.bytes);integer("device",a.device,true);integer("inode",a.inode,true);
  file.Put(",\"writerExitConfirmed\":true,\"callbackKind\":\"");file.Put(a.owner.callback_kind);file.Put("\",\"sourceKind\":\"");file.Put(a.source_kind);file.Put("\",\"name\":");file.Put(a.name_json);file.Put(",\"contentType\":");file.Put(a.content_type_json);file.Put(",\"path\":");file.Put(AttachmentPathJSON(a.path));file.Put(",\"startedMonotonicMs\":");file.NumberValue(a.started);file.Put(",\"commitReceiptMonotonicMs\":");file.NumberValue(a.committed);file.Put(",\"writerExitReceiptMonotonicMs\":");file.NumberValue(a.received);file.Put(",\"durationMs\":");file.NumberValue(a.received-a.started);PutAttachmentCallbackRetry(file,a.callback_retry);file.Put("}");
}
inline void AttachmentProperties(AtomicFile& file,const AttachmentRecord& a){
  if(!a.callback_retry.Empty()){const auto prefix="native.attachment"+std::to_string(a.id)+".";NumericProperty(file,prefix+"callbackAssertionId",a.callback_retry.assertion_id);NumericProperty(file,prefix+"callbackInvocation",a.callback_retry.invocation_nonce);NumericProperty(file,prefix+"callbackIterationStepId",a.callback_retry.iteration_step);}
  const auto prefix="native.attachment"+std::to_string(a.id)+".";if(a.step_id)NumericProperty(file,prefix+"stepId",a.step_id);NumericProperty(file,prefix+"testId",a.owner.test_id);NumericProperty(file,prefix+"generation",a.owner.attempt_generation);NumericProperty(file,prefix+"callbackToken",a.owner.callback_token);NumericProperty(file,prefix+"scopeId",a.owner.scope);NumericProperty(file,prefix+"scopeGeneration",a.owner.scope_generation);NumericProperty(file,prefix+"workerIndex",a.owner.worker_index);NumericProperty(file,prefix+"pid",a.owner.pid);NumericProperty(file,prefix+"writerPid",a.writer_pid);NumericProperty(file,prefix+"writerExitCode",a.writer_exit);Property(file,prefix+"writerExitConfirmed","true");Property(file,prefix+"callbackKind",a.owner.callback_kind);Property(file,prefix+"sourceKind",a.source_kind);NumericProperty(file,prefix+"byteLength",a.bytes);NumericProperty(file,prefix+"device",a.device);NumericProperty(file,prefix+"inode",a.inode);NumericProperty(file,prefix+"startedMonotonicMs",a.started);NumericProperty(file,prefix+"commitReceiptMonotonicMs",a.committed);NumericProperty(file,prefix+"writerExitReceiptMonotonicMs",a.received);
  const auto path_json=AttachmentPathJSON(a.path);
  for(const auto& [label,value]:{std::pair<std::string,const std::string&>{"name",a.name_json},{"contentType",a.content_type_json},{"path",path_json}}){const auto text=XML(value);Property(file,prefix+label,text.text);NumericProperty(file,prefix+label+"XmlSanitizedUnits",text.sanitized);}
}
inline const char* OutputStatusName(OutputDirectoryStatus status){switch(status){case OutputDirectoryStatus::Ready:return "ready";case OutputDirectoryStatus::Failed:return "failed";case OutputDirectoryStatus::TimedOut:return "timedOut";case OutputDirectoryStatus::Interrupted:return "interrupted";default:throw std::invalid_argument("Native output directory is not physically retired");}}
inline void PutOutput(AtomicFile& file,const OutputDirectoryRecord& o){
  file.Put("{\"id\":\"");file.NumberValue(o.id);file.Put("\",\"testId\":\"");file.NumberValue(o.owner.test_id);file.Put("\",\"generation\":\"");file.NumberValue(o.owner.generation);file.Put("\",\"workerIndex\":");file.NumberValue(o.owner.worker_index);file.Put(",\"pid\":");file.NumberValue(o.owner.pid);file.Put(",\"status\":\"");file.Put(OutputStatusName(o.status));file.Put("\",\"proposedPath\":");file.Put(AttachmentPathJSON(o.path));
  if(o.assigned){file.Put(",\"outputDir\":");file.Put(AttachmentPathJSON(o.path));file.Put(",\"assignmentReceiptMonotonicMs\":");file.NumberValue(*o.assigned);}
  file.Put(",\"startedMonotonicMs\":");file.NumberValue(o.started);file.Put(",\"deadlineMonotonicMs\":");file.NumberValue(o.deadline);file.Put(",\"writerPid\":");file.NumberValue(o.writer_pid);file.Put(",\"writerSignaled\":");file.Put(o.writer_signaled?"true":"false");file.Put(",\"writerExitCode\":");file.NumberValue(o.writer_exit);file.Put(",\"writerExitReceiptMonotonicMs\":");file.NumberValue(*o.writer_received);file.Put(",\"physicalReceiptMonotonicMs\":");file.NumberValue(*o.received);
  if(o.created){file.Put(",\"createdReceiptMonotonicMs\":");file.NumberValue(*o.created);file.Put(",\"device\":");file.NumberValue(o.device);file.Put(",\"inode\":\"");file.NumberValue(o.inode);file.Put("\",\"parentDevice\":");file.NumberValue(o.parent_device);file.Put(",\"parentInode\":\"");file.NumberValue(o.parent_inode);file.Put("\"");}
  file.Put(",\"readyReceipt\":");file.Put(o.ready_receipt?"true":"false");file.Put(",\"rollbackReceipt\":");file.Put(o.rollback_receipt?"true":"false");file.Put(",\"foreignPreserved\":");file.Put(o.foreign_preserved?"true":"false");
  if(o.rollback_pid){file.Put(",\"rollbackPid\":");file.NumberValue(o.rollback_pid);file.Put(",\"rollbackSignaled\":");file.Put(o.rollback_signaled?"true":"false");file.Put(",\"rollbackExitCode\":");file.NumberValue(o.rollback_exit);}
  file.Put(",\"error\":");file.Put(AttachmentPathJSON(o.error));file.Put(",\"cleanupError\":");file.Put(AttachmentPathJSON(o.cleanup_error));file.Put("}");
}
inline void OutputProperties(AtomicFile& file,const OutputDirectoryRecord& o){
  const auto prefix="native.output"+std::to_string(o.id)+".";NumericProperty(file,prefix+"testId",o.owner.test_id);NumericProperty(file,prefix+"generation",o.owner.generation);NumericProperty(file,prefix+"workerIndex",o.owner.worker_index);NumericProperty(file,prefix+"pid",o.owner.pid);Property(file,prefix+"status",OutputStatusName(o.status));Property(file,prefix+"proposedPath",XML(AttachmentPathJSON(o.path)).text);if(o.assigned){Property(file,prefix+"outputDir",XML(AttachmentPathJSON(o.path)).text);NumericProperty(file,prefix+"assignmentReceiptMonotonicMs",*o.assigned);}NumericProperty(file,prefix+"writerPid",o.writer_pid);NumericProperty(file,prefix+"writerExitCode",o.writer_exit);Property(file,prefix+"writerSignaled",o.writer_signaled?"true":"false");NumericProperty(file,prefix+"writerExitReceiptMonotonicMs",*o.writer_received);NumericProperty(file,prefix+"physicalReceiptMonotonicMs",*o.received);Property(file,prefix+"rollbackReceipt",o.rollback_receipt?"true":"false");if(o.created){NumericProperty(file,prefix+"device",o.device);NumericProperty(file,prefix+"inode",o.inode);NumericProperty(file,prefix+"parentDevice",o.parent_device);NumericProperty(file,prefix+"parentInode",o.parent_inode);}if(o.rollback_pid){NumericProperty(file,prefix+"rollbackPid",o.rollback_pid);NumericProperty(file,prefix+"rollbackExitCode",o.rollback_exit);}Property(file,prefix+"error",XML(AttachmentPathJSON(o.error)).text);Property(file,prefix+"cleanupError",XML(AttachmentPathJSON(o.cleanup_error)).text);
}
inline void PutAnnotations(AtomicFile& file,const std::vector<ModifierAnnotation>& values){file.Put("[");bool first=true;for(const auto& annotation:values){if(!first)file.Put(",");first=false;file.Put(ModifierAnnotationJSON(annotation));}file.Put("]");}
inline void PutAttemptSecondaryErrors(AtomicFile& file,const ReportAttempt& attempt){
  if(attempt.secondary_errors.empty())return;
  file.Put(",\"secondaryErrors\":[");bool first=true;
  for(const auto& secondary:attempt.secondary_errors){const auto& source=secondary.source;const auto& owner=source.attempt;
    if(!first){file.Put(",");}first=false;file.Put("{\"origin\":\"");file.Put(secondary.fixture_raw?"fixtureRaw":"phaseResult");file.Put("\",\"testId\":\"");file.NumberValue(owner.test);file.Put("\",\"generation\":\"");file.NumberValue(owner.generation);file.Put("\",\"workerIndex\":");file.NumberValue(owner.worker);file.Put(",\"pid\":");file.NumberValue(owner.pid);file.Put(",\"scopeId\":\"");file.NumberValue(source.scope);file.Put("\",\"scopeGeneration\":\"");file.NumberValue(source.generation);file.Put("\",\"error\":");file.Put(secondary.error_json);if(secondary.originating_step){file.Put(",\"originatingStepId\":\"");file.NumberValue(secondary.originating_step);file.Put("\"");}file.Put("}");
  }file.Put("]");
}
inline void AttemptSecondaryProperties(AtomicFile& file,const ReportAttempt& attempt,const std::string& prefix){
  for(size_t i=0;i<attempt.secondary_errors.size();++i){const auto& secondary=attempt.secondary_errors[i];const auto& source=secondary.source;const auto& owner=source.attempt;const auto key=prefix+"secondaryError"+std::to_string(i)+".";const auto text=XML(secondary.error_json);
    Property(file,key+"origin",secondary.fixture_raw?"fixtureRaw":"phaseResult");NumericProperty(file,key+"testId",owner.test);NumericProperty(file,key+"generation",owner.generation);NumericProperty(file,key+"workerIndex",owner.worker);NumericProperty(file,key+"pid",owner.pid);NumericProperty(file,key+"scopeId",source.scope);NumericProperty(file,key+"scopeGeneration",source.generation);Property(file,key+"error",text.text);NumericProperty(file,key+"errorXmlSanitizedUnits",text.sanitized);if(secondary.originating_step)NumericProperty(file,key+"originatingStepId",secondary.originating_step);
  }
}
} // namespace report_detail

// The scalar JSON strings are validated before any report file is created.
// Worker byte streams are encoded as hex; UTF16 metadata remains lossless JSON.
inline void WriteRunnerJson(const RunnerReport& report,const std::filesystem::path& path,NativeReportPublication* publication=nullptr){
  const auto summary=report_detail::Validate(report);const auto step_index=report_detail::IndexSteps(report.steps);report_detail::AtomicFile file(path,publication);
  file.Put("{\"version\":1,\"summary\":{\"tests\":");file.NumberValue(summary.total_tests());file.Put(",\"attempts\":");file.NumberValue(report.attempts.size());file.Put(",\"expected\":");file.NumberValue(summary.expected);file.Put(",\"unexpected\":");file.NumberValue(summary.unexpected);file.Put(",\"skipped\":");file.NumberValue(summary.skipped);file.Put(",\"flaky\":");file.NumberValue(summary.flaky);file.Put(",\"didNotRun\":");file.NumberValue(summary.did_not_run);file.Put(",\"globalErrors\":");file.NumberValue(report.global_errors_json.size());file.Put(",\"totalAttemptDurationMs\":");file.NumberValue(summary.duration_ms);file.Put("},\"attempts\":[");
  bool first=true;for(const auto& attempt:report.attempts){if(!first)file.Put(",");first=false;file.Put("{\"testId\":\"");file.NumberValue(attempt.test_id);file.Put("\",\"generation\":\"");file.NumberValue(attempt.generation);file.Put("\",\"retry\":");file.NumberValue(attempt.retry);file.Put(",\"workerIndex\":");file.NumberValue(attempt.worker_index);file.Put(",\"pid\":");file.NumberValue(attempt.pid);file.Put(",\"title\":");file.Put(attempt.title_json);file.Put(",\"error\":");file.Put(attempt.error_json);file.Put(",\"status\":\"");file.Put(attempt.status);file.Put("\",\"expected\":");file.Put(attempt.expected?"true":"false");file.Put(",\"durationMs\":");file.NumberValue(attempt.duration_ms);if(attempt.wall_duration_ms){file.Put(",\"wallDurationMs\":");file.NumberValue(*attempt.wall_duration_ms);}report_detail::PutAttemptSecondaryErrors(file,attempt);if(attempt.skip_reason_json){file.Put(",\"skipReason\":");file.Put(*attempt.skip_reason_json);}if(!attempt.expected_status.empty()){file.Put(",\"expectedStatus\":\"");file.Put(attempt.expected_status);file.Put("\"");}if(attempt.expected_failure_reason_json){file.Put(",\"expectedFailureReason\":");file.Put(*attempt.expected_failure_reason_json);}file.Put(",\"annotations\":");report_detail::PutAnnotations(file,attempt.annotations);report_detail::PutAttemptSource(file,attempt,step_index);if(attempt.soft_error_id){file.Put(",\"softErrorId\":\"");file.NumberValue(attempt.soft_error_id);file.Put("\"");}file.Put("}");}
  file.Put("]");if(!report.callback_assertions.empty()){file.Put(",\"callbackAssertions\":");PutCallbackAssertions(file,report.callback_assertions);}if(!report.soft_assertions.empty()){file.Put(",\"softAssertions\":");PutSoftAssertions(file,report.soft_assertions);}if(!report.files.empty()){file.Put(",\"files\":");PutFileReports(file,report.files);file.Put(",\"fileWorkGroups\":");PutFileGroupReports(file,report.file_work_groups);if(!report.file_resource_locks.empty()){file.Put(",\"fileResourceLocks\":");PutFileLockReports(file,report.file_resource_locks);}}if(!report.work_groups.empty()){file.Put(",\"workGroups\":");PutWorkGroupReports(file,report.work_groups);}if(!report.resource_locks.empty()){file.Put(",\"resourceLocks\":");PutResourceLockReports(file,report.resource_locks);}if(!report.serial_groups.empty()){file.Put(",\"serialGroups\":");PutSerialGroupReports(file,report.serial_groups);}if(!report.projects.empty()){file.Put(",\"projects\":");PutProjectReports(file,report.projects);}if(!report.worker_reuse.hosts.empty()){file.Put(",\"workerReuse\":");PutWorkerReuseReports(file,report.worker_reuse);}file.Put(",\"workers\":[");first=true;for(const auto& worker:report.workers){if(!first)file.Put(",");first=false;file.Put("{\"workerIndex\":");file.NumberValue(worker.worker_index);file.Put(",\"pid\":");file.NumberValue(worker.pid);file.Put(",\"exitStatusKnown\":");file.Put(worker.exit_code>=0?"true":"false");file.Put(",\"exitCode\":");if(worker.exit_code>=0&&!worker.signaled)file.NumberValue(worker.exit_code);else file.Put("null");file.Put(",\"signal\":");if(worker.signaled)file.NumberValue(worker.exit_code);else file.Put("null");for(const auto& item:{std::pair<std::string_view,const std::string&>{"stdout",worker.stdout_bytes},{"stderr",worker.stderr_bytes}}){file.Put(",\"");file.Put(item.first);file.Put("\":{\"encoding\":\"hex\",\"byteLength\":");file.NumberValue(item.second.size());file.Put(",\"data\":\"");file.Hex(item.second);file.Put("\"}");}file.Put("}");}
  file.Put("],\"skipped\":[");first=true;for(const auto& declaration:report.skipped){if(!first)file.Put(",");first=false;file.Put("{\"testId\":\"");file.NumberValue(declaration.test_id);file.Put("\",\"title\":");file.Put(declaration.title_json);file.Put(",\"reason\":");file.Put(declaration.reason_json);file.Put("}");}
  file.Put("],\"skipCauses\":[");first=true;for(const auto& cause:report.skip_causes){if(!first)file.Put(",");first=false;file.Put("{\"causeId\":\"");file.NumberValue(cause.scope_id);file.Put("\",\"scopeGeneration\":\"");file.NumberValue(cause.scope_generation);file.Put("\",\"callbackId\":\"");file.NumberValue(cause.callback_id);file.Put("\",\"callbackToken\":\"");file.NumberValue(cause.callback_token);file.Put("\",\"associatedTestId\":\"");file.NumberValue(cause.test_id);file.Put("\",\"associatedGeneration\":\"");file.NumberValue(cause.generation);file.Put("\",\"suiteId\":\"");file.NumberValue(cause.suite_id);file.Put("\",\"workerIndex\":");file.NumberValue(cause.worker_index);file.Put(",\"pid\":");file.NumberValue(cause.pid);file.Put(",\"reason\":");file.Put(cause.reason_json);file.Put(",\"status\":\"");file.Put(cause.status);file.Put("\",\"annotations\":[");if(cause.annotation)file.Put(ModifierAnnotationJSON(*cause.annotation));file.Put("]}");}
  file.Put("],\"runtimeSkipped\":[");first=true;for(const auto& declaration:report.runtime_skipped){if(!first)file.Put(",");first=false;file.Put("{\"testId\":\"");file.NumberValue(declaration.test_id);file.Put("\",\"causeId\":\"");file.NumberValue(declaration.cause_id);file.Put("\",\"title\":");file.Put(declaration.title_json);file.Put(",\"annotations\":[");const auto cause=std::find_if(report.skip_causes.begin(),report.skip_causes.end(),[&](const auto& value){return value.scope_id==declaration.cause_id;});if(cause->annotation)file.Put(ModifierAnnotationJSON(*cause->annotation));file.Put("]}");}
  file.Put("],\"unrun\":[");first=true;for(const auto& declaration:report.unrun){if(!first)file.Put(",");first=false;file.Put("{\"testId\":\"");file.NumberValue(declaration.test_id);file.Put("\",\"title\":");file.Put(declaration.title_json);file.Put(",\"reason\":");file.Put(declaration.reason_json);file.Put("}");}
  file.Put("],\"steps\":[");first=true;for(const auto& step:report.steps){if(!first)file.Put(",");first=false;report_detail::PutStep(file,step);}
  file.Put("]");if(!report.attachment_declarations.empty()){file.Put(",\"attachmentDeclarations\":");PutAttachmentDeclarationReports(file,report.attachment_declarations);}if(!report.attachment_views.empty()){file.Put(",\"attachmentViews\":");PutAttachmentProjections(file,report.attachment_views);}file.Put(",\"attachments\":[");first=true;for(const auto& artifact:report.attachments){if(!first)file.Put(",");first=false;report_detail::PutAttachment(file,artifact);}
  file.Put("]");if(report.output_preservation){file.Put(",\"preserveOutput\":\"");file.Put(PreserveOutputName(*report.output_preservation));file.Put("\",\"outputRetention\":");PutOutputRetentionReports(file,report.output_retentions);}
  file.Put(",\"outputs\":[");first=true;for(const auto& output:report.outputs){if(!first)file.Put(",");first=false;report_detail::PutOutput(file,output);}
  file.Put("]");if(report.run_policy.enabled){file.Put(",\"runPolicy\":");report.run_policy.Write(file,summary.flaky);}if(report.repeat_configuration){file.Put(",\"repeatConfiguration\":");report.repeat_configuration->Write(file);}if(!report.retained_sources.empty()){file.Put(",\"retainedSources\":");PutRetainedSourceReports(file,report.retained_sources);}file.Put(",\"globalErrors\":[");first=true;for(const auto& error:report.global_errors_json){if(!first)file.Put(",");first=false;file.Put(error);}file.Put("]}\n");file.Commit();
}

// JUnit emits one final testcase per test identity and retains every earlier
// attempt as properties. XML1.0-invalid scalars become U+FFFD with explicit
// per-field counts; the JSON report retains the original UTF16 units.
inline void WriteRunnerJUnit(const RunnerReport& report,const std::filesystem::path& path){
  using namespace report_detail;const auto summary=Validate(report);const auto step_index=IndexSteps(report.steps);AtomicFile file(path);
  file.Put("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites tests=\"");file.NumberValue(summary.total_tests()+report.global_errors_json.size());file.Put("\" failures=\"");file.NumberValue(summary.failures);file.Put("\" errors=\"");file.NumberValue(summary.errors);file.Put("\" skipped=\"");file.NumberValue(summary.skipped+summary.did_not_run);file.Put("\" time=\"");file.DecimalValue(summary.duration_ms/1000);file.Put("\"><testsuite name=\"SuiteCut native\" tests=\"");file.NumberValue(summary.total_tests()+report.global_errors_json.size());file.Put("\" failures=\"");file.NumberValue(summary.failures);file.Put("\" errors=\"");file.NumberValue(summary.errors);file.Put("\" skipped=\"");file.NumberValue(summary.skipped+summary.did_not_run);file.Put("\" time=\"");file.DecimalValue(summary.duration_ms/1000);file.Put("\"><properties>");Property(file,"native.binaryStreams","Suite-level worker byte streams are hex encoded, without per-test attribution");Property(file,"native.xmlSanitization","XML1.0-invalid scalars become U+FFFD; JSON preserves original UTF16");
  report.run_policy.Properties(file,summary.flaky);
  if(report.repeat_configuration)Property(file,"native.repeatConfiguration",report.repeat_configuration->JSON());
  if(!report.attachment_views.empty()){
    const auto original=AttachmentProjectionJSON(report.attachment_views);const auto text=XML(AttachmentProjectionQuotedJSON(original));
    Property(file,"native.attachmentViewsJSON",text.text);NumericProperty(file,"native.attachmentViewsXmlSanitizedUnits",text.sanitized);
    if(text.sanitized){file.Put("<property name=\"native.attachmentViewsJSONUtf8Hex\" value=\"");file.Hex(original);file.Put("\"/>");}
  }
  for(const auto& step:report.steps)StepProperties(file,step);
  for(const auto& step:report.steps)if(step.callback_iteration){const auto prefix="native.step"+std::to_string(step.id)+".";NumericProperty(file,prefix+"callbackAssertionId",step.callback_iteration->assertion);NumericProperty(file,prefix+"callbackInvocation",step.callback_iteration->invocation);}
  for(const auto& record:report.callback_assertions){const auto prefix="native.callbackAssertion"+std::to_string(record.id)+".";NumericProperty(file,prefix+"requestId",record.request);NumericProperty(file,prefix+"testId",record.owner.test_id);NumericProperty(file,prefix+"generation",record.owner.attempt_generation);NumericProperty(file,prefix+"parentStepId",record.parent);NumericProperty(file,prefix+"attempts",record.attempts);NumericProperty(file,prefix+"lastInvocation",record.last_invocation);NumericProperty(file,prefix+"startedMonotonicMs",record.started);NumericProperty(file,prefix+"deadlineMonotonicMs",record.deadline);if(record.own_deadline)NumericProperty(file,prefix+"ownDeadlineMonotonicMs",*record.own_deadline);NumericProperty(file,prefix+"receiptMonotonicMs",record.received);Property(file,prefix+"status",CallbackAssertionStatusName(record.status));Property(file,prefix+"receipt",CallbackAssertionReceiptName(record.receipt));const auto error=XML(record.error_json);Property(file,prefix+"error",error.text);NumericProperty(file,prefix+"errorXmlSanitizedUnits",error.sanitized);}
  for(const auto& r:report.soft_assertions){const auto prefix="native.softAssertion"+std::to_string(r.id)+".";const auto& o=r.owner;NumericProperty(file,prefix+"requestId",r.request);NumericProperty(file,prefix+"testId",o.test_id);NumericProperty(file,prefix+"generation",o.attempt_generation);NumericProperty(file,prefix+"callbackToken",o.callback_token);NumericProperty(file,prefix+"scopeId",o.scope);NumericProperty(file,prefix+"scopeGeneration",o.scope_generation);NumericProperty(file,prefix+"workerIndex",o.worker_index);NumericProperty(file,prefix+"pid",o.pid);if(r.step)NumericProperty(file,prefix+"stepId",r.step);NumericProperty(file,prefix+"admittedMonotonicMs",r.admitted);NumericProperty(file,prefix+"receiptMonotonicMs",r.received);Property(file,prefix+"callbackKind",o.callback_kind);Property(file,prefix+"rawOutcome",SoftRawName(r.raw));Property(file,prefix+"receipt",SoftReceiptName(r.receipt));Property(file,prefix+"acceptedMismatch",r.receipt==SoftReceipt::Mismatch?"true":"false");Property(file,prefix+"acceptedDeadline",r.receipt==SoftReceipt::AcceptedDeadline?"true":"false");Property(file,prefix+"cleanup",r.cleanup?"true":"false");for(const auto& value:{std::pair<std::string,const std::string&>{"error",r.error_json},{"cancelReason",r.cancel_reason_json}}){const auto text=XML(value.second);Property(file,prefix+value.first,text.text);NumericProperty(file,prefix+value.first+"XmlSanitizedUnits",text.sanitized);}if(r.location){const auto text=XML(r.location->file_json);Property(file,prefix+"location.file",text.text);NumericProperty(file,prefix+"location.fileXmlSanitizedUnits",text.sanitized);NumericProperty(file,prefix+"location.line",r.location->line);NumericProperty(file,prefix+"location.column",r.location->column);Property(file,prefix+"location.origin","callsite");}}

  for(const auto& declaration:report.attachment_declarations){const auto prefix="native.attachmentDeclaration"+std::to_string(declaration.id)+".";const auto& owner=declaration.owner;NumericProperty(file,prefix+"requestId",declaration.request);NumericProperty(file,prefix+"testId",owner.test_id);NumericProperty(file,prefix+"generation",owner.attempt_generation);NumericProperty(file,prefix+"callbackToken",owner.callback_token);NumericProperty(file,prefix+"scopeId",owner.scope);NumericProperty(file,prefix+"scopeGeneration",owner.scope_generation);NumericProperty(file,prefix+"workerIndex",owner.worker_index);NumericProperty(file,prefix+"pid",owner.pid);Property(file,prefix+"callbackKind",owner.callback_kind);Property(file,prefix+"sourceKind",AttachmentDeclarationSource(declaration.source));Property(file,prefix+"receipt",AttachmentDeclarationReceipt(declaration.receipt));NumericProperty(file,prefix+"declaredMonotonicMs",declaration.declared);NumericProperty(file,prefix+"terminalMonotonicMs",*declaration.received);for(const auto& value:{std::pair<std::string,const std::string&>{"name",declaration.name_json},{"contentType",declaration.type_json}}){const auto text=XML(value.second);Property(file,prefix+value.first,text.text);NumericProperty(file,prefix+value.first+"XmlSanitizedUnits",text.sanitized);}if(declaration.author_path_json){const auto text=XML(*declaration.author_path_json);Property(file,prefix+"declaredPath",text.text);NumericProperty(file,prefix+"declaredPathXmlSanitizedUnits",text.sanitized);}if(declaration.expected_bytes)NumericProperty(file,prefix+"declaredByteLength",*declaration.expected_bytes);if(declaration.receipt==AttachmentDeclarations::Receipt::Committed)NumericProperty(file,prefix+"attachmentId",declaration.artifact_id);if(declaration.receipt==AttachmentDeclarations::Receipt::Failed||declaration.receipt==AttachmentDeclarations::Receipt::Rejected){const auto text=XML(declaration.error_json);Property(file,prefix+"error",text.text);NumericProperty(file,prefix+"errorXmlSanitizedUnits",text.sanitized);}if(declaration.cancellation_request)NumericProperty(file,prefix+"cancellationRequestId",declaration.cancellation_request);}
  for(const auto& artifact:report.attachments)AttachmentProperties(file,artifact);
  for(const auto& output:report.outputs)OutputProperties(file,output);
  if(report.output_preservation){Property(file,"native.preserveOutput",PreserveOutputName(*report.output_preservation));const auto original=OutputRetentionJSON(report.output_retentions);const auto text=XML(AttachmentProjectionQuotedJSON(original));Property(file,"native.outputRetentionJSON",text.text);NumericProperty(file,"native.outputRetentionXmlSanitizedUnits",text.sanitized);if(text.sanitized){file.Put("<property name=\"native.outputRetentionJSONUtf8Hex\" value=\"");file.Hex(original);file.Put("\"/>");}}
  for(const auto& cause:report.skip_causes){const auto prefix="native.beforeAllSkipCause"+std::to_string(cause.scope_id)+".";NumericProperty(file,prefix+"associatedTestId",cause.test_id);NumericProperty(file,prefix+"associatedGeneration",cause.generation);NumericProperty(file,prefix+"scopeGeneration",cause.scope_generation);NumericProperty(file,prefix+"callbackId",cause.callback_id);NumericProperty(file,prefix+"callbackToken",cause.callback_token);NumericProperty(file,prefix+"workerIndex",cause.worker_index);NumericProperty(file,prefix+"pid",cause.pid);NumericProperty(file,prefix+"suiteId",cause.suite_id);Property(file,prefix+"status",cause.status);const auto reason=XML(cause.reason_json);Property(file,prefix+"reason",reason.text);NumericProperty(file,prefix+"reasonXmlSanitizedUnits",reason.sanitized);if(cause.annotation)AnnotationProperties(file,prefix+"annotation.",*cause.annotation);}
  file.Put("</properties>");
  for(const auto& [id,attempts]:summary.tests){
    const auto& last=*attempts.back();const auto title=XML(last.title_json),error=XML(last.error_json);double duration=0;for(const auto* attempt:attempts)duration+=attempt->duration_ms;
    file.Put("<testcase classname=\"SuiteCut\" name=\"");file.Put(title.text);file.Put("\" time=\"");file.DecimalValue(duration/1000);file.Put("\"><properties>");NumericProperty(file,"native.testId",id);NumericProperty(file,"native.finalRetry",last.retry);NumericProperty(file,"native.titleXmlSanitizedUnits",title.sanitized);
    for(size_t i=0;i<attempts.size();++i){const auto& attempt=*attempts[i];const auto prefix="native.attempt"+std::to_string(i)+".";const auto text=XML(attempt.error_json);AttemptSecondaryProperties(file,attempt,prefix);if(attempt.originating_step)NumericProperty(file,prefix+"originatingStepId",attempt.originating_step);if(attempt.soft_error_id)NumericProperty(file,prefix+"softErrorId",attempt.soft_error_id);if(const auto* source=AttemptBoxedSource(attempt,step_index))StepBoxProperties(file,*source,prefix);NumericProperty(file,prefix+"generation",attempt.generation);NumericProperty(file,prefix+"retry",attempt.retry);NumericProperty(file,prefix+"workerIndex",attempt.worker_index);NumericProperty(file,prefix+"pid",attempt.pid);NumericProperty(file,prefix+"durationMs",attempt.duration_ms);if(attempt.wall_duration_ms)NumericProperty(file,prefix+"wallDurationMs",*attempt.wall_duration_ms);Property(file,prefix+"status",attempt.status);Property(file,prefix+"expected",attempt.expected?"true":"false");Property(file,prefix+"error",text.text);NumericProperty(file,prefix+"errorXmlSanitizedUnits",text.sanitized);if(attempt.skip_reason_json){const auto reason=XML(*attempt.skip_reason_json);Property(file,prefix+"skipReason",reason.text);NumericProperty(file,prefix+"skipReasonXmlSanitizedUnits",reason.sanitized);}if(!attempt.expected_status.empty())Property(file,prefix+"expectedStatus",attempt.expected_status);if(attempt.expected_failure_reason_json){const auto reason=XML(*attempt.expected_failure_reason_json);Property(file,prefix+"expectedFailureReason",reason.text);NumericProperty(file,prefix+"expectedFailureReasonXmlSanitizedUnits",reason.sanitized);}for(size_t n=0;n<attempt.annotations.size();++n)AnnotationProperties(file,prefix+"annotation"+std::to_string(n)+".",attempt.annotations[n]);}
    file.Put("</properties>");if(summary.final_skipped.at(id))file.Put("<skipped/>");else if(!summary.final_expected.at(id)){const bool interrupted=last.status=="interrupted";file.Put(interrupted?"<error type=\"interrupted\" message=\"":"<failure type=\"");if(!interrupted){file.Put(last.status=="passed"?"unexpectedPass":last.status);file.Put("\" message=\"");}PutAttemptDisplayedError(file,last,step_index,error.text);file.Put("\">");PutAttemptDisplayedError(file,last,step_index,error.text);file.Put(interrupted?"</error>":"</failure>");}file.Put("</testcase>");
  }
  for(const auto& declaration:report.skipped){
    const auto title=XML(declaration.title_json),reason=XML(declaration.reason_json);
    file.Put("<testcase classname=\"SuiteCut\" name=\"");file.Put(title.text);file.Put("\"><properties>");NumericProperty(file,"native.testId",declaration.test_id);Property(file,"native.declaredSkip","true");Property(file,"native.noAttempt","true");NumericProperty(file,"native.titleXmlSanitizedUnits",title.sanitized);NumericProperty(file,"native.reasonXmlSanitizedUnits",reason.sanitized);file.Put("</properties><skipped type=\"declared\" message=\"");file.Put(reason.text);file.Put("\">");file.Put(reason.text);file.Put("</skipped></testcase>");
  }
  for(const auto& declaration:report.runtime_skipped){
    const auto title=XML(declaration.title_json);const auto cause=std::find_if(report.skip_causes.begin(),report.skip_causes.end(),[&](const auto& entry){return entry.scope_id==declaration.cause_id;});const auto reason=XML(cause->reason_json);
    file.Put("<testcase classname=\"SuiteCut\" name=\"");file.Put(title.text);file.Put("\"><properties>");NumericProperty(file,"native.testId",declaration.test_id);NumericProperty(file,"native.beforeAllSkipCauseId",declaration.cause_id);if(cause->annotation)AnnotationProperties(file,"native.annotation0.",*cause->annotation);NumericProperty(file,"native.titleXmlSanitizedUnits",title.sanitized);NumericProperty(file,"native.reasonXmlSanitizedUnits",reason.sanitized);Property(file,"native.noAttempt","true");file.Put("</properties><skipped type=\"runtimeBeforeAll\" message=\"");file.Put(reason.text);file.Put("\"/></testcase>");
  }
  for(const auto& declaration:report.unrun){
    const auto title=XML(declaration.title_json),reason=XML(declaration.reason_json);
    file.Put("<testcase classname=\"SuiteCut\" name=\"");file.Put(title.text);file.Put("\"><properties>");NumericProperty(file,"native.testId",declaration.test_id);Property(file,"native.didNotRun","true");Property(file,"native.noAttempt","true");NumericProperty(file,"native.titleXmlSanitizedUnits",title.sanitized);NumericProperty(file,"native.reasonXmlSanitizedUnits",reason.sanitized);file.Put("</properties><skipped type=\"didNotRun\" message=\"");file.Put(reason.text);file.Put("\">");file.Put(reason.text);file.Put("</skipped></testcase>");
  }
  for(size_t i=0;i<report.global_errors_json.size();++i){const auto error=XML(report.global_errors_json[i]);file.Put("<testcase classname=\"SuiteCut\" name=\"Native runner global error ");file.NumberValue(i+1);file.Put("\"><properties>");NumericProperty(file,"native.errorXmlSanitizedUnits",error.sanitized);file.Put("</properties><error type=\"global\" message=\"");file.Put(error.text);file.Put("\">");file.Put(error.text);file.Put("</error></testcase>");}
  for(const bool stderr_stream:{false,true}){file.Put(stderr_stream?"<system-err>":"<system-out>");for(const auto& worker:report.workers){file.Put("worker=");file.NumberValue(worker.worker_index);file.Put(" pid=");file.NumberValue(worker.pid);file.Put(" encoding=hex bytes=");const auto& bytes=stderr_stream?worker.stderr_bytes:worker.stdout_bytes;file.NumberValue(bytes.size());file.Put("\n");file.Hex(bytes);file.Put("\n");}file.Put(stderr_stream?"</system-err>":"</system-out>");}
  file.Put("</testsuite></testsuites>\n");file.Commit();
}
} // namespace suitecut::runner
