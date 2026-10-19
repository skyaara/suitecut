// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static RunnerReport Example(){
 RunnerReport r;r.workers={{1,101,false,0,"",""},{2,102,false,0,"",""}};
 r.attempts={{1,1,0,1,101,"\"first\"","\"\"","passed",true,1},{2,2,0,1,101,"\"second\"","\"bad\"","failed",false,1},
  {1,3,1,2,102,"\"first\"","\"bad\"","failed",false,1}};
 r.unrun={{3,"\"third\"","\"serial prior failure\""}};
 r.serial_groups={{8,{1,2,3},1,"failed",{{3,0,2,2},{2,1,1,3},{3,1,1,3}}}};return r;
}
template<class F>static void Reject(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){unsigned controls=0;
 {const auto r=Example();const auto s=report_detail::Validate(r);assert(s.expected==1&&s.flaky==1&&s.unexpected==1&&s.did_not_run==1&&s.final_expected.at(1)&&!s.final_expected.at(2));++controls;}
 {auto r=Example();r.serial_groups.clear();const auto s=report_detail::Validate(r);assert(s.unexpected==2&&s.flaky==0);++controls;}
 {auto r=Example();r.serial_groups[0].omissions[0].cause_generation=99;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.serial_groups[0].omissions.push_back(r.serial_groups[0].omissions[0]);Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.serial_groups[0].tests={2,1,3};Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts.back().worker_index=1;r.attempts.back().pid=101;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.serial_groups[0].status="passed";Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.serial_groups[0].retries=2;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.serial_groups[0].tests.push_back(999);Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.serial_groups.push_back(r.serial_groups[0]);r.serial_groups.back().id=9;Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.workers.pop_back();Reject([&]{report_detail::Validate(r);});++controls;}
 {auto r=Example();r.attempts.back().status="interrupted";r.serial_groups[0].status="interrupted";const auto s=report_detail::Validate(r);assert(s.unexpected==2&&s.flaky==0&&s.errors==1);++controls;}
 std::cout<<"Native serial-group actual-report validation "<<controls<<" controls passed\n";
}
