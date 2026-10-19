// SPDX-License-Identifier: MIT
#include "runner_attachments.h"
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
// Report DTO attempt/worker fields below are controlled validation fixtures;
// the artifact itself is produced by a real native child with real waitpid.
int main(int argc,char** argv){
 if(argc==2&&std::string_view(argv[1])=="--native-attachment-worker")return attachment_files::Worker();
 if(argc==2&&std::string_view(argv[1])=="--native-attachment-cleanup")return attachment_files::Cleanup();
 assert(argc==2);const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 const StepOwner owner{1,1,0,1,1,1,getpid(),"body"};std::vector<std::vector<std::string>> packets;
 RunnerAttachments actor(out,{[&](const auto& line){packets.push_back(attachment_files::Fields(line));},[](const auto&){assert(false);}});actor.WorkerStarted();actor.BeginCallback(owner,DedicatedRealMilliseconds()+5000,DedicatedRealMilliseconds());
 assert(actor.Packet({"ATTACH_BEGIN","1","0","1","1","6d0065007400610000d800003c002600","B","4","~","-"},DedicatedRealMilliseconds(),false));
 const auto end=DedicatedRealMilliseconds()+5000;bool bytes=false,finish=false;
 while(actor.Records().empty()){
  assert(DedicatedRealMilliseconds()<end);actor.Tick(DedicatedRealMilliseconds());
  for(const auto& p:packets){if(p[0]=="ATTACH_ADMITTED"&&!bytes){assert(actor.Packet({"ATTACH_CHUNK","1","0","1","1","1","000180ff"},DedicatedRealMilliseconds(),false));bytes=true;}if(p[0]=="ATTACH_CHUNK_OK"&&!finish){assert(actor.Packet({"ATTACH_FINISH","1","0","1","1"},DedicatedRealMilliseconds(),false));finish=true;}}
  poll(nullptr,0,1);
 }
 RunnerReport r;r.workers.push_back({1,getpid(),false,0,{},{}});r.attempts.push_back({1,1,0,1,getpid(),"\"controlled report attempt\"","\"\"","passed",true,20});r.attachments=actor.Records();assert(report_detail::Validate(r).expected==1);unsigned checked=0;
 const auto invalid=[&](const auto& change){auto bad=r;change(bad);bool rejected=false;try{(void)report_detail::Validate(bad);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++checked;};
 invalid([](auto& v){v.attachments[0].writer_exit=1;});invalid([](auto& v){v.attachments[0].writer_signaled=true;});invalid([](auto& v){v.attachments[0].writer_pid=v.attachments[0].owner.pid;});invalid([](auto& v){v.attachments[0].owner.callback_kind="fixture";});invalid([](auto& v){v.attachments[0].committed=v.attachments[0].received+1;});invalid([](auto& v){v.attachments[0].path="relative";});invalid([](auto& v){v.attachments[0].name_json="not JSON";});invalid([](auto& v){v.attachments[0].content_type_json="false";});invalid([](auto& v){v.attachments[0].bytes=16*1024*1024+1;});invalid([](auto& v){v.attachments.push_back(v.attachments[0]);});invalid([](auto& v){v.attachments[0].owner.test_id=2;});invalid([](auto& v){v.attachments[0].owner.attempt_generation=2;});invalid([](auto& v){v.attachments[0].owner.worker_index=2;});invalid([](auto& v){auto a=v.attachments[0];a.id=2;a.path+="different";v.attachments.push_back(a);});invalid([](auto& v){auto a=v.attachments[0];a.id=2;a.request=2;v.attachments.push_back(a);});invalid([](auto& v){v.attachments[0].inode=0;});invalid([](auto& v){v.attachments[0].id=0;});invalid([](auto& v){v.attachments[0].request=0;});invalid([](auto& v){v.attachments[0].path+='\xff';});invalid([](auto& v){v.attachments[0].owner.callback_kind="beforeAll";});
 WriteRunnerJson(r,out/"attachment.json");WriteRunnerJUnit(r,out/"attachment.xml");WriteRunnerHTML(r,out/"attachment.html");
 std::cout<<"PASS"<<checked<<" controlled attachment report validation groups plus genuine native artifact JSON/XML/HTML fixture\n";
}
