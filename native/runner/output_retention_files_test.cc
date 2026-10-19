// SPDX-License-Identifier: MIT
#include "runner_output_directories.h"
#include "runner_output_retention.h"
#include "output_retention_files.h"
#include "output_retention_report.h"
#include <cassert>
#include <fstream>
#include <iostream>
namespace sr=suitecut::runner;namespace fs=std::filesystem;namespace af=sr::attachment_files;
struct ActualRetentionAttempt {uint64_t test_id=1,generation=1;unsigned worker_index=1;int64_t pid=getpid();unsigned retry=0;std::string status="passed",expected_status="passed";bool expected=true;};
struct ActualRetentionReport {std::optional<sr::PreserveOutput> output_preservation;std::vector<sr::OutputDirectoryRecord> outputs;std::vector<sr::OutputRetentionRecord> output_retentions;std::vector<ActualRetentionAttempt> attempts{{}};};
struct ActualRetentionProof {
 fs::path root;size_t physical=0;std::vector<std::string> failures;
 sr::RunnerOutputDirectories::Budget output_budget;sr::RunnerOutputRetention::Budget retention_budget;
 sr::RunnerOutputDirectories output;sr::RunnerOutputRetention retention;
 sr::OutputDirectoryOwner owner{1,1,1,getpid()};sr::OutputDirectoryRecord source;
 explicit ActualRetentionProof(fs::path path,sr::RunnerOutputRetention::Hooks hooks={}):root(std::move(path)),output("/proc/self/exe",{[this](const auto& error){failures.push_back(error);}},output_budget,{&physical,1}),retention("/proc/self/exe",{[this](const auto& error){failures.push_back(error);},std::move(hooks.after_spawn)},retention_budget,{&physical,1}){
  fs::create_directories(root);const auto now=af::Now();assert(output.Begin(owner,root/"out",now+5000,now));const auto end=now+7000;
  while(output.Physical()){assert(af::Now()<end);output.Tick(af::Now());if(output.Physical())poll(nullptr,0,1);}
  assert(!physical&&failures.empty()&&output.Assign(owner,af::Now()));source=*output.Find(owner);assert(source.created&&source.ready_receipt&&source.writer_received&&source.received&&source.assigned&&source.writer_exit==0&&source.writer_pid!=getpid());
 }
 sr::OutputRetentionOutcome Outcome()const{return {owner,0,"passed","passed",true};}
 void Admit(sr::PreserveOutput policy,unsigned milliseconds=5000){const auto now=af::Now();retention.Admit(source,Outcome(),policy,now+milliseconds,now);}
 void Drain(){const auto end=af::Now()+7000;while(!retention.IdleReady()){assert(af::Now()<end);retention.StartPending({},af::Now());retention.Tick(af::Now());if(!retention.IdleReady())poll(nullptr,0,1);}assert(!physical&&!retention.Physical());}
 const sr::OutputRetentionRecord& Row()const{const auto* row=retention.Find(source.id);assert(row);return *row;}
 void Validate(sr::PreserveOutput policy)const{ActualRetentionReport report{policy,{source},retention.Records(),{{}}};assert(sr::ValidateOutputRetentionReports(report)>0);}
};
void ActualWrite(const fs::path& path,std::string_view bytes){std::ofstream file(path,std::ios::binary);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.close();assert(file);}
std::string ActualRead(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
int main(int argc,char** argv){
 if(argc==2&&std::string_view(argv[1])=="--native-output-directory-worker")return sr::output_directory_files::Worker();
 if(argc==2&&std::string_view(argv[1])=="--native-output-directory-cleanup")return sr::output_directory_files::Cleanup();
 if(argc==2&&std::string_view(argv[1])=="--native-output-retention-worker")return sr::output_retention_files::Worker();
 assert(argc==1);char temporary[]="/tmp/suitecut-output-retention-XXXXXX";assert(mkdtemp(temporary));const fs::path root=temporary;unsigned groups=0;
 {ActualRetentionProof p(root/"nested-native-removal");const fs::path path=p.source.path;fs::create_directories(path/"nested/deep");ActualWrite(path/"nested/deep/bytes",std::string_view("\0\x80\xff",3));ActualWrite(p.root/"outside","genuine outside bytes");fs::create_symlink(p.root/"outside",path/"outside-link");p.Admit(sr::PreserveOutput::Never);assert(p.retention.Start(p.source.id,{},af::Now())&&p.retention.Physical()==1&&p.physical==1&&!p.retention.IdleReady());p.Drain();const auto& row=p.Row();assert(row.status==sr::OutputRetentionStatus::Removed&&row.helper_pid>0&&row.helper_exit==0&&!row.helper_signaled&&row.physical_received&&row.receipt&&row.removed_files==1&&row.removed_directories==3&&row.removed_symlinks==1&&!fs::exists(path)&&ActualRead(p.root/"outside")=="genuine outside bytes"&&p.failures.empty());p.Validate(sr::PreserveOutput::Never);++groups;}
 {ActualRetentionProof p(root/"kept-no-filesystem-observation");ActualWrite(fs::path(p.source.path)/"kept","actual retained bytes");p.Admit(sr::PreserveOutput::Always);p.Drain();assert(p.Row().status==sr::OutputRetentionStatus::Kept&&!p.Row().helper_pid&&!p.Row().physical_received&&ActualRead(fs::path(p.source.path)/"kept")=="actual retained bytes"&&p.failures.empty());p.Validate(sr::PreserveOutput::Always);++groups;}
 {ActualRetentionProof p(root/"genuine-absent-location");fs::remove(p.source.path);p.Admit(sr::PreserveOutput::Never);p.Drain();assert(p.Row().status==sr::OutputRetentionStatus::AlreadyAbsent&&p.Row().physical_received&&p.Row().removed_directories==0&&p.failures.empty());p.Validate(sr::PreserveOutput::Never);++groups;}
 {ActualRetentionProof p(root/"foreign-root-preserved");fs::rename(p.source.path,p.root/"actual-renamed-original");fs::create_directory(p.source.path);ActualWrite(fs::path(p.source.path)/"foreign","actual foreign bytes");p.Admit(sr::PreserveOutput::Never);p.Drain();assert(p.Row().status==sr::OutputRetentionStatus::ForeignPreserved&&fs::is_directory(p.root/"actual-renamed-original")&&ActualRead(fs::path(p.source.path)/"foreign")=="actual foreign bytes"&&p.failures.empty());p.Validate(sr::PreserveOutput::Never);++groups;}
 {ActualRetentionProof p(root/"foreign-parent-fails-without-removal");fs::rename(p.root/"out",p.root/"actual-renamed-parent");fs::create_directory(p.root/"out");ActualWrite(p.root/"out/foreign","foreign parent bytes");p.Admit(sr::PreserveOutput::Never);p.Drain();assert(p.Row().status==sr::OutputRetentionStatus::Failed&&p.Row().helper_exit==1&&p.Row().physical_received&&!p.Row().error.empty()&&!p.failures.empty()&&fs::is_directory(p.root/"actual-renamed-parent")&&ActualRead(p.root/"out/foreign")=="foreign parent bytes");p.Validate(sr::PreserveOutput::Never);++groups;}
 {ActualRetentionProof p(root/"whole-inventory-before-mutation");fs::path deep=p.source.path;for(unsigned i=0;i<66;++i){deep/="depth";fs::create_directory(deep);}ActualWrite(fs::path(p.source.path)/"untouched","inventory rejected before mutation");p.Admit(sr::PreserveOutput::Never);p.Drain();assert(p.Row().status==sr::OutputRetentionStatus::Failed&&p.Row().error.find("depth")!=std::string::npos&&!p.Row().removed_directories&&fs::is_directory(deep)&&ActualRead(fs::path(p.source.path)/"untouched")=="inventory rejected before mutation");p.Validate(sr::PreserveOutput::Never);++groups;}
 {ActualRetentionProof p(root/"actual-stopped-helper-deadline",{ {},[](int64_t pid){assert(!kill(static_cast<pid_t>(pid),SIGSTOP));} });ActualWrite(fs::path(p.source.path)/"retained","actual timeout did not imply unlink");p.Admit(sr::PreserveOutput::Never,100);assert(p.retention.Start(p.source.id,{},af::Now())&&p.physical==1&&p.retention.Physical()==1);p.Drain();assert(p.Row().status==sr::OutputRetentionStatus::Failed&&p.Row().helper_signaled&&p.Row().helper_exit==SIGKILL&&p.Row().physical_received&&!p.Row().removed_directories&&fs::is_directory(p.source.path)&&!p.failures.empty());p.Validate(sr::PreserveOutput::Never);++groups;}
 {ActualRetentionProof p(root/"actual-post-fork-admission-exception",{ {},[](int64_t){throw std::runtime_error("controlled post-fork exception");} });p.Admit(sr::PreserveOutput::Never);assert(p.retention.Start(p.source.id,{},af::Now())&&p.physical==1&&p.retention.Physical()==1);p.Drain();assert(p.Row().status==sr::OutputRetentionStatus::Failed&&p.Row().physical_received&&!p.failures.empty());p.Validate(sr::PreserveOutput::Never);++groups;}
 fs::remove_all(root);std::cout<<"PASS "<<groups<<" genuine native output creation/assignment and retention helper/filesystem/waitpid groups; controlled attempt facts only, no Node-runner claim\n";
}
