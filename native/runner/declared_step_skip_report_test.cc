// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Invalid(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}assert(failed);}
int main(int argc,char** argv){const StepOwner owner{1,1,0,1,1,1,getpid()};TestSteps c;assert(c.Open(owner,100,0));assert(c.DeclareSkipped(owner,0,"\"declared\\ud800<&\"",0,1,1));assert(c.DeclareSkipped(owner,0,"\"actual second\"",0,2,2));assert(c.Close(owner,3)&&c.Retire(owner));RunnerReport r;r.workers.push_back({1,getpid(),false,0,{},{}});r.attempts.push_back(ReportAttempt{1,1,0,1,getpid(),"\"actual passed test\"","\"\"","passed",true,4});r.steps=c.Records();assert(report_detail::Validate(r).expected==1);
 auto invalid=[&](auto change){auto v=r;change(v);Invalid([&]{report_detail::Validate(v);});};
 invalid([](auto& v){v.steps[0].receipt=StepReceipt::Settled;});invalid([](auto& v){v.steps[0].receipt=StepReceipt::WorkerExited;});invalid([](auto& v){v.steps[0].receipt_ok=true;});invalid([](auto& v){v.steps[0].declared_skip_request=0;});invalid([](auto& v){v.steps[0].status=StepStatus::Passed;});invalid([](auto& v){v.steps[0].test_skip=StepSkipCause{1,"\"invented test skip\""};});invalid([](auto& v){v.steps[0].error_json="\"invented callback error\"";});invalid([](auto& v){v.steps[0].receipt_error_json="\"invented raw error\"";});invalid([](auto& v){*v.steps[0].received+=1;});invalid([](auto& v){v.steps[1].declared_skip_request=1;});invalid([](auto& v){v.steps[1].parent=1;v.steps[1].depth=2;v.steps[1].started=*v.steps[1].ended=*v.steps[1].received=1;});
 if(argc>1){const auto p=std::filesystem::path(argv[1]);WriteRunnerJson(r,p/"declared.json");WriteRunnerJUnit(r,p/"declared.xml");WriteRunnerHTML(r,p/"declared.html");}
 std::cout<<"PASS12 native predeclared-step no-invocation report validation groups\n";
}
