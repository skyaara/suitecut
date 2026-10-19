// SPDX-License-Identifier: MIT
#pragma once
#include "worker_reuse_pool.h"
#include <set>
namespace suitecut::runner {
// Snapshots of native ownership receipts; a live PID cannot appear as retired.
struct ReportWorkerReuse {
  std::vector<WorkerReusePool::HostRecord> hosts;
  std::vector<WorkerReusePool::BorrowRecord> borrows;
};
template<class Report>void ValidateWorkerReuseReports(const Report& report){
  const auto& reuse=report.worker_reuse;
  if(reuse.hosts.empty()&&reuse.borrows.empty())return;
  if(report.files.size()!=1||!report.projects.empty()||!report.work_groups.empty()||!report.file_resource_locks.empty()||reuse.hosts.empty()||reuse.hosts.size()>WorkerReusePool::kGroups||reuse.borrows.empty()||reuse.borrows.size()>WorkerReusePool::kGroups||reuse.hosts.size()!=report.workers.size())
    throw std::invalid_argument("Native worker reuse report owner/capacity rejected");
  std::set<unsigned> indices;std::set<uint64_t> groups;std::vector<size_t> last(reuse.hosts.size(),SIZE_MAX);
  for(size_t i=0;i<reuse.hosts.size();++i){const auto& host=reuse.hosts[i];
    const auto actual=std::find_if(report.workers.begin(),report.workers.end(),[&](const auto& worker){return worker.worker_index==host.host.worker&&worker.pid==host.host.pid;});
    if(host.host.lease!=i+1||!host.host.worker||host.host.pid<=0||!indices.insert(host.host.worker).second||actual==report.workers.end()||!host.physical_retired||!host.exit||host.exit->code!=actual->exit_code||host.exit->signaled!=actual->signaled||!std::isfinite(host.exited)||!std::isfinite(host.retired)||host.exited<0||host.retired<host.exited||host.retired>report.files[0].retired)
      throw std::invalid_argument("Native reusable host lacks actual final worker exit/physical receipt");
  }
  for(size_t i=0;i<reuse.borrows.size();++i){const auto& record=reuse.borrows[i];const auto& owner=record.borrow;const auto& group_owner=owner.group;
    if(owner.generation!=i+1||!owner.host.lease||owner.host.lease>reuse.hosts.size()||reuse.hosts[owner.host.lease-1].host!=owner.host||group_owner.file!=report.files[0].owner||group_owner.index>=report.file_work_groups.size()||!groups.insert(group_owner.index).second||!record.outcome)
      throw std::invalid_argument("Native reused group borrow has foreign/duplicate ownership");
    const auto& group=report.file_work_groups[group_owner.index];
    if(!group.generation||*group.generation!=group_owner.generation||group.workers!=std::vector<unsigned>{owner.host.worker}||group.mode!=WorkGroupMode::Parallel||group.suite||group.tests.size()!=1||
       group.status!=(*record.outcome==WorkerReusePool::Outcome::Passed?"passed":*record.outcome==WorkerReusePool::Outcome::Failed?"failed":"interrupted"))
      throw std::invalid_argument("Native group report disagrees with its live host borrow");
    if(last[owner.host.lease-1]!=SIZE_MAX){const auto& previous=reuse.borrows[last[owner.host.lease-1]];
      if(previous.receipt!=WorkerReusePool::Receipt::IdleTransferred||previous.outcome!=WorkerReusePool::Outcome::Passed)throw std::invalid_argument("Native host reused without exact preceding idle transfer");
    }
    last[owner.host.lease-1]=i;
    if(record.canceled_idle_request&&(record.idle_request||record.receipt!=WorkerReusePool::Receipt::PhysicalRetired||!std::isfinite(record.canceled_acknowledged)||record.canceled_acknowledged<0||record.canceled_acknowledged>reuse.hosts[owner.host.lease-1].exited))throw std::invalid_argument("Native canceled idle ACK has no exact physical-close receipt chain");
    if(record.receipt==WorkerReusePool::Receipt::IdleTransferred){
      if(group.receipt!="idleTransferred"||*record.outcome!=WorkerReusePool::Outcome::Passed||!record.idle_request||!std::isfinite(record.acknowledged)||!std::isfinite(record.transferred)||record.acknowledged<0||record.transferred<record.acknowledged||record.transferred>reuse.hosts[owner.host.lease-1].exited)
        throw std::invalid_argument("Native idle transfer lacks genuine readiness receipt chain");
    }else if(record.receipt!=WorkerReusePool::Receipt::PhysicalRetired||group.receipt!="physicalRetired")throw std::invalid_argument("Native group borrow remains physically owned");
  }
  for(const auto index:last)if(index==SIZE_MAX)throw std::invalid_argument("Native reusable host has no actual group borrow");
  for(const auto& group:report.file_work_groups)if(group.generation&&!groups.contains(group.id-1))throw std::invalid_argument("Native admitted file group lacks exact borrow receipt");
}
template<class Writer>void PutWorkerReuseReports(Writer& out,const ReportWorkerReuse& reuse){
  out.Put("{\"hosts\":[");bool first=true;
  for(const auto& row:reuse.hosts){if(!first)out.Put(",");first=false;out.Put("{\"lease\":\"");out.NumberValue(row.host.lease);out.Put("\",\"workerIndex\":");out.NumberValue(row.host.worker);out.Put(",\"pid\":");out.NumberValue(row.host.pid);out.Put(",\"receipt\":\"physicalRetired\",\"exitedMs\":");out.NumberValue(row.exited);out.Put(",\"retiredMs\":");out.NumberValue(row.retired);out.Put("}");}
  out.Put("],\"borrows\":[");first=true;
  for(const auto& row:reuse.borrows){if(!first)out.Put(",");first=false;out.Put("{\"generation\":\"");out.NumberValue(row.borrow.generation);out.Put("\",\"hostLease\":\"");out.NumberValue(row.borrow.host.lease);out.Put("\",\"fileWorkGroupId\":\"");out.NumberValue(row.borrow.group.index+1);out.Put("\",\"groupGeneration\":\"");out.NumberValue(row.borrow.group.generation);out.Put("\",\"receipt\":\"");out.Put(row.receipt==WorkerReusePool::Receipt::IdleTransferred?"idleTransferred":"physicalRetired");out.Put("\",\"idleRequest\":");if(row.idle_request){out.Put("\"");out.NumberValue(row.idle_request);out.Put("\"");}else out.Put("null");out.Put(",\"acknowledgedMs\":");if(row.idle_request)out.NumberValue(row.acknowledged);else out.Put("null");out.Put(",\"transferredMs\":");if(row.receipt==WorkerReusePool::Receipt::IdleTransferred)out.NumberValue(row.transferred);else out.Put("null");if(row.canceled_idle_request){out.Put(",\"canceledIdleRequest\":\"");out.NumberValue(row.canceled_idle_request);out.Put("\",\"canceledAcknowledgedMs\":");out.NumberValue(row.canceled_acknowledged);}out.Put("}");}
  out.Put("]}");
}
}
