// SPDX-License-Identifier: MIT
#include "attachment_append_source.h"
#include "attachment_declaration_report.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class Call>void RefuseAppend(Call&& call){bool refused=false;try{call();}catch(const std::exception&){refused=true;}assert(refused);}
struct EntryReport {
 struct Attempt {uint64_t test_id=1,generation=1;unsigned worker_index=1;int64_t pid=101;};
 struct Worker {unsigned worker_index=1;int64_t pid=101;int exit_code=0;};
 std::vector<AttachmentDeclarations::Record> attachment_declarations;
 std::vector<Attempt> attempts{{}};std::vector<Worker> workers{{}};
 std::vector<AttachmentRecord> attachments;
};
struct EntryWriter {std::string text;void Put(std::string_view value){text+=value;}template<class Number>void NumberValue(Number value){text+=std::to_string(value);}};
int main(){
 unsigned groups=0;
 auto plain=ReadAttachmentAppendSource("B",3,"\"\"");assert(plain.source==AttachmentDeclarations::Source::Body&&plain.expected_bytes==3&&!plain.author_path_json);++groups;
 auto both=ReadAttachmentAppendSource("D",3,"\"original path\\ud800\"");assert(both.source==AttachmentDeclarations::Source::Body&&both.expected_bytes==3&&both.author_path_json=="\"original path\\ud800\"");++groups;
 auto empty=ReadAttachmentAppendSource("D",0,"\"\"");assert(empty.author_path_json&&*empty.author_path_json=="\"\""&&empty.expected_bytes==0);++groups;
 auto reference=ReadAttachmentAppendSource("R",0,"\"no filesystem claim\""),absent=ReadAttachmentAppendSource("N",0,"\"\"");
 assert(reference.source==AttachmentDeclarations::Source::PathReference&&!reference.expected_bytes&&reference.author_path_json);assert(absent.source==AttachmentDeclarations::Source::Absent&&!absent.expected_bytes&&!absent.author_path_json);++groups;
 for(const auto* bad:{"Q","P","","BD"})RefuseAppend([&]{ReadAttachmentAppendSource(bad,0,"\"\"");});
 RefuseAppend([]{ReadAttachmentAppendSource("N",1,"\"\"");});RefuseAppend([]{ReadAttachmentAppendSource("R",1,"\"path\"");});RefuseAppend([]{ReadAttachmentAppendSource("B",1,"\"path\"");});RefuseAppend([]{ReadAttachmentAppendSource("D",16*1024*1024+1,"\"path\"");});++groups;
 RefuseAppend([]{ReadAttachmentAppendSource("D",3,"null");});RefuseAppend([]{ReadAttachmentAppendSource("D",3,"\""+std::string(8193,'x')+"\"");});++groups;
 AttachmentDeclarations::Budget budget;AttachmentDeclarations ledger(&budget);const StepOwner owner{1,1,0,1,1,1,101,"body"};ledger.Open(owner,100,0);
 auto id=ledger.Declare(owner,1,"\"initial\"","\"x\"",both.source,both.author_path_json,both.expected_bytes,1);
 const auto* record=ledger.Find(id);assert(record&&record->receipt==AttachmentDeclarations::Receipt::Pending&&record->source==AttachmentDeclarations::Source::Body&&record->author_path_json==both.author_path_json&&record->expected_bytes==3&&!record->artifact_id&&budget.records==1);
 both.author_path_json="\"later caller\"";assert(record->author_path_json=="\"original path\\ud800\"");++groups;
 const auto old=budget.metadata;
 RefuseAppend([&]{ledger.Declare(owner,2,"\"bad\"","\"x\"",AttachmentDeclarations::Source::Absent,"\"path\"",std::nullopt,2);});
 RefuseAppend([&]{ledger.Declare(owner,2,"\"bad\"","\"x\"",AttachmentDeclarations::Source::Body,"\"path\"",std::nullopt,2);});
 RefuseAppend([&]{ledger.Declare(owner,2,"\"expired\"","\"x\"",AttachmentDeclarations::Source::Body,"\"path\"",3,100);});
 assert(budget.records==1&&budget.metadata==old&&record->receipt==AttachmentDeclarations::Receipt::Pending&&!record->received);++groups;
 ledger.Failed(owner,id,"\"controlled failure keeps actual path metadata\"",3);
 EntryReport report;report.attachment_declarations=ledger.Records();assert(ValidateAttachmentDeclarationReports(report)>0);
 EntryWriter writer;PutAttachmentDeclarationReports(writer,report.attachment_declarations);
 assert(writer.text.find("\"declaredPath\":\"original path\\ud800\"")!=std::string::npos&&writer.text.find("\"sourceKind\":\"body\"")!=std::string::npos&&report.attachments.empty());++groups;
 auto forged=report;forged.attachment_declarations[0].source=AttachmentDeclarations::Source::Absent;forged.attachment_declarations[0].expected_bytes.reset();RefuseAppend([&]{ValidateAttachmentDeclarationReports(forged);});
 forged=report;forged.attachment_declarations[0].source=AttachmentDeclarations::Source::PathReference;forged.attachment_declarations[0].author_path_json.reset();forged.attachment_declarations[0].expected_bytes.reset();RefuseAppend([&]{ValidateAttachmentDeclarationReports(forged);});++groups;
 std::cout<<"PASS "<<groups<<" native append-source/parser/declaration metadata controls; no file/callback retirement or runtime claim\n";
}
