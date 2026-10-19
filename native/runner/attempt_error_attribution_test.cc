// SPDX-License-Identifier: MIT
#include "report_merge.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <iterator>
#include <unistd.h>
using namespace suitecut::runner;
template<class F>static void Reject(F callback){bool rejected=false;try{callback();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char** argv){assert(argc==2);const AttemptErrorOwner owner{1,2,3,getpid()};unsigned n=0;
 {PhaseErrorAttribution c;c.Begin(4,5,owner);const auto source=c.Consume(4,5,owner);assert(source&&source->attempt==owner&&source->scope==4&&source->generation==5);++n;}
 {PhaseErrorAttribution c;c.Begin(4,5,std::nullopt);assert(!c.Consume(4,5,owner));++n;}
 {PhaseErrorAttribution c;c.Begin(4,5,owner);auto wrong=owner;++wrong.generation;Reject([&]{c.Consume(4,5,wrong);});assert(c.Consume(4,5,owner));++n;}
 {PhaseErrorAttribution c;c.Begin(4,5,owner);Reject([&]{c.Consume(4,6,owner);});assert(c.Consume(4,5,owner));++n;}
 {PhaseErrorAttribution c;c.Begin(4,5,owner);Reject([&]{c.Consume(8,5,owner);});assert(c.Consume(4,5,owner));++n;}
 {PhaseErrorAttribution c;c.Begin(4,5,owner);Reject([&]{c.Consume(4,5,std::nullopt);});assert(c.Consume(4,5,owner));++n;}
 {PhaseErrorAttribution c;c.Begin(4,5,owner);assert(c.Consume(4,5,owner));Reject([&]{c.Consume(4,5,owner);});++n;}
 {PhaseErrorAttribution c;c.Begin(4,5,owner);Reject([&]{c.Begin(6,7,owner);});c.CancelBeforeAdmission();c.Begin(6,7,owner);assert(c.Consume(6,7,owner));++n;}
 RunnerReport report;report.workers={{owner.worker,owner.pid,false,0,"",""}};report.attempts={{owner.test,owner.generation,0,owner.worker,owner.pid,"\"fixture\"","\"actual raw Error\\n  genuine stack\"","failed",false,1}};
 report.attempts[0].secondary_errors={{{owner,4,5},"\"distinct owner-bound wrapper <& \\ud800\"",0}};
 const auto summary=report_detail::Validate(report);assert(summary.unexpected==1&&report.global_errors_json.empty());++n;
 {auto bad=report;++bad.attempts[0].secondary_errors[0].source.attempt.generation;Reject([&]{report_detail::Validate(bad);});++n;}
 {auto bad=report;++bad.attempts[0].secondary_errors[0].source.attempt.pid;Reject([&]{report_detail::Validate(bad);});++n;}
 {auto bad=report;bad.attempts[0].secondary_errors[0].error_json="{}";Reject([&]{report_detail::Validate(bad);});++n;}
 {auto bad=report;bad.attempts[0].secondary_errors.resize(257,report.attempts[0].secondary_errors[0]);Reject([&]{report_detail::Validate(bad);});++n;}
 const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);WriteRunnerJson(report,root/"actual.json");WriteRunnerJUnit(report,root/"actual.xml");WriteRunnerHTML(report,root/"actual.html");++n;
 report.global_errors_json={"\"genuine unassociated worker error\""};assert(report_detail::Validate(report).errors==1);WriteRunnerJson(report,root/"with-global.json");++n;
 auto merged=MergedRunnerReport::Read({{"actual",root/"actual.json"}});merged.WriteJSON(root/"merged.json");merged.WriteJUnit(root/"merged.xml");merged.WriteHTML(root/"merged.html");++n;

 {std::ifstream input(root/"actual.json");std::string bytes{std::istreambuf_iterator<char>(input),{}};
  const auto scopes=bytes.find("\"scopeGeneration\":\"5\",\"error\"");assert(scopes!=std::string::npos);bytes.insert(scopes+std::string("\"scopeGeneration\":\"5\",").size(),"\"originatingStepId\":\"9\",");
  const auto steps=bytes.find("\"steps\":[]");assert(steps!=std::string::npos);
  const auto step=std::string("\"steps\":[{\"id\":\"9\",\"parentId\":\"0\",\"testId\":\"1\",\"generation\":\"2\",\"callbackToken\":\"6\",\"scopeId\":\"4\",\"scopeGeneration\":\"5\",\"workerIndex\":3,\"pid\":")+std::to_string(owner.pid)+",\"callbackKind\":\"body\",\"title\":\"step\",\"error\":\"hard\",\"receiptError\":\"\",\"status\":\"failed\",\"startedMonotonicMs\":0,\"endedMonotonicMs\":1,\"receiptMonotonicMs\":2,\"receipt\":\"settled\",\"receiptOK\":false,\"callbackReceiptMonotonicMs\":null,\"callbackReceiptOK\":false}]";
  bytes.replace(steps,std::string("\"steps\":[]").size(),step);const auto malformed=root/"malformed-null.json";{std::ofstream out(malformed);out<<bytes;}
  Reject([&]{(void)MergedRunnerReport::Read({{"malformed",malformed}});});++n;
  const auto receipt=bytes.find("\"callbackReceiptMonotonicMs\":null");assert(receipt!=std::string::npos);bytes.replace(receipt+std::string("\"callbackReceiptMonotonicMs\":").size(),4,"1.5");const auto valid=root/"valid-step.json";{std::ofstream out(valid);out<<bytes;}
  (void)MergedRunnerReport::Read({{"valid",valid}});++n;
 }
 assert(n==18);std::cout<<"PASS "<<n<<" native typed phase error attribution/report controls\n";
}
