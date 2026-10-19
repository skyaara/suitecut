// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
// DTO validation controls; actual file/PID receipts are separate kernel and
// public adapter proofs, not manufactured by this example.
static RunnerReport Example(){
  RunnerReport r;r.files={{{1,23,47},"/actual/controlled/suite.mjs","passed",{1,2,3},{1,2},100,200}};
  r.workers={{1,101,false,0,"",""},{2,102,false,0,"",""}};
  r.attempts={{1,1,0,1,101,"\"first\"","\"\"","passed",true,1},{2,2,0,2,102,"\"second\"","\"\"","passed",true,1}};
  r.file_work_groups={{1,1,0,1,WorkGroupMode::Parallel,"passed","physicalRetired",{1},{1}},
    {2,1,0,2,WorkGroupMode::Parallel,"passed","physicalRetired",{2},{2}}};return r;
}
template<class F>static void Reject(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {const auto r=Example();assert(report_detail::Validate(r).expected==2);++controls;}
 {auto r=Example();r.files[0].owner.id=0;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.files[0].retired=99;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.files[0].path="relative";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.files.push_back(r.files[0]);Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[1].file_id=2;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[1].generation=1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[0].workers={2};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.workers[0].exit_code=-1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[0].tests={3};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups.pop_back();Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[0].receipt="active";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[0].mode=WorkGroupMode::Serial;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_work_groups[0].mode=WorkGroupMode::Default;r.file_work_groups[0].tests={1,2};r.file_work_groups[0].workers={1,2};r.file_work_groups.resize(1);report_detail::Validate(r);++controls;}
 {auto r=Example();r.file_work_groups[0].status="failed";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts[0].worker_index=2;r.attempts[0].pid=102;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts.pop_back();r.workers.pop_back();r.files[0].status="notRun";r.unrun={{2,"\"second\"","\"original stop\""}};r.file_work_groups[1].workers.clear();r.file_work_groups[1].receipt="neverStarted";r.file_work_groups[1].status="notRun";report_detail::Validate(r);++controls;}
 {auto r=Example();r.attempts.pop_back();r.workers.pop_back();r.files[0].status="notRun";r.unrun={{2,"\"second\"","\"original stop\""}};r.file_work_groups[1].workers.clear();r.file_work_groups[1].generation.reset();r.file_work_groups[1].receipt="notAdmitted";r.file_work_groups[1].status="notRun";report_detail::Validate(r);++controls;}
 {auto r=Example();r.file_resource_locks={{1,1,1,"physicalRetired",{ResourceLockName("00d8")}}};report_detail::Validate(r);++controls;}
 {auto r=Example();r.file_resource_locks={{1,1,2,"physicalRetired",{ResourceLockName("00d8")}}};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_resource_locks={{1,1,1,"neverStarted",{ResourceLockName("00d8")}}};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_resource_locks={{2,1,1,"physicalRetired",{ResourceLockName("00d8")}}};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.file_resource_locks={{1,1,1,"physicalRetired",{ResourceLockName("00d8"),ResourceLockName("00d8")}}};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.workers.push_back({3,103,false,0,"",""});Reject([&]{report_detail::Validate(r);});++controls;}
 assert(controls==24);std::cout<<"Native anonymous file group report: "<<controls<<" controls passed; actual receipts are separate\n";
}
