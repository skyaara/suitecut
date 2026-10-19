// SPDX-License-Identifier: MIT
#pragma once
#include "runner_output_retention.h"
#include "output_retention_report.h"
#include <array>
namespace suitecut::runner {
// Called by the native coordinator only after its real, final all-owner fence.
// There is no host wire command for either the outcome or this fence. Every
// assigned source is admitted before the first destructive helper can start.
// A live reused host, unsettled callback or borrowed file lease rejects the
// entire phase without sending any filesystem command.
template<class Report,class Failure>
void FinalizeNativeOutputRetention(Report& report,const std::string& executable,
 PreserveOutput policy,const OutputRetentionFence& actual,
 RunnerOutputRetention::PhysicalBudget physical,Failure&& failure){
 if(!actual.Retired()||!physical.count||*physical.count)
  throw std::runtime_error("Native output retention requires actual final owner retirement");
 if(report.output_preservation){
  if(*report.output_preservation!=policy)throw std::runtime_error("Native output retention run policy changed after finalization");
  (void)ValidateOutputRetentionReports(report);return;
 }
 if(!report.output_retentions.empty())throw std::runtime_error("Native output retention rows have no accepted run policy");
 (void)PreserveOutputName(policy);
 RunnerOutputRetention::Budget budget;
 RunnerOutputRetention actor(executable,{std::forward<Failure>(failure)},budget,physical);
 const auto started=DedicatedRealMilliseconds(),deadline=started+kOutputRetentionCleanupMaximum;
 for(const auto& output:report.outputs){
  if(output.status!=OutputDirectoryStatus::Ready||!output.assigned)continue;
  const auto attempt=std::find_if(report.attempts.begin(),report.attempts.end(),[&](const auto& row){return row.test_id==output.owner.test_id&&row.generation==output.owner.generation&&row.worker_index==output.owner.worker_index&&row.pid==output.owner.pid;});
  if(attempt==report.attempts.end())throw std::runtime_error("Native output retention has no final actual attempt");
  actor.Admit(output,{output.owner,attempt->retry,attempt->status,attempt->expected_status,attempt->expected},policy,deadline,DedicatedRealMilliseconds());
 }
 // Admission/validation above is nondestructive. The same one cleanup clock
 // bounds every queued directory; starting a later helper never renews it.
 while(!actor.IdleReady()){
  actor.StartPending(actual,DedicatedRealMilliseconds());actor.Tick(DedicatedRealMilliseconds());
  if(actor.IdleReady())break;
  std::array<pollfd,320> descriptors{};size_t count=0;
  actor.ForEachPollDescriptor([&](const auto& descriptor){if(count==descriptors.size())throw std::runtime_error("Native output retention poll capacity exhausted");descriptors[count++]=descriptor;});
  int result;do{result=::poll(descriptors.data(),count,10);}while(result<0&&errno==EINTR);
  if(result<0)throw std::runtime_error("Native output retention actual helper poll failed");
 }
 auto records=actor.Records();
 report.output_retentions=std::move(records);report.output_preservation=policy;
 (void)ValidateOutputRetentionReports(report);
}
} // namespace suitecut::runner
