// SPDX-License-Identifier: MIT
#include "attachment_projection_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
struct ProjectionReportFixture{
  struct Attempt{uint64_t test_id=1,generation=1;unsigned worker_index=1;int64_t pid=101;};
  struct Worker{unsigned worker_index=1;int64_t pid=101;int exit_code=0;};
  std::vector<Attempt> attempts{{}};std::vector<Worker> workers{{}};
  std::vector<AttachmentDeclarations::Record> attachment_declarations;
  std::vector<AttachmentRecord> attachments;std::vector<StepRecord> steps;
  std::vector<AttachmentProjectionReport> attachment_views;
};
struct ProjectionWriter{
  std::string text;void Put(std::string_view value){text+=value;}
  template<class T>void NumberValue(T value){text+=std::to_string(value);}
};
int main(){
  unsigned controls=0;const StepOwner body{1,1,0,1,1,1,101,"body"};
  AttachmentDeclarations declarations;declarations.Open(body,100,0);
  const auto path=declarations.Declare(body,1,"\"authored \\ud800\"","\"x\"",AttachmentDeclarations::Source::PathReference,"\"missing\\ud800\"",std::nullopt,1);
  AttachmentMutations native(body);native.Open(body,100,0);const auto ref=native.AdmitDeclaration(declarations,path);
  native.Apply(body,2,0,{ref,std::nullopt,ref},2);
  ProjectionReportFixture report;report.attachment_declarations=declarations.Records();
  UpsertAttachmentProjection(report.attachment_views,native,{});assert(ValidateAttachmentProjections(report)>0);++controls;
  native.Apply(body,3,1,{},3);UpsertAttachmentProjection(report.attachment_views,native,{});
  assert(report.attachment_views.size()==1&&report.attachment_views[0].changes.back().slots.empty()&&report.attachment_declarations.size()==1);++controls;
  AttachmentRecord actual{17,4,body,"\"genuine artifact fixture\"","\"x\"","bytes","/native/owned.dat",3,7,9,102,false,0,4,5,6,0};
  report.attachments.push_back(actual);AttachmentViewReceipts receipts({[](const auto&){},[](const auto&){}});
  const auto ticket=receipts.Issue(actual,100,6);
  native.Close(body);native.Publish(body,5,2,report.attachments,17,100,7);
  receipts.Acknowledge(ticket,body,17,5,true,7);UpsertAttachmentProjection(report.attachment_views,native,receipts.Records());
  assert(ValidateAttachmentProjections(report)>0&&report.attachment_views[0].changes.back().publication_artifact==17);++controls;
  auto hook=body;hook.callback_kind="afterEach";hook.callback_token=9;hook.scope=2;native.Open(hook,100,8);
  native.Apply(hook,6,3,{ref},9);native.Reject(hook,7,0,"\"actual native mutation failure\"",10);
  UpsertAttachmentProjection(report.attachment_views,native,receipts.Records());assert(ValidateAttachmentProjections(report)>0&&report.attachments.size()==1);++controls;
  const auto invalid=[&](auto change){auto bad=report;change(bad);bool rejected=false;try{ValidateAttachmentProjections(bad);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++controls;};
  invalid([](auto& v){v.attachment_views[0].changes[2].publication_artifact=99;});
  invalid([](auto& v){v.attachment_views[0].publications.clear();});
  invalid([](auto& v){v.attachment_views[0].publications[0].accepted=false;});
  invalid([](auto& v){v.attachment_views[0].publications[0].mutation_request=99;});
  invalid([](auto& v){v.attachment_views[0].publications[0].end.reset();});
  invalid([](auto& v){v.attachment_views[0].publications[0].received=100;});
  invalid([](auto& v){v.attachment_views[0].publications[0].cleanup_deadline+=1;});
  invalid([](auto& v){v.attachment_views[0].publications[0].owner.pid=102;});
  invalid([](auto& v){v.attachment_views.push_back(v.attachment_views[0]);});
  invalid([](auto& v){v.attachment_views[0].changes[0].slots[0]->id=999;});
  invalid([](auto& v){v.attachment_views[0].changes[0].slots[0]=AttachmentMutations::Reference{AttachmentMutations::Kind::Artifact,17};});
  invalid([](auto& v){v.attachment_views[0].changes[0].revision=2;});
  invalid([](auto& v){v.attachment_views[0].rejections[0].observed_revision=0;});
  invalid([](auto& v){v.attachment_views[0].references[0].owner.attempt_generation=2;});
  invalid([](auto& v){v.attachment_views[0].references[0].request=99;});
  invalid([](auto& v){v.attempts.clear();});
  auto failed=actual;failed.id=18;failed.request=8;failed.started=11;failed.committed=12;failed.received=13;report.attachments.push_back(failed);
  const auto rejected=receipts.Issue(failed,100,13);native.Reject(hook,9,0,"\"actual stale publication revision\"",14);
  // Publication ownership is the genuine original artifact owner, independent
  // of the currently active hook. Use its closed native callback for rejection.
  native.Reject(body,10,0,"\"actual stale publication revision\"",14);
  receipts.Acknowledge(rejected,body,18,10,false,14);UpsertAttachmentProjection(report.attachment_views,native,receipts.Records());
  assert(ValidateAttachmentProjections(report)>0);++controls;
  auto abandoned=actual;abandoned.id=19;abandoned.request=11;abandoned.started=15;abandoned.committed=16;abandoned.received=17;report.attachments.push_back(abandoned);
  receipts.Issue(abandoned,100,17);receipts.WorkerExited(1,101,18);UpsertAttachmentProjection(report.attachment_views,native,receipts.Records());
  assert(ValidateAttachmentProjections(report)>0);++controls;
  invalid([](auto& v){v.workers.clear();});
  ProjectionWriter writer;PutAttachmentProjections(writer,report.attachment_views);
  assert(writer.text.find("\"revision\":\"4\",\"slots\":[{\"kind\":\"declaration\"")!=std::string::npos&&writer.text.find("workerExited")!=std::string::npos&&writer.text.find("publicationReceipts")!=std::string::npos);++controls;
  std::cout<<"PASS "<<controls<<" controlled native final projection/history/publication-receipt report validation groups; no file/process receipt claim\n";
}
