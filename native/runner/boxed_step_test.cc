// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Rejected(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char**argv){
 unsigned groups=0;const StepOwner owner{1,1,0,1,1,1,getpid()};
 const StepLocation site{"\"helper.ts\"",4,12,false},caller{"\"caller.ts\"",8,15,false},explicit_site{"\"declared.ts\"",123,45,true};
 const StepBox box{true,site,caller};assert(ValidStepBox(box)&&!ValidStepBox({false,site,caller})&&!ValidStepBox({true,explicit_site,caller})&&!ValidStepBox({true,std::nullopt,caller}));auto oversized=box;oversized.caller->file_json="\""+std::string(4097,'x')+"\"";assert(!ValidStepBox(oversized));auto zero=box;zero.caller->line=0;assert(!ValidStepBox(zero));++groups;
 TestSteps steps;assert(steps.Open(owner,1000,0));const auto id=steps.Begin(owner,0,"\"boxed\"",0,1,explicit_site,box);assert(id&&steps.Find(*id)->box==box&&steps.Find(*id)->location==explicit_site&&!StepErrorAttribution(*steps.Find(*id)));++groups;
 const auto error="\"Error: original\\n at authored.ts:77:9\"";assert(steps.Settle(owner,*id,false,error,2));const auto* record=steps.Find(*id);assert(*StepErrorAttribution(*record)==caller&&record->error_json==error&&record->receipt_error_json==error&&record->location==explicit_site);++groups;
 const auto fallback=steps.Begin(owner,0,"\"direct\"",0,3,site,{true,site,std::nullopt});assert(fallback&&steps.Settle(owner,*fallback,false,error,4)&&*StepErrorAttribution(*steps.Find(*fallback))==site);++groups;
 const auto unavailable=steps.Begin(owner,0,"\"no capture\"",0,5,explicit_site,{true});assert(unavailable&&steps.Settle(owner,*unavailable,false,error,6)&&!StepErrorAttribution(*steps.Find(*unavailable)));++groups;
 auto foreign=owner;++foreign.scope_generation;assert(!steps.Begin(foreign,0,"\"stale\"",0,7,site,box));assert(!steps.Begin(owner,0,"\"bad\"",0,7,site,{true,explicit_site,caller}));++groups;
 const auto parent=steps.Begin(owner,0,"\"parent\"",0,8,site,box),child=steps.Begin(owner,*parent,"\"child\"",0,9,caller,{});assert(parent&&child&&steps.Settle(owner,*child,false,error,10)&&steps.Settle(owner,*parent,false,error,11));assert(!StepErrorAttribution(*steps.Find(*child))&&*StepErrorAttribution(*steps.Find(*parent))==caller);++groups;
 const auto timed=steps.Begin(owner,0,"\"late\"",1,12,site,box);assert(timed);steps.Tick(13);assert(steps.Find(*timed)->status==StepStatus::TimedOut&&steps.Inspect().physical==1&&StepErrorAttribution(*steps.Find(*timed)));assert(steps.Settle(owner,*timed,false,error,14)&&steps.Find(*timed)->status==StepStatus::TimedOut&&steps.Find(*timed)->receipt_error_json==error);++groups;
 const auto omitted=steps.DeclareSkipped(owner,0,"\"omitted\"",0,1,15,site,box);assert(omitted&&!StepErrorAttribution(*steps.Find(*omitted))&&steps.Find(*omitted)->receipt==StepReceipt::NotInvoked);steps.Close(owner,16);assert(steps.Retire(owner));++groups;
 TestSteps::Budget budget;TestSteps left(&budget),right(&budget);auto other=owner;other.worker_index=2;assert(left.Open(owner,100,0)&&right.Open(other,100,0));const auto l=left.Begin(owner,0,"\"left\"",0,1,site,box),r=right.Begin(other,0,"\"right\"",0,1,site,box);assert(l==1&&r==2&&!left.Find(*r)&&budget.reserved==left.Inspect().metadata_reserved+right.Inspect().metadata_reserved&&budget.physical==2);++groups;
 RunnerSteps packets({[](const auto&){},[](const auto&){assert(false);}});packets.BeginCallback(owner,100,0);
 std::vector<std::string> packet={"STEP_BEGIN","1","0","1","1","0","227822","0","6100","4","12","1","1","6100","4","12","1","6200","8","15","1"};
 assert(packets.Packet(packet,1,false)&&packets.Find(1)->box.enabled&&packets.Find(1)->box.caller->line==8);++groups;
 packet[1]="2";packet[20]="2";Rejected([&]{packets.Packet(packet,2,false);});assert(packets.Records().size()==1);packet[20]="1";packet[12]="2";Rejected([&]{packets.Packet(packet,2,false);});assert(packets.Records().size()==1);++groups;
 RunnerReport report;report.workers.push_back({1,getpid(),false,0,{},{}});report.attempts.push_back(ReportAttempt{1,1,0,1,getpid(),"\"actual boxed attempt\"",error,"failed",false,20});report.steps=steps.Records();report_detail::Validate(report);auto bad=report;bad.steps[0].box.caller->explicit_location=true;Rejected([&]{report_detail::Validate(bad);});++groups;
 if(argc>1){const auto dir=std::filesystem::path(argv[1]);WriteRunnerJson(report,dir/"boxed.json");WriteRunnerJUnit(report,dir/"boxed.xml");WriteRunnerHTML(report,dir/"boxed.html");}
 std::cout<<"PASS "<<groups<<" native boxed ownership/location/status/report controls\n";
}
