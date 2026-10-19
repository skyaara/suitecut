// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static RunnerReport Example(){RunnerReport r;ReportProject p;p.id=1;p.name_json="\"project\"";p.status="passed";p.test_ids={1};p.selected_test_ids={1};p.selection_role="primary";p.workers={1};r.projects={p};r.workers={{1,101,false,0,"",""}};r.attempts={{1,1,0,1,101,"\"test\"","\"\"","passed",true,1}};r.resource_locks={{1,1,{ResourceLockName("-") ,ResourceLockName("4100")},"physicalRetired"}};return r;}
template<class F>static void Reject(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {const auto r=Example();const auto summary=report_detail::Validate(r);assert(summary.expected==1&&summary.unexpected==0);++controls;}
 {auto r=Example();r.resource_locks[0].generation=0;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks[0].project_id=2;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks[0].receipt="active";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks[0].receipt="neverStarted";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.workers[0].exit_code=-1;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks.push_back(r.resource_locks.front());Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks[0].names={ResourceLockName("4100"),ResourceLockName("4100")};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();std::reverse(r.resource_locks[0].names.begin(),r.resource_locks[0].names.end());Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.resource_locks[0].names={std::string(1,'x')};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts.clear();r.workers.clear();r.projects[0].status="notRun";r.projects[0].workers.clear();r.unrun={{1,"\"test\"","\"original native stop\""}};r.resource_locks[0].receipt="neverStarted";const auto s=report_detail::Validate(r);assert(s.did_not_run==1&&s.tests.empty());++controls;}
 std::cout<<"Native resource-lock report "<<controls<<" controls passed; actual process proof is separate\n";
}
