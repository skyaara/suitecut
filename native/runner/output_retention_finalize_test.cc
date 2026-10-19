// SPDX-License-Identifier: MIT
#include "output_retention_finalize.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
struct FinalRetentionAttempt {uint64_t test_id=1,generation=1;unsigned worker_index=1;int64_t pid=101;unsigned retry=0;std::string status="passed",expected_status="passed";bool expected=true;};
struct FinalRetentionReport {std::optional<PreserveOutput> output_preservation;std::vector<OutputDirectoryRecord> outputs;std::vector<OutputRetentionRecord> output_retentions;std::vector<FinalRetentionAttempt> attempts{{}};};
template<class Call>void RejectFinalRetention(Call&& call){bool rejected=false;try{call();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
 // Controlled native final-report DTOs; no fake filesystem/worker/callback
 // receipt is substituted for an actual runtime. The policy is always/kept,
 // so these controls MUST NOT spawn or perform filesystem IO.
 unsigned groups=0,failures=0;size_t physical=0;
 OutputDirectoryRecord source;source.id=1;source.owner={1,1,1,101};source.path="/tmp/native-retention-finalizer/suitecut-output-fixture";source.started=1;source.deadline=100;source.created=2;source.writer_received=3;source.received=4;source.assigned=5;source.device=7;source.inode=8;source.parent_device=7;source.parent_inode=9;source.writer_pid=202;source.writer_exit=0;source.ready_receipt=true;source.status=OutputDirectoryStatus::Ready;
 const auto finalize=[&](auto& report,PreserveOutput policy=PreserveOutput::Always,const OutputRetentionFence& fence={}){FinalizeNativeOutputRetention(report,"/must-not-spawn-retention-executable",policy,fence,{&physical,1},[&](const auto&){++failures;});};
 FinalRetentionReport report;report.outputs={source};finalize(report);assert(report.output_preservation==PreserveOutput::Always&&report.output_retentions.size()==1&&report.output_retentions[0].status==OutputRetentionStatus::Kept&&!report.output_retentions[0].helper_pid&&!physical&&!failures);++groups;
 const auto original=OutputRetentionJSON(report.output_retentions);finalize(report);assert(OutputRetentionJSON(report.output_retentions)==original&&!physical);RejectFinalRetention([&]{finalize(report,PreserveOutput::Never);});++groups;
 for(auto member:{&OutputRetentionFence::hosts,&OutputRetentionFence::descendants,&OutputRetentionFence::raw_callbacks,&OutputRetentionFence::steps,&OutputRetentionFence::file_jobs,&OutputRetentionFence::file_callbacks,&OutputRetentionFence::publication_callbacks,&OutputRetentionFence::output_preparation,&OutputRetentionFence::sources}){FinalRetentionReport pending;pending.outputs={source};OutputRetentionFence actual;actual.*member=1;RejectFinalRetention([&]{finalize(pending,PreserveOutput::Always,actual);});assert(!pending.output_preservation&&pending.output_retentions.empty()&&!physical);}
 FinalRetentionReport borrowed;borrowed.outputs={source};physical=1;RejectFinalRetention([&]{finalize(borrowed);});assert(physical==1&&!borrowed.output_preservation);physical=0;++groups;
 FinalRetentionReport orphan;orphan.outputs={source};orphan.attempts.clear();RejectFinalRetention([&]{finalize(orphan,PreserveOutput::Never);});assert(!orphan.output_preservation&&orphan.output_retentions.empty()&&!physical);++groups;
 FinalRetentionReport atomic;atomic.outputs={source,source};atomic.outputs[1].id=2;atomic.outputs[1].owner.generation=2;atomic.attempts.push_back(atomic.attempts[0]);atomic.attempts[1].generation=2;atomic.outputs[1].created.reset();RejectFinalRetention([&]{finalize(atomic,PreserveOutput::Never);});assert(!atomic.output_preservation&&atomic.output_retentions.empty()&&!physical&&!failures);++groups;
 FinalRetentionReport empty;empty.attempts.clear();finalize(empty);assert(empty.output_preservation==PreserveOutput::Always&&empty.output_retentions.empty()&&!physical);++groups;
 FinalRetentionReport escaped;escaped.outputs={source};escaped.outputs[0].path+="-<&\n";finalize(escaped);const auto json=OutputRetentionJSON(escaped.output_retentions);assert(json.find("-<&\\u000a\"")!=std::string::npos&&json.find("physicalReceiptMonotonicMs")==std::string::npos);OutputRetentionJSONWriter bounded;RejectFinalRetention([&]{bounded.Put(std::string(16*1024*1024+1,'x'));});assert(!physical&&!failures);++groups;
 std::cout<<"PASS "<<groups<<" controlled native finalizer pre-admission/fence/idempotence/stream-bound groups; no filesystem/process/Runner claim\n";
}
