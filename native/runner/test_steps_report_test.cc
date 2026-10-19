// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Invalid(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}assert(failed);}
int main(int argc,char**argv){
 TestSteps actual;const StepOwner owner{1,1,2,3,1,1,getpid()};assert(actual.Open(owner,1000,0));const auto step=*actual.Begin(owner,0,"\"actual\\ud800\\u0000<&\"",0,1);assert(actual.Settle(owner,step,false,"\"real error\\udfff\\u0000<&\"",2));assert(actual.Close(owner,3)&&actual.Retire(owner));
 RunnerReport report;report.workers.push_back({1,getpid(),false,0,{},{}});report.attempts.push_back(ReportAttempt{1,1,0,1,getpid(),"\"test passed after catching actual child error\"","\"\"","passed",true,4});report.steps=actual.Records();assert(report_detail::Validate(report).expected==1);
 auto invalid=[&](auto mutate){auto bad=report;mutate(bad.steps[0]);Invalid([&]{report_detail::Validate(bad);});};
 invalid([](auto& s){s.id=0;});invalid([](auto& s){s.parent=s.id;});invalid([](auto& s){s.owner.attempt_generation=2;});invalid([](auto& s){s.owner.pid+=1;});invalid([](auto& s){s.owner.scope=0;});invalid([](auto& s){s.status=StepStatus::Running;});invalid([](auto& s){s.receipt=StepReceipt::Pending;});invalid([](auto& s){s.started=NAN;});invalid([](auto& s){s.ended=0;});invalid([](auto& s){s.received=1;});invalid([](auto& s){s.title_json="not JSON";});invalid([](auto& s){s.status=static_cast<StepStatus>(99);});invalid([](auto& s){s.receipt=static_cast<StepReceipt>(99);});invalid([](auto& s){s.status=StepStatus::Passed;s.receipt_ok=false;});
 if(argc>1){const auto dir=std::filesystem::path(argv[1]);WriteRunnerJson(report,dir/"steps.json");WriteRunnerJUnit(report,dir/"steps.xml");WriteRunnerHTML(report,dir/"steps.html");const auto protected_path=dir/"protected.json";{std::ofstream stream(protected_path);stream<<"actual prior report";}auto bad=report;bad.steps[0].parent=999;Invalid([&]{WriteRunnerJson(bad,protected_path);});std::ifstream stream(protected_path);std::string retained;std::getline(stream,retained);assert(retained=="actual prior report");}
 std::cout<<"PASS14 native step report identity/status/scalar validation groups\n";
}
