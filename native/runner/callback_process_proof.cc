// SPDX-License-Identifier: MIT
#include "fixture_graph.h"
#include "retry_plan.h"
#include "worker_process.h"
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace suitecut::runner;
static std::string Json(const std::string& text){std::ostringstream out;out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else out<<c;}out<<'"';return out.str();}
static std::string Unhex(const std::string& h){if(h=="-")return{};assert(h.size()%2==0&&h.size()<=32768);std::string out;for(size_t i=0;i<h.size();i+=2)out.push_back(static_cast<char>(std::stoul(h.substr(i,2),nullptr,16)));return out;}
int main(int argc,char** argv){
 assert(argc==5);const bool held=std::string(argv[4])=="held";std::ofstream journal(argv[3]);assert(journal);size_t journal_count=0;
 auto log=[&](std::string event,pid_t pid,uint64_t test,uint64_t generation,uint64_t token,const std::string& detail){journal<<"{\"event\":"<<Json(event)<<",\"pid\":"<<pid<<",\"test\":"<<test<<",\"generation\":"<<generation<<",\"token\":"<<token<<",\"monotonicMs\":"<<std::fixed<<std::setprecision(3)<<RealMilliseconds()<<",\"detail\":"<<Json(detail)<<"}\n";journal.flush();++journal_count;};
 RetryPlan plan(held?std::vector<TestCase>{{1,"held setup callback",{"never"},0,false}}:std::vector<TestCase>{{1,"worker fixture reuse",{"page"},0,false},{2,"retry fresh actual worker",{"page"},1,false}});
 std::vector<pid_t> pids;bool successful=true;unsigned workers=0;const auto overall=RealMilliseconds()+15000;
 while(!plan.Done()){
  assert(RealMilliseconds()<overall);WorkerProcess child(argv[1],{argv[2]});++workers;pids.push_back(child.Pid());log("worker-start",child.Pid(),0,0,0,"");
  std::optional<Attempt> current;bool attempt_finished=false,stopping=false,close_sent=false,body_live=false;
  FixtureOwners* live=nullptr;
  FixtureOwners manager(FixtureGraph(held?std::vector<Fixture>{{"never",Scope::Test,{}}}:std::vector<Fixture>{{"worker",Scope::Worker,{}},{"page",Scope::Test,{"worker"}}}),{
   [&](const auto& event){const auto kind=event.kind==FixtureOwners::EventKind::Setup?"SETUP":event.kind==FixtureOwners::EventKind::ResumeTeardown?"TEARDOWN":"CANCEL";log(kind,child.Pid(),event.test,event.generation,event.token,event.fixture);assert(child.Send(std::string(kind)+" "+std::to_string(event.token)+" "+std::to_string(event.test)+" "+std::to_string(event.generation)+" "+event.fixture));},
   [&](uint64_t test,uint64_t generation){assert(current&&current->test.id==test&&current->generation==generation);body_live=true;log("body-start",child.Pid(),test,generation,0,"");assert(child.Send("BODY "+std::to_string(test)+" "+std::to_string(generation)+" 0 "+std::to_string(current->retry)));},
   [&](uint64_t test,uint64_t generation,bool ok,const std::string& error){assert(current&&current->test.id==test&&current->generation==generation);const auto outcome=ok?Outcome::Passed:error.find("deadline")!=std::string::npos?Outcome::TimedOut:Outcome::Failed;assert(plan.Finish(static_cast<uint64_t>(child.Pid()),generation,outcome,error));log("attempt-end",child.Pid(),test,generation,0,ok?"passed":error);attempt_finished=true;},
   [&](const auto& error){log("physical-watchdog-close",child.Pid(),current?current->test.id:0,current?current->generation:0,0,error);child.Kill();}});
  live=&manager;(void)live;
  auto begin=[&]{current=plan.Next(static_cast<uint64_t>(child.Pid()));if(!current)return false;attempt_finished=false;log("attempt-start",child.Pid(),current->test.id,current->generation,0,std::to_string(current->retry));assert(manager.Begin(current->test.id,current->generation,current->test.fixtures,RealMilliseconds()+(held?40:2000),RealMilliseconds()));return true;};
  assert(begin());
  while(!child.Exited()){
   assert(RealMilliseconds()<overall);for(const auto& line:child.Poll(5)){
    std::istringstream input(line);std::string type;input>>type;
    if(type=="READY"){uint64_t token,test,generation;assert(input>>token>>test>>generation);log("callback-ready",child.Pid(),test,generation,token,"");manager.Ready(token,test,generation,RealMilliseconds());}
    else if(type=="SETTLED"){uint64_t token,test,generation;int ok;std::string error;assert(input>>token>>test>>generation>>ok>>error);log("callback-settled",child.Pid(),test,generation,token,Unhex(error));manager.Settled(token,test,generation,ok==1,Unhex(error),RealMilliseconds());}
    else if(type=="BODY_RESULT"){uint64_t test,generation;int ok,pid,counter;std::string error;assert(input>>test>>generation>>ok>>error>>pid>>counter);assert(current&&test==current->test.id&&generation==current->generation&&pid==child.Pid()&&body_live);body_live=false;log("body-result",child.Pid(),test,generation,0,std::to_string(counter));manager.Finish(ok==1,Unhex(error),RealMilliseconds());}
    else throw std::runtime_error("Unknown actual host reply");
   }
   manager.Tick(RealMilliseconds());
   if(attempt_finished&&!stopping){if(plan.NeedsWorkerRetirement()||plan.Done()){stopping=true;manager.StopWorker(RealMilliseconds());}else assert(begin());}
   if(stopping&&!close_sent&&manager.State().physical==0&&!body_live){close_sent=true;assert(child.Send("CLOSE"));}
  }
  const auto exited=*child.Exited();log("worker-confirmed-exit",child.Pid(),current?current->test.id:0,current?current->generation:0,0,std::to_string(exited.code));manager.WorkerClosed();body_live=false;assert(manager.State().physical==0);
  if(plan.NeedsWorkerRetirement())assert(plan.WorkerExited(static_cast<uint64_t>(child.Pid())));
  if(!held&&exited.code!=0)successful=false;
  assert(child.Stderr().empty());
 }
 if(held){assert(workers==1&&plan.Results().size()==1&&plan.Results()[0].outcome==Outcome::TimedOut);}
 else{assert(workers==2&&pids[0]!=pids[1]&&plan.Results().size()==3);assert(plan.Results()[0].expected&&!plan.Results()[1].expected&&plan.Results()[2].expected);assert(plan.Results()[1].error.find("genuine first-attempt user exception")!=std::string::npos);}
 assert(successful);std::cout<<"{\"nativeRunnerActualHostProof\":true,\"held\":"<<(held?"true":"false")<<",\"workers\":"<<workers<<",\"attempts\":"<<plan.Results().size()<<",\"journalRows\":"<<journal_count<<",\"physicalCallbacksAfterConfirmedExit\":0}\n";
}
