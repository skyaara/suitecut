// SPDX-License-Identifier: MIT
#include "runner_output_directories.h"
#include <cassert>
#include <fstream>
#include <iostream>
namespace sr=suitecut::runner;namespace af=sr::attachment_files;namespace od=sr::output_directory_files;namespace fs=std::filesystem;
int main(int argc,char**argv){
 if(argc==2&&std::string_view(argv[1])=="--native-output-directory-worker")return od::Worker();
 if(argc==2&&std::string_view(argv[1])=="--native-output-directory-cleanup")return od::Cleanup();
 char name[]="/tmp/suitecut-output-directories-XXXXXX";assert(mkdtemp(name));const fs::path root=name;sr::RunnerOutputDirectories::Budget budget;size_t physical=0;std::vector<std::string> failures;sr::RunnerOutputDirectories actor("/proc/self/exe",{[&](const auto& error){failures.push_back(error);}},budget,{&physical,2});sr::OutputDirectoryOwner owner{1,1,1,getpid()};unsigned groups=0;
 auto drain=[&]{const auto deadline=af::Now()+7000;while(actor.Physical()){assert(af::Now()<deadline);actor.Tick(af::Now());if(actor.Physical())poll(nullptr,0,1);}assert(!physical);};
 assert(actor.Begin(owner,root/"ordinary",af::Now()+5000,af::Now()));assert(physical==1&&!actor.Find(owner)&&!actor.Assign(owner,af::Now()));drain();const auto* actual=actor.Find(owner);assert(actual&&actual->status==sr::OutputDirectoryStatus::Ready&&actual->writer_exit==0&&!actual->writer_signaled&&actual->created&&actual->writer_received&&actual->received&&*actual->created<=*actual->writer_received&&*actual->writer_received<=*actual->received);struct stat info{};assert(!::stat(actual->path.c_str(),&info)&&static_cast<uint64_t>(info.st_ino)==actual->inode&&static_cast<uint64_t>(info.st_dev)==actual->device&&S_ISDIR(info.st_mode));assert(actual->writer_pid>0&&actual->writer_pid!=getpid());auto stale=owner;++stale.generation;assert(!actor.Assign(stale,af::Now()));assert(actor.Assign(owner,af::Now())&&actual->assigned&&*actual->assigned>=*actual->received);assert(!actor.Assign(owner,af::Now()));++groups;
 const auto first=actual->path;assert(!actor.Begin(owner,root/"duplicate",af::Now()+5000,af::Now())&&!physical&&budget.records==1);++groups;
 auto second=owner;second.generation=2;assert(actor.Begin(second,root/"ordinary",af::Now()+5000,af::Now()));drain();assert(actor.Find(second)->path!=first&&actor.Find(second)->id>actual->id);++groups;
 auto busy=owner;busy.generation=3;physical=2;const auto nonce=budget.sequence;assert(!actor.Begin(busy,root,af::Now()+5000,af::Now())&&budget.sequence==nonce);physical=0;assert(actor.Begin(busy,root/"ordinary",af::Now()+5000,af::Now()));drain();++groups;
 auto bad=owner;bad.generation=4;{std::ofstream file(root/"not-directory");file<<"real unchanged bytes";}assert(actor.Begin(bad,root/"not-directory",af::Now()+5000,af::Now()));drain();assert(actor.Find(bad)->status==sr::OutputDirectoryStatus::Failed&&actor.Find(bad)->writer_exit!=0&&!actor.Find(bad)->inode&&!actor.Find(bad)->ready_receipt);assert(fs::is_regular_file(root/"not-directory"));++groups;
 auto foreign=owner;foreign.generation=5;fs::create_directories(root/"foreign");const auto occupied=root/"foreign"/("suitecut-output-"+std::to_string(getpid())+"-"+std::to_string(budget.sequence+1)+"-1-5");fs::create_directory(occupied);{std::ofstream file(occupied/"foreign");file<<"must survive genuine EEXIST";}assert(actor.Begin(foreign,root/"foreign",af::Now()+5000,af::Now()));drain();assert(actor.Find(foreign)->status==sr::OutputDirectoryStatus::Failed&&!actor.Find(foreign)->inode&&fs::is_regular_file(occupied/"foreign"));++groups;
 auto link=owner;link.generation=6;fs::create_directory(root/"real-parent");fs::create_directory_symlink(root/"real-parent",root/"symlink-parent");assert(actor.Begin(link,root/"symlink-parent",af::Now()+5000,af::Now()));drain();assert(actor.Find(link)->status==sr::OutputDirectoryStatus::Failed&&!actor.Find(link)->inode&&fs::is_symlink(root/"symlink-parent"));++groups;
 auto expired=owner;expired.generation=7;const auto now=af::Now();assert(!actor.Begin(expired,root,now,now)&&!actor.Observe(expired)&&!physical);++groups;
 // Inject an actual allocation exception AFTER the real child PID exists.
 // It must not escape Begin or retire the physical reservation before waitpid.
 int64_t spawned=0;sr::RunnerOutputDirectories::Budget fault_budget;size_t fault_physical=0;
 sr::RunnerOutputDirectories fault("/proc/self/exe",{{},[&](int64_t pid){spawned=pid;throw std::bad_alloc();}},fault_budget,{&fault_physical,1});
 assert(fault.Begin(owner,root/"post-spawn-fault",af::Now()+5000,af::Now())&&spawned>0&&fault_physical==1&&fault.Physical()==1&&!fault.Find(owner));
 auto queued=owner;queued.generation=2;assert(!fault.Begin(queued,root,af::Now()+5000,af::Now()));
 while(fault.Physical()){fault.Tick(af::Now());if(fault.Physical())poll(nullptr,0,1);}
 const auto* fault_row=fault.Find(owner);assert(fault_row&&fault_row->status==sr::OutputDirectoryStatus::Failed&&fault_row->writer_pid==spawned&&fault_row->writer_signaled&&fault_row->writer_exit==SIGKILL&&!fault_row->ready_receipt&&fault_row->writer_received&&fault_row->received&&!fault_physical);int status=0;assert(::waitpid(static_cast<pid_t>(spawned),&status,WNOHANG)==-1&&errno==ECHILD);++groups;
 assert(failures.empty());fs::remove_all(root);std::cout<<"PASS"<<groups<<" actual native output directory inode/exit/uniqueness/budget/filesystem admission groups\n";
}
