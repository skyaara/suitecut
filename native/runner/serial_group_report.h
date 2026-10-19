// SPDX-License-Identifier: MIT
#pragma once
#include "serial_group.h"
namespace suitecut::runner {
struct ReportSerialGroup {
  uint64_t id=0;std::vector<uint64_t> tests;unsigned retries=0;
  std::string status="notRun";std::vector<SerialGroup::Omission> omissions;
};
// Validate only native metadata against actual attempts/OS worker records. Never
// invent missing member attempts or infer successful callback receipts.
template<class Report>std::set<uint64_t> ValidateSerialGroupReports(const Report& report){
  if(report.serial_groups.size()>256)throw std::invalid_argument("Native serial group report capacity256 exceeded");
  std::set<uint64_t> ids,members;size_t omissions=0;
  for(const auto& group:report.serial_groups){
    if(!group.id||!ids.insert(group.id).second||group.tests.empty()||group.tests.size()>4096||group.retries>100||
       (group.status!="passed"&&group.status!="failed"&&group.status!="interrupted"&&group.status!="notRun"))
      throw std::invalid_argument("Invalid native serial group report metadata");
    std::map<uint64_t,size_t> positions;std::optional<uint64_t> project;
    for(size_t index=0;index<group.tests.size();++index){
      const auto id=group.tests[index];if(!id||!members.insert(id).second)throw std::invalid_argument("Duplicate native serial group report member");positions.emplace(id,index);
      bool known=false;
      for(const auto& a:report.attempts)known|=a.test_id==id;
      for(const auto& a:report.unrun)known|=a.test_id==id;
      for(const auto& a:report.skipped)known|=a.test_id==id;
      for(const auto& a:report.runtime_skipped)known|=a.test_id==id;
      if(!known)throw std::invalid_argument("Native serial group member lacks a declared/actual report identity");
      for(const auto& p:report.projects)if(std::find(p.test_ids.begin(),p.test_ids.end(),id)!=p.test_ids.end()){
        if(project&&*project!=p.id)throw std::invalid_argument("Native serial group crosses project identity");
        project=p.id;
      }
    }
    std::optional<unsigned> retry;size_t previous=0;unsigned owner=0;int64_t pid=0;bool failed=false,interrupted=false,any=false;
    std::set<unsigned> wave_workers;
    for(const auto& a:report.attempts)if(positions.contains(a.test_id)){
      if(a.retry>group.retries)throw std::invalid_argument("Native serial group retry exceeds declared budget");
      if(!retry||*retry!=a.retry){
        if((!retry&&a.retry!=0)||(retry&&(!failed||a.retry!=*retry+1)))throw std::invalid_argument("Native serial group replay lacks preceding actual failure");
        if(!wave_workers.insert(a.worker_index).second)throw std::invalid_argument("Native serial group replay reused a physical worker");
        retry=a.retry;previous=positions.at(a.test_id);owner=a.worker_index;pid=a.pid;failed=false;
      }else if(a.worker_index!=owner||a.pid!=pid||positions.at(a.test_id)<=previous||failed)
        throw std::invalid_argument("Native serial group actual order/worker ownership mismatch");
      previous=positions.at(a.test_id);failed|=!a.expected;interrupted|=a.status=="interrupted";any=true;
      const auto worker=std::find_if(report.workers.begin(),report.workers.end(),[&](const auto& w){return w.worker_index==a.worker_index&&w.pid==a.pid&&w.exit_code>=0;});
      if(worker==report.workers.end())throw std::invalid_argument("Native serial group lacks real worker exit receipt");
    }
    if(group.status=="passed"&&(failed||interrupted))throw std::invalid_argument("Native passed serial group has unexpected final wave");
    if(group.status=="failed"&&(!any||!failed||interrupted||*retry!=group.retries))throw std::invalid_argument("Native failed serial group lacks exhausted actual failure");
    if(group.status=="notRun"&&any)throw std::invalid_argument("Native unrun serial group has actual attempts");
    if(group.omissions.size()>8192-omissions)throw std::invalid_argument("Native serial group omission capacity8192 exceeded");
    omissions+=group.omissions.size();
    std::set<std::pair<uint64_t,unsigned>> omitted;
    for(const auto& omission:group.omissions){
      if(!positions.contains(omission.test_id)||!positions.contains(omission.cause_test)||positions.at(omission.test_id)<=positions.at(omission.cause_test)||
         omission.retry>group.retries||!omitted.emplace(omission.test_id,omission.retry).second)
        throw std::invalid_argument("Invalid native serial group omission metadata");
      const auto cause=std::find_if(report.attempts.begin(),report.attempts.end(),[&](const auto& a){return a.test_id==omission.cause_test&&a.generation==omission.cause_generation&&a.retry==omission.retry&&!a.expected;});
      if(cause==report.attempts.end()||std::any_of(report.attempts.begin(),report.attempts.end(),[&](const auto& a){return a.test_id==omission.test_id&&a.retry==omission.retry;}))
        throw std::invalid_argument("Native serial group omission invented an attempt/failure cause");
    }
  }
  return members;
}
template<class File>void PutSerialGroupReports(File& file,const std::vector<ReportSerialGroup>& groups){
  file.Put("[");bool first=true;
  for(const auto& group:groups){
    if(!first)file.Put(",");
    first=false;file.Put("{\"id\":\"");file.NumberValue(group.id);file.Put("\",\"retries\":");file.NumberValue(group.retries);file.Put(",\"status\":\"");file.Put(group.status);file.Put("\",\"testIds\":[");bool member_first=true;
    for(const auto id:group.tests){if(!member_first)file.Put(",");member_first=false;file.Put("\"");file.NumberValue(id);file.Put("\"");}
    file.Put("],\"omissions\":[");member_first=true;
    for(const auto& omission:group.omissions){if(!member_first)file.Put(",");member_first=false;file.Put("{\"testId\":\"");file.NumberValue(omission.test_id);file.Put("\",\"retry\":");file.NumberValue(omission.retry);file.Put(",\"causeTestId\":\"");file.NumberValue(omission.cause_test);file.Put("\",\"causeGeneration\":\"");file.NumberValue(omission.cause_generation);file.Put("\"}");}
    file.Put("]}");
  }
  file.Put("]");
}
}
