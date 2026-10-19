// SPDX-License-Identifier: MIT
#include "runner_attachments.h"
#include "runner_report.h"
#include <cassert>
#include <filesystem>
#include <iostream>
namespace sr=suitecut::runner;namespace af=sr::attachment_files;namespace fs=std::filesystem;
struct Proof {
 sr::StepOwner owner{1,1,0,1,1,1,getpid(),"body"};sr::TestSteps steps;
 fs::path root;std::vector<std::vector<std::string>> packets;std::vector<std::string> failures;sr::RunnerAttachments files;
 Proof(fs::path path):root(std::move(path)),files(root,{[this](const auto& line){packets.push_back(af::Fields(line));},[this](const auto& error){failures.push_back(error);},[this](uint64_t id){return steps.Find(id);}}){const auto now=af::Now();assert(steps.Open(owner,now+5000,now));files.WorkerStarted();files.BeginCallback(owner,now+5000,now);}
 uint64_t Step(uint64_t parent=0,uint64_t timeout=0){const auto id=steps.Begin(owner,parent,"\"actual native step\"",timeout,af::Now());assert(id);return *id;}
 void Begin(uint64_t step,const std::string& kind="B",const std::string& path=""){assert(files.Packet({"ATTACH_STEP_BEGIN","1","0","1","1",std::to_string(step),"6100",kind,"0","~",af::Hex(path)},af::Now(),false));}
 void Drain(bool finish){size_t cursor=0;const auto cap=af::Now()+7000;while(files.Physical()){assert(af::Now()<cap);files.Tick(af::Now());while(cursor<packets.size()){const auto p=packets[cursor++];if(finish&&p[0]=="ATTACH_STEP_ADMITTED"&&p[7]=="1")files.Packet({"ATTACH_FINISH",p[5],"0","1","1"},af::Now(),false);}if(files.Physical())poll(nullptr,0,1);}assert(failures.empty());}
 void Fence(uint64_t id,bool& forwarded){files.FenceStep(owner,id,[&,id](const auto& error){assert(steps.Complete(owner,id,af::Now(),error));forwarded=true;},af::Now());}
 void Empty()const{assert(files.Records().empty());if(fs::exists(root))for(const auto& p:fs::recursive_directory_iterator(root))assert(!p.is_regular_file());}
};
int main(int argc,char** argv){
 if(argc==2&&std::string_view(argv[1])=="--native-attachment-worker")return af::Worker();
 if(argc==2&&std::string_view(argv[1])=="--native-attachment-cleanup")return af::Cleanup();
 char name[]="/tmp/suitecut-native-step-attachments-XXXXXX";assert(mkdtemp(name));const fs::path root=name;unsigned groups=0;
 {Proof p(root/"actual-file-fence");const auto id=p.Step();p.Begin(id);assert(p.files.Physical()==1);assert(p.steps.Receive(p.owner,id,true,"\"\"",af::Now()));bool forwarded=false;p.Fence(id,forwarded);assert(!forwarded&&p.steps.Find(id)->receipt==sr::StepReceipt::Pending);p.Drain(true);assert(forwarded&&p.steps.Find(id)->status==sr::StepStatus::Passed);const auto& artifact=p.files.Records().at(0);assert(artifact.step_id==id&&artifact.owner==p.owner&&fs::is_regular_file(artifact.path)&&artifact.bytes==0&&artifact.received<=*p.steps.Find(id)->received&&*p.steps.Find(id)->callback_received<=artifact.received);++groups;
  // Artifact identity is a genuine native writer result. Associated attempt
  // fields here are controlled report validation fixtures, not a JS test claim.
  sr::RunnerReport report;report.workers.push_back({1,getpid(),false,0,{},{}});report.attempts.push_back({1,1,0,1,getpid(),"\"controlled step attachment report\"","\"\"","passed",true,20});report.steps=p.steps.Records();report.attachments=p.files.Records();assert(sr::report_detail::Validate(report).expected==1);
  const auto invalid=[&](const auto& change){auto bad=report;change(bad);bool rejected=false;try{sr::report_detail::Validate(bad);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);};
  invalid([](auto& r){r.attachments[0].step_id=999;});invalid([](auto& r){r.attachments[0].owner.scope+=1;});invalid([](auto& r){r.attachments[0].started=r.steps[0].started-1;});invalid([](auto& r){r.attachments[0].received=*r.steps[0].received+1;});invalid([](auto& r){r.steps[0].callback_received=NAN;});invalid([](auto& r){r.steps[0].callback_received=*r.steps[0].received+1;});invalid([](auto& r){r.steps[0].callback_received=std::nullopt;});invalid([](auto& r){r.steps[0].attachment_error_json="\"cannot pass with actual file error\"";});++groups;}
 {Proof p(root/"wrong-callback-owner");const auto id=p.Step();auto other=p.owner;other.scope=2;p.files.BeginCallback(other,af::Now()+1000,af::Now());assert(p.files.Packet({"ATTACH_STEP_BEGIN","1","0","2","1",std::to_string(id),"6100","B","0","~","-"},af::Now(),false));assert(p.files.Physical()==0&&p.packets.back()[0]=="ATTACH_STEP_ADMITTED"&&p.packets.back()[7]=="0");p.Empty();++groups;}
 {Proof p(root/"raw-callback-closed");const auto id=p.Step();assert(p.steps.Receive(p.owner,id,true,"\"\"",af::Now()));p.Begin(id);assert(p.files.Physical()==0&&p.packets.back()[7]=="0");bool forwarded=false;p.Fence(id,forwarded);assert(forwarded);p.Empty();++groups;}
 {Proof p(root/"own-deadline");const auto id=p.Step(0,100);p.Begin(id);const auto deadline=p.steps.Find(id)->deadline;while(af::Now()<deadline)poll(nullptr,0,1);p.steps.Tick(af::Now());for(const auto& e:p.steps.TakeEvents())p.files.StepChanged(*p.steps.Find(e.id),af::Now());assert(p.steps.Receive(p.owner,id,true,"\"\"",af::Now()));bool forwarded=false;p.Fence(id,forwarded);assert(!forwarded);p.Drain(false);assert(forwarded&&p.steps.Find(id)->status==sr::StepStatus::TimedOut&&p.steps.Find(id)->receipt==sr::StepReceipt::Settled);p.Empty();++groups;}
 {Proof p(root/"actual-descendant-file-fence");const auto parent=p.Step(),child=p.Step(parent);p.Begin(child);assert(p.files.PendingStep(p.owner,parent));assert(p.steps.Receive(p.owner,parent,true,"\"\"",af::Now()));for(const auto& e:p.steps.TakeEvents())p.files.StepChanged(*p.steps.Find(e.id),af::Now());bool forwarded=false;p.Fence(parent,forwarded);assert(!forwarded);p.Drain(false);assert(forwarded&&p.steps.Find(parent)->status==sr::StepStatus::Passed&&p.steps.Find(child)->status==sr::StepStatus::Interrupted);assert(p.steps.Settle(p.owner,child,true,"\"\"",af::Now()));p.Empty();++groups;}
 {Proof p(root/"real-filesystem-error-after-raw-success");const auto id=p.Step();p.Begin(id,"P",(root/"actual-missing-file").string());assert(p.steps.Receive(p.owner,id,true,"\"\"",af::Now()));bool forwarded=false;p.Fence(id,forwarded);p.Drain(false);assert(forwarded&&p.steps.Find(id)->status==sr::StepStatus::Failed&&p.steps.Find(id)->callback_ok&&p.steps.Find(id)->attachment_error_json);p.Empty();++groups;}
 fs::remove_all(root);std::cout<<"PASS"<<groups<<" native actual step/file identity, raw/joint receipt, deadline, descendant and filesystem groups\n";
}
