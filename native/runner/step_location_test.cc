// SPDX-License-Identifier: MIT
#include "runner_steps.h"
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char**argv){
 const StepOwner owner{1,1,0,1,1,1,getpid()};unsigned groups=0;
 const auto custom=PrepareStepLocation("640075006d006d0079002d00660069006c0065002e0074007300",123,45,2);assert(custom&&custom->explicit_location&&custom->file_json=="\"\\u0064\\u0075\\u006d\\u006d\\u0079\\u002d\\u0066\\u0069\\u006c\\u0065\\u002e\\u0074\\u0073\"");++groups;
 for(const auto& f:std::vector<std::function<void()>>{[]{PrepareStepLocation("-",1,1,0);},[]{PrepareStepLocation("6100",0,1,2);},[]{PrepareStepLocation("6100",1,2147483648ULL,2);},[]{PrepareStepLocation("-",1,1,2);},[]{PrepareStepLocation("6100",1,1,3);},[]{PrepareStepLocation(std::string(4097*4,'0'),1,1,2);}}){Reject(f);}
 ++groups;
 TestSteps native;assert(native.Open(owner,1000,0));const auto id=native.Begin(owner,0,"\"custom\"",10,1,custom);assert(id&&native.Find(*id)->location==custom&&native.Find(*id)->deadline==11);assert(!native.Begin(owner,0,"\"invalid\"",0,2,StepLocation{"not scalar",1,1,true}));assert(native.Inspect().records==1);++groups;
 const auto callsite=PrepareStepLocation("610000d80000",7,9,1);const auto child=native.Begin(owner,*id,"\"child\"",0,2,callsite);assert(child&&native.Find(*child)->parent==*id&&native.Find(*child)->location==callsite&&native.Find(*child)->deadline==11);++groups;
 assert(native.Settle(owner,*child,true,"\"\"",3)&&native.Settle(owner,*id,true,"\"\"",4));const auto skipped=native.DeclareSkipped(owner,0,"\"omitted\"",0,7,5,custom);assert(skipped&&native.Find(*skipped)->receipt==StepReceipt::NotInvoked&&native.Find(*skipped)->location==custom);++groups;
 const auto late=native.Begin(owner,0,"\"late\"",1,6,custom);assert(late);native.Tick(7);assert(native.Find(*late)->status==StepStatus::TimedOut&&native.Find(*late)->location==custom&&native.Inspect().physical==1);native.Settle(owner,*late,true,"\"\"",8);native.Close(owner,9);native.Retire(owner);++groups;
 std::vector<std::string> sent;RunnerSteps packet({[&](const auto& p){sent.push_back(p);},[](const auto&){assert(false);}});packet.BeginCallback(owner,1000,0);assert(packet.Packet({"STEP_BEGIN","1","0","1","1","0","2261637475616c22","0","640075006d006d0079002d00660069006c0065002e0074007300","123","45","2"},1,false));assert(packet.Find(1)->location==custom&&sent.back()=="STEP_ADMITTED 1 0 1 1 1 1 -");++groups;
 Reject([&]{packet.Packet({"STEP_BEGIN","2","0","1","1","0","227822","0","6100","0","1","2"},2,false);});assert(packet.Records().size()==1&&packet.Physical()==1);assert(packet.Packet({"STEP_BEGIN","2","0","1","1","0","226c656761637922","0"},2,false));assert(packet.Records().size()==2&&!packet.Find(2)->location);++groups;
 packet.Packet({"STEP_SETTLED","1","0","1","1","1","-"},3,false);packet.WorkerExited(1,getpid(),4);
 RunnerReport report;report.workers.push_back({1,getpid(),false,0,{},{}});report.attempts.push_back(ReportAttempt{1,1,0,1,getpid(),"\"actual location attempt\"","\"\"","passed",true,10});report.steps=native.Records();report_detail::Validate(report);
 auto invalid=report;invalid.steps[0].location->line=0;Reject([&]{report_detail::Validate(invalid);});++groups;
 if(argc>1){const auto dir=std::filesystem::path(argv[1]);WriteRunnerJson(report,dir/"location.json");WriteRunnerJUnit(report,dir/"location.xml");WriteRunnerHTML(report,dir/"location.html");}
 {TestSteps::Budget budget;TestSteps first(&budget),second(&budget);auto other=owner;other.worker_index=2;assert(first.Open(owner,1000,0)&&second.Open(other,1000,0));const auto a=first.Begin(owner,0,"\"first\"",0,1,custom),b=second.Begin(other,0,"\"second\"",0,1,callsite),c=first.Begin(owner,*a,"\"own child\"",0,2,custom);assert(a==1&&b==2&&c==3&&first.Find(*c)->parent==*a&&first.Find(*c)->location==custom&&!first.Find(*b)&&!second.Find(*c));assert(budget.reserved==first.Inspect().metadata_reserved+second.Inspect().metadata_reserved&&budget.physical==3);assert(first.Settle(owner,*c,true,"\"\"",3)&&first.Settle(owner,*a,true,"\"\"",4)&&second.Settle(other,*b,true,"\"\"",4));assert(!budget.physical);++groups;}
 std::cout<<"PASS"<<groups<<" native step location admission/bounds/ancestry/deadline/report groups\n";
}
