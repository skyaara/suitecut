// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace suitecut::runner;
namespace {
template<class F>void Reject(F&& operation){
  bool rejected=false;
  try{operation();}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
}
RunnerReport Recovered(bool nested=false,std::optional<double> cap=std::nullopt,uint64_t sequence=0,bool source=false,uint64_t token=0,bool with_soft=false){
  const StepOwner owner{1,1,token,1,1,1,getpid(),"body"};
  TestSteps::Budget budget;budget.sequence=sequence;TestSteps steps(&budget);
  assert(steps.Open(owner,1000,0));
  const auto parent=nested?steps.Begin(owner,0,"\"outer\"",0,1).value():0;
  const auto first=steps.BeginCallbackIteration(owner,parent,0,1,cap,2).value();
  assert(steps.Receive(owner,first,false,"\"authored first failure\"",3));
  assert(steps.Complete(owner,first,4));
  const auto last=steps.BeginCallbackIteration(owner,parent,first,2,cap,104).value();
  SoftAssertions soft([&](uint64_t id){return steps.Find(id);});
  uint64_t soft_id=0;
  if(with_soft){soft.Open(owner,1000,104);soft_id=soft.Begin(owner,1,last,std::nullopt,104.5).id;}
  assert(steps.Receive(owner,last,true,"\"\"",105));
  if(with_soft){
    assert(soft.Settled(owner,soft_id,SoftRawOutcome::RecognizedMismatch,"\"accepted soft failure\"",105.5)==SoftAssertions::Verdict::Mismatch);
    assert(steps.NoteSoftFailure(owner,last,soft_id,"\"accepted soft failure\"",105.5));
  }
  if(source){
    const SourceCallbackIterationReceipt receipt{RetainedSourceWorkWire::Issue(1,1,1,1,1100),
      {1,1,{10,1,1},{20,1,1}},2,true,true,true,true,true,105.1,105.2,105.4,105.3,105.5};
    assert(steps.SetCallbackSourceFence(owner,last,receipt));
    assert(!steps.SetCallbackSourceFence(owner,last,receipt));
  }
  assert(steps.Complete(owner,last,106));
  if(parent)assert(steps.Settle(owner,parent,true,"\"\"",110));
  assert(steps.Close(owner,111)&&steps.Retire(owner));
  RunnerReport report;
  report.workers.push_back({1,getpid(),false,0,{},{}});
  report.attempts.push_back({1,1,0,1,getpid(),"\"recovered callback\"","\"\"","passed",true,112});
  report.steps=steps.Records();
  if(with_soft){
    report.soft_assertions=soft.Records();report.attempts.front().soft_error_id=soft_id;
    report.attempts.front().status="failed";report.attempts.front().expected=false;
    report.attempts.front().error_json="\"accepted soft failure\"";
  }
  report.callback_assertions.push_back({first,1,parent,2,2,owner,2,cap.value_or(1000),106,cap,
    CallbackAssertionStatus::Passed,CallbackAssertionReceipt::Settled,"\"\""});
  if(source){
    // Controlled report DTOs only: this does not launch or attest a Source.
    ReportRetainedSource row;
    row.ticket={1,{{1,1,getpid(),10},{1,1,1,1,1}}};row.executable={1,1};
    row.startup=RetainedSourceLeases::Process{getpid()+1,getpid(),11,{1,1}};
    row.pidfd_exit=true;row.channel_drained=true;row.retired_batch=1;
    report.retained_sources.push_back(row);
  }
  return report;
}
std::string Read(const std::filesystem::path& path){
  std::ifstream stream(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
}
int main(int argc,char** argv){
  unsigned controls=0;
  auto recovered=Recovered();
  const auto summary=report_detail::Validate(recovered);
  assert(summary.expected==1&&summary.flaky==0&&summary.failures==0&&recovered.attempts.size()==1);
  assert(recovered.steps.front().status==StepStatus::Failed&&recovered.steps.back().status==StepStatus::Passed);
  ++controls;
  report_detail::Validate(Recovered(true,500));++controls;
  auto wrong=recovered;wrong.callback_assertions.clear();Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=recovered;wrong.callback_assertions.front().owner.scope_generation=2;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=recovered;wrong.steps.pop_back();Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=recovered;wrong.steps.back().callback_iteration->invocation=1;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=recovered;wrong.callback_assertions.front().attempts=3;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=recovered;wrong.steps.back().received.reset();Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=Recovered(true);wrong.steps.front().received=105;wrong.steps.front().ended=105;
  wrong.steps.front().callback_received=105;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=Recovered(false,500);wrong.steps.back().callback_deadline=600;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  // A real host exit while backoff is pending does not mutate settled history.
  auto exited=recovered;exited.steps.pop_back();auto& final=exited.callback_assertions.front();
  final.attempts=final.last_invocation=1;final.received=20;
  final.status=CallbackAssertionStatus::Infrastructure;final.receipt=CallbackAssertionReceipt::WorkerExited;
  final.error_json="\"actual host exit\"";
  exited.workers.front().signaled=true;exited.workers.front().exit_code=9;
  exited.attempts.front().status="failed";exited.attempts.front().expected=false;
  exited.attempts.front().error_json=final.error_json;
  report_detail::Validate(exited);
  assert(exited.steps.front().receipt==StepReceipt::Settled);++controls;
  wrong=exited;wrong.callback_assertions.front().status=CallbackAssertionStatus::Passed;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  const auto large=Recovered(false,std::nullopt,9007199254740992ULL);
  report_detail::Validate(large);assert(large.callback_assertions.front().id==9007199254740993ULL);++controls;
  report_detail::Validate(Recovered(false,std::nullopt,0,false,7));++controls;
  const auto source=Recovered(false,std::nullopt,0,true);
  report_detail::Validate(source);++controls;
  wrong=source;wrong.steps.back().source_fence->host_resumed_at=105.3;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=source;wrong.steps.back().source_fence->observed_at=104;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=source;wrong.steps.back().source_fence->resume_generation=1;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=source;wrong.retained_sources.front().ticket.owner.host.lease=2;
  Reject([&]{report_detail::Validate(wrong);});++controls;
  auto amended=exited;
  amended.workers.front().signaled=false;amended.workers.front().exit_code=0;
  amended.callback_assertions.front().status=CallbackAssertionStatus::TimedOut;
  amended.callback_assertions.front().receipt=CallbackAssertionReceipt::Settled;
  amended.callback_assertions.front().deadline=1;
  amended.attempts.front().status="timedOut";
  report_detail::Validate(amended);++controls;
  const auto soft=Recovered(false,std::nullopt,0,false,0,true);
  const auto soft_summary=report_detail::Validate(soft);
  assert(soft_summary.unexpected==1&&soft_summary.expected==0&&soft_summary.flaky==0);
  assert(soft.callback_assertions.front().status==CallbackAssertionStatus::Passed);
  assert(soft.steps.back().status==StepStatus::Failed&&soft.steps.back().soft_error_id);++controls;
  wrong=soft;wrong.soft_assertions.clear();Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=soft;wrong.steps.back().error_json="\"unrelated actual error\"";
  Reject([&]{report_detail::Validate(wrong);});++controls;
  wrong=soft;wrong.attempts.front().status="passed";wrong.attempts.front().expected=true;
  wrong.attempts.front().soft_error_id=0;wrong.attempts.front().error_json="\"\"";
  Reject([&]{report_detail::Validate(wrong);});++controls;
  if(argc>1){
    const auto directory=std::filesystem::path(argv[1]);
    WriteRunnerJson(recovered,directory/"recovered.json");
    WriteRunnerJUnit(recovered,directory/"recovered.xml");
    WriteRunnerHTML(recovered,directory/"recovered.html");
    WriteRunnerJson(large,directory/"large.json");
    WriteRunnerJson(exited,directory/"exited.json");
    WriteRunnerJson(source,directory/"source.json");
    auto escaped=exited;escaped.callback_assertions.front().error_json="\"\\u0000\\ud800<&\"";
    WriteRunnerJson(escaped,directory/"escaped.json");WriteRunnerJUnit(escaped,directory/"escaped.xml");
    const auto json=Read(directory/"recovered.json"),xml=Read(directory/"recovered.xml");
    assert(json.find("\"callbackAssertions\":[")!=std::string::npos);
    assert(json.find("\"callbackAssertion\":{\"id\":\"1\",\"invocation\":\"1\"}")!=std::string::npos);
    assert(xml.find("native.callbackAssertion1.attempts\" value=\"2\"")!=std::string::npos);
    assert(xml.find("<failure")==std::string::npos&&xml.find("<error")==std::string::npos);
    assert(Read(directory/"large.json").find("\"id\":\"9007199254740993\"")!=std::string::npos);
    assert(Read(directory/"escaped.json").find("\\u0000\\ud800<&")!=std::string::npos);
    assert(Read(directory/"escaped.xml").find("errorXmlSanitizedUnits\" value=\"2\"")!=std::string::npos);
    ++controls;
  }
  std::cout<<"callback assertion reporting controls "<<controls<<" passed\n";
}
