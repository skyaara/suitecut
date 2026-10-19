// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static unsigned checked=0;template<class F>void Invalid(F fn){++checked;bool failed=false;try{fn();}catch(const std::exception&){failed=true;}if(!failed)std::cerr<<"Expected validation rejection group "<<checked<<"\n";assert(failed);}
int main(int argc,char** argv){
 const StepOwner owner{1,1,0,1,1,1,getpid()};TestSteps actual;assert(actual.Open(owner,1000,0));const auto source=*actual.Begin(owner,0,"\"actual local skipped step\"",0,1),child=*actual.Begin(owner,source,"\"actual child\"",0,2);assert(actual.SkipStep(owner,source,7,"\"reason\\ud800<&\"",true,3));assert(actual.Settle(owner,child,true,"\"\"",4,0,7));assert(actual.Settle(owner,source,true,"\"\"",5,0,7));assert(actual.Close(owner,6));assert(actual.Retire(owner));RunnerReport r;r.workers.push_back({1,getpid(),false,0,{},{}});r.attempts.push_back(ReportAttempt{1,1,0,1,getpid(),"\"actual passed test\"","\"\"","passed",true,7});r.steps=actual.Records();assert(report_detail::Validate(r).expected==1);
 auto invalid=[&](auto mutate){auto bad=r;mutate(bad);Invalid([&]{report_detail::Validate(bad);});};
 invalid([](auto& v){v.steps[0].step_skip.reset();});invalid([](auto& v){v.steps[0].step_skip->request=0;});invalid([](auto& v){v.steps[0].step_skip->reason_json="not JSON";});invalid([](auto& v){v.steps[0].receipt_step_skip=8;});invalid([](auto& v){v.steps[0].receipt_ok=false;});invalid([](auto& v){v.steps[0].error_json="\"not actual skip error\"";});invalid([](auto& v){v.steps[0].receipt=StepReceipt::WorkerExited;});invalid([](auto& v){v.steps[0].step_skip->description_present=false;});invalid([](auto& v){v.steps[1].step_skip->source_step=v.steps[1].id;});invalid([](auto& v){v.steps[0].step_skip->source_step=v.steps[1].id;});invalid([](auto& v){v.steps[1].step_skip->reason_json="\"different cause\"";});invalid([](auto& v){v.steps[0].test_skip=StepSkipCause{3,"\"test cause\""};v.steps[0].receipt_test_skip=3;});
 // A native cause source must be a real self/ancestor in the same callback.
 auto wrong=r;wrong.steps[1].step_skip->source_step=999;Invalid([&]{report_detail::Validate(wrong);});
 if(argc>1){const auto p=std::filesystem::path(argv[1]);WriteRunnerJson(r,p/"local-skip.json");WriteRunnerJUnit(r,p/"local-skip.xml");WriteRunnerHTML(r,p/"local-skip.html");}
 std::cout<<"PASS14 native local-step-skip report cause/source/status/receipt validation groups\n";
}
