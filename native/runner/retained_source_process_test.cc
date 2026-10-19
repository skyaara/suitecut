// SPDX-License-Identifier: MIT
#include "retained_source_process.h"
#include <cassert>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
using namespace suitecut::runner;
using Native=RetainedSourceProcess;using Policy=RetainedSourceLeases;
constexpr int kChannel=7;
static void Send(int fd,const std::string& bytes){assert(send(fd,bytes.data(),bytes.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(bytes.size()));}
static void Ready(int fd){pollfd item{fd,POLLIN,0};assert(poll(&item,1,5000)==1&&item.revents&POLLIN);}
static void Wait(pid_t pid){int status=0;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}
static std::string Executable(){std::array<char,4096> bytes{};const auto size=readlink("/proc/self/exe",bytes.data(),bytes.size());assert(size>0&&static_cast<size_t>(size)<bytes.size());return {bytes.data(),static_cast<size_t>(size)};}
static void Rejected(auto action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}assert(rejected);}
struct Child {
 int fd=-1;pid_t pid=-1;Native::PidHandle* handle=nullptr;
 Child(uint64_t ticket){int pair[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);fd=pair[0];Native::ReceiveCredentials(fd);
  const auto executable=Executable(),number=std::to_string(ticket);posix_spawn_file_actions_t actions;assert(posix_spawn_file_actions_init(&actions)==0);
  assert(posix_spawn_file_actions_adddup2(&actions,pair[1],kChannel)==0);if(fd!=kChannel)assert(posix_spawn_file_actions_addclose(&actions,fd)==0);if(pair[1]!=kChannel)assert(posix_spawn_file_actions_addclose(&actions,pair[1])==0);
  std::array<char*,4> argv{const_cast<char*>(executable.c_str()),const_cast<char*>("--native-child"),const_cast<char*>(number.c_str()),nullptr};assert(posix_spawn(&pid,executable.c_str(),&actions,nullptr,argv.data(),environ)==0);assert(posix_spawn_file_actions_destroy(&actions)==0);close(pair[1]);handle=new Native::PidHandle(pid);
 }
 ~Child(){assert(pid==-1);delete handle;if(fd>=0)close(fd);}
 void Release(){assert(pid>0&&!handle->Exited());Send(fd,"G");// Exact child owner performs waitpid, not the observer.
  Wait(pid);assert(handle->Exited());pid=-1;
 }
};
int main(int argc,char** argv){
 if(argc==2&&std::string_view(argv[1])=="--descriptor-closed"){assert(fcntl(kChannel,F_GETFD)==-1&&errno==EBADF);return 0;}
 if(argc==3&&std::string_view(argv[1])=="--native-child"){
  const auto flags=fcntl(kChannel,F_GETFD);assert(flags>=0&&fcntl(kChannel,F_SETFD,flags|FD_CLOEXEC)==0);
  // A genuine exec descendant cannot inherit the registered endpoint. This is
  // a native component process, not a CEF renderer or Source readiness proof.
  const auto executable=Executable();std::array<char*,3> args{const_cast<char*>(executable.c_str()),const_cast<char*>("--descriptor-closed"),nullptr};pid_t child=-1;assert(posix_spawn(&child,executable.c_str(),nullptr,nullptr,args.data(),environ)==0);Wait(child);
  Send(kChannel,Native::StartupPacket(std::stoull(argv[2]),Native::Observe(getpid())));Ready(kChannel);char released=0;assert(recv(kChannel,&released,1,0)==1&&released=='G');return 0;
 }
 assert(argc==1);size_t groups=0;const auto process=Native::Observe(getpid());Policy::Owner owner{{1,1,process.pid,process.start},{1,2,3,4,5}};
 // Real native credentials, exec identity and parent/start identity establish
 // only the controlled child registration. No browser/idle/close is claimed.
 {Policy pool;const auto ticket=*pool.Reserve(owner,process.executable,100,0);Child child(ticket.id);Ready(child.fd);const auto receipt=Native::ReceiveStartup(child.fd,ticket,process.executable);assert(receipt&&receipt->credential_pid==child.pid&&receipt->process.parent==getpid()&&pool.Started(*receipt,1));assert(pool.LiveChildren()==1&&!pool.IdleReady(ticket));child.Release();assert(pool.ObservedExit(ticket,receipt->process,2)&&pool.LiveChildren()==1&&!pool.IdleReady(ticket));++groups;}
 {int pair[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);Native::ReceiveCredentials(pair[0]);Policy::Ticket ticket{1,owner};assert(!Native::ReceiveStartup(pair[0],ticket,process.executable));Send(pair[1],Native::StartupPacket(ticket.id,process));Ready(pair[0]);Rejected([&]{Native::ReceiveStartup(pair[0],ticket,process.executable);});close(pair[0]);close(pair[1]);++groups;}
 {Policy::Ticket ticket{1,owner};Child child(ticket.id);Ready(child.fd);++ticket.owner.host.start;Rejected([&]{Native::ReceiveStartup(child.fd,ticket,process.executable);});child.Release();++groups;}
 {Policy::Ticket ticket{1,owner};Child child(ticket.id);Ready(child.fd);auto foreign=process.executable;++foreign.inode;Rejected([&]{Native::ReceiveStartup(child.fd,ticket,foreign);});child.Release();++groups;}
 {Policy::Ticket ticket{1,owner};Child child(ticket.id);Ready(child.fd);++ticket.id;Rejected([&]{Native::ReceiveStartup(child.fd,ticket,process.executable);});child.Release();++groups;}
 {int pair[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);Native::ReceiveCredentials(pair[0]);Send(pair[1],std::string(1024,'x'));Ready(pair[0]);Rejected([&]{Native::ReceiveStartup(pair[0],{1,owner},process.executable);});Send(pair[1],"SOURCE_START 1 01 2 3 4\n");Rejected([&]{Native::ReceiveStartup(pair[0],{1,owner},process.executable);});close(pair[1]);Rejected([&]{Native::ReceiveStartup(pair[0],{1,owner},process.executable);});close(pair[0]);++groups;}
 {int pair[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);Native::ReceiveCredentials(pair[0]);
  const auto descriptors=[](){size_t count=0;for(const auto& entry:std::filesystem::directory_iterator("/proc/self/fd")){(void)entry;++count;}return count;};const auto before=descriptors();
  const std::string bytes=Native::StartupPacket(1,process);iovec io{const_cast<char*>(bytes.data()),bytes.size()};alignas(cmsghdr) std::array<char,CMSG_SPACE(sizeof(int))> ancillary{};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=ancillary.data();message.msg_controllen=ancillary.size();auto* header=CMSG_FIRSTHDR(&message);header->cmsg_level=SOL_SOCKET;header->cmsg_type=SCM_RIGHTS;header->cmsg_len=CMSG_LEN(sizeof(int));__builtin_memcpy(CMSG_DATA(header),&pair[1],sizeof(int));assert(sendmsg(pair[1],&message,MSG_NOSIGNAL)==static_cast<ssize_t>(bytes.size()));Ready(pair[0]);Rejected([&]{Native::ReceiveStartup(pair[0],{1,owner},process.executable);});assert(descriptors()==before);close(pair[0]);close(pair[1]);++groups;}
 {int pair[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);Native::ReceiveCredentials(pair[0]);Send(pair[1],"" );Rejected([&]{Native::ReceiveReceipt(pair[0]);});assert(!Native::ReceiveReceipt(pair[0]));close(pair[1]);const auto eof=Native::ReceiveReceipt(pair[0]);assert(eof&&eof->eof);close(pair[0]);++groups;}
 assert(groups==8);std::cout<<"Retained Source kernel component: "<<groups<<" actual credential/exec/parent/start/EOF-rejection controls passed; native child exact waitpid and CLOEXEC exec descendant confirmed; no CEF/Source idle claim\n";
}
