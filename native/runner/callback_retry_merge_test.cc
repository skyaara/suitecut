// SPDX-License-Identifier: MIT
#include "report_merge.h"
#include <cassert>
#include <fstream>
#include <iostream>
namespace sr=suitecut::runner;
namespace suitecut::runner {void WriteRepeatReportFixture(const RunnerReport&,const std::filesystem::path&);}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Replace(std::string& text,std::string_view old,std::string_view value){const auto at=text.find(old);assert(at!=text.npos);text.replace(at,old.size(),value);}
template<class F>void Reject(F callback){bool rejected=false;try{callback();}catch(const std::exception&){rejected=true;}assert(rejected);}
std::string Summary(std::string_view status="passed",std::string_view receipt="settled"){
  return "\"callbackAssertions\":[{\"id\":\"10\",\"requestId\":\"1\",\"testId\":\"1\",\"generation\":\"1\",\"callbackToken\":\"0\",\"scopeId\":\"1\",\"scopeGeneration\":\"1\",\"parentStepId\":\"0\",\"workerIndex\":1,\"pid\":"+std::to_string(getpid())+",\"callbackKind\":\"body\",\"startedMonotonicMs\":10,\"ownDeadlineMonotonicMs\":100,\"deadlineMonotonicMs\":100,\"attempts\":2,\"lastInvocation\":\"2\",\"status\":\""+std::string(status)+"\",\"error\":\"\",\"receiptMonotonicMs\":40,\"receipt\":\""+std::string(receipt)+"\"}]";
}
std::string WithEvidence(std::string feature){Replace(feature,"\"id\":\"10\",\"parentId\":\"0\"","\"id\":\"10\",\"callbackAssertion\":{\"id\":\"10\",\"invocation\":\"1\"},\"parentId\":\"0\"");Replace(feature,"\"id\":\"12\",\"parentId\":\"0\"","\"id\":\"12\",\"callbackAssertion\":{\"id\":\"10\",\"invocation\":\"2\"},\"parentId\":\"0\"");Replace(feature,"\"globalErrors\":[]",Summary()+",\"globalErrors\":[]");return feature;}
int main(int argc,char** argv){
  assert(argc==2);const std::filesystem::path folder=argv[1];std::filesystem::create_directories(folder);
  sr::RunnerReport report;report.workers={{1,getpid(),false,0,"controlled report fixture",""}};report.attempts={{1,1,0,1,getpid(),"\"actual test\"","\"\"","passed",true,40}};
  const sr::StepOwner owner{1,1,0,1,1,1,getpid(),"body"};
  sr::StepRecord outer;outer.id=1;outer.owner=owner;outer.title_json="\"outer author step\"";outer.started=5;outer.deadline=100;outer.own_deadline=std::numeric_limits<double>::max();outer.ended=44;outer.received=45;outer.status=sr::StepStatus::Passed;outer.receipt=sr::StepReceipt::Settled;outer.receipt_ok=true;outer.depth=1;outer.callback_received=44;outer.callback_ok=true;report.steps.push_back(outer);
  for(const auto id:{10,12}){sr::StepRecord step;step.id=id;step.owner=owner;step.title_json="\"actual iteration\"";step.started=id==10?10:30;step.deadline=100;step.own_deadline=std::numeric_limits<double>::max();step.ended=id==10?11:39;step.received=id==10?20:40;step.status=id==10?sr::StepStatus::Failed:sr::StepStatus::Passed;step.receipt=sr::StepReceipt::Settled;step.receipt_ok=id!=10;step.error_json=id==10?"\"ordinary author failure\"":"\"\"";step.receipt_error_json=step.error_json;step.depth=1;step.callback_received=*step.ended;step.callback_ok=id!=10;report.steps.push_back(step);}
  sr::WriteRepeatReportFixture(report,folder/"base.json");const auto base=Read(folder/"base.json");
  auto feature=WithEvidence(base);
  unsigned checks=0;
  const auto Load=[&](const std::string& bytes){const auto path=folder/"input.json";{std::ofstream out(path);out<<bytes;}return sr::MergedRunnerReport::Read({{"source",path}});};
  auto plain=Load(base);plain.WriteJSON(folder/"plain.json");plain.WriteJUnit(folder/"plain.xml");assert(Read(folder/"plain.json").find("callbackAssertions")==std::string::npos&&Read(folder/"plain.xml").find("native.original.callbackAssertions")==std::string::npos);++checks;
  auto valid=Load(feature);assert(!valid.RunFailed());valid.WriteJSON(folder/"feature.json");valid.WriteJUnit(folder/"feature.xml");assert(Read(folder/"feature.json").find(feature)!=std::string::npos&&Read(folder/"feature.xml").find("native.original.callbackAssertions")!=std::string::npos);++checks;
  // Backoff host death can follow an already settled ordinary failed iteration.
  auto backoff=feature;Replace(backoff,Summary(),Summary("infrastructure","workerExited"));const auto final_at=backoff.find("\"id\":\"12\",\"callbackAssertion\"");assert(final_at!=backoff.npos);auto final_bytes=backoff.substr(final_at);Replace(final_bytes,"\"status\":\"passed\",\"callbackKind\"","\"status\":\"failed\",\"callbackKind\"");Replace(final_bytes,"\"receiptOK\":true","\"receiptOK\":false");Replace(final_bytes,"\"callbackReceiptOK\":true","\"callbackReceiptOK\":false");backoff.replace(final_at,backoff.npos,final_bytes);(void)Load(backoff);++checks;
  auto no_own=feature;Replace(no_own,"\"ownDeadlineMonotonicMs\":100,","");(void)Load(no_own);++checks;
  auto parent=feature;Replace(parent,"\"parentStepId\":\"0\"","\"parentStepId\":\"1\"");for(const auto nonce:{"1","2"}){const auto field=std::string("\"invocation\":\"")+nonce+"\"},\"parentId\":\"0\"";auto replacement=field;Replace(replacement,"\"parentId\":\"0\"","\"parentId\":\"1\"");Replace(parent,field,replacement);}(void)Load(parent);++checks;
  auto hook=feature;while(hook.find("\"callbackToken\":\"0\"")!=hook.npos)Replace(hook,"\"callbackToken\":\"0\"","\"callbackToken\":\"9\"");while(hook.find("\"callbackKind\":\"body\"")!=hook.npos)Replace(hook,"\"callbackKind\":\"body\"","\"callbackKind\":\"beforeEach\"");(void)Load(hook);++checks;
  auto body_call=feature;while(body_call.find("\"callbackToken\":\"0\"")!=body_call.npos)Replace(body_call,"\"callbackToken\":\"0\"","\"callbackToken\":\"9\"");(void)Load(body_call);++checks;
  auto shortened=backoff;Replace(shortened,Summary("infrastructure","workerExited"),Summary("timedOut","settled"));Replace(shortened,"\"deadlineMonotonicMs\":100,\"attempts\"","\"deadlineMonotonicMs\":5,\"attempts\"");Replace(shortened,"\"deadlineMonotonicMs\":100,\"durationMs\":9","\"deadlineMonotonicMs\":5,\"durationMs\":9");(void)Load(shortened);++checks;
  auto soft_report=report;auto& actual=soft_report.attempts[0];actual.status="failed";actual.expected=false;actual.error_json="\"actual soft mismatch\"";actual.soft_error_id=7;
  auto& soft_step=soft_report.steps.back();soft_step.status=sr::StepStatus::Failed;soft_step.error_json=actual.error_json;soft_step.soft_error_id=7;soft_step.soft_error_json=actual.error_json;
  sr::SoftAssertionRecord soft_record;soft_record.id=7;soft_record.request=1;soft_record.step=12;soft_record.owner=owner;soft_record.admitted=32;soft_record.received=35;soft_record.raw=sr::SoftRawOutcome::RecognizedMismatch;soft_record.receipt=sr::SoftReceipt::Mismatch;soft_record.error_json=actual.error_json;soft_report.soft_assertions.push_back(soft_record);
  sr::WriteRepeatReportFixture(soft_report,folder/"soft-base.json");const auto soft=WithEvidence(Read(folder/"soft-base.json"));assert(Load(soft).RunFailed());++checks;
  auto accepted_deadline=soft_report;accepted_deadline.soft_assertions[0].raw=sr::SoftRawOutcome::RecognizedDeadline;accepted_deadline.soft_assertions[0].receipt=sr::SoftReceipt::AcceptedDeadline;sr::WriteRepeatReportFixture(accepted_deadline,folder/"soft-deadline-base.json");assert(Load(WithEvidence(Read(folder/"soft-deadline-base.json"))).RunFailed());++checks;
  auto generic_failed=feature;const auto failed_at=generic_failed.find("\"id\":\"12\",\"callbackAssertion\"");auto failed_tail=generic_failed.substr(failed_at);Replace(failed_tail,"\"status\":\"passed\",\"callbackKind\"","\"status\":\"failed\",\"callbackKind\"");generic_failed.replace(failed_at,generic_failed.npos,failed_tail);Reject([&]{(void)Load(generic_failed);});++checks;
  for(const auto& edit:std::vector<std::pair<std::string,std::string>>{
    {"\"callbackReceiptOK\":true,\"softErrorId\":\"7\"","\"callbackReceiptOK\":true,\"softErrorId\":\"99\""},
    {"\"receipt\":\"mismatch\"","\"receipt\":\"otherFailure\""},
    {"\"acceptedMismatch\":true","\"acceptedMismatch\":false"},
    {"\"rawOutcome\":\"recognizedMismatch\"","\"rawOutcome\":\"otherFailure\""},
    {"\"stepId\":\"12\"","\"stepId\":\"10\""},
    {"\"admittedMonotonicMs\":32","\"admittedMonotonicMs\":29"},
    {"\"receiptMonotonicMs\":35","\"receiptMonotonicMs\":41"}}){auto bad=soft;Replace(bad,edit.first,edit.second);Reject([&]{(void)Load(bad);});++checks;}
  for(const auto& edit:std::vector<std::pair<std::string,std::string>>{
    {"\"attempts\":2,\"lastInvocation\":\"2\"","\"attempts\":3,\"lastInvocation\":\"3\""},
    {"\"attempts\":2,\"lastInvocation\":\"2\"","\"attempts\":2,\"lastInvocation\":\"1\""},
    {"\"invocation\":\"2\"","\"invocation\":\"3\""},
    {"\"callbackAssertion\":{\"id\":\"10\",\"invocation\":\"1\"}","\"callbackAssertion\":{\"id\":\"99\",\"invocation\":\"1\"}"},
    {"\"parentStepId\":\"0\"","\"parentStepId\":\"99\""},
    {"\"ownDeadlineMonotonicMs\":100","\"ownDeadlineMonotonicMs\":50"},
    {"\"deadlineMonotonicMs\":100,\"attempts\"","\"deadlineMonotonicMs\":40,\"attempts\""},
    {"\"lastInvocation\":\"2\",\"status\":\"passed\"","\"lastInvocation\":\"2\",\"status\":\"unknown\""},
    {"\"scopeGeneration\":\"1\",\"parentStepId\"","\"scopeGeneration\":\"2\",\"parentStepId\""},
    {"\"requestId\":\"1\"","\"requestId\":\"0\""},
    {"\"attempts\":2,\"lastInvocation\"","\"attempts\":2.5,\"lastInvocation\""},
    {"\"callbackReceiptMonotonicMs\":11","\"callbackReceiptMonotonicMs\":null"},
    {"\"callbackReceiptMonotonicMs\":11","\"callbackReceiptMonotonicMs\":21"},
    {"\"physicalDurationMs\":10","\"physicalDurationMs\":11"},
    {"\"durationMs\":1,\"receiptMonotonicMs\":20","\"durationMs\":2,\"receiptMonotonicMs\":20"}}){auto bad=feature;Replace(bad,edit.first,edit.second);Reject([&]{(void)Load(bad);});++checks;}
  auto orphan=feature;Replace(orphan,Summary()+",","");Reject([&]{(void)Load(orphan);});++checks;
  auto missing=base;Replace(missing,"\"globalErrors\":[]",Summary()+",\"globalErrors\":[]");Reject([&]{(void)Load(missing);});++checks;
  std::cout<<"callback assertion merge controls "<<checks<<" passed\n";
}
