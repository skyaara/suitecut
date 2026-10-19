// SPDX-License-Identifier: MIT
#include "retained_source_receiver.h"
#include <cassert>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
#include <vector>
extern char** environ;
using namespace suitecut::runner;
using Receiver=RetainedSourceReceiver;using Native=RetainedSourceProcess;using Policy=RetainedSourceLeases;using Endpoint=RetainedSourceEndpoint;
static std::string Executable(){std::array<char,4096> bytes{};const auto n=readlink("/proc/self/exe",bytes.data(),bytes.size());assert(n>0&&static_cast<size_t>(n)<bytes.size());return {bytes.data(),static_cast<size_t>(n)};}
static void Send(int fd,char value){assert(send(fd,&value,1,MSG_NOSIGNAL)==1);}
static char Read(int fd){char value=0;pollfd item{fd,POLLIN,0};assert(poll(&item,1,5000)==1);assert(recv(fd,&value,1,0)==1);return value;}
static void Wait(pid_t pid){int status=0;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}
static void Tick(Receiver& receiver,double now){std::array<pollfd,2> fds{};size_t size=0;receiver.ForEachPollDescriptor([&](pollfd fd){assert(size<fds.size());fds[size++]=fd;});assert(size);assert(poll(fds.data(),size,5000)>0);receiver.Tick(now);}
static void Reject(auto action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}assert(rejected);}
struct Launch {uint64_t ticket=0,cookie=0;bool forged=false;};
static void Worker(int channel,int endpoint,bool forged){
 Launch launch;assert(recv(channel,&launch,sizeof(launch),0)==sizeof(launch));assert(launch.ticket&&launch.cookie&&launch.forged==forged);
 if(forged){const auto identity=Native::Observe(getpid());const auto packet=Native::StartupPacket(launch.ticket,identity);assert(send(endpoint,packet.data(),packet.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(packet.size()));Send(channel,'F');assert(Read(channel)=='X');close(endpoint);close(channel);_exit(0);}
 int control[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,control)==0);const auto executable=Executable();
 std::vector<std::string> args{executable,"--native-source","--suitecut-runner-source-fd=7","--suitecut-runner-source-ticket="+std::to_string(launch.ticket),"--suitecut-runner-source-cookie="+std::to_string(launch.cookie)};std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());raw.push_back(nullptr);
 posix_spawn_file_actions_t actions;assert(posix_spawn_file_actions_init(&actions)==0&&posix_spawn_file_actions_adddup2(&actions,endpoint,7)==0&&posix_spawn_file_actions_adddup2(&actions,control[1],8)==0);
 pid_t source=-1;assert(posix_spawn(&source,executable.c_str(),&actions,nullptr,raw.data(),environ)==0&&posix_spawn_file_actions_destroy(&actions)==0);close(control[1]);Send(channel,'S');
 assert(Read(channel)=='G');Send(control[0],'G');assert(Read(control[0])=='B');Send(channel,'B');assert(Read(channel)=='X');Send(control[0],'X');Wait(source);close(control[0]);Send(channel,'R'); // Actual same-parent Source waitpid, not pidfd readiness.
 assert(Read(channel)=='H');close(endpoint);Send(channel,'H');assert(Read(channel)=='E');close(channel);_exit(0);
}
int main(int argc,char** argv){
 if(argc>1&&std::string_view(argv[1])=="--native-source"){
  const auto config=Endpoint::Prepare(argc,argv);Endpoint endpoint(config);assert(endpoint.WriteStartup());assert(Read(8)=='G');assert(endpoint.WriteShutdownReached(0));Send(8,'B');assert(Read(8)=='X');close(8);return 0;
 }
 assert(argc==1);size_t groups=0;
 for(unsigned scenario=0;scenario<3;++scenario){
  const bool late=scenario==1,forged=scenario==2;Policy policy(100);Receiver receiver(policy);assert(receiver.PeerDescriptor()>=16&&receiver.PeerCookie()&&fcntl(receiver.PeerDescriptor(),F_GETFD)&FD_CLOEXEC);++groups;
  Reject([&]{receiver.HostLaunched({1,1,getpid(),Native::ReadStat(getpid()).start});});assert(receiver.PeerDescriptor()>=16);++groups;
  int channel[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,channel)==0);const pid_t host=fork();assert(host>=0);
  if(!host){close(channel[0]);receiver.ForEachPollDescriptor([](pollfd fd){close(fd.fd);});Worker(channel[1],receiver.PeerDescriptor(),forged);}
  close(channel[1]);const auto identity=Native::Observe(host);const Policy::Host owner{1,1,host,identity.start};receiver.HostLaunched(owner);assert(receiver.PeerDescriptor()==-1);Reject([&]{receiver.HostLaunched(owner);});
  const auto ticket=receiver.Reserve({19,7,23,31,41},Native::Observe(getpid()).executable,2,1);assert(ticket&&policy.Outstanding()==1&&!receiver.Reserve({19,8,24,31,41},Native::Observe(getpid()).executable,2,1));++groups;
  const Launch launch{ticket->id,receiver.PeerCookie(),forged};assert(send(channel[0],&launch,sizeof(launch),MSG_NOSIGNAL)==sizeof(launch));assert(Read(channel[0])==(forged?'F':'S'));
  if(forged){Reject([&]{Tick(receiver,1.5);});assert(!receiver.StartupObserved()&&policy.Outstanding()==1);Send(channel[0],'X');Wait(host);Tick(receiver,2);assert(receiver.ChannelDrained());assert(receiver.Retire({owner,1,true,true,true},3)&&policy.Outstanding()==0);close(channel[0]);++groups;continue;}
  Tick(receiver,late?3:1.5);assert(receiver.StartupObserved()&&receiver.AdmissionActive()==!late&&!receiver.ExitObserved()&&!receiver.BrowserReuseSupported());const auto& record=policy.Records().back();assert(record.state==(late?Policy::State::Closing:Policy::State::Active));assert(!policy.IdleReady(*ticket)&&policy.LiveChildren()==1&&policy.Outstanding()==1);++groups;
  Send(channel[0],'G');assert(Read(channel[0])=='B');Tick(receiver,4);assert(receiver.Boundary()&&receiver.Boundary()->ticket==*ticket&&receiver.Boundary()->planned_code==0&&!receiver.ExitObserved()&&!record.close_ack&&!record.idle_ack&&policy.Outstanding()==1);++groups;
  Send(channel[0],'X');assert(Read(channel[0])=='R');Tick(receiver,5);assert(receiver.ExitObserved()&&!receiver.ChannelDrained()&&policy.LiveChildren()==1&&policy.Outstanding()==1);assert(!receiver.Retire({owner,1,true,true,true},5));++groups;
  Send(channel[0],'H');assert(Read(channel[0])=='H');Tick(receiver,6);assert(receiver.ChannelDrained()&&!receiver.Retire({owner,1,false,true,true},6)&&!receiver.Retire({owner,1,true,true,false},6));Send(channel[0],'E');Wait(host);auto wrong=owner;++wrong.start;assert(!receiver.Retire({wrong,1,true,true,true},7));assert(receiver.Retire({owner,1,true,true,true},7)&&receiver.Retired()&&policy.Outstanding()==0&&policy.LiveChildren()==0&&!receiver.Retire({owner,1,true,true,true},7));close(channel[0]);++groups;
 }
 assert(groups==18);std::cout<<"Retained Source receiver: "<<groups<<" native host/fixture/credentials/persistent-pidfd/shutdown-label/known-parent-waitpid controls passed; no CEF or browser idle eligibility claim\n";
}
