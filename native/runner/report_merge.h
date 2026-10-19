// SPDX-License-Identifier: MIT
#pragma once
#include "report_merge_json.h"
#include "fail_on_flaky_json.h"
#include "repeat_configuration_json.h"
#include "runner_report.h"
#include "runner_html.h"
#include "directory_report.h"
#include <fstream>
#include <memory>
#include <sys/stat.h>

namespace suitecut::runner {
namespace merge_detail {
inline unsigned Index(const JSON& row,std::string_view key){const auto value=UInt(row,key);if(value>UINT32_MAX)Invalid("worker/index exceeds native capacity");return static_cast<unsigned>(value);}
inline int64_t PID(const JSON& row,std::string_view key){const auto value=UInt(row,key);if(!value||value>INT32_MAX)Invalid("invalid process identity");return static_cast<int64_t>(value);}
inline std::vector<uint64_t> IDs(const JSON& row,std::string_view key){std::vector<uint64_t> result;for(const auto& id:Rows(row,key)){const auto value=Integer(id,true);if(!value)Invalid("zero test/project identity");result.push_back(value);}return result;}
inline StepOwner Owner(const JSON& row){return {ID(row,"testId"),ID(row,"generation"),ID(row,"callbackToken",true),ID(row,"scopeId"),ID(row,"scopeGeneration"),Index(row,"workerIndex"),PID(row,"pid"),Text(Get(row,"callbackKind"))};}
inline void OptionalString(const JSON& row,std::string_view key,size_t limit,std::optional<std::string>& target){if(Find(row,key))target=CheckedString(row,key,limit);}
inline std::string Wire(std::string_view checked){
  if(checked=="\"\"")return "-";
  std::string result;
  for(size_t at=1;at+1<checked.size();at+=6){
    if(at+5>=checked.size()-1||checked.substr(at,2)!="\\u")Invalid("modifier text lacks original native UTF16 encoding");
    result+=checked.substr(at+4,2);result+=checked.substr(at+2,2);
  }
  return result;
}
inline ModifierAnnotation Annotation(const JSON& row){
  ModifierAnnotation value;value.type=Text(Get(row,"type"));
  if(Find(row,"description")){value.description_present=true;value.reason_json=CheckedString(row,"description",8192);value.reason_wire=Wire(value.reason_json);}
  if(const auto* location=Find(row,"location")){ModifierLocation loc;loc.file_json=CheckedString(*location,"file",8192);loc.file_wire=Wire(loc.file_json);loc.line=Index(*location,"line");loc.column=Index(*location,"column");value.location=loc;}
  ValidateModifierAnnotation(value);return value;
}
inline std::vector<AttemptSecondaryError> SecondaryErrors(const JSON& row,const ReportAttempt& attempt){
  std::vector<AttemptSecondaryError> result;const auto& rows=OptionalRows(row,"secondaryErrors");if(rows.size()>256)Invalid("secondary error capacity exceeded");
  for(const auto& item:rows){const auto origin=Text(Get(item,"origin"));if(origin!="phaseResult"&&origin!="fixtureRaw")Invalid("unknown secondary error origin");
    AttemptSecondaryError value{{{ID(item,"testId"),ID(item,"generation"),Index(item,"workerIndex"),PID(item,"pid")},ID(item,"scopeId"),ID(item,"scopeGeneration")},CheckedString(item,"error",8192),0,origin=="fixtureRaw"};
    if(value.source.attempt!=AttemptErrorOwner{attempt.test_id,attempt.generation,attempt.worker_index,attempt.pid})Invalid("secondary error has foreign actual attempt owner");
    if(const auto* source=Find(item,"originatingStepId")){value.originating_step=Integer(*source,true);if(!value.originating_step||value.fixture_raw)Invalid("invalid secondary originating step");}
    result.push_back(std::move(value));
  }return result;
}
struct ArtifactCheck {uint64_t id=0;std::string path,state;};
struct Source {
  std::string id,path;
  std::shared_ptr<std::string> bytes;
  JSON document;
  RunnerReport core;
  report_detail::Summary summary;
  std::vector<ArtifactCheck> artifacts;
  std::map<uint64_t,std::vector<AttemptSecondaryError>> secondary_errors;
};
inline void Associated(const JSON& row,const RunnerReport& core){
  const auto test=ID(row,"testId"),generation=ID(row,"generation");const auto worker=Index(row,"workerIndex");const auto pid=PID(row,"pid");
  if(std::none_of(core.attempts.begin(),core.attempts.end(),[&](const auto& a){return a.test_id==test&&a.generation==generation&&a.worker_index==worker&&a.pid==pid;}))Invalid("record has no actual associated attempt");
  if(std::none_of(core.workers.begin(),core.workers.end(),[&](const auto& w){return w.worker_index==worker&&w.pid==pid;}))Invalid("record has no actual associated worker");
}
inline void Unique(std::set<uint64_t>& ids,uint64_t id){if(!ids.insert(id).second)Invalid("duplicate local record identity");}
inline void ReadCore(Source& source){
  const auto& doc=source.document;auto& core=source.core;
  if(UInt(doc,"version")!=1||Find(doc,"kind"))Invalid("incompatible input; expected a native version 1 run report");
  if(const auto* policy=Find(doc,"runPolicy"))core.run_policy.enabled=Bool(Get(*policy,"failOnFlakyTests"));
  if(const auto* config=Find(doc,"repeatConfiguration"))core.repeat_configuration=ReadRepeatConfiguration(*config);
  for(const auto& row:Rows(doc,"workers")){
    ReportWorker worker;worker.worker_index=Index(row,"workerIndex");worker.pid=PID(row,"pid");const bool known=Bool(Get(row,"exitStatusKnown"));
    const auto& code=Get(row,"exitCode");const auto& signal=Get(row,"signal");
    if(known){if((code.kind==JSON::Null)==(signal.kind==JSON::Null))Invalid("contradictory worker exit receipt");const auto value=Integer(code.kind==JSON::Null?signal:code);if(value>255||(code.kind==JSON::Null&&!value))Invalid("invalid worker exit status");worker.exit_code=static_cast<int>(value);worker.signaled=code.kind==JSON::Null;}
    else if(code.kind!=JSON::Null||signal.kind!=JSON::Null)Invalid("unknown worker exit has a fabricated status");
    for(const auto name:{"stdout","stderr"}){const auto& stream=Get(row,name);if(Text(Get(stream,"encoding"))!="hex")Invalid("incompatible worker stream encoding");const auto text=Text(Get(stream,"data"));if(text.size()%2||text.size()/2!=UInt(stream,"byteLength"))Invalid("corrupt worker byte length");auto bytes=DecodeHex(text.empty()?"-":text,kRunnerReportStreamBytes);if(std::string_view(name)=="stdout")worker.stdout_bytes=std::move(bytes);else worker.stderr_bytes=std::move(bytes);}
    core.workers.push_back(std::move(worker));
  }
  for(const auto& row:Rows(doc,"attempts")){
    ReportAttempt attempt;attempt.test_id=ID(row,"testId");attempt.generation=ID(row,"generation");attempt.retry=Index(row,"retry");attempt.worker_index=Index(row,"workerIndex");attempt.pid=PID(row,"pid");attempt.title_json=CheckedString(row,"title",4096);attempt.error_json=CheckedString(row,"error",8192);attempt.status=Text(Get(row,"status"));attempt.expected=Bool(Get(row,"expected"));attempt.duration_ms=Number(Get(row,"durationMs"));
    if(const auto* wall=Find(row,"wallDurationMs"))attempt.wall_duration_ms=Number(*wall);
    OptionalString(row,"skipReason",8192,attempt.skip_reason_json);OptionalString(row,"expectedFailureReason",8192,attempt.expected_failure_reason_json);if(const auto* status=Find(row,"expectedStatus"))attempt.expected_status=Text(*status);
    for(const auto& annotation:Rows(row,"annotations"))attempt.annotations.push_back(Annotation(annotation));
    // Rich source links are validated against the original step transcript below;
    // they are never reconstructed as fabricated presentation-core steps.
    source.secondary_errors.emplace(attempt.generation,SecondaryErrors(row,attempt));
    core.attempts.push_back(std::move(attempt));
  }
  for(const auto& row:Rows(doc,"unrun"))core.unrun.push_back({ID(row,"testId"),CheckedString(row,"title",4096),CheckedString(row,"reason",8192)});
  for(const auto& row:Rows(doc,"skipped"))core.skipped.push_back({ID(row,"testId"),CheckedString(row,"title",4096),CheckedString(row,"reason",8192)});
  for(const auto& row:Rows(doc,"skipCauses")){
    ReportSkipCause cause;cause.scope_id=ID(row,"causeId");cause.scope_generation=ID(row,"scopeGeneration");cause.callback_id=ID(row,"callbackId");cause.callback_token=ID(row,"callbackToken");cause.test_id=ID(row,"associatedTestId");cause.generation=ID(row,"associatedGeneration");cause.suite_id=ID(row,"suiteId",true);cause.worker_index=Index(row,"workerIndex");cause.pid=PID(row,"pid");cause.reason_json=CheckedString(row,"reason",8192);cause.status=Text(Get(row,"status"));const auto& annotations=Rows(row,"annotations");if(annotations.size()>1)Invalid("multiple beforeAll cause annotations");if(!annotations.empty())cause.annotation=Annotation(annotations.front());core.skip_causes.push_back(std::move(cause));
  }
  for(const auto& row:Rows(doc,"runtimeSkipped"))core.runtime_skipped.push_back({ID(row,"testId"),ID(row,"causeId"),CheckedString(row,"title",4096)});
  for(const auto& error:Rows(doc,"globalErrors")){if(error.kind!=JSON::String||!JsonString(error.raw,8192))Invalid("invalid global error");core.global_errors_json.emplace_back(error.raw);}
  for(const auto& row:OptionalRows(doc,"serialGroups")){
    ReportSerialGroup group;group.id=ID(row,"id");group.retries=Index(row,"retries");group.status=Text(Get(row,"status"));group.tests=IDs(row,"testIds");
    for(const auto& omission:Rows(row,"omissions"))group.omissions.push_back({ID(omission,"testId"),Index(omission,"retry"),ID(omission,"causeTestId"),ID(omission,"causeGeneration")});
    core.serial_groups.push_back(std::move(group));
  }
  // Native summary policy is shared, including serial replay and propagated
  // skips. Rich lifecycle records stay in the original document; core is only
  // the terminal presentation view, not a fabricated execution transcript.
  source.summary=report_detail::Validate(core);
  if(const auto* policy=Find(doc,"runPolicy"))core.run_policy=ReadFailOnFlakyPolicy(*policy,source.summary.flaky);
  const auto& summary=Get(doc,"summary");
  const std::pair<const char*,size_t> counts[]={{"tests",source.summary.total_tests()},{"attempts",core.attempts.size()},{"expected",source.summary.expected},{"unexpected",source.summary.unexpected},{"skipped",source.summary.skipped},{"flaky",source.summary.flaky},{"didNotRun",source.summary.did_not_run},{"globalErrors",core.global_errors_json.size()}};
  for(const auto& [key,value]:counts)if(UInt(summary,key)!=value)Invalid("summary disagrees with actual attempts/declarations: "+std::string(key));
  const auto duration=Number(Get(summary,"totalAttemptDurationMs"));if(duration!=source.summary.duration_ms)Invalid("summary duration disagrees with actual attempts");
}
// Validate the public lifecycle transcript separately from presentation-core
// steps. Private runtime callback deadlines are not present in source JSON.
inline void ValidateCallbackAssertionEvidence(const Source& source,const std::map<uint64_t,const JSON*>& steps){
  const auto& rows=OptionalRows(source.document,"callbackAssertions");
  if(rows.size()>TestSteps::kRecords)Invalid("callback assertion capacity exceeded");
  if(rows.empty()){for(const auto&[id,step]:steps){(void)id;if(Find(*step,"callbackAssertion"))Invalid("iteration lacks callback assertion");}return;}
  std::map<std::tuple<uint64_t,uint64_t,unsigned,int64_t>,const ReportAttempt*> actual_attempts;
  for(const auto& attempt:source.core.attempts)actual_attempts.emplace(std::tuple{attempt.test_id,attempt.generation,attempt.worker_index,attempt.pid},&attempt);
  std::map<unsigned,int64_t> retired_workers;for(const auto& worker:source.core.workers)if(worker.exit_code>=0)retired_workers.emplace(worker.worker_index,worker.pid);
  struct State {const JSON* row;StepOwner owner;uint64_t parent,attempts,count=0;double started,deadline,received;std::optional<double> own;const JSON* last=nullptr;uint64_t last_id=0;};
  std::map<uint64_t,State> assertions;std::set<std::pair<unsigned,uint64_t>> requests;size_t bytes=0;
  for(const auto& row:rows){
    const auto owner=Owner(row);const auto id=ID(row,"id"),request=ID(row,"requestId"),parent=ID(row,"parentStepId",true),attempts=UInt(row,"attempts");
    const auto status=Text(Get(row,"status")),receipt=Text(Get(row,"receipt"));const auto error=CheckedString(row,"error",8192);
    const auto start=Number(Get(row,"startedMonotonicMs")),deadline=Number(Get(row,"deadlineMonotonicMs")),received=Number(Get(row,"receiptMonotonicMs"));
    if(!attempts||attempts>TestSteps::kRecords||ID(row,"lastInvocation")!=attempts||received<start||
       (owner.callback_kind!="body"&&owner.callback_kind!="beforeEach"&&owner.callback_kind!="afterEach")||
       (owner.callback_kind!="body"&&!owner.callback_token)||
       (status!="passed"&&status!="timedOut"&&status!="canceled"&&status!="infrastructure")||
       (receipt!="settled"&&receipt!="workerExited")||!requests.emplace(owner.worker_index,request).second)
      Invalid("callback assertion owner/count/verdict rejected");
    const auto worker=retired_workers.find(owner.worker_index);
    if(worker==retired_workers.end()||worker->second!=owner.pid||!actual_attempts.contains({owner.test_id,owner.attempt_generation,owner.worker_index,owner.pid}))Invalid("callback assertion lacks actual attempt/worker retirement metadata");
    std::optional<double> own;if(const auto* value=Find(row,"ownDeadlineMonotonicMs")){own=Number(*value);if(*own<=start||deadline>*own)Invalid("callback assertion own deadline rejected");}
    if(parent){const auto found=steps.find(parent);if(found==steps.end()||Owner(*found->second)!=owner||Number(Get(*found->second,"startedMonotonicMs"))>start||Number(Get(*found->second,"receiptMonotonicMs"))<received||Text(Get(*found->second,"receipt"))=="notInvoked")Invalid("callback assertion parent physical ownership rejected");}
    const auto cost=error.size()+owner.callback_kind.size()+512;if(cost>TestSteps::kMetadata-bytes)Invalid("callback assertion metadata capacity exceeded");bytes+=cost;
    if(!assertions.emplace(id,State{&row,owner,parent,attempts,0,start,deadline,received,own}).second)Invalid("duplicate callback assertion");
  }
  // Iterate source order, not sorted IDs: contiguous invocation history must
  // match the original actual step transcript and cannot be repaired by merge.
  for(const auto& step:Rows(source.document,"steps"))if(const auto* origin=Find(step,"callbackAssertion")){
    const auto found=assertions.find(ID(*origin,"id"));if(found==assertions.end())Invalid("iteration lacks callback assertion");auto& state=found->second;
    const auto id=ID(step,"id"),invocation=ID(*origin,"invocation");const auto start=Number(Get(step,"startedMonotonicMs")),received=Number(Get(step,"receiptMonotonicMs")),deadline=Number(Get(step,"deadlineMonotonicMs"));const auto receipt=Text(Get(step,"receipt"));
    if(Owner(step)!=state.owner||ID(step,"parentId",true)!=state.parent||invocation!=state.count+1||state.count>=state.attempts||
       (!state.count?(id!=found->first||start!=state.started):(id<=state.last_id||start<Number(Get(*state.last,"receiptMonotonicMs"))))||
       received>state.received||receipt=="notInvoked"||(state.own&&deadline>*state.own))Invalid("callback assertion iteration history rejected");
    if(Number(Get(step,"durationMs"))!=Number(Get(step,"endedMonotonicMs"))-start||Number(Get(step,"physicalDurationMs"))!=received-start)Invalid("callback iteration duration contradicts public receipts");
    if(const auto* raw=Find(step,"callbackReceiptMonotonicMs")){const auto observed=Number(*raw);if(observed<start||observed>received)Invalid("callback iteration raw receipt time rejected");(void)Bool(Get(step,"callbackReceiptOK"));}
    else if(receipt=="settled")Invalid("settled callback iteration lacks raw receipt");
    state.last=&step;state.last_id=id;++state.count;
  }
  std::map<uint64_t,const JSON*> soft_rows;
  const auto& soft=OptionalRows(source.document,"softAssertions");if(soft.size()>SoftAssertions::kRecords)Invalid("callback soft cause capacity exceeded");
  for(const auto& row:soft)if(!soft_rows.emplace(ID(row,"id"),&row).second)Invalid("duplicate callback soft cause");
  const auto accepted_soft=[&](const JSON& step){
    if(Text(Get(step,"status"))!="failed"||!Find(step,"softErrorId")||Find(step,"attachmentError"))return false;
    const auto found=soft_rows.find(ID(step,"softErrorId"));if(found==soft_rows.end())Invalid("callback soft failure lacks actual accepted cause");
    const auto& row=*found->second;const auto owner=Owner(step);const auto receipt=Text(Get(row,"receipt")),raw=Text(Get(row,"rawOutcome"));
    const bool mismatch=receipt=="mismatch",deadline=receipt=="acceptedDeadline";
    if((!mismatch&&!deadline)||raw!=(mismatch?"recognizedMismatch":"recognizedDeadline")||
       Bool(Get(row,"acceptedMismatch"))!=mismatch||Bool(Get(row,"acceptedDeadline"))!=deadline||
       Owner(row)!=owner||!ID(row,"requestId")||Bool(Get(row,"cleanup"))!=(owner.callback_kind=="afterEach")||
       CheckedString(row,"error",8192)!=CheckedString(step,"error",8192)||CheckedString(step,"receiptError",8192)!="\"\""||
       (Find(row,"cancelReason")&&CheckedString(row,"cancelReason",8192)!="\"\""))Invalid("callback soft failure has contradictory actual cause");
    const auto attempt=actual_attempts.find({owner.test_id,owner.attempt_generation,owner.worker_index,owner.pid});
    if(attempt==actual_attempts.end()||attempt->second->status=="passed"||attempt->second->status=="skipped")Invalid("callback soft failure lost its actual outer attempt failure");
    const auto source_step=steps.find(ID(row,"stepId"));
    if(source_step==steps.end()||Owner(*source_step->second)!=owner)Invalid("callback soft cause has foreign step");
    const auto admitted=Number(Get(row,"admittedMonotonicMs")),received=Number(Get(row,"receiptMonotonicMs"));
    if(admitted<Number(Get(*source_step->second,"startedMonotonicMs"))||received<admitted||received>Number(Get(*source_step->second,"receiptMonotonicMs"))||received>Number(Get(step,"receiptMonotonicMs")))Invalid("callback soft cause time order rejected");
    auto id=source_step->first;for(size_t depth=0;id&&depth<=TestSteps::kDepth;++depth){
      const auto parent=steps.find(id);if(parent==steps.end()||Owner(*parent->second)!=owner)break;
      if(id==ID(step,"id"))return true;
      id=ID(*parent->second,"parentId",true);
    }
    Invalid("callback soft cause is not an actual descendant");
  };
  for(const auto&[id,state]:assertions){(void)id;if(state.count!=state.attempts||!state.last)Invalid("callback assertion invocation coverage rejected");
    const auto& row=*state.row;const auto& last=*state.last;const auto receipt=Text(Get(row,"receipt")),status=Text(Get(row,"status"));
    // A worker may die during backoff after a prior iteration settled. The
    // converse does not hold: an exited final iteration cannot claim Settled.
    if(Text(Get(last,"receipt"))=="workerExited"&&receipt!="workerExited")Invalid("callback assertion worker exit receipt contradicted");
    if(status=="passed"&&(receipt!="settled"||(Text(Get(last,"status"))!="passed"&&!accepted_soft(last))||!Bool(Get(last,"receiptOK"))||!Bool(Get(last,"callbackReceiptOK"))||CheckedString(row,"error",8192)!="\"\""||state.received>=state.deadline))Invalid("callback assertion passing verdict rejected");
  }
}
inline void ValidateEvidence(Source& source,bool require_artifacts){
  const auto& doc=source.document;const auto& core=source.core;
  std::set<uint64_t> steps;std::map<uint64_t,const JSON*> step_rows;
  if(Rows(doc,"steps").size()>TestSteps::kRecords)Invalid("step record capacity exceeded");
  for(const auto& row:Rows(doc,"steps")){
    const auto id=ID(row,"id");Unique(steps,id);Associated(row,core);const auto parent=ID(row,"parentId",true);
    if(parent&&(parent==id||!step_rows.contains(parent)||Owner(row)!=Owner(*step_rows.at(parent))))Invalid("step references missing/later/foreign parent");
    step_rows.emplace(id,&row);
    CheckedString(row,"title",4096);CheckedString(row,"error",8192);CheckedString(row,"receiptError",8192);
    if(!report_detail::Status(Text(Get(row,"status"))))Invalid("invalid terminal step status");
    const auto start=Number(Get(row,"startedMonotonicMs")),end=Number(Get(row,"endedMonotonicMs")),received=Number(Get(row,"receiptMonotonicMs"));if(start>end||end>received)Invalid("step receipt time order");
    const auto receipt=Text(Get(row,"receipt"));if(receipt!="settled"&&receipt!="workerExited"&&receipt!="notInvoked")Invalid("invalid step receipt");
    const bool ok=Bool(Get(row,"receiptOK"));if(receipt!="settled"&&ok)Invalid("non-settled step invented successful receipt");
    if(receipt=="notInvoked"&&(!Find(row,"declaredSkipRequestId")||Bool(Get(row,"callbackInvoked"))||Find(row,"callbackReceiptMonotonicMs")||Find(row,"physicalDurationMs")||start!=end||start!=received||Text(Get(row,"status"))!="skipped"))Invalid("uninvoked step invented a callback receipt or lacks skipped declaration");
    if(Text(Get(row,"status"))=="passed"&&(receipt!="settled"||!ok))Invalid("passed step has no successful receipt");
  }
  ValidateCallbackAssertionEvidence(source,step_rows);
  for(const auto& row:Rows(doc,"steps"))if(const auto* origin=Find(row,"originatingStepId")){if(!steps.contains(Integer(*origin,true)))Invalid("missing originating step");}
  for(const auto& row:Rows(doc,"attempts"))if(const auto* origin=Find(row,"originatingStepId")){if(!steps.contains(Integer(*origin,true)))Invalid("attempt references missing originating step");}
  for(const auto&[generation,errors]:source.secondary_errors){(void)generation;for(const auto& error:errors)if(error.originating_step){
    const auto found=step_rows.find(error.originating_step);if(found==step_rows.end())Invalid("secondary error references absent step");
    const auto owner=Owner(*found->second);const auto& actual=error.source;
    const auto raw_receipt=Number(Get(*found->second,"callbackReceiptMonotonicMs"));
    if(raw_receipt<Number(Get(*found->second,"startedMonotonicMs"))||raw_receipt>Number(Get(*found->second,"receiptMonotonicMs")))Invalid("secondary error raw callback receipt has impossible time order");
    if(owner.test_id!=actual.attempt.test||owner.attempt_generation!=actual.attempt.generation||owner.worker_index!=actual.attempt.worker||owner.pid!=actual.attempt.pid||owner.scope!=actual.scope||owner.scope_generation!=actual.generation||Text(Get(*found->second,"status"))!="failed"||!Find(*found->second,"callbackReceiptMonotonicMs")||Bool(Get(*found->second,"callbackReceiptOK")))Invalid("secondary error has foreign or absent raw step receipt");
  }}
  size_t artifact_bytes=0;std::set<uint64_t> artifacts;std::set<std::pair<unsigned,uint64_t>> requests;std::set<std::string> paths;
  for(const auto& row:Rows(doc,"attachments")){
    AttachmentRecord artifact;artifact.id=ID(row,"id");artifact.request=ID(row,"requestId");artifact.owner=Owner(row);artifact.name_json=CheckedString(row,"name",4096);artifact.content_type_json=CheckedString(row,"contentType",1024);artifact.path=Text(Get(row,"path"));artifact.source_kind=Text(Get(row,"sourceKind"));artifact.bytes=UInt(row,"byteLength");artifact.device=ID(row,"device",true);artifact.inode=ID(row,"inode");artifact.writer_pid=PID(row,"writerPid");const auto exit=UInt(row,"writerExitCode");if(exit||!Bool(Get(row,"writerExitConfirmed")))Invalid("attachment lacks successful writer retirement");artifact.writer_exit=0;
    artifact.started=Number(Get(row,"startedMonotonicMs"));artifact.committed=Number(Get(row,"commitReceiptMonotonicMs"));artifact.received=Number(Get(row,"writerExitReceiptMonotonicMs"));if(const auto* step=Find(row,"stepId")){artifact.step_id=Integer(*step,true);if(!steps.contains(artifact.step_id))Invalid("attachment references missing step");}
    ValidateNativeAttachment(artifact);Associated(row,core);Unique(artifacts,artifact.id);
    if(artifact.bytes>64*1024*1024-artifact_bytes)Invalid("committed artifact byte capacity exceeded");
    artifact_bytes+=artifact.bytes;
    if(Number(Get(row,"durationMs"))!=artifact.received-artifact.started)Invalid("attachment duration disagrees with writer receipt times");
    if(!requests.emplace(artifact.owner.worker_index,artifact.request).second||!paths.insert(artifact.path).second)Invalid("duplicate attachment request/path");
    ArtifactCheck check{artifact.id,artifact.path,"unavailable"};struct stat info{};
    if(::lstat(artifact.path.c_str(),&info)==0){check.state=S_ISREG(info.st_mode)&&static_cast<uint64_t>(info.st_dev)==artifact.device&&static_cast<uint64_t>(info.st_ino)==artifact.inode&&info.st_size>=0&&static_cast<uint64_t>(info.st_size)==artifact.bytes?"matchesRecordedIdentity":"identityMismatch";}
    else if(errno!=ENOENT&&errno!=ENOTDIR)check.state="unreadable";
    if(require_artifacts&&check.state!="matchesRecordedIdentity")Invalid("required committed artifact "+artifact.path+": "+check.state);
    source.artifacts.push_back(std::move(check));
  }
  if(artifacts.size()>128)Invalid("attachment capacity exceeded");
  if(OptionalRows(doc,"attachmentDeclarations").size()>128)Invalid("attachment declaration capacity exceeded");
  for(const auto& row:OptionalRows(doc,"attachmentDeclarations")){
    Associated(row,core);const auto receipt=Text(Get(row,"receipt"));if(receipt=="committed"){const auto artifact=ID(row,"attachmentId");if(!artifacts.contains(artifact))Invalid("committed declaration has no actual attachment");}
    else if(Find(row,"attachmentId"))Invalid("uncommitted declaration invented attachment reference");
  }
  std::set<uint64_t> outputs;
  for(const auto& row:OptionalRows(doc,"outputs")){Unique(outputs,ID(row,"id"));Associated(row,core);const auto proposed=Text(Get(row,"proposedPath"));if(proposed.empty()||proposed.front()!='/'||proposed.find('\0')!=std::string::npos)Invalid("invalid output directory reference");}
  std::vector<ReportProject> projects;
  for(const auto& row:OptionalRows(doc,"projects")){
    ReportProject project;project.id=ID(row,"id");project.name_json=CheckedString(row,"name",256);project.metadata_json=std::string(Get(row,"metadata").raw);project.status=Text(Get(row,"status"));project.test_ids=IDs(row,"testIds");project.selected_test_ids=IDs(row,"selectedTestIds");project.dependencies=IDs(row,"dependencies");project.selection_role=Text(Get(row,"selectionRole"));project.dependencies_ignored=Bool(Get(row,"dependenciesIgnored"));project.default_enabled=Bool(Get(row,"default"));
    for(const auto& worker:Rows(row,"workerIndices")){const auto index=Integer(worker);if(index>UINT32_MAX)Invalid("project worker out of range");project.workers.push_back(static_cast<unsigned>(index));}
    if(Find(row,"teardown"))project.teardown=ID(row,"teardown");
    if(Find(row,"failedDependency"))project.failed_dependency=ID(row,"failedDependency");
    projects.push_back(std::move(project));
  }
  ValidateProjectReports(projects);
  for(const auto& project:projects)for(const auto worker:project.workers)if(std::none_of(core.workers.begin(),core.workers.end(),[&](const auto& w){return w.worker_index==worker;}))Invalid("project references missing worker");
  if(!projects.empty())for(const auto& attempt:core.attempts){const auto* project=ProjectForTest(projects,attempt.test_id);if(!project||std::find(project->workers.begin(),project->workers.end(),attempt.worker_index)==project->workers.end())Invalid("attempt lacks actual project membership");}
  for(const auto name:{"files","fileWorkGroups","fileResourceLocks","workGroups","resourceLocks","softAssertions"}){
    std::set<uint64_t> ids;for(const auto& row:OptionalRows(doc,name)){if(Find(row,"id"))Unique(ids,ID(row,"id"));if(std::string_view(name)=="softAssertions")Associated(row,core);}
  }
  if(const auto* reuse=Find(doc,"workerReuse")){Rows(*reuse,"hosts");Rows(*reuse,"borrows");}
}
inline Source Load(std::string id,const std::filesystem::path& path,bool require_artifacts,size_t& total_bytes){
  Source source;source.id=std::move(id);source.path=std::filesystem::absolute(path).lexically_normal().string();source.bytes=std::make_shared<std::string>();
  std::ifstream input(path,std::ios::binary);if(!input)Invalid("cannot open input "+path.string());
  char chunk[65536];while(input){input.read(chunk,sizeof(chunk));const auto count=static_cast<size_t>(input.gcount());if(count>128*1024*1024-total_bytes)Invalid("combined input byte capacity128MiB exceeded");total_bytes+=count;source.bytes->append(chunk,count);}if(!input.eof())Invalid("cannot read input "+path.string());
  try{source.document=JSONParser(*source.bytes).Parse();ReadCore(source);ValidateEvidence(source,require_artifacts);}catch(const std::exception& error){throw std::invalid_argument("Input "+source.path+": "+error.what());}return source;
}
inline std::string Quote(std::string_view text){
  static constexpr char hex[]="0123456789abcdef";std::string result="\"";
  for(const unsigned char value:text){if(value=='"'||value=='\\'){result+='\\';result+=static_cast<char>(value);}else if(value<32){result+="\\u00";result+=hex[value>>4];result+=hex[value&15];}else result+=static_cast<char>(value);}
  result+='"';if(!JsonString(result,128*1024*1024))Invalid("non-UTF8 output string");return result;
}
inline void JSONProperty(report_detail::AtomicFile& out,std::string_view name,std::string_view json){
  const auto escaped=report_detail::XML(json);report_detail::Property(out,name,escaped.text);report_detail::NumericProperty(out,std::string(name)+".xmlSanitizedUnits",escaped.sanitized);
  if(escaped.sanitized){out.Put("<property name=\"");out.Put(name);out.Put(".originalPropertyJSONUTF8Hex\" value=\"");out.Hex(json);out.Put("\"/>");}
}
inline std::string Title(const Source& source,uint64_t id){const auto found=source.summary.tests.find(id);if(found!=source.summary.tests.end())return found->second.back()->title_json;for(const auto& row:source.core.unrun)if(row.test_id==id)return row.title_json;for(const auto& row:source.core.skipped)if(row.test_id==id)return row.title_json;for(const auto& row:source.core.runtime_skipped)if(row.test_id==id)return row.title_json;Invalid("missing test title");}
inline std::string TestOutcome(const Source& source,uint64_t id){if(source.summary.final_expected.contains(id)){if(source.summary.final_skipped.at(id))return "skipped";if(source.summary.final_flaky.at(id))return "flaky";return source.summary.final_expected.at(id)?"expected":"unexpected";}for(const auto& row:source.core.unrun)if(row.test_id==id)return "didNotRun";return "skipped";}
inline std::vector<uint64_t> TestIDs(const Source& source){std::vector<uint64_t> ids;for(const auto& [id,attempts]:source.summary.tests){(void)attempts;ids.push_back(id);}for(const auto& row:source.core.unrun)ids.push_back(row.test_id);for(const auto& row:source.core.skipped)ids.push_back(row.test_id);for(const auto& row:source.core.runtime_skipped)ids.push_back(row.test_id);return ids;}
inline std::vector<std::string> RunFailures(const Source& source,bool include_policy=true){
  std::vector<std::string> reasons;
  if(include_policy&&source.core.run_policy.Failed(source.summary.flaky))reasons.push_back("flakyTests");
  if(source.summary.unexpected)reasons.push_back("unexpectedTests");
  if(source.summary.did_not_run)reasons.push_back("unrunTests");
  if(!source.core.global_errors_json.empty())reasons.push_back("globalErrors");
  if(std::any_of(source.core.attempts.begin(),source.core.attempts.end(),[](const auto& a){return a.status=="interrupted";}))reasons.push_back("interruptedAttempt");
  if(std::any_of(source.core.workers.begin(),source.core.workers.end(),[](const auto& w){return w.signaled||w.exit_code>0;}))reasons.push_back("workerFailure");
  if(std::any_of(source.core.workers.begin(),source.core.workers.end(),[](const auto& w){return w.exit_code<0;}))reasons.push_back("workerExitUnknown");
  for(const auto name:{"projects","files","workGroups","fileWorkGroups","serialGroups"})for(const auto& row:OptionalRows(source.document,name))if(const auto* status=Find(row,"status")){const auto text=Text(*status);if(text=="failed"||text=="interrupted"||text=="blocked"||text=="notRun"){const auto reason=std::string(name)+"Failure";if(std::find(reasons.begin(),reasons.end(),reason)==reasons.end())reasons.push_back(reason);}}
  return reasons;
}
struct Totals {size_t tests=0,attempts=0,expected=0,unexpected=0,skipped=0,flaky=0,unrun=0,global_errors=0,failures=0,errors=0;double duration=0;};
inline Totals Sum(const std::vector<Source>& sources){Totals total;for(const auto& source:sources){const auto& s=source.summary;total.tests+=s.total_tests();total.attempts+=source.core.attempts.size();total.expected+=s.expected;total.unexpected+=s.unexpected;total.skipped+=s.skipped;total.flaky+=s.flaky;total.unrun+=s.did_not_run;total.global_errors+=source.core.global_errors_json.size();total.failures+=s.failures;total.errors+=s.errors;total.duration+=s.duration_ms;if(!std::isfinite(total.duration))Invalid("aggregate duration overflow");}return total;}
} // namespace merge_detail
struct ReportMergeInput {std::string source_id;std::filesystem::path path;};
struct ReportMergeOptions {bool require_artifacts=false;size_t expected_inputs=0;bool fail_on_flaky_tests=false;};
class MergedRunnerReport {
 public:
  MergedRunnerReport()=default;
  MergedRunnerReport(const MergedRunnerReport&)=delete;
  MergedRunnerReport& operator=(const MergedRunnerReport&)=delete;
  MergedRunnerReport(MergedRunnerReport&&)=default;
  MergedRunnerReport& operator=(MergedRunnerReport&&)=default;
  static MergedRunnerReport Read(const std::vector<ReportMergeInput>& inputs,const ReportMergeOptions& options={}){
    using namespace merge_detail;
    if(inputs.empty()||inputs.size()>128||(options.expected_inputs&&inputs.size()!=options.expected_inputs))Invalid("missing inputs or input count mismatch (capacity128)");
    MergedRunnerReport result;result.run_policy_.enabled=options.fail_on_flaky_tests;result.sources_.reserve(inputs.size());std::set<std::string> ids,paths;std::set<std::pair<uint64_t,uint64_t>> files;size_t total_bytes=0;
    for(const auto& input:inputs){if(input.source_id.empty()||input.source_id.size()>256||!JsonString(Quote(input.source_id),256)||input.source_id.find('\0')!=std::string::npos||!ids.insert(input.source_id).second)Invalid("invalid or duplicate source ID");
      const auto path=std::filesystem::weakly_canonical(std::filesystem::absolute(input.path)).string();if(!paths.insert(path).second)Invalid("same input path repeated");struct stat info{};if(::stat(input.path.c_str(),&info)<0)throw std::system_error(errno,std::generic_category(),"Native report merge input stat "+input.path.string());if(!S_ISREG(info.st_mode)||!files.emplace(static_cast<uint64_t>(info.st_dev),static_cast<uint64_t>(info.st_ino)).second)Invalid("input is not a distinct regular report file");
      result.sources_.push_back(Load(input.source_id,input.path,options.require_artifacts,total_bytes));
    }result.totals_=Sum(result.sources_);return result;
  }
  static MergedRunnerReport ReadDirectory(const std::vector<ReportMergeInput>& inputs,std::shared_ptr<const DirectoryReport> directory,const ReportMergeOptions& options={}){
    MergedRunnerReport result;result.run_policy_.enabled=options.fail_on_flaky_tests;if(!inputs.empty())result=Read(inputs,options);
    std::map<std::string,std::set<uint64_t>> terminal;std::map<std::string,bool> failed;
    for(const auto& source:result.sources_){auto& ids=terminal[source.id];for(const auto& a:source.core.attempts)ids.insert(a.test_id);for(const auto& a:source.core.skipped)ids.insert(a.test_id);for(const auto& a:source.core.runtime_skipped)ids.insert(a.test_id);failed.emplace(source.id,!merge_detail::RunFailures(source).empty());}
    if(!directory)throw std::invalid_argument("Native directory report missing");
    directory->Validate(terminal,failed);
    if(directory->repeat_selection){
      std::map<size_t,std::set<uint64_t>> reported;
      for(const auto& source:result.sources_){
        const auto* segment=directory->RepeatForSource(source.id);
        if(!segment||!segment->repeat_slice)merge_detail::Invalid("repeated source lacks selected logical slice");
        if(source.core.repeat_configuration){
          const auto& slice=directory->repeat_selection->Slices().at(*segment->repeat_slice);const auto& unit=directory->repeat_selection->Catalog().Units().at(slice.unit);
          const auto& local_projects=directory->catalog->Files().at(segment->file).catalog.projects;uint64_t local_project=0;
          if(segment->project!=directory->catalog->AnonymousProject()){
            const auto& name=directory->catalog->Graph().Declarations().at(segment->project).name;
            const auto found=std::find_if(local_projects.begin(),local_projects.end(),[&](const auto& project){return project.name==name;});
            if(found==local_projects.end())merge_detail::Invalid("repeated source lacks actual collected local project");
            local_project=static_cast<uint64_t>(found-local_projects.begin())+1;
          }
          source.core.repeat_configuration->ValidateAdmission(local_project,local_projects.size(),unit.role,unit.counts);
        }
        const std::set<uint64_t> selected(segment->selected_local_ids.begin(),segment->selected_local_ids.end());
        for(auto id:merge_detail::TestIDs(source)){
          if(!selected.contains(id))merge_detail::Invalid("repeated source outcome belongs to another selected group");
          (void)directory->repeat_selection->LogicalID(*segment->repeat_slice,id);
          if(!reported[*segment->repeat_slice].insert(id).second)merge_detail::Invalid("repeated source duplicated logical outcome");
        }
      }
      if(directory->repeat_groups){
        for(const auto& group:directory->repeat_groups->Groups())for(auto id:*group.local_ids)
          if(!reported[group.slice].contains(id))result.repeat_unrun_.push_back({group.slice,id,group.index});
      }else for(size_t slice=0;slice<directory->repeat_selection->Slices().size();++slice)
        for(auto id:*directory->repeat_selection->Slices()[slice].local_ids)
          if(!reported[slice].contains(id))result.repeat_unrun_.push_back({slice,id});
      result.totals_.tests+=result.repeat_unrun_.size();if(!directory->listing)result.totals_.unrun+=result.repeat_unrun_.size();
    }
    result.directory_json_=directory->JSON();result.directory_=std::move(directory);return result;
  }
  bool FlakyPolicyFailed()const{return run_policy_.Failed(totals_.flaky);}
  void WritePolicyConsole(std::ostream& out)const{run_policy_.Console(out,totals_.flaky);}
  bool RunFailed()const {return FlakyPolicyFailed()||(directory_&&directory_->RunFailed())||std::any_of(sources_.begin(),sources_.end(),[](const auto& source){return !merge_detail::RunFailures(source).empty();});}
  void WriteJSON(const std::filesystem::path& path,NativeReportPublication* publication=nullptr)const;
  void WriteJUnit(const std::filesystem::path& path)const;
  void WriteHTML(const std::filesystem::path& path)const;
  void CheckOutput(const std::filesystem::path& path)const{
    const auto target=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    const auto same_file=[&](const std::filesystem::path& other){std::error_code error;return std::filesystem::equivalent(path,other,error)&&!error;};
    for(const auto& source:sources_){
      if(target==std::filesystem::weakly_canonical(source.path)||same_file(source.path))merge_detail::Invalid("output would replace an input report");
      for(const auto& artifact:source.artifacts){
        // Artifact references may belong to an inaccessible or unmounted
        // machine. Keep lexical and available inode/canonical collision checks
        // without turning reference availability into a merge prerequisite.
        const auto lexical=std::filesystem::path(artifact.path).lexically_normal();
        std::error_code error;const auto canonical=std::filesystem::weakly_canonical(lexical,error);
        if(target==lexical||(!error&&target==canonical)||same_file(lexical))merge_detail::Invalid("output would replace a committed artifact");
      }
    }
  }
 private:
  FailOnFlakyTestsPolicy run_policy_;
  struct SelectedUnrun {size_t slice;uint64_t id;std::optional<size_t> group=std::nullopt;};
  std::vector<SelectedUnrun> repeat_unrun_;
  std::optional<size_t> RepeatSliceForSource(std::string_view source)const {
    if(directory_)if(const auto* segment=directory_->RepeatForSource(source))return segment->repeat_slice;
    return std::nullopt;
  }
  std::string RepeatJSON(size_t slice)const {DirectoryJSONBuffer out;directory_->WriteRepeatContext(out,slice);return std::move(out.bytes);}
  std::optional<size_t> RepeatGroupForSource(std::string_view source)const {
    if(directory_)if(const auto* segment=directory_->RepeatForSource(source))return segment->repeat_group;
    return std::nullopt;
  }
  std::string RepeatGroupJSON(size_t group)const {DirectoryJSONBuffer out;directory_->WriteRepeatGroupContext(out,group);return std::move(out.bytes);}
  template<class Sink>void RepeatGroupProperty(Sink& out,size_t group)const {merge_detail::JSONProperty(out,"native.repeatGroup",merge_detail::Quote(RepeatGroupJSON(group)));}
  template<class Sink>void RepeatProperties(Sink& out,size_t slice,std::optional<uint64_t> id=std::nullopt)const {
    merge_detail::JSONProperty(out,"native.repeat",merge_detail::Quote(RepeatJSON(slice)));
    if(id)merge_detail::JSONProperty(out,"native.logicalId",merge_detail::Quote(directory_->repeat_selection->LogicalID(slice,*id)));
  }
  std::vector<merge_detail::Source> sources_;merge_detail::Totals totals_;std::shared_ptr<const DirectoryReport> directory_;std::string directory_json_;
};
inline void MergedRunnerReport::WriteJSON(const std::filesystem::path& path,NativeReportPublication* publication)const{
  using namespace merge_detail;CheckOutput(path);report_detail::AtomicFile out(path,publication);
  out.Put("{\"version\":1,\"kind\":\"suitecut-native-merged-report\",\"identity\":\"sourceId plus original local identity\",\"artifactPolicy\":\"preserve absolute references; availability checked during merge; no files copied\",\"summary\":{");
  const std::pair<const char*,size_t> counts[]={{"sources",sources_.size()},{"tests",totals_.tests},{"attempts",totals_.attempts},{"expected",totals_.expected},{"unexpected",totals_.unexpected},{"skipped",totals_.skipped},{"flaky",totals_.flaky},{"didNotRun",totals_.unrun},{"globalErrors",totals_.global_errors}};
  bool first=true;for(const auto& [key,value]:counts){if(!first)out.Put(",");first=false;out.Put(Quote(key));out.Put(":");out.NumberValue(value);}out.Put(",\"totalAttemptDurationMs\":");out.NumberValue(totals_.duration);
  const auto failed=std::count_if(sources_.begin(),sources_.end(),[](const auto& source){return !RunFailures(source).empty();});out.Put(",\"failedSources\":");out.NumberValue(failed);out.Put(",\"runFailed\":");out.Put(RunFailed()?"true":"false");out.Put("}");if(run_policy_.enabled){out.Put(",\"runPolicy\":");run_policy_.Write(out,totals_.flaky);}if(directory_){out.Put(",\"directory\":");out.Put(directory_json_);}out.Put(",\"sources\":[");
  first=true;for(const auto& source:sources_){if(!first)out.Put(",");first=false;out.Put("{\"sourceId\":");out.Put(Quote(source.id));out.Put(",\"inputPath\":");out.Put(Quote(source.path));out.Put(",\"runFailed\":");const auto failures=RunFailures(source);out.Put(failures.empty()?"false":"true");out.Put(",\"runFailureReasons\":[");bool first_reason=true;for(const auto& reason:failures){if(!first_reason)out.Put(",");first_reason=false;out.Put(Quote(reason));}out.Put("]");if(const auto slice=RepeatSliceForSource(source.id)){out.Put(",\"repeat\":");out.Put(RepeatJSON(*slice));}if(const auto group=RepeatGroupForSource(source.id)){out.Put(",\"repeatGroup\":");out.Put(RepeatGroupJSON(*group));}out.Put(",\"artifactAvailability\":[");bool first_artifact=true;for(const auto& check:source.artifacts){if(!first_artifact)out.Put(",");first_artifact=false;out.Put("{\"attachmentId\":\"");out.NumberValue(check.id);out.Put("\",\"path\":");out.Put(Quote(check.path));out.Put(",\"availability\":");out.Put(Quote(check.state));out.Put("}");}out.Put("],\"report\":");out.Put(*source.bytes);out.Put("}");}
  out.Put("],\"tests\":[");first=true;
  for(const auto& source:sources_)for(const auto id:TestIDs(source)){if(!first)out.Put(",");first=false;out.Put("{\"sourceId\":");out.Put(Quote(source.id));out.Put(",\"testId\":\"");out.NumberValue(id);out.Put("\"");if(const auto slice=RepeatSliceForSource(source.id)){out.Put(",\"repeat\":");out.Put(RepeatJSON(*slice));out.Put(",\"logicalId\":");out.Put(Quote(directory_->repeat_selection->LogicalID(*slice,id)));}if(const auto group=RepeatGroupForSource(source.id)){out.Put(",\"repeatGroup\":");out.Put(RepeatGroupJSON(*group));}out.Put(",\"title\":");out.Put(Title(source,id));out.Put(",\"outcome\":");out.Put(Quote(TestOutcome(source,id)));out.Put(",\"attemptIndices\":[");bool first_attempt=true;for(size_t index=0;index<source.core.attempts.size();++index)if(source.core.attempts[index].test_id==id){if(!first_attempt)out.Put(",");first_attempt=false;out.NumberValue(index);}out.Put("]}");}
  for(const auto& unrun:repeat_unrun_){if(!first)out.Put(",");first=false;out.Put("{\"sourceId\":null,\"testId\":\"");out.NumberValue(unrun.id);out.Put("\",\"title\":");out.Put(directory_->RepeatTitle(unrun.slice,unrun.id));out.Put(",\"logicalId\":");out.Put(Quote(directory_->repeat_selection->LogicalID(unrun.slice,unrun.id)));out.Put(",\"repeat\":");out.Put(RepeatJSON(unrun.slice));if(unrun.group){out.Put(",\"repeatGroup\":");out.Put(RepeatGroupJSON(*unrun.group));}out.Put(",\"outcome\":");out.Put(directory_->listing?"\"selected\"":"\"didNotRun\"");out.Put(",\"nativeSelectionOnly\":true,\"attemptIndices\":[]}");}
  out.Put("]}\n");out.Commit();
}
inline void MergedRunnerReport::WriteJUnit(const std::filesystem::path& path)const{
  using namespace merge_detail;CheckOutput(path);report_detail::AtomicFile out(path);
  // Extra source-level error cases make failure evidence visible even when all
  // final tests passed (global errors, unrun tests, worker failure/unknown exit).
  size_t source_errors=0;for(const auto& source:sources_)if(!RunFailures(source,false).empty())++source_errors;
  out.Put("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites name=\"SuiteCut native merged\" tests=\"");out.NumberValue(totals_.tests+source_errors+(directory_?1:0));out.Put("\" failures=\"");out.NumberValue(totals_.failures);out.Put("\" errors=\"");out.NumberValue(totals_.errors-totals_.global_errors+source_errors+(directory_&&directory_->RunFailed()?1:0));out.Put("\" skipped=\"");out.NumberValue(totals_.skipped+totals_.unrun+(directory_&&directory_->listing?repeat_unrun_.size():0));out.Put("\" time=\"");out.DecimalValue(totals_.duration/1000);out.Put("\">");
  if(run_policy_.enabled){out.Put("<properties>");run_policy_.Properties(out,totals_.flaky);out.Put("</properties>");}
  for(const auto& source:sources_){const auto failures=RunFailures(source,false);const auto& summary=source.summary;const bool failed=!failures.empty();
    out.Put("<testsuite name=\"");out.Put(report_detail::XML(Quote(source.id)).text);out.Put("\" tests=\"");out.NumberValue(summary.total_tests()+(failed?1:0));out.Put("\" failures=\"");out.NumberValue(summary.failures);out.Put("\" errors=\"");out.NumberValue(summary.errors-source.core.global_errors_json.size()+(failed?1:0));out.Put("\" skipped=\"");out.NumberValue(summary.skipped+summary.did_not_run);out.Put("\" time=\"");out.DecimalValue(summary.duration_ms/1000);out.Put("\"><properties>");
    source.core.run_policy.Properties(out,source.summary.flaky);
    JSONProperty(out,"native.sourceId",Quote(source.id));JSONProperty(out,"native.inputPath",Quote(source.path));JSONProperty(out,"native.originalReportJSON",Quote(*source.bytes));if(const auto slice=RepeatSliceForSource(source.id))RepeatProperties(out,*slice);if(const auto group=RepeatGroupForSource(source.id))RepeatGroupProperty(out,*group);report_detail::Property(out,"native.identity","sourceId plus original testId");report_detail::Property(out,"native.artifactPolicy","Absolute references retained; no files copied");
    for(const auto name:{"projects","files","fileWorkGroups","workGroups","serialGroups","resourceLocks","fileResourceLocks","workerReuse","steps","attachments","attachmentDeclarations","outputs","softAssertions","skipCauses","runtimeSkipped","globalErrors","repeatConfiguration","runPolicy","callbackAssertions"})if(const auto* evidence=Find(source.document,name))JSONProperty(out,std::string("native.original.")+name,Quote(evidence->raw));
    for(size_t index=0;index<source.core.workers.size();++index){const auto& worker=source.core.workers[index];const std::string prefix="native.worker"+std::to_string(worker.worker_index)+".";report_detail::NumericProperty(out,prefix+"pid",worker.pid);report_detail::NumericProperty(out,prefix+"exitCode",worker.exit_code);report_detail::Property(out,prefix+"signaled",worker.signaled?"true":"false");for(const auto name:{"stdout","stderr"}){out.Put("<property name=\"");out.Put(prefix+name+"Hex");out.Put("\" value=\"");out.Hex(std::string_view(name)=="stdout"?worker.stdout_bytes:worker.stderr_bytes);out.Put("\"/>");}}
    for(const auto& artifact:source.artifacts){const auto prefix="native.attachment"+std::to_string(artifact.id)+".";JSONProperty(out,prefix+"path",Quote(artifact.path));JSONProperty(out,prefix+"availability",Quote(artifact.state));}
    out.Put("</properties>");
    for(const auto id:TestIDs(source)){const auto outcome=TestOutcome(source,id);const auto found=summary.tests.find(id);double duration=0;if(found!=summary.tests.end())for(const auto* attempt:found->second)duration+=attempt->duration_ms;
      out.Put("<testcase classname=\"");out.Put(report_detail::XML(Quote(source.id)).text);out.Put("\" name=\"");out.Put(report_detail::XML(Title(source,id)).text);out.Put(" [");out.NumberValue(id);out.Put("]\" time=\"");out.DecimalValue(duration/1000);out.Put("\"><properties>");JSONProperty(out,"native.sourceId",Quote(source.id));report_detail::NumericProperty(out,"native.testId",id);JSONProperty(out,"native.outcome",Quote(outcome));if(const auto slice=RepeatSliceForSource(source.id))RepeatProperties(out,*slice,id);if(const auto group=RepeatGroupForSource(source.id))RepeatGroupProperty(out,*group);JSONProperty(out,"native.originalTitle",Quote(Title(source,id)));report_detail::NumericProperty(out,"native.titleXmlSanitizedUnits",report_detail::XML(Title(source,id)).sanitized);
      size_t attempt_index=0;for(const auto& row:Rows(source.document,"attempts"))if(ID(row,"testId")==id){const auto prefix="native.attempt"+std::to_string(attempt_index++)+".";JSONProperty(out,prefix+"originalJSON",Quote(row.raw));if(const auto* wall=Find(row,"wallDurationMs"))report_detail::NumericProperty(out,prefix+"wallDurationMs",Number(*wall));ReportAttempt evidence;evidence.secondary_errors=source.secondary_errors.at(ID(row,"generation"));report_detail::AttemptSecondaryProperties(out,evidence,prefix);}
      if(found==summary.tests.end())for(const auto name:{"unrun","skipped","runtimeSkipped"})for(const auto& row:Rows(source.document,name))if(ID(row,"testId")==id)JSONProperty(out,"native.originalDeclaration",Quote(row.raw));
      out.Put("</properties>");if(outcome=="skipped"||outcome=="didNotRun")out.Put("<skipped/>");
      else if(outcome=="unexpected"){
        const auto& last=*found->second.back();const bool serial=std::any_of(source.core.serial_groups.begin(),source.core.serial_groups.end(),[&](const auto& group){return std::find(group.tests.begin(),group.tests.end(),id)!=group.tests.end();});
        const bool interrupted=last.status=="interrupted"||(serial&&std::any_of(found->second.begin(),found->second.end(),[](const auto* attempt){return attempt->status=="interrupted";}));
        out.Put(interrupted?"<error type=\"interrupted\" message=\"":"<failure type=\"unexpected\" message=\"");const auto error=report_detail::XML(last.error_json);out.Put(error.text);out.Put("\">");out.Put(error.text);out.Put(interrupted?"</error>":"</failure>");
      }out.Put("</testcase>");
    }
    if(failed){out.Put("<testcase classname=\"");out.Put(report_detail::XML(Quote(source.id)).text);out.Put("\" name=\"Native source run failure\" time=\"0\"><error type=\"nativeRunFailure\" message=\"");for(size_t index=0;index<failures.size();++index){if(index)out.Put(", ");out.Put(failures[index]);}out.Put("\">Source-level report evidence; this is not an executed test or lifecycle callback.</error></testcase>");}
    out.Put("</testsuite>");
  }if(!repeat_unrun_.empty()){
    out.Put("<testsuite name=\"native.selectedUnrunRepeats\" tests=\"");out.NumberValue(repeat_unrun_.size());out.Put("\" failures=\"0\" errors=\"0\" skipped=\"");out.NumberValue(repeat_unrun_.size());out.Put("\">");
    for(const auto& unrun:repeat_unrun_){out.Put("<testcase name=\"");out.Put(report_detail::XML(directory_->RepeatTitle(unrun.slice,unrun.id)).text);out.Put(" [");out.Put(report_detail::XML(Quote(directory_->repeat_selection->LogicalID(unrun.slice,unrun.id))).text);out.Put("]");out.Put("\"><properties>");RepeatProperties(out,unrun.slice,unrun.id);if(unrun.group)RepeatGroupProperty(out,*unrun.group);report_detail::Property(out,"native.selectionOnly","true");out.Put("</properties><skipped message=\"");out.Put(directory_->listing?"Listing only; native logical selection has no execution receipts":"Selected native logical execution has no source outcome or attempt receipt");out.Put("\"/></testcase>");}out.Put("</testsuite>");
  }if(directory_){out.Put("<testsuite name=\"native.directory\" tests=\"1\" failures=\"0\" errors=\"");out.Put(directory_->RunFailed()?"1":"0");out.Put("\"><testcase name=\"native directory policy receipts (not an authored test)\">");out.Put("<system-out>");out.Put(report_detail::XML(Quote(directory_json_)).text);out.Put("</system-out>");if(directory_->RunFailed())out.Put("<error message=\"Native directory failed; see JSON directory receipts\"/>");out.Put("</testcase></testsuite>");}
  out.Put("</testsuites>\n");out.Commit();
}
inline void MergedRunnerReport::WriteHTML(const std::filesystem::path& path)const{
  using namespace merge_detail;CheckOutput(path);report_detail::AtomicFile out(path);
  out.Put(R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'"><title>SuiteCut merged native reports</title><style>body{font:16px system-ui;max-width:1100px;margin:32px auto;padding:0 20px}details{border:1px solid #bbb;padding:12px;margin:12px 0}summary{cursor:pointer;overflow-wrap:anywhere}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f4f5f7;padding:12px}dl{display:grid;grid-template-columns:1fr 2fr}dd{margin:0}dt{font-weight:600}.unexpected{border-left:5px solid #b3261e}.expected,.flaky{border-left:5px solid #137333}</style></head><body><h1>SuiteCut merged native reports</h1><p>Every test identity includes its source ID. Original attempts, errors, metadata, worker bytes and artifact receipts remain in each original JSON report. Artifact availability below describes this machine at merge time; files are not copied.</p><dl>)");
  html_detail::Number(out,"Sources",sources_.size());html_detail::Number(out,"Tests",totals_.tests);html_detail::Number(out,"Attempts",totals_.attempts);html_detail::Number(out,"Expected",totals_.expected);html_detail::Number(out,"Unexpected",totals_.unexpected);html_detail::Number(out,"Skipped",totals_.skipped);html_detail::Number(out,"Flaky",totals_.flaky);html_detail::Number(out,"Did not run",totals_.unrun);html_detail::Number(out,"Global errors",totals_.global_errors);out.Put("</dl>");run_policy_.HTML(out,totals_.flaky);
  for(const auto& source:sources_){out.Put("<section><h2>Source: ");out.Put(report_detail::XML(Quote(source.id)).text);out.Put("</h2><p>Run outcome: ");const auto failures=RunFailures(source);out.Put(failures.empty()?"passed":"failed");for(const auto& failure:failures){out.Put(" · ");html_detail::Text(out,failure);}out.Put("</p>");
    source.core.run_policy.HTML(out,source.summary.flaky);
    html_detail::Field(out,"Original input path",Quote(source.path));
    if(const auto slice=RepeatSliceForSource(source.id))html_detail::Field(out,"Native repeat context",Quote(RepeatJSON(*slice)));
    if(const auto group=RepeatGroupForSource(source.id))html_detail::Field(out,"Native selected group",Quote(RepeatGroupJSON(*group)));
    for(const auto id:TestIDs(source)){const auto outcome=TestOutcome(source,id);out.Put("<details class=\"");out.Put(outcome);out.Put("\"><summary>");out.Put(report_detail::XML(Title(source,id)).text);out.Put(" · ");out.Put(outcome);out.Put(" · test ");out.NumberValue(id);out.Put("</summary>");
      html_detail::Field(out,"Source ID",Quote(source.id));
      if(const auto slice=RepeatSliceForSource(source.id))html_detail::Field(out,"Native logical test ID",Quote(directory_->repeat_selection->LogicalID(*slice,id)));
      for(const auto& row:Rows(source.document,"attempts"))if(ID(row,"testId")==id){out.Put("<details><summary>Actual attempt ");out.NumberValue(UInt(row,"retry"));out.Put(" · ");html_detail::Text(out,Text(Get(row,"status")));out.Put("</summary>");html_detail::Field(out,"Raw error",std::string(Get(row,"error").raw));if(const auto* wall=Find(row,"wallDurationMs")){out.Put("<dl>");html_detail::Number(out,"Actual wall duration (ms)",Number(*wall));out.Put("</dl>");}for(const auto& error:source.secondary_errors.at(ID(row,"generation"))){html_detail::Field(out,error.fixture_raw?"Raw associated fixture error":"Associated phase diagnostic",error.error_json);out.Put("<dl>");html_detail::Number(out,"Scope",error.source.scope);html_detail::Number(out,"Scope generation",error.source.generation);if(error.originating_step)html_detail::Number(out,"Originating step",error.originating_step);out.Put("</dl>");}html_detail::Field(out,"Original attempt JSON",Quote(row.raw));out.Put("</details>");}
      for(const auto name:{"unrun","skipped","runtimeSkipped"})for(const auto& row:Rows(source.document,name))if(ID(row,"testId")==id)html_detail::Field(out,"Original declaration JSON",Quote(row.raw));
      out.Put("</details>");
    }
    out.Put("<h3>Committed artifact history</h3><p>This list preserves every committed artifact record. Current attachment view selections remain in each original report, together with their accepted changes and history.</p>");for(const auto& check:source.artifacts){out.Put("<details><summary>Attachment ");out.NumberValue(check.id);out.Put(" · ");html_detail::Text(out,check.state);out.Put("</summary>");html_detail::Field(out,"Original absolute path",Quote(check.path));out.Put("</details>");}
    out.Put("<details><summary>Complete original JSON report</summary><pre>");html_detail::Text(out,*source.bytes);out.Put("</pre></details></section>");
  }if(!repeat_unrun_.empty()){out.Put(directory_->listing?"<section><h2>Selected repeats (listing only)</h2><p>No execution receipts are created by listing.</p>":"<section><h2>Selected repeats without a source outcome</h2><p>These native logical selections did not produce an attempt or source outcome receipt.</p>");for(const auto& unrun:repeat_unrun_){html_detail::Field(out,directory_->listing?"Selected title":"Did not run",directory_->RepeatTitle(unrun.slice,unrun.id));html_detail::Field(out,"Native logical test ID",Quote(directory_->repeat_selection->LogicalID(unrun.slice,unrun.id)));if(unrun.group)html_detail::Field(out,"Native selected group",Quote(RepeatGroupJSON(*unrun.group)));}out.Put("</section>");}if(directory_){out.Put("<section><h2>Native directory policy and physical receipts</h2><p>");out.Put(directory_->RunFailed()?"Failed":"Passed");out.Put("; serial file actors, not authored test evidence</p><pre>");out.Put(report_detail::XML(Quote(directory_json_)).text);out.Put("</pre></section>");}out.Put("</body></html>\n");out.Commit();
}
} // namespace suitecut::runner
