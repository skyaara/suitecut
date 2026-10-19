// SPDX-License-Identifier: MIT
#include "terminal_reporter.h"
#include "runner_report.h"
#include "runner_interrupt.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
namespace sr=suitecut::runner;
struct File {
  int fd=-1;
  explicit File(int value):fd(value){assert(fd>=0);}
  ~File(){if(fd>=0)::close(fd);}
  File(const File&)=delete;File&operator=(const File&)=delete;
};
template<class F>std::string Reject(F&& f){try{f();}catch(const std::exception& error){return error.what();}assert(false);return {};}
std::string Read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void Publish(const std::filesystem::path& path,sr::NativeReportPublication* publication=nullptr){
  sr::report_detail::AtomicFile file(path,publication);file.Put("{\"version\":1,\"globalErrors\":[\"actual \\ud800 \\u0000 error\"]}\n");file.Commit();
}
int main(){
  char directory[]="/tmp/suitecut-terminal-report-test-XXXXXX";const auto made=::mkdtemp(directory);assert(made);const std::filesystem::path root=made;
  unsigned controls=0;
  assert(sr::ParseNativeTerminalReporter("json")==sr::NativeTerminalReporter::JSON);++controls;
  assert(sr::ParseNativeTerminalReporter("none")==sr::NativeTerminalReporter::None);++controls;
  for(const auto bad:{"","JSON","console","json,none"}){Reject([&]{sr::ParseNativeTerminalReporter(bad);});++controls;}
  sr::NativeReportPublication publication;
  Publish(root/"original.json",&publication);publication.Validate();const auto bytes=Read(root/"original.json");assert(publication.Ready()&&publication.Bytes()==bytes.size());++controls;
  {
    File output(::open((root/"regular.stdout").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600));assert(::write(output.fd,"prefix\n",7)==7);
    const auto flags=::fcntl(output.fd,F_GETFL);sr::EmitNativeJSONReporter(publication,{},output.fd);
    assert(Read(root/"regular.stdout")=="prefix\n"+bytes&&::fcntl(output.fd,F_GETFL)==flags);++controls;
  }
  {
    File output(::open((root/"completion-stop.stdout").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600));
    const auto stopped=[&]{struct stat actual{};assert(::fstat(output.fd,&actual)==0);return static_cast<size_t>(actual.st_size)==publication.Bytes();};
    assert(Reject([&]{sr::EmitNativeJSONReporter(publication,stopped,output.fd);}).find("publication interrupted")!=std::string::npos);assert(Read(root/"completion-stop.stdout")==bytes);++controls;
  }
  // Path replacement cannot switch the descriptor to an invented report.
  std::filesystem::rename(root/"original.json",root/"retained-inode.json");{std::ofstream changed(root/"original.json");changed<<"{\"forged\":true}\n";}
  {
    File output(::open((root/"exact-inode.stdout").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600));sr::EmitNativeJSONReporter(publication,{},output.fd);assert(Read(root/"exact-inode.stdout")==bytes);++controls;
  }
  assert(Reject([&]{sr::EmitNativeJSONReporter(publication,{},publication.Descriptor());}).find("aliases")!=std::string::npos);assert(Read(root/"retained-inode.json")==bytes);++controls;
  {
    File output(::open("/dev/full",O_WRONLY|O_CLOEXEC));assert(Reject([&]{sr::EmitNativeJSONReporter(publication,{},output.fd);}).find("stdout write")!=std::string::npos);assert(Read(root/"retained-inode.json")==bytes);++controls;
  }
  {
    int pipe[2];assert(::pipe2(pipe,O_CLOEXEC)==0);::close(pipe[0]);File output(pipe[1]);sigset_t before{},after{};assert(::sigprocmask(SIG_BLOCK,nullptr,&before)==0);
    Reject([&]{sr::EmitNativeJSONReporter(publication,{},output.fd);});assert(::sigprocmask(SIG_BLOCK,nullptr,&after)==0);assert(::sigismember(&before,SIGPIPE)==::sigismember(&after,SIGPIPE));++controls;
  }
  {
    int pipe[2];assert(::pipe2(pipe,O_CLOEXEC|O_NONBLOCK)==0);File reader(pipe[0]),writer(pipe[1]);std::array<char,4096> data{};
    while(::write(writer.fd,data.data(),data.size())>0){}assert(errno==EAGAIN||errno==EWOULDBLOCK);
    const int flags=::fcntl(writer.fd,F_GETFL)&~O_NONBLOCK;assert(::fcntl(writer.fd,F_SETFL,flags)==0);
    sr::NativeInterruptLatch interrupt;assert(::kill(::getpid(),SIGINT)==0);assert(interrupt.Signal()==SIGINT);
    const auto error=Reject([&]{sr::EmitNativeJSONReporter(publication,[&]{return interrupt.Signal()!=0;},writer.fd);});assert(error.find("backpressure after original execution stop")!=std::string::npos&&::fcntl(writer.fd,F_GETFL)==flags);++controls;
  }
  {
    int pair[2];assert(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)==0);File writer(pair[0]),reader(pair[1]);const int flags=::fcntl(writer.fd,F_GETFL);sr::EmitNativeJSONReporter(publication,{},writer.fd);std::string result(bytes.size(),'x');size_t count=0;
    while(count<result.size()){const auto received=::read(reader.fd,result.data()+count,result.size()-count);assert(received>0);count+=static_cast<size_t>(received);}assert(result==bytes&&::fcntl(writer.fd,F_GETFL)==flags);++controls;
  }
  {
    int pair[2];assert(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)==0);File writer(pair[0]);::close(pair[1]);
    assert(Reject([&]{sr::EmitNativeJSONReporter(publication,{},writer.fd);}).find("stdout write")!=std::string::npos);++controls;
  }
  {
    int pipe[2];assert(::pipe2(pipe,O_CLOEXEC|O_NONBLOCK)==0);File reader(pipe[0]),writer(pipe[1]);std::array<char,4096> data{};
    while(::write(writer.fd,data.data(),data.size())>0){}assert(errno==EAGAIN||errno==EWOULDBLOCK);
    const int flags=::fcntl(writer.fd,F_GETFL)&~O_NONBLOCK;assert(::fcntl(writer.fd,F_SETFL,flags)==0);
    const auto begin=std::chrono::steady_clock::now();assert(Reject([&]{sr::EmitNativeJSONReporter(publication,{},writer.fd);}).find("exceeded5000ms")!=std::string::npos);
    const auto elapsed=std::chrono::steady_clock::now()-begin;assert(elapsed>=std::chrono::seconds(4)&&elapsed<std::chrono::seconds(15)&&::fcntl(writer.fd,F_GETFL)==flags);++controls;
  }
  {
    sr::NativeReportPublication abandoned;{sr::report_detail::AtomicFile file(root/"abandoned.json",&abandoned);file.Put("{}");}assert(!abandoned.Ready()&&!std::filesystem::exists(root/"abandoned.json"));++controls;
  }
  {
    sr::NativeReportPublication failed;std::filesystem::create_directory(root/"directory-target");Reject([&]{sr::report_detail::AtomicFile file(root/"directory-target",&failed);file.Put("{}");file.Commit();});assert(!failed.Ready()&&std::filesystem::is_directory(root/"directory-target"));++controls;
  }
  Reject([&]{sr::report_detail::AtomicFile file(root/"occupied.json",&publication);});assert(!std::filesystem::exists(root/"occupied.json"));++controls;
  {
    int descriptor=-1;{sr::NativeReportPublication first;Publish(root/"move.json",&first);descriptor=first.Descriptor();sr::NativeReportPublication second(std::move(first));assert(!first.Ready());second.Validate();sr::NativeReportPublication third;third=std::move(second);assert(!second.Ready()&&third.Descriptor()==descriptor);}assert(::fcntl(descriptor,F_GETFD)==-1&&errno==EBADF);++controls;
  }
  {
    sr::NativeReportPublication changed;Publish(root/"changed.json",&changed);assert(::truncate((root/"changed.json").c_str(),0)==0);assert(Reject([&]{changed.Validate();}).find("length changed")!=std::string::npos);++controls;
  }
  // Native publication handle is absent unless explicitly requested.
  Publish(root/"default.json");assert(Read(root/"default.json").starts_with("{\"version\":1"));++controls;
  std::filesystem::remove_all(root);std::cout<<"PASS "<<controls<<" native terminal report controls\n";
}
