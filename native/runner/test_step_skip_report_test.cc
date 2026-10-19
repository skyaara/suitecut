// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Invalid(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}assert(failed);}
int main(int argc,char** argv){
 const StepOwner owner{1,1,0,1,1,1,getpid()};TestSteps actual;assert(actual.Open(owner,1000,0));const auto id=*actual.Begin(owner,0,"\"actual skipped step\"",0,1);assert(actual.SkipTestCallback(owner,7,"\"reason\\ud800<&\"",2));assert(actual.Settle(owner,id,true,"\"\"",3,7));assert(actual.Retire(owner));RunnerReport r;r.workers.push_back({1,getpid(),false,0,{},{}});r.attempts.push_back(ReportAttempt{1,1,0,1,getpid(),"\"actual skipped test\"","\"\"","skipped",true,4,std::string("\"reason\\ud800<&\""),"skipped"});r.steps=actual.Records();assert(report_detail::Validate(r).skipped==1);
 auto invalid=[&](auto mutate){auto bad=r;mutate(bad.steps[0]);Invalid([&]{report_detail::Validate(bad);});};
 invalid([](auto& s){s.test_skip.reset();});invalid([](auto& s){s.test_skip->request=0;});invalid([](auto& s){s.test_skip->reason_json="not JSON";});invalid([](auto& s){s.receipt_test_skip=8;});invalid([](auto& s){s.receipt_ok=false;});invalid([](auto& s){s.error_json="\"not actual skip error\"";});invalid([](auto& s){s.receipt=StepReceipt::WorkerExited;});
 if(argc>1){const auto p=std::filesystem::path(argv[1]);WriteRunnerJson(r,p/"skip.json");WriteRunnerJUnit(r,p/"skip.xml");WriteRunnerHTML(r,p/"skip.html");}
 std::cout<<"PASS8 native test-skip step report cause/status/receipt validation groups\n";
}
