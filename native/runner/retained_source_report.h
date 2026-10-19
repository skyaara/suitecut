// SPDX-License-Identifier: MIT
#pragma once
#include "retained_source_leases.h"
#include "retained_source_work_wire.h"
#include "test_steps.h"
#include <set>
#include <vector>
namespace suitecut::runner {
// Observed transport/kernel evidence only. No Engine accepted-work-zero, idle,
// successful close ACK, or Source waitpid status can be inferred from this row.
struct ReportSourceShutdown {int64_t current_parent=0;unsigned planned_code=0;};
// Actual private-channel challenge/cut and original stdout decoder receipts.
// This is separate from Source exit and never authorizes CEF cohort reuse.
struct ReportSourceCallbackIteration {
  StepOwner callback;uint64_t request=0,outer_step=0,iteration_step=0,assertion=0,invocation=0;
};
struct ReportSourceAcceptedWork {
  RetainedSourceWorkWire::Challenge challenge;
  std::optional<RetainedSourceWorkWire::Cuts> cuts;
  bool observed=false,acknowledged=false,canceled=false,late_acknowledged=false;
  uint64_t refusal=0;double observed_at=0,acknowledged_at=0;
  std::optional<ReportSourceCallbackIteration> callback_iteration=std::nullopt;
};
struct ReportRetainedSource {
  RetainedSourceLeases::Ticket ticket;RetainedSourceLeases::Executable executable;
  std::optional<RetainedSourceLeases::Process> startup;std::optional<ReportSourceShutdown> shutdown;
  bool pidfd_exit=false,channel_drained=false;uint64_t retired_batch=0;
  std::optional<ReportSourceAcceptedWork> accepted_work{};
};
inline void ValidateRetainedSource(const ReportRetainedSource& row){
  const auto& ticket=row.ticket;const auto& host=ticket.owner.host;const auto& fixture=ticket.owner.fixture;
  if(!ticket.id||ticket.id>RetainedSourceLeases::kHistory||!host.lease||!host.worker||host.pid<=0||host.pid>INT32_MAX||!host.start||!fixture.catalog||!fixture.declaration||!fixture.token||!fixture.generation||
     (fixture.setup_scope==RetainedSourceLeases::SetupScope::Test?!fixture.test:fixture.test||!fixture.callback_owner||
       (fixture.setup_scope!=RetainedSourceLeases::SetupScope::BeforeAll&&fixture.setup_scope!=RetainedSourceLeases::SetupScope::AfterAll))||!row.executable.inode||!row.channel_drained||!row.retired_batch||row.pidfd_exit!=row.startup.has_value())throw std::invalid_argument("Native Source report lacks exact final physical/fixture receipts");
  if(row.startup){const auto& process=*row.startup;if(process.pid<=0||process.pid>INT32_MAX||process.pid==host.pid||process.parent!=host.pid||process.start<host.start||process.executable!=row.executable)throw std::invalid_argument("Native Source report startup has foreign kernel identity");}
  if(row.shutdown&&(!row.startup||row.shutdown->current_parent<=0||row.shutdown->current_parent>INT32_MAX||row.shutdown->planned_code>255))throw std::invalid_argument("Native Source shutdown boundary lacks actual startup or valid observational label");
  if(row.accepted_work){const auto& work=*row.accepted_work;
    if(work.callback_iteration){const auto& iteration=*work.callback_iteration;const auto& callback=iteration.callback;
      if(!iteration.request||!iteration.assertion||!iteration.invocation||!iteration.iteration_step||iteration.outer_step==iteration.iteration_step||
         !callback.test_id||!callback.attempt_generation||!callback.scope||!callback.scope_generation||callback.worker_index!=host.worker||callback.pid!=host.pid||
         (callback.callback_kind!="body"&&callback.callback_kind!="beforeEach"&&callback.callback_kind!="afterEach")||
         (callback.callback_kind!="body"&&!callback.callback_token)||work.challenge.borrow_lease!=host.lease)
        throw std::invalid_argument("Native partial Source callback iteration has foreign owner/channel metadata");
    }
    if(!row.startup||work.challenge.ticket!=ticket.id||
       RetainedSourceWorkWire::Decode(RetainedSourceWorkWire::Encode(work.challenge)).challenge!=work.challenge||
       (work.cuts&&(work.cuts->ticket!=ticket.id||work.cuts->nonce!=work.challenge.nonce))||
       (work.observed&&(!work.cuts||!std::isfinite(work.observed_at)||work.observed_at<0))||
       (work.acknowledged&&(!work.observed||work.late_acknowledged||work.refusal||!std::isfinite(work.acknowledged_at)||work.acknowledged_at<work.observed_at||work.acknowledged_at>=work.challenge.deadline))||
       (work.late_acknowledged&&!work.observed)|| (work.refusal&&work.acknowledged))
      throw std::invalid_argument("Native Source accepted-work report lacks genuine ordered challenge/decoder/ACK receipts");
  }
}
// Attempt generation and fixture-scope generation are distinct native domains.
// Associate by the genuine host/test, never flatten a hook scope into attemptID.
template<class Report>size_t ValidateRetainedSourceReports(const Report& report){
  if(report.retained_sources.size()>RetainedSourceLeases::kHistory)throw std::invalid_argument("Native Source report capacity exceeded");
  std::set<uint64_t> tickets;std::set<std::pair<int64_t,uint64_t>> sources;std::set<unsigned> hosts;size_t bytes=0;
  for(const auto& row:report.retained_sources){ValidateRetainedSource(row);bytes+=row.accepted_work?(row.accepted_work->callback_iteration?1024:768):384;const auto& owner=row.ticket.owner;
    if(!tickets.insert(row.ticket.id).second||!hosts.insert(owner.host.worker).second||(row.startup&&!sources.emplace(row.startup->pid,row.startup->start).second))throw std::invalid_argument("Native Source report duplicate ticket/host/process incarnation");
    const auto worker=std::find_if(report.workers.begin(),report.workers.end(),[&](const auto& value){return value.worker_index==owner.host.worker&&value.pid==owner.host.pid&&value.exit_code>=0;});
    const auto attempt=std::find_if(report.attempts.begin(),report.attempts.end(),[&](const auto& value){return value.test_id==owner.fixture.test&&value.worker_index==owner.host.worker&&value.pid==owner.host.pid;});
    if(worker==report.workers.end()||(owner.fixture.setup_scope==RetainedSourceLeases::SetupScope::Test&&attempt==report.attempts.end()))throw std::invalid_argument("Native Source report has no genuine native worker/associated attempt");
    if(row.accepted_work){
      if(row.accepted_work->callback_iteration){
        if constexpr(requires {report.steps;report.callback_assertions;}){
          const auto& iteration=*row.accepted_work->callback_iteration;const auto& callback=iteration.callback;
          const auto step=std::find_if(report.steps.begin(),report.steps.end(),[&](const auto& value){return value.id==iteration.iteration_step;});
          const auto assertion=std::find_if(report.callback_assertions.begin(),report.callback_assertions.end(),[&](const auto& value){return value.id==iteration.assertion;});
          const auto actual=std::find_if(report.attempts.begin(),report.attempts.end(),[&](const auto& value){return value.test_id==callback.test_id&&value.generation==callback.attempt_generation&&value.worker_index==callback.worker_index&&value.pid==callback.pid;});
          if(step==report.steps.end()||step->owner!=callback||step->parent!=iteration.outer_step||!step->callback_iteration||
             step->callback_iteration->assertion!=iteration.assertion||step->callback_iteration->invocation!=iteration.invocation||
             !step->received||(step->receipt!=StepReceipt::Settled&&step->receipt!=StepReceipt::WorkerExited)||actual==report.attempts.end()||
             assertion==report.callback_assertions.end()||assertion->owner!=callback||assertion->request!=iteration.request||
             assertion->parent!=iteration.outer_step||iteration.invocation>assertion->attempts)
            throw std::invalid_argument("Native partial Source callback work has no actual iteration/assertion/attempt history");
        }else throw std::invalid_argument("Native partial Source callback work lacks native iteration history");
      }else if constexpr(requires {report.worker_reuse.borrows;}){
        const auto& challenge=row.accepted_work->challenge;
        const auto borrow=std::find_if(report.worker_reuse.borrows.begin(),report.worker_reuse.borrows.end(),[&](const auto& value){return value.borrow.host.lease==challenge.borrow_lease&&value.borrow.generation==challenge.borrow_generation&&value.borrow.host.worker==owner.host.worker&&value.borrow.host.pid==owner.host.pid;});
        if(borrow==report.worker_reuse.borrows.end())throw std::invalid_argument("Native Source work report has no genuine group borrow owner");
      }else throw std::invalid_argument("Native Source work report lacks native borrow history");
    }
    // An all-hook worker fixture has native associatedTest0. Preserve that real
    // scope receipt instead of fabricating a neighboring test attempt.

  }
  return bytes;
}
template<class Writer>void PutRetainedSourceReports(Writer& out,const std::vector<ReportRetainedSource>& rows){
  out.Put("[");bool first=true;
  for(const auto& row:rows){if(!first)out.Put(",");first=false;const auto& owner=row.ticket.owner;
    out.Put("{\"ticketId\":\"");out.NumberValue(row.ticket.id);out.Put("\",\"hostLease\":\"");out.NumberValue(owner.host.lease);out.Put("\",\"workerIndex\":");out.NumberValue(owner.host.worker);out.Put(",\"hostPID\":");out.NumberValue(owner.host.pid);out.Put(",\"hostStart\":\"");out.NumberValue(owner.host.start);
    out.Put("\",\"catalogId\":\"");out.NumberValue(owner.fixture.catalog);out.Put("\",\"declarationId\":\"");out.NumberValue(owner.fixture.declaration);out.Put("\",\"fixtureToken\":\"");out.NumberValue(owner.fixture.token);out.Put("\",\"associatedTestId\":\"");out.NumberValue(owner.fixture.test);out.Put("\",\"fixtureScopeGeneration\":\"");out.NumberValue(owner.fixture.generation);
    if(owner.fixture.callback_owner){out.Put("\",\"callbackScope\":\"");out.Put(owner.fixture.setup_scope==RetainedSourceLeases::SetupScope::BeforeAll?"beforeAll":owner.fixture.setup_scope==RetainedSourceLeases::SetupScope::AfterAll?"afterAll":"test");out.Put("\",\"callbackOwner\":\"");out.NumberValue(owner.fixture.callback_owner);}
    out.Put("\",\"executableDevice\":\"");out.NumberValue(row.executable.device);out.Put("\",\"executableInode\":\"");out.NumberValue(row.executable.inode);out.Put("\",\"startup\":");
    if(row.startup){out.Put("{\"receipt\":\"credentialedKernelStartup\",\"pid\":");out.NumberValue(row.startup->pid);out.Put(",\"parentPID\":");out.NumberValue(row.startup->parent);out.Put(",\"start\":\"");out.NumberValue(row.startup->start);out.Put("\"}");}else out.Put("null");
    out.Put(",\"shutdownBoundary\":");if(row.shutdown){out.Put("{\"receipt\":\"shutdownReachedLabel\",\"parentAtBoundary\":");out.NumberValue(row.shutdown->current_parent);out.Put(",\"plannedExitCode\":");out.NumberValue(row.shutdown->planned_code);out.Put("}");}else out.Put("null");
    if(row.accepted_work){const auto& work=*row.accepted_work;
      out.Put(",\"acceptedWork\":{\"receipt\":\"originalStdinAndDecodedStdout\",\"nonce\":\"");out.NumberValue(work.challenge.nonce);
      if(work.callback_iteration){const auto& iteration=*work.callback_iteration;const auto& callback=iteration.callback;
        out.Put("\",\"purpose\":\"callbackIteration\",\"channelLease\":\"");out.NumberValue(work.challenge.borrow_lease);out.Put("\",\"channelGeneration\":\"");out.NumberValue(work.challenge.borrow_generation);
        out.Put("\",\"callbackIteration\":{");const auto id=[&](const char* key,uint64_t value){out.Put(key);out.NumberValue(value);out.Put("\"");};
        id("\"assertionId\":\"",iteration.assertion);id(",\"requestId\":\"",iteration.request);id(",\"invocation\":\"",iteration.invocation);id(",\"iterationStepId\":\"",iteration.iteration_step);id(",\"parentStepId\":\"",iteration.outer_step);
        id(",\"testId\":\"",callback.test_id);id(",\"generation\":\"",callback.attempt_generation);id(",\"callbackToken\":\"",callback.callback_token);id(",\"scopeId\":\"",callback.scope);id(",\"scopeGeneration\":\"",callback.scope_generation);
        out.Put(",\"workerIndex\":");out.NumberValue(callback.worker_index);out.Put(",\"pid\":");out.NumberValue(callback.pid);out.Put(",\"callbackKind\":\"");out.Put(callback.callback_kind);out.Put("\"}");
        out.Put(",\"originalDeadlineMs\":");
      }else{out.Put("\",\"borrowLease\":\"");out.NumberValue(work.challenge.borrow_lease);out.Put("\",\"borrowGeneration\":\"");out.NumberValue(work.challenge.borrow_generation);out.Put("\",\"originalDeadlineMs\":");}
      out.NumberValue(work.challenge.deadline);
      out.Put(",\"cuts\":");if(work.cuts){const auto put=[&](const auto& cut){out.Put("{\"instance\":\"");out.NumberValue(cut.instance);out.Put("\",\"sequence\":\"");out.NumberValue(cut.sequence);out.Put("\",\"nonce\":\"");out.NumberValue(cut.nonce);out.Put("\"}");};out.Put("{\"reader\":");put(work.cuts->reader);out.Put(",\"writer\":");put(work.cuts->writer);out.Put("}");}else out.Put("null");
      out.Put(",\"decoderObserved\":");out.Put(work.observed?"true":"false");out.Put(",\"acknowledged\":");out.Put(work.acknowledged?"true":"false");
      out.Put(",\"canceled\":");out.Put(work.canceled?"true":"false");out.Put(",\"lateAcknowledged\":");out.Put(work.late_acknowledged?"true":"false");out.Put(",\"refusal\":\"");out.NumberValue(work.refusal);out.Put("\",\"observedMs\":");if(work.observed)out.NumberValue(work.observed_at);else out.Put("null");out.Put(",\"acknowledgedMs\":");if(work.acknowledged)out.NumberValue(work.acknowledged_at);else out.Put("null");out.Put("}");
    }
    out.Put(",\"pidfdExitObserved\":");out.Put(row.pidfd_exit?"true":"false");out.Put(",\"channelDrained\":true,\"physicalRetirement\":{\"receipt\":\"hostWaitpidAndDescendantBatchRetired\",\"batch\":\"");out.NumberValue(row.retired_batch);out.Put("\"}}");
  }out.Put("]");
}
} // namespace suitecut::runner
