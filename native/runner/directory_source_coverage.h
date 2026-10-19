// SPDX-License-Identifier: MIT
#pragma once
#include "retained_source_report.h"
namespace suitecut::runner {
// Concurrent actors cannot use a begin-index suffix of the run-global ledger.
// Actual globally unique worker identities select the original native tickets;
// each selected ticket must be retired and represented exactly once in report.
template<class Workers>void ValidateDirectorySourceCoverage(const RetainedSourceLeases& ledger,const Workers& workers,const std::vector<ReportRetainedSource>& rows){
  std::set<std::pair<unsigned,int64_t>> owners;std::set<unsigned> indices;
  for(const auto& worker:workers)if(!worker.worker_index||worker.pid<=0||!indices.insert(worker.worker_index).second||!owners.emplace(worker.worker_index,worker.pid).second)throw std::invalid_argument("Native directory repeated actual host identity");
  std::set<uint64_t> expected;
  for(const auto& record:ledger.Records())if(owners.contains({record.ticket.owner.host.worker,record.ticket.owner.host.pid})){
    if(record.state!=RetainedSourceLeases::State::Retired)throw std::invalid_argument("Native directory Source ticket is physically owned");
    expected.insert(record.ticket.id);
  }
  for(const auto& row:rows){const auto id=row.ticket.id;if(!id||id>ledger.Records().size()||ledger.Records()[id-1].ticket!=row.ticket||!expected.erase(id))throw std::invalid_argument("Native directory Source report has a foreign/missing/duplicate ticket");}
  if(!expected.empty())throw std::invalid_argument("Native directory Source report omits an original retired ticket");
}
}
