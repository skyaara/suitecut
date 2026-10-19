// SPDX-License-Identifier: MIT
// Linux-only controlled pass-through syscall gate. Native reads/close execute
// unchanged; the test holds only their real call boundary, never their bytes.
#include "screenshot_inputs.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
namespace {
std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
std::atomic<int> mode=0,closes=0;dev_t device=0;ino_t inode=0;int writer=-1;
bool Target(int fd){struct stat info{};return fstat(fd,&info)==0&&info.st_dev==device&&info.st_ino==inode;}
void Arm(int next){std::lock_guard<std::mutex>lock(mutex);entered=false;released=false;closes=0;mode=next;}
void Await(){std::unique_lock<std::mutex>lock(mutex);assert(changed.wait_for(lock,std::chrono::seconds(2),[]{return entered;}));}
void Release(){std::lock_guard<std::mutex>lock(mutex);released=true;changed.notify_all();}
}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" int __real_close(int);
extern "C" ssize_t __wrap_read(int fd,void*bytes,size_t length){
 const int action=Target(fd)?mode.exchange(0):0;
 if(action==1){std::unique_lock<std::mutex>lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[]{return released;});}
 const auto result=__real_read(fd,bytes,length);
 if(action==2&&result>0){const uint8_t byte=173;assert(pwrite(writer,&byte,1,0)==1);struct timespec times[2]{{1,0},{2,0}};assert(futimens(writer,times)==0);}
 return result;
}
extern "C" int __wrap_close(int fd){const bool target=Target(fd);const int result=__real_close(fd);if(target&&result==0)++closes;return result;}
namespace {
struct Fixture{
 double now=1;std::deque<std::function<void()>>cpu,ui;std::vector<std::string>errors,watches;int results=0;
 std::shared_ptr<suitecut::ScreenshotByteBudget>budget=std::make_shared<suitecut::ScreenshotByteBudget>();std::shared_ptr<suitecut::ScreenshotInputs>inputs;
 suitecut::ScreenshotInputs::Owner owner{2,3,7};
 Fixture(){suitecut::ScreenshotInputs::Hooks hooks;hooks.now=[this]{return now;};hooks.background=[this](auto job){cpu.push_back(std::move(job));return true;};hooks.post=[this](auto job){ui.push_back(std::move(job));return true;};inputs=std::make_shared<suitecut::ScreenshotInputs>(budget,hooks);}
 void Start(const std::string&path,double deadline){std::string error;const auto ticket=inputs->Prepare(owner,"actual-gated-file",true,0,2,deadline,error);assert(error.empty()&&!ticket.token.empty());assert(inputs->Append(owner,ticket.token,true,0,{'{','}'},error));assert(inputs->Watch(owner,ticket.token,9,[this](bool ok,const std::string&error){watches.push_back(ok?"committed":error);},error));assert(inputs->Claim(owner,ticket.token,path,10,[this](auto data,double,const std::string&error){if(data)++results;errors.push_back(error);},error));assert(cpu.size()==1&&inputs->PhysicalCount()==1);}
 std::thread Worker(){auto work=std::move(cpu.front());cpu.pop_front();return std::thread([work=std::move(work)]{work();});}
 void UI(){assert(ui.size()==1);auto work=std::move(ui.front());ui.pop_front();work();work={};assert(!inputs->PhysicalCount()&&!inputs->LogicalCount()&&!budget->Used()&&inputs->ContextReady(2));}
};
}
int main(){
 char path[]="/tmp/suitecut-screenshot-real-read-XXXXXX";writer=mkstemp(path);assert(writer>=0);std::vector<uint8_t>bytes(131077,91);assert(write(writer,bytes.data(),bytes.size())==ssize_t(bytes.size()));assert(fchmod(writer,0444)==0);struct stat info{};assert(fstat(writer,&info)==0);device=info.st_dev;inode=info.st_ino;
 {
  Fixture test;Arm(1);test.Start(path,50);auto worker=test.Worker();Await();assert(test.inputs->PhysicalCount()==1&&test.budget->Used()>0);test.now=50;test.inputs->Tick();assert(test.errors.size()==1&&test.errors[0].find("original deadline")!=std::string::npos&&test.results==0);test.inputs->PageClosed(3);assert(test.inputs->PhysicalCount()==1&&!test.inputs->ContextReady(2)&&closes==0);Release();worker.join();assert(closes==1&&test.inputs->PhysicalCount()==1&&test.budget->Used()>0);test.UI();assert(test.errors.size()==1&&test.results==0);
 }
 {
  Fixture test;Arm(1);test.Start(path,0);auto worker=test.Worker();Await();test.now=1e12;test.inputs->PageClosed(3);assert(test.inputs->PhysicalCount()==1&&!test.inputs->ContextReady(2)&&test.errors.empty()&&test.watches.size()==1);Release();worker.join();assert(closes==1&&test.inputs->PhysicalCount()==1);test.UI();assert(test.errors.empty()&&test.results==0);
 }
 {
  Fixture test;Arm(2);test.Start(path,50);auto worker=test.Worker();worker.join();assert(closes==1&&test.inputs->PhysicalCount()==1&&test.errors.empty());test.UI();assert(test.results==0&&test.errors.size()==1&&test.errors[0].find("changed while reading")!=std::string::npos);
 }
 assert(fstat(writer,&info)==0&&(info.st_mode&0222)==0);inode=0;assert(close(writer)==0);assert(unlink(path)==0);
}
