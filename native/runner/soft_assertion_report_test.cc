// SPDX-License-Identifier: MIT
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
RunnerReport ControlledReport(){
 const StepOwner owner{1,1,0,1,1,1,101,"body"};
 TestSteps steps;assert(steps.Open(owner,100,0));const auto id=*steps.Begin(owner,0,"\"actual soft step\"",0,1);
 SoftAssertions soft([&](auto actual){return steps.Find(actual);});soft.Open(owner,100,0);const auto a=soft.Begin(owner,1,id,StepLocation{"\"/actual/caller.ts\"",7,9,false},2);
 assert(soft.Settled(owner,a.id,SoftRawOutcome::RecognizedMismatch,"\"raw mismatch \\ud800\\u0000 <&>\"",3)==SoftAssertions::Verdict::Mismatch);
 assert(steps.NoteSoftFailure(owner,id,a.id,soft.Records()[0].error_json,3));assert(steps.Settle(owner,id,true,"\"\"",4));assert(steps.Close(owner,5)&&steps.Retire(owner));soft.CloseAdmission(owner);soft.RetireContext(owner);
 RunnerReport r;r.workers.push_back({1,101,false,0,{},{}});r.attempts.push_back({1,1,0,1,101,"\"actual test\"",soft.Records()[0].error_json,"failed",false,5,std::nullopt,"passed",std::nullopt,{},0,a.id});r.soft_assertions=soft.Records();r.steps=steps.Records();return r;
}
template<class F>void BadSoftReport(F&& f){auto r=ControlledReport();f(r);bool rejected=false;try{(void)report_detail::Validate(r);}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char** argv){assert(argc==2);unsigned groups=0;auto r=ControlledReport();assert(report_detail::Validate(r).unexpected==1);WriteRunnerJson(r,std::filesystem::path(argv[1])/"soft.json");WriteRunnerJUnit(r,std::filesystem::path(argv[1])/"soft.xml");WriteRunnerHTML(r,std::filesystem::path(argv[1])/"soft.html");++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].owner.pid=102;});++groups;
 BadSoftReport([](auto& v){v.soft_assertions.push_back(v.soft_assertions[0]);});++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].owner.attempt_generation=2;});++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].receipt=SoftReceipt::Late;});++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].raw=SoftRawOutcome::OtherFailure;});++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].location->explicit_location=true;});++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].received=6;});++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].cleanup=true;});++groups;
 BadSoftReport([](auto& v){v.steps[0].soft_error_id=2;});++groups;
 BadSoftReport([](auto& v){v.soft_assertions[0].step=99;});++groups;
 BadSoftReport([](auto& v){v.attempts[0].soft_error_id=99;});++groups;
 BadSoftReport([](auto& v){v.attempts[0].status="timedOut";});++groups;
 {auto hard=r;hard.attempts[0].soft_error_id=0;hard.attempts[0].error_json="\"genuine hard primary\"";assert(report_detail::Validate(hard).unexpected==1);WriteRunnerJson(hard,std::filesystem::path(argv[1])/"hard.json");++groups;}
 std::cout<<"PASS "<<groups<<" native soft report controls (controlled attempt/worker DTO, no OS receipt claim)\n";
}
