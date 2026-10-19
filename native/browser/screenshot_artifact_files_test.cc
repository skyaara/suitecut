// SPDX-License-Identifier: MIT
// Real native files and pass-through syscall gate; no screenshot observations
// or successful write/close receipts are manufactured by this component.
#include "screenshot_artifact_files.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
namespace {
std::mutex mutex;std::condition_variable changed;bool armed=false,entered=false,released=false;int held=-1;
void Arm(){std::lock_guard lock(mutex);armed=true;entered=released=false;held=-1;}
void Await(){std::unique_lock lock(mutex);assert(changed.wait_for(lock,std::chrono::seconds(3),[]{return entered;}));}
void Release(){std::lock_guard lock(mutex);released=true;changed.notify_all();}
void Closed(){errno=0;assert(held>=0&&::fcntl(held,F_GETFD)==-1&&errno==EBADF);}
const std::vector<uint8_t>png{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,252,255,31,0,3,3,2,0,239,163,147,91,0,0,0,0,73,69,78,68,174,66,96,130};
std::vector<uint8_t> Read(const fs::path&p){std::ifstream input(p,std::ios::binary);assert(input);return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>{});}
struct Scope{
 fs::path root;std::atomic_bool canceled=false;std::atomic<double>now=1;suitecut::ScreenshotArtifactPublication result;
 explicit Scope(fs::path value):root(std::move(value)){}
 void Write(bool diff=true){result=suitecut::WriteScreenshotArtifacts(root.string(),png,diff?png:std::vector<uint8_t>{},100,canceled,[this]{return now.load();});}
 std::thread Start(){return std::thread([this]{Write();});}
};
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){
 const int result=__real_fsync(fd);struct stat state{};
 if(result==0&&::fstat(fd,&state)==0&&S_ISREG(state.st_mode)){
  std::unique_lock lock(mutex);if(armed){armed=false;entered=true;held=fd;changed.notify_all();changed.wait(lock,[]{return released;});}
 }
 return result;
}
int main(){
 char pattern[]="/tmp/suitecut-screenshot-native-artifacts-XXXXXX";const auto made=::mkdtemp(pattern);assert(made);const fs::path base=made;unsigned groups=0;
 {Scope s(base/"actual-and-diff");s.Write();assert(s.result.committed&&!s.result.cleanup_failed&&s.result.error.empty());assert(Read(s.result.actual_path)==png&&Read(s.result.diff_path)==png);assert(fs::path(s.result.actual_path).parent_path()==fs::path(s.result.diff_path).parent_path());struct stat state{};assert(::stat(s.result.actual_path.c_str(),&state)==0&&(state.st_mode&0777)==0600);for(const auto&entry:fs::directory_iterator(s.root))assert(!entry.path().filename().string().starts_with('.'));++groups;}
 {Scope s(base/"actual-only");s.Write(false);assert(s.result.committed&&s.result.diff_path.empty()&&Read(s.result.actual_path)==png);++groups;}
 {Scope s(base/"canceled-before");s.canceled=true;s.Write();assert(!s.result.committed&&!fs::exists(s.root)&&s.result.actual_path.empty());++groups;}
 {Scope s(base/"expired-before");s.now=100;s.Write();assert(!s.result.committed&&!fs::exists(s.root)&&s.result.actual_path.empty());++groups;}
 {Scope s(base/"held-cancel");Arm();auto worker=s.Start();Await();assert(::fcntl(held,F_GETFD)>=0);for(const auto&entry:fs::directory_iterator(s.root))assert(entry.path().filename().string().starts_with('.'));s.canceled=true;assert(!s.result.committed);Release();worker.join();Closed();assert(!s.result.committed&&s.result.rollback&&!s.result.cleanup_failed&&s.result.actual_path.empty()&&fs::is_empty(s.root));++groups;}
 {Scope s(base/"held-expiry");Arm();auto worker=s.Start();Await();s.now=100;Release();worker.join();Closed();assert(!s.result.committed&&s.result.rollback&&!s.result.cleanup_failed&&s.result.error.find("deadline")!=std::string::npos&&fs::is_empty(s.root));++groups;}
 {Scope s(base/"parent-replaced");Arm();auto worker=s.Start();Await();const auto moved=base/"actual-moved-parent";fs::rename(s.root,moved);fs::create_directory(s.root);{std::ofstream foreign(s.root/"foreign");foreign<<"actual foreign bytes";}Release();worker.join();Closed();assert(!s.result.committed&&s.result.rollback&&!s.result.cleanup_failed&&s.result.actual_path.empty()&&Read(s.root/"foreign")==std::vector<uint8_t>({'a','c','t','u','a','l',' ','f','o','r','e','i','g','n',' ','b','y','t','e','s'})&&fs::is_empty(moved));++groups;}
 {Scope s(base/"staging-replaced");Arm();auto worker=s.Start();Await();auto iterator=fs::directory_iterator(s.root);assert(iterator!=fs::directory_iterator{});const auto staging=iterator->path(),moved=s.root/"actual-moved-owned";fs::rename(staging,moved);fs::create_directory(staging);{std::ofstream foreign(staging/"foreign");foreign<<"foreign";}Release();worker.join();Closed();assert(!s.result.committed&&s.result.cleanup_failed&&s.result.actual_path.empty()&&fs::exists(staging/"foreign")&&fs::is_empty(moved));assert(std::distance(fs::directory_iterator(s.root),fs::directory_iterator{})==2);++groups;}
 {const auto root=base/"not-a-directory";{std::ofstream file(root);file<<"foreign";}Scope s(root);s.Write();assert(!s.result.committed&&!s.result.error.empty()&&fs::is_regular_file(root));++groups;}
 {Scope a(base/"concurrent"),b(base/"concurrent");auto first=a.Start(),second=b.Start();first.join();second.join();assert(a.result.committed&&b.result.committed&&a.result.actual_path!=b.result.actual_path&&Read(a.result.actual_path)==png&&Read(b.result.actual_path)==png);++groups;}
 {std::atomic_bool cancel=false;const auto root=base/"bad-png";auto result=suitecut::WriteScreenshotArtifacts(root.string(),{},png,100,cancel,[]{return 1;});assert(!result.committed&&!fs::exists(root)&&!result.error.empty());++groups;}
 fs::remove_all(base);std::cout<<"PASS "<<groups<<" real native screenshot artifact files/atomic batch/held fsync close/cancel/deadline/identity rollback controls; no browser or screenshot comparison claim\n";
}
