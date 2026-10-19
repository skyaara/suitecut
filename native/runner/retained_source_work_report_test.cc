// SPDX-License-Identifier: MIT
#include "retained_source_report.h"
#include "worker_reuse_report.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace suitecut::runner;
struct Writer {std::string data;void Put(std::string_view s){data+=s;}template<class T>void NumberValue(T n){data+=std::to_string(n);}};
struct Report {struct Worker{unsigned worker_index;int64_t pid;int exit_code;};struct Attempt{uint64_t test_id;unsigned worker_index;int64_t pid;uint64_t generation=0;};struct Assertion{uint64_t id,request,parent,attempts;StepOwner owner;};std::vector<ReportRetainedSource> retained_sources;std::vector<Worker>workers;std::vector<Attempt>attempts;ReportWorkerReuse worker_reuse;std::vector<StepRecord> steps{};std::vector<Assertion> callback_assertions{};};
template<class F>void Rejected(F call){bool caught=false;try{call();}catch(const std::invalid_argument&){caught=true;}assert(caught);}
int main(){
  ReportRetainedSource row{{1,{{1,2,100,10},{3,4,5,6,7}}},{8,9},RetainedSourceLeases::Process{101,100,11,{8,9}},std::nullopt,true,true,1};
  ValidateRetainedSource(row); // Legacy startup/physical row remains supported.
  const auto challenge=RetainedSourceWorkWire::Issue(1,1,2,3,1000);
  const RetainedSourceWorkWire::Cuts cuts{1,1,{2,3,4},{5,6,7}};
  row.accepted_work=ReportSourceAcceptedWork{challenge,cuts,true,true,false,false,0,900,901};ValidateRetainedSource(row);
  Writer writer;PutRetainedSourceReports(writer,std::vector{row});assert(writer.data.find("originalStdinAndDecodedStdout")!=std::string::npos);assert(writer.data.find("\"acknowledged\":true")!=std::string::npos);
  auto bad=row;bad.accepted_work->cuts.reset();Rejected([&]{ValidateRetainedSource(bad);});
  bad=row;bad.accepted_work->challenge.ticket=2;Rejected([&]{ValidateRetainedSource(bad);});
  bad=row;bad.accepted_work->challenge.deadline_text="1000.0";Rejected([&]{ValidateRetainedSource(bad);});
  bad=row;bad.accepted_work->acknowledged_at=1000;Rejected([&]{ValidateRetainedSource(bad);});
  bad=row;bad.accepted_work->acknowledged_at=899;Rejected([&]{ValidateRetainedSource(bad);});
  bad=row;bad.accepted_work->late_acknowledged=true;Rejected([&]{ValidateRetainedSource(bad);});
  bad=row;bad.accepted_work->refusal=1;Rejected([&]{ValidateRetainedSource(bad);});
  Report report{{row},{{2,100,0}},{{6,2,100}},{}};Rejected([&]{ValidateRetainedSourceReports(report);});
  WorkerReusePool::BorrowRecord borrow{};borrow.borrow.host={2,2,100};borrow.borrow.generation=3;report.worker_reuse.borrows.push_back(borrow);assert(ValidateRetainedSourceReports(report)==768);
  report.retained_sources[0].accepted_work->canceled=true;assert(ValidateRetainedSourceReports(report)==768); // Later real cleanup cannot erase a timely actual ACK.
  report.retained_sources[0].ticket.owner.fixture={3,4,5,0,7,RetainedSourceLeases::SetupScope::BeforeAll,99};report.attempts.clear();assert(ValidateRetainedSourceReports(report)==768);
  // A partial callback fence is not a WorkerReuse borrow or a completed
  // SourceFence. The Source fixture may genuinely belong to an all-hook owner.
  auto partial=row;partial.ticket.owner.fixture={3,4,5,0,7,RetainedSourceLeases::SetupScope::BeforeAll,99};
  partial.accepted_work->challenge=RetainedSourceWorkWire::Issue(1,1,1,3,1000);
  const StepOwner callback{44,55,0,66,77,2,100,"body"};
  partial.accepted_work->callback_iteration=ReportSourceCallbackIteration{callback,123,0,20,10,2};
  partial.accepted_work->canceled=true;
  Report callback_report{{partial},{{2,100,0}},{{44,2,100,55}},{}};
  for(const auto id:{10,20}){StepRecord step;step.id=id;step.owner=callback;step.received=950;step.receipt=StepReceipt::WorkerExited;step.callback_iteration=CallbackIterationOrigin{10,id==10?1u:2u};callback_report.steps.push_back(step);}
  callback_report.callback_assertions.push_back({10,123,0,2,callback});
  assert(ValidateRetainedSourceReports(callback_report)==1024&&callback_report.worker_reuse.borrows.empty());
  Writer callback_writer;PutRetainedSourceReports(callback_writer,callback_report.retained_sources);
  assert(callback_writer.data.find("\"purpose\":\"callbackIteration\"")!=std::string::npos&&callback_writer.data.find("\"iterationStepId\":\"20\"")!=std::string::npos&&callback_writer.data.find("borrowLease")==std::string::npos&&callback_writer.data.find("\"associatedTestId\":\"0\"")!=std::string::npos);
  auto changed=callback_report;changed.steps.back().callback_iteration->invocation=1;Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.steps.back().owner.scope_generation++;Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.retained_sources[0].accepted_work->callback_iteration->request++;Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.attempts[0].generation++;Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.steps.back().received.reset();Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.steps.back().receipt=StepReceipt::Pending;Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.retained_sources[0].accepted_work->challenge=RetainedSourceWorkWire::Issue(1,1,2,3,1000);Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.callback_assertions.clear();Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.retained_sources[0].accepted_work->callback_iteration.reset();Rejected([&]{ValidateRetainedSourceReports(changed);});
  changed=callback_report;changed.retained_sources[0].accepted_work->acknowledged=false;changed.retained_sources[0].accepted_work->refusal=1;assert(ValidateRetainedSourceReports(changed)==1024);
  changed=callback_report;changed.retained_sources[0].accepted_work->acknowledged=false;changed.retained_sources[0].accepted_work->late_acknowledged=true;assert(ValidateRetainedSourceReports(changed)==1024);
  changed=callback_report;changed.retained_sources[0].accepted_work->acknowledged=false;changed.retained_sources[0].accepted_work->observed=false;changed.retained_sources[0].accepted_work->cuts.reset();assert(ValidateRetainedSourceReports(changed)==1024);
  auto body_call=callback_report;body_call.retained_sources[0].accepted_work->callback_iteration->callback.callback_token=88;for(auto& step:body_call.steps)step.owner.callback_token=88;body_call.callback_assertions[0].owner.callback_token=88;assert(ValidateRetainedSourceReports(body_call)==1024);
  Writer unchanged;PutRetainedSourceReports(unchanged,std::vector{row});assert(unchanged.data==writer.data);
  std::puts("PASS29 native Source work receipt report controls (partial callback ownership is controlled metadata, not successful Source fence evidence)");
}
