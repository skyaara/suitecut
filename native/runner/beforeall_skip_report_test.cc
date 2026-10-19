// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <functional>
#include <iostream>
using namespace suitecut::runner;
void Reject(const RunnerReport& r){bool rejected=false;try{(void)report_detail::Validate(r);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(int argc,char**argv){
 assert(argc==2);const auto dir=std::filesystem::path(argv[1]);RunnerReport r;
 r.workers.push_back({1,getpid(),false,0,{},{}});
 r.attempts.push_back({11,1,0,1,getpid(),"\"associated\"","\"\"","skipped",true,0.001,"\"actual\\ud800\\u0000<&\"","skipped"});
 r.skip_causes.push_back({77,1,2,3,11,1,5,1,getpid(),"\"actual\\ud800\\u0000<&\"","skipped"});
 r.runtime_skipped.push_back({12,77,"\"not admitted\""});
 const auto summary=report_detail::Validate(r);assert(summary.total_tests()==2&&summary.skipped==2&&summary.duration_ms==0.001);
 WriteRunnerJson(r,dir/"valid.json");WriteRunnerJUnit(r,dir/"valid.xml");WriteRunnerHTML(r,dir/"valid.html");
 {auto bad=r;bad.skip_causes.push_back(r.skip_causes[0]);Reject(bad);}
 {auto bad=r;bad.skip_causes[0].pid++;Reject(bad);}
 {auto bad=r;bad.skip_causes[0].callback_token=0;Reject(bad);}
 {auto bad=r;bad.skip_causes[0].generation=999;Reject(bad);}
 {auto bad=r;bad.skip_causes[0].reason_json="\"different actual reason\"";Reject(bad);}
 {auto bad=r;bad.runtime_skipped[0].cause_id=999;Reject(bad);}
 {auto bad=r;bad.runtime_skipped[0].test_id=11;Reject(bad);}
 {auto bad=r;bad.runtime_skipped.push_back(r.runtime_skipped[0]);Reject(bad);}
 {auto bad=r;bad.skip_causes[0].status="failed";Reject(bad);}
 {auto bad=r;bad.attempts[0].status="failed";bad.attempts[0].expected=false;Reject(bad);bad.runtime_skipped.clear();assert(report_detail::Validate(bad).unexpected==1);WriteRunnerJson(bad,dir/"accepted-cause-cleanup-failed.json");}
 {auto bad=r;bad.unrun.push_back({12,"\"duplicate\"","\"unrun\""});Reject(bad);}
 {auto bad=r;bad.runtime_skipped.clear();bad.skip_causes[0].status="timedOut";bad.attempts[0].status="timedOut";bad.attempts[0].expected=false;assert(report_detail::Validate(bad).unexpected==1);}
 std::cout<<"PASS12 native beforeAll skip report attribution/validation groups\n";
}
