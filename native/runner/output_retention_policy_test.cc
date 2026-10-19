// SPDX-License-Identifier: MIT
#include "runner_output_retention.h"
#include "output_retention_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class Call>void RefuseRetention(Call&& call){bool rejected=false;try{call();}catch(const std::exception&){rejected=true;}assert(rejected);}
struct RetentionAttemptDTO {uint64_t test_id=1,generation=1;unsigned worker_index=1;int64_t pid=101;unsigned retry=0;std::string status="passed",expected_status="passed";bool expected=true;};
struct RetentionReportDTO {std::optional<PreserveOutput> output_preservation=PreserveOutput::Always;std::vector<OutputDirectoryRecord> outputs;std::vector<OutputRetentionRecord> output_retentions;std::vector<RetentionAttemptDTO> attempts{{}};};
struct RetentionWriter {std::string text;void Put(std::string_view value){text+=value;}template<class Number>void NumberValue(Number value){text+=std::to_string(value);}};
int main(){
 unsigned groups=0;const OutputDirectoryOwner owner{1,1,1,101};OutputRetentionOutcome pass{owner,0,"passed","passed",true};
 assert(NativePreserveOutput("always")==PreserveOutput::Always&&NativePreserveOutput("never")==PreserveOutput::Never&&NativePreserveOutput("failures-only")==PreserveOutput::FailuresOnly);
 RefuseRetention([]{NativePreserveOutput("failures");});RefuseRetention([]{NativePreserveOutput("");});++groups;
 assert(PreserveNativeOutput(PreserveOutput::Always,pass)&&!PreserveNativeOutput(PreserveOutput::Never,pass)&&!PreserveNativeOutput(PreserveOutput::FailuresOnly,pass));++groups;
 auto expected_failure=pass;expected_failure.status="failed";expected_failure.expected_status="failed";
 assert(!PreserveNativeOutput(PreserveOutput::FailuresOnly,expected_failure));++groups;
 auto unexpected_pass=pass;unexpected_pass.expected_status="failed";unexpected_pass.expected=false;assert(PreserveNativeOutput(PreserveOutput::FailuresOnly,unexpected_pass));++groups;
 auto retry0=pass;retry0.status="failed";retry0.expected=false;auto retry1=pass;retry1.retry=1;retry1.owner.generation=2;
 assert(PreserveNativeOutput(PreserveOutput::FailuresOnly,retry0)&&!PreserveNativeOutput(PreserveOutput::FailuresOnly,retry1)&&retry0.owner.generation!=retry1.owner.generation);++groups;
 auto teardown=retry0;assert(PreserveNativeOutput(PreserveOutput::FailuresOnly,teardown));auto interrupted=pass;interrupted.status="interrupted";interrupted.expected=false;assert(PreserveNativeOutput(PreserveOutput::FailuresOnly,interrupted));++groups;
 auto malformed=pass;malformed.status="unknown";RefuseRetention([&]{PreserveNativeOutput(PreserveOutput::Always,malformed);});malformed=pass;malformed.owner.pid=0;RefuseRetention([&]{PreserveNativeOutput(PreserveOutput::Never,malformed);});++groups;
 // Native-owner DTOs below are controlled policy fixtures, not mkdir, process,
 // inode availability, callback or actual file receipts.
 OutputDirectoryRecord source;source.id=1;source.owner=owner;source.path="/tmp/native-retention-fixture/suitecut-output-fixture";source.started=1;source.deadline=100;source.created=2;source.writer_received=3;source.received=4;source.assigned=5;source.device=7;source.inode=8;source.parent_device=7;source.parent_inode=9;source.writer_pid=202;source.writer_exit=0;source.ready_receipt=true;source.status=OutputDirectoryStatus::Ready;
 ValidateRetentionSource(source,pass);auto foreign=pass;foreign.owner.generation=2;RefuseRetention([&]{ValidateRetentionSource(source,foreign);});auto invalid_source=source;invalid_source.assigned.reset();RefuseRetention([&]{ValidateRetentionSource(invalid_source,pass);});invalid_source=source;invalid_source.received=NAN;RefuseRetention([&]{ValidateRetentionSource(invalid_source,pass);});++groups;
 RunnerOutputRetention::Budget kept_budget;size_t kept_physical=0;RunnerOutputRetention kept("/proc/self/exe",{},kept_budget,{&kept_physical,1});kept.Admit(source,pass,PreserveOutput::Always,0,10);
 assert(kept.IdleReady()&&!kept.Physical()&&!kept_physical&&kept.Find(1)->status==OutputRetentionStatus::Kept&&!kept.Find(1)->physical_received&&!kept.Find(1)->helper_pid&&source.status==OutputDirectoryStatus::Ready&&source.assigned==5);++groups;
 RetentionReportDTO report;report.outputs={source};report.output_retentions=kept.Records();assert(ValidateOutputRetentionReports(report)>0);
 RetentionWriter writer;PutOutputRetentionReports(writer,report.output_retentions);assert(writer.text.find("\"availability\":\"notObserved\"")!=std::string::npos&&writer.text.find("physicalReceiptMonotonicMs")==std::string::npos);++groups;
 auto forged=report;forged.output_retentions[0].helper_pid=303;RefuseRetention([&]{ValidateOutputRetentionReports(forged);});forged=report;forged.output_retentions[0].outcome.expected=false;RefuseRetention([&]{ValidateOutputRetentionReports(forged);});forged=report;forged.outputs[0].owner.generation=2;RefuseRetention([&]{ValidateOutputRetentionReports(forged);});++groups;
 RunnerOutputRetention::Budget pending_budget;size_t pending_physical=0;unsigned failures=0;RunnerOutputRetention pending("/proc/self/exe",{[&](const auto&){++failures;}},pending_budget,{&pending_physical,1});pending.Admit(source,pass,PreserveOutput::Never,100,10);
 assert(!pending.IdleReady()&&!pending.Physical()&&!pending_physical);RefuseRetention([&]{pending.Records();});++groups;
 for(auto member:{&OutputRetentionFence::hosts,&OutputRetentionFence::descendants,&OutputRetentionFence::raw_callbacks,&OutputRetentionFence::steps,&OutputRetentionFence::file_jobs,&OutputRetentionFence::file_callbacks,&OutputRetentionFence::publication_callbacks,&OutputRetentionFence::output_preparation,&OutputRetentionFence::sources}){OutputRetentionFence fence;fence.*member=1;assert(!pending.Start(1,fence,11)&&!pending.Physical()&&!pending_physical&&!pending.Find(1)->helper_pid);}
 assert(!failures);++groups;
 assert(pending.Start(1,{},100)&&pending.IdleReady()&&!pending.Physical()&&!pending_physical&&failures==1&&pending.Find(1)->status==OutputRetentionStatus::Failed&&!pending.Find(1)->physical_received&&pending.Find(1)->helper_exit==-1);++groups;
 report.output_preservation=PreserveOutput::Never;report.output_retentions=pending.Records();assert(ValidateOutputRetentionReports(report)>0);assert(OutputRetentionAvailability(report.output_retentions[0].status)==std::string_view("unknownAfterFailure"));++groups;
 forged=report;forged.output_retentions[0].receipt.reset();RefuseRetention([&]{ValidateOutputRetentionReports(forged);});
 forged=report;auto& impossible=forged.output_retentions[0];impossible.helper_pid=303;impossible.helper_signaled=true;impossible.helper_exit=0;impossible.physical_received=101;RefuseRetention([&]{ValidateOutputRetentionReports(forged);});++groups;
 forged=report;forged.output_retentions.clear();RefuseRetention([&]{ValidateOutputRetentionReports(forged);});
 forged=report;forged.output_preservation.reset();RefuseRetention([&]{ValidateOutputRetentionReports(forged);});
 forged=report;forged.output_retentions[0].deadline=forged.output_retentions[0].decided+kOutputRetentionCleanupMaximum+1;RefuseRetention([&]{ValidateOutputRetentionReports(forged);});++groups;
 std::cout<<"PASS "<<groups<<" controlled native output-retention policy/report/fence groups; no filesystem/process/availability/runtime claim\n";
}
