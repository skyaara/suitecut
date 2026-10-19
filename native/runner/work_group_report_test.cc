// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static RunnerReport Example(){
  RunnerReport r;ReportProject p;p.id=1;p.name_json="\"project\"";p.status="passed";p.test_ids={1,2};p.selected_test_ids={1,2};p.selection_role="primary";p.workers={1,2};r.projects={p};
  r.workers={{1,101,false,0,"",""},{2,102,false,0,"",""}};
  r.attempts={{1,1,0,1,101,"\"first\"","\"\"","passed",true,1},{2,2,0,2,102,"\"second\"","\"\"","passed",true,1}};
  r.work_groups={{1,1,0,1,WorkGroupMode::Parallel,"passed","physicalRetired",{1},{1}},{2,1,0,2,WorkGroupMode::Parallel,"passed","physicalRetired",{2},{2}}};return r;
}
template<class F>static void Reject(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {auto r=Example();const auto s=report_detail::Validate(r);assert(s.expected==2);++controls;}
 {auto r=Example();r.work_groups.clear();report_detail::Validate(r);++controls;}
 {auto r=Example();r.work_groups[1].id=1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[1].generation=1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[0].generation=0;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[0].project_id=2;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[0].workers={2};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.workers[0].exit_code=-1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[0].tests={2};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups.pop_back();Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[0].receipt="active";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[0].mode=WorkGroupMode::Serial;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.work_groups[0].mode=WorkGroupMode::Default;r.work_groups[0].tests={1,2};r.work_groups[0].workers={1,2};r.work_groups.resize(1);report_detail::Validate(r);++controls;}
 {auto r=Example();r.work_groups[0].status="failed";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts[0].worker_index=2;r.attempts[0].pid=102;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts.erase(r.attempts.begin()+1);r.workers.resize(1);r.projects[0].workers={1};r.projects[0].status="interrupted";r.unrun={{2,"\"second\"","\"original stop\""}};r.work_groups[1].workers.clear();r.work_groups[1].receipt="neverStarted";r.work_groups[1].status="notRun";report_detail::Validate(r);++controls;}
 {auto r=Example();r.attempts.erase(r.attempts.begin()+1);r.workers.resize(1);r.projects[0].workers={1};r.projects[0].status="interrupted";r.unrun={{2,"\"second\"","\"original stop\""}};r.work_groups[1].workers.clear();r.work_groups[1].receipt="notAdmitted";r.work_groups[1].generation.reset();r.work_groups[1].status="notRun";report_detail::Validate(r);++controls;}
 {auto r=Example();r.work_groups[0].receipt="notAdmitted";r.work_groups[0].status="notRun";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks={{1,1,{ResourceLockName("4100")},"physicalRetired",1},{1,2,{ResourceLockName("4100")},"physicalRetired",2}};report_detail::Validate(r);++controls;}
 {auto r=Example();r.resource_locks={{1,1,{ResourceLockName("4100")},"physicalRetired",2}};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks={{1,1,{ResourceLockName("4100")},"physicalRetired"}};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks={{1,1,{ResourceLockName("4100")},"neverStarted",1}};Reject([&]{report_detail::Validate(r);});++controls;}
 assert(controls==22);std::cout<<"Native work-group report "<<controls<<" controls passed; actual process proof is separate\n";
}
