// SPDX-License-Identifier: MIT
#include "runner_retained_sources.h"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace suitecut::runner;
using Sources=RunnerRetainedSources;using Native=RetainedSourceProcess;using Endpoint=RetainedSourceEndpoint;
static std::string Executable(){std::array<char,4096> bytes{};const auto n=readlink("/proc/self/exe",bytes.data(),bytes.size());assert(n>0&&static_cast<size_t>(n)<bytes.size());return {bytes.data(),static_cast<size_t>(n)};}
static std::vector<std::string> Fields(const std::string& text){std::istringstream stream(text);std::vector<std::string> fields;std::string part;while(stream>>part)fields.push_back(part);return fields;}
static std::string Hex(std::string_view text){constexpr char chars[]="0123456789abcdef";std::string encoded;for(unsigned char byte:text){encoded+=chars[byte>>4];encoded+=chars[byte&15];}return encoded;}
static void Write(int fd,std::string_view text){size_t offset=0;while(offset<text.size()){const auto n=write(fd,text.data()+offset,text.size()-offset);if(n<0&&errno==EINTR)continue;assert(n>0);offset+=static_cast<size_t>(n);}}
static char ReadByte(int fd){char byte=0;ssize_t n;do{n=read(fd,&byte,1);}while(n<0&&errno==EINTR);assert(n==1);return byte;}
static std::string ReadLine(){std::string line;while(line.size()<65536){const auto byte=ReadByte(3);if(byte=='\n')return line;line+=byte;}assert(false);return {};}
static std::string Error(const std::exception_ptr& error){assert(error);try{std::rethrow_exception(error);}catch(const std::runtime_error& caught){return caught.what();}assert(false);return {};}
static int Host(bool startup){
 const auto setup=Fields(ReadLine());assert(setup.size()==8&&setup[0]=="SOURCE_SETUP");
 const auto executable=Executable();Write(3,"SOURCE_REGISTER 1 3 11 7 5 5 13 nativeFixture11 "+Hex(executable)+"\n");const auto registered=Fields(ReadLine());assert(registered.size()==9&&registered[0]=="SOURCE_REGISTERED"&&registered[5]=="1");
 int channel[2];assert(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,channel)==0);const auto child=fork();assert(child>=0);
 if(!child){assert(dup2(4,7)==7&&dup2(channel[1],8)==8);close(3);close(4);const auto ticket="--suitecut-runner-source-ticket="+registered[6],cookie="--suitecut-runner-source-cookie="+registered[8];execl(executable.c_str(),executable.c_str(),startup?"--bad-startup":"--bad-boundary","--suitecut-runner-source-fd=7",ticket.c_str(),cookie.c_str(),nullptr);_exit(127);}
 close(channel[1]);
 if(!startup){Write(3,"SOURCE_WAIT 2 "+registered[6]+" 7 5 13\n");const auto started=Fields(ReadLine());assert(started.size()==9&&started[0]=="SOURCE_STARTED"&&started[6]=="1"&&started[7]==std::to_string(child));Write(channel[0],"B");}
 assert(ReadByte(channel[0])=='H');Write(3,"HELD "+std::to_string(child)+"\n");assert(ReadLine()=="EXIT_HOST");close(channel[0]);return 0;
}
int main(int argc,char** argv){
 if(argc>1&&(std::string_view(argv[1])=="--bad-startup"||std::string_view(argv[1])=="--bad-boundary")){
  const bool startup=std::string_view(argv[1])=="--bad-startup";Endpoint endpoint(Endpoint::Prepare(argc,argv));if(!startup){assert(endpoint.WriteStartup());assert(ReadByte(8)=='B');}
  // Two genuine credentialed datagrams: the ordinary actor observes the first
  // error, then bounded emergency draining must consume the second error too.
  for(unsigned i=0;i<2;++i){constexpr std::string_view bad="SOURCE_INVALID 1\n";assert(send(7,bad.data(),bad.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(bad.size()));}
  int blocked[2];assert(pipe2(blocked,O_CLOEXEC)==0);Write(8,"H");(void)ReadByte(blocked[0]);return 127;
 }
 if(argc==2&&(std::string_view(argv[1])=="--host-startup"||std::string_view(argv[1])=="--host-boundary"))return Host(std::string_view(argv[1])=="--host-startup");
 assert(argc==1);size_t controls=0;
 for(bool startup:{true,false}){
  ProjectDescendants descendants;RetainedSourceLeases policy(DedicatedRealMilliseconds()+10000);DedicatedHostProcess* actual=nullptr;Sources sources(policy,{[&](const std::string& line){assert(actual&&actual->Send(line));}});
  DedicatedHostProcess host(Executable(),{startup?"--host-startup":"--host-boundary"},sources.PeerDescriptor());actual=&host;const auto identity=Native::ReadStat(host.Pid());const RetainedSourceLeases::Host owner{1,1,host.Pid(),identity.start};const auto batch=descendants.Enroll(1,host);sources.HostLaunched(owner,host);sources.Enable();
  const auto now=DedicatedRealMilliseconds();const auto setup=sources.Setup({3,11,7,5,5,13,"nativeFixture11",Scope::Worker,FixtureOptionCatalog::Origin::Callback,now+5000},now);assert(setup&&host.Send(*setup));++controls;
  // Filling optional sidecar history never overwrites the actual registered
  // callback below and never rejects ordinary fixture admission at the bound.
  for(uint64_t token=8;token<71;++token)assert(sources.Setup({3,token,token,5,5,13,"nativeFixtureOther",Scope::Worker,FixtureOptionCatalog::Origin::Callback,now+5000},now));
  assert(!sources.Setup({3,71,71,5,5,13,"nativeFixtureOther",Scope::Worker,FixtureOptionCatalog::Origin::Callback,now+5000},now));++controls;
  bool rejected=false;try{(void)sources.Setup({3,11,7,5,5,13,"nativeFixture11",Scope::Worker,FixtureOptionCatalog::Origin::Callback,now+5000},now);}catch(const std::runtime_error&){rejected=true;}assert(rejected);++controls;
  std::exception_ptr original;pid_t child=-1;
  const auto pump=[&](auto predicate){const auto end=DedicatedRealMilliseconds()+5000;while(!predicate()){
   for(const auto& line:host.Poll(0)){const auto fields=Fields(line);try{if(sources.Packet(fields,DedicatedRealMilliseconds(),false))continue;}catch(...){assert(!original);original=std::current_exception();continue;}assert(fields.size()==2&&fields[0]=="HELD");child=static_cast<pid_t>(std::stoll(fields[1]));}
   if(original)sources.Drain(DedicatedRealMilliseconds());else try{sources.Tick(DedicatedRealMilliseconds());}catch(...){original=std::current_exception();}
   descendants.Tick(DedicatedRealMilliseconds());assert(DedicatedRealMilliseconds()<end);std::array<pollfd,5> fds{};size_t count=0;for(auto fd:host.PollDescriptors())fds[count++]=fd;sources.ForEachPollDescriptor([&](pollfd fd){assert(count<fds.size());fds[count++]=fd;});const auto result=poll(fds.data(),count,10);assert(result>=0||errno==EINTR);
  }};
  pump([&]{return child>0&&original&&sources.DrainFailure();});assert(Error(original)==(startup?"Native Source startup packet malformed":"Native Source shutdown sender/packet invalid"));++controls;
  const auto failure=sources.DrainFailure();assert(Error(failure)==Error(original));++controls;
  assert(sources.Receiver().StartupObserved()!=startup&&(startup||sources.Receiver().ProcessIdentity()->pid==child));++controls;
  assert(kill(child,0)==0&&!host.Exited()&&sources.Physical()==1&&policy.Outstanding()==1);++controls;
  assert(!sources.RetireDrained({owner,batch,true,true,true},DedicatedRealMilliseconds()));++controls;
  assert(!sources.Receiver().Boundary()&&!policy.Records().back().close_ack&&!policy.Records().back().idle_ack);++controls;
  assert(host.Send("EXIT_HOST"));pump([&]{return host.Exited().has_value();});assert(!host.Exited()->signaled&&host.Exited()->code==0&&kill(child,0)==0&&sources.Physical()==1);++controls;
  assert(descendants.Arrive(batch,1,host,true,DedicatedRealMilliseconds(),0));sources.DomainArrived(descendants,batch);assert(!sources.RetireDrained({owner,batch,true,true,true},DedicatedRealMilliseconds()));++controls;
  pump([&]{return !descendants.Pending(batch)&&sources.ChannelDrained()&&(startup||sources.Receiver().ExitObserved());});const auto errors=descendants.TakeErrors();assert(errors.size()==1&&errors[0].find("PID "+std::to_string(child)+" signal 9")!=std::string::npos);++controls;
  assert(sources.RetireDrained({owner,batch,true,true,true},DedicatedRealMilliseconds())&&sources.Physical()==0&&policy.Outstanding()==0&&kill(child,0)==-1&&errno==ESRCH);++controls;
  sources.Drain(DedicatedRealMilliseconds());assert(sources.DrainFailure()==failure&&Error(original)==Error(failure)&&host.Stdout().empty()&&host.Stderr().empty());++controls;
 }
 assert(controls==28);std::cout<<"Retained Source error drain: "<<controls<<" controls passed (24 actual malformed datagram/held child/original error/pidfd/host waitpid/sole-domain receipts, 4 bounded sidecar-history policy controls); no CEF idle eligibility\n";
}
