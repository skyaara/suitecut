// SPDX-License-Identifier: MIT
#include "attachment_declaration_report.h"
#include "attachment_projection_report.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
// Deliberately controlled DTOs exercise validation; these are not IO/host receipts.
struct ControlledStep:sr::StepRecord {struct Origin{uint64_t assertion,invocation;};std::optional<Origin> callback_iteration;std::optional<double> callback_deadline;};
struct Report {
 struct Attempt{uint64_t test_id=1,generation=1;unsigned worker_index=1;int64_t pid=101;};
 struct Worker{unsigned worker_index=1;int64_t pid=101;int exit_code=0;};
 std::vector<Attempt> attempts{{}};std::vector<Worker> workers{{}};std::vector<ControlledStep> steps;
 std::vector<sr::AttachmentDeclarations::Record> attachment_declarations;
 std::vector<sr::AttachmentRecord> attachments;std::vector<sr::AttachmentProjectionReport> attachment_views;
};
struct Writer{std::string text;void Put(std::string_view v){text+=v;}template<class T>void NumberValue(T v){text+=std::to_string(v);}};
int main(){unsigned count=0;const sr::StepOwner owner{1,1,0,1,1,1,101,"body"};const sr::AttachmentRetryProvenance retry{10,2,20};Report report;
 ControlledStep outer;outer.id=1;outer.owner=owner;outer.started=0;outer.deadline=100;outer.own_deadline=100;outer.callback_received=80;outer.received=95;outer.receipt=sr::StepReceipt::Settled;
 ControlledStep first;first.id=10;first.parent=1;first.owner=owner;first.started=10;first.deadline=100;first.own_deadline=100;first.callback_deadline=100;first.callback_received=20;first.received=30;first.receipt=sr::StepReceipt::Settled;first.callback_iteration=ControlledStep::Origin{10,1};
 auto second=first;second.id=20;second.started=50;second.callback_received=70;second.received=90;second.callback_iteration=ControlledStep::Origin{10,2};report.steps={outer,first,second};
 sr::ValidateAttachmentCallbackRetry(report,owner,retry,0,55,60);sr::ValidateAttachmentCallbackRetry(report,owner,retry,1,55,60);++count;
 const auto invalid=[&](const auto& change){auto bad=report;change(bad);bool rejected=false;try{sr::ValidateAttachmentCallbackRetry(bad,owner,retry,1,55,60);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++count;};
 invalid([](auto& v){v.steps[2].own_deadline=55;});invalid([](auto& v){v.steps[0].own_deadline=55;});invalid([](auto& v){v.steps[2].callback_deadline=55;});invalid([](auto& v){v.steps[2].own_deadline=NAN;});
 invalid([](auto& v){v.steps[2].owner.scope=2;});invalid([](auto& v){v.steps[2].callback_iteration->invocation=3;});invalid([](auto& v){v.steps[1].callback_iteration->assertion=99;});invalid([](auto& v){v.steps[2].parent=0;});invalid([](auto& v){v.steps[1].received=51;});invalid([](auto& v){v.steps[2].started=56;});invalid([](auto& v){v.steps[2].callback_received=54;});invalid([](auto& v){v.steps[2].received=59;});invalid([](auto& v){v.steps[2].receipt=sr::StepReceipt::NotInvoked;});invalid([](auto& v){v.steps[0].callback_received=54;});invalid([](auto& v){v.steps.erase(v.steps.begin()+1);});
 {auto exited=report;exited.steps[2].receipt=sr::StepReceipt::WorkerExited;exited.steps[2].callback_received.reset();exited.steps[2].received=56;sr::ValidateAttachmentCallbackRetry(exited,owner,retry,0,55,65);++count;}
 sr::AttachmentDeclarations declarations;declarations.Open(owner,100,0);declarations.Declare(owner,1,"\"reference\"","\"x\"",sr::AttachmentDeclarations::Source::PathReference,"\"no read\"",std::nullopt,55,std::nullopt,retry);report.attachment_declarations=declarations.Records();assert(sr::ValidateAttachmentDeclarationReports(report)>0);Writer declared;sr::PutAttachmentDeclarationReports(declared,report.attachment_declarations);const std::string expected="\"callbackAssertion\":{\"id\":\"10\",\"invocation\":\"2\",\"stepId\":\"20\"}";assert(declared.text.find(expected)!=std::string::npos);++count;
 sr::AttachmentRecord artifact{17,2,owner,"\"controlled artifact\"","\"x\"","bytes","/controlled/native.dat",0,7,9,102,false,0,55,56,57,1,retry};report.attachments={artifact};sr::AttachmentMutations journal(owner);journal.Open(owner,100,0);sr::AttachmentViewReceipts receipts({[](const auto&){},[](const auto&){}});const auto ticket=receipts.Issue(artifact,100,57);journal.Publish(owner,3,0,report.attachments,17,100,58);receipts.Acknowledge(ticket,owner,17,3,true,58);sr::UpsertAttachmentProjection(report.attachment_views,journal,receipts.Records());assert(sr::ValidateAttachmentProjections(report)>0);Writer projected;sr::PutAttachmentProjections(projected,report.attachment_views);assert(projected.text.find(expected)!=std::string::npos&&projected.text.find("\"stepId\":\"1\"")!=std::string::npos);++count;
 {auto bad=report;bad.attachment_views[0].publications[0].callback_retry.invocation_nonce=3;bool rejected=false;try{sr::ValidateAttachmentProjections(bad);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++count;}
 {auto sibling=report;sibling.steps[2].parent=0;sibling.steps[1].parent=0;sr::ValidateAttachmentCallbackRetry(sibling,owner,retry,1,55,60);sibling.steps[0].received=59;bool rejected=false;try{sr::ValidateAttachmentCallbackRetry(sibling,owner,retry,1,55,60);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++count;}
 {auto forged=report;forged.attachment_declarations[0].retry_failure_disposed=true;bool rejected=false;try{sr::ValidateAttachmentDeclarationReports(forged);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++count;}
 {auto shortened=report;for(auto& step:shortened.steps)step.deadline=5;sr::ValidateAttachmentCallbackRetry(shortened,owner,retry,0,55,60);sr::ValidateAttachmentCallbackRetry(shortened,owner,retry,1,55,60);assert(sr::ValidateAttachmentDeclarationReports(shortened)>0&&sr::ValidateAttachmentProjections(shortened)>0);++count;}
 {auto legacy=report;legacy.attachment_declarations[0].callback_retry={};legacy.attachments[0].callback_retry={};legacy.attachment_views[0].publications[0].callback_retry={};assert(sr::ValidateAttachmentDeclarationReports(legacy)>0&&sr::ValidateAttachmentProjections(legacy)>0);Writer d,p;sr::PutAttachmentDeclarationReports(d,legacy.attachment_declarations);sr::PutAttachmentProjections(p,legacy.attachment_views);assert(d.text.find("callbackAssertion")==std::string::npos&&p.text.find("callbackAssertion")==std::string::npos);++count;}
 std::cout<<"PASS"<<count<<" controlled attachment callback report identity/ancestry/admission/physical-end validation and optional serialization groups; no IO or process receipt claim\n";
}
