// SPDX-License-Identifier: MIT
#include "runner_retained_sources.h"
#include "project_descendants.h"
#include <cassert>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
using namespace suitecut::runner;
using Sources=RunnerRetainedSources;using Native=RetainedSourceProcess;using Endpoint=RetainedSourceEndpoint;
static std::string Executable(){std::array<char,4096> bytes{};const auto n=readlink("/proc/self/exe",bytes.data(),bytes.size());assert(n>0&&static_cast<size_t>(n)<bytes.size());return {bytes.data(),static_cast<size_t>(n)};}
static std::vector<std::string> Fields(const std::string& text){std::istringstream stream(text);std::vector<std::string> fields;std::string part;while(stream>>part)fields.push_back(part);return fields;}
static std::string Hex(std::string_view text){constexpr char chars[]="0123456789abcdef";std::string encoded;for(unsigned char byte:text){encoded+=chars[byte>>4];encoded+=chars[byte&15];}return encoded;}
static void Write(int fd,std::string text){size_t offset=0;while(offset<text.size()){const auto n=write(fd,text.data()+offset,text.size()-offset);if(n<0&&errno==EINTR)continue;assert(n>0);offset+=static_cast<size_t>(n);}}
static std::string ReadLine(){std::string line;char byte=0;while(line.size()<65536){ssize_t n;do{n=read(3,&byte,1);}while(n<0&&errno==EINTR);assert(n==1);if(byte=='\n')return line;line+=byte;}assert(false);return {};}
static char ReadByte(int fd){char value=0;ssize_t n;do{n=read(fd,&value,1);}while(n<0&&errno==EINTR);assert(n==1);return value;}
static void Wait(pid_t child){int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}
static void Rejected(auto action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}assert(rejected);}
static int Host(bool cancel){
 assert(fcntl(4,F_GETFD)>=0);const auto setup=Fields(ReadLine());assert(setup==std::vector<std::string>({"SOURCE_SETUP","3","11","7","5","5","13","nativeFixture11"}));
 const auto executable=Executable();Write(3,"SOURCE_REGISTER 1 3 11 7 5 5 13 nativeFixture11 "+Hex(executable)+"\n");const auto registered=Fields(ReadLine());assert(registered.size()==9&&registered[0]=="SOURCE_REGISTERED"&&registered[5]=="1"&&registered[7]=="4");
 if(cancel){Write(3,"SOURCE_CANCEL 2 "+registered[6]+" 7 5 13\n");const auto cancelled=Fields(ReadLine());assert(cancelled.size()==6&&cancelled[0]=="SOURCE_CANCELLED"&&cancelled[2]==registered[6]);Write(3,"CANCELLED\n");assert(ReadLine()=="EXIT_HOST");return 0;}
 int channel[2];assert(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,channel)==0);const pid_t child=fork();assert(child>=0);
 if(!child){assert(dup2(4,7)==7&&dup2(channel[1],8)==8);close(3);close(4);const auto ticket="--suitecut-runner-source-ticket="+registered[6],cookie="--suitecut-runner-source-cookie="+registered[8];execl(executable.c_str(),executable.c_str(),"--source","--suitecut-runner-source-fd=7",ticket.c_str(),cookie.c_str(),nullptr);_exit(127);}
 close(channel[1]);Write(3,"SOURCE_WAIT 2 "+registered[6]+" 7 5 13\n");const auto started=Fields(ReadLine());assert(started.size()==9&&started[0]=="SOURCE_STARTED"&&started[6]=="1"&&started[7]==std::to_string(child));Write(3,"ATTESTED "+std::to_string(child)+"\n");
 auto command=ReadLine();
 if(command=="PROBE"){Write(3,"SOURCE_REGISTER 3 3 11 7 5 5 13 nativeFixture11 "+Hex(executable)+"\n");const auto fresh=Fields(ReadLine());assert(fresh.size()==9&&fresh[0]=="SOURCE_REGISTERED"&&fresh[5]=="0"&&fresh[6]=="0"&&fresh[7]=="0"&&fresh[8]=="0");Write(3,"FRESH\n");command=ReadLine();}
 if(command=="ORPHAN"){Write(channel[0],"H");assert(ReadByte(channel[0])=='H');Write(3,"HELD\n");assert(ReadLine()=="EXIT_HOST");close(channel[0]);return 0;}
 assert(command=="FINISH");Write(channel[0],"G");assert(ReadByte(channel[0])=='B');Write(3,"BOUNDARY\n");assert(ReadLine()=="EXIT_SOURCE");Write(channel[0],"X");Wait(child);close(channel[0]);Write(3,"REAPED\n");assert(ReadLine()=="EXIT_HOST");return 0;
}
int main(int argc,char** argv){
 if(argc>1&&std::string_view(argv[1])=="--source"){
  Endpoint endpoint(Endpoint::Prepare(argc,argv));assert(endpoint.WriteStartup());const char command=ReadByte(8);
  if(command=='H'){int blocked[2];assert(pipe2(blocked,O_CLOEXEC)==0);Write(8,"H");(void)ReadByte(blocked[0]);return 127;}
  assert(command=='G'&&endpoint.WriteShutdownReached(0));Write(8,"B");assert(ReadByte(8)=='X');return 0;
 }
 if(argc==2&&(std::string_view(argv[1])=="--host"||std::string_view(argv[1])=="--host-cancel"))return Host(std::string_view(argv[1])=="--host-cancel");
 assert(argc==1);size_t controls=0;
 for(unsigned scenario=0;scenario<3;++scenario){
  const bool orphan=scenario==1,cancel=scenario==2;
  ProjectDescendants descendants;RetainedSourceLeases policy(DedicatedRealMilliseconds()+10000);DedicatedHostProcess* actual=nullptr;
  Sources sources(policy,{[&](const std::string& line){assert(actual&&actual->Send(line));}});
  DedicatedHostProcess host(Executable(),{cancel?"--host-cancel":"--host"},sources.PeerDescriptor());actual=&host;const auto identity=Native::ReadStat(host.Pid());const RetainedSourceLeases::Host owner{1,1,host.Pid(),identity.start};const auto batch=descendants.Enroll(1,host);
  sources.HostLaunched(owner,host);sources.Enable();
  const auto now=DedicatedRealMilliseconds();Sources::Selection selection{3,11,7,5,5,13,"nativeFixture11",Scope::Worker,FixtureOptionCatalog::Origin::Callback,now+5000};
  for(auto origin:{FixtureOptionCatalog::Origin::DeclaredLiteral,FixtureOptionCatalog::Origin::ConfiguredLiteral}){auto literal=selection;literal.origin=origin;assert(!sources.Setup(literal,now));++controls;}
  auto hook=selection;hook.test=0;assert(!sources.Setup(hook,now));++controls;
  const auto sidecar=sources.Setup(selection,now);assert(sidecar&&host.Send(*sidecar));++controls;
  std::vector<std::string> events;pid_t child=-1;
  const auto pump=[&](auto predicate){const auto end=DedicatedRealMilliseconds()+5000;while(!predicate()){
    for(const auto& line:host.Poll(0)){const auto fields=Fields(line);assert(!fields.empty());if(sources.Packet(fields,DedicatedRealMilliseconds(),false))continue;if(fields[0]=="ATTESTED"){assert(fields.size()==2);child=static_cast<pid_t>(std::stoll(fields[1]));}else events.push_back(line);}
    sources.Tick(DedicatedRealMilliseconds());descendants.Tick(DedicatedRealMilliseconds());
    assert(DedicatedRealMilliseconds()<end);std::array<pollfd,5> fds{};size_t count=0;for(auto fd:host.PollDescriptors())fds[count++]=fd;sources.ForEachPollDescriptor([&](pollfd fd){assert(count<fds.size());fds[count++]=fd;});const auto result=poll(fds.data(),count,10);assert(result>=0||errno==EINTR);
  }};
  if(cancel){
    pump([&]{return std::find(events.begin(),events.end(),"CANCELLED")!=events.end();});assert(sources.Physical()==1&&!sources.Receiver().StartupObserved()&&policy.Records().back().state==RetainedSourceLeases::State::Closing);++controls;
  }else{
  pump([&]{return child>0;});assert(sources.Physical()==1&&sources.Receiver().StartupObserved()&&sources.Receiver().ProcessIdentity()->pid==child&&!sources.Receiver().ExitObserved()&&!sources.Receiver().BrowserReuseSupported());++controls;
  assert(!sources.Retire({owner,batch,true,true,true},DedicatedRealMilliseconds()));Rejected([&]{sources.DomainArrived(descendants,batch);});++controls;
  assert(host.Send("PROBE"));pump([&]{return std::find(events.begin(),events.end(),"FRESH")!=events.end();});assert(policy.Outstanding()==1&&policy.Records().size()==1);++controls;
  Rejected([&]{sources.Packet(Fields("SOURCE_REGISTER 4 3 12 7 5 5 13 nativeFixture11 "+Hex(Executable())),DedicatedRealMilliseconds(),false);});assert(policy.Outstanding()==1);++controls;
  if(orphan){assert(host.Send("ORPHAN"));pump([&]{return std::find(events.begin(),events.end(),"HELD")!=events.end();});assert(!sources.Receiver().ExitObserved()&&!sources.Receiver().Boundary());++controls;}
  else{assert(host.Send("FINISH"));pump([&]{return std::find(events.begin(),events.end(),"BOUNDARY")!=events.end()&&sources.Receiver().Boundary();});assert(!sources.Receiver().ExitObserved()&&!policy.Records().back().close_ack&&!policy.Records().back().idle_ack);++controls;assert(host.Send("EXIT_SOURCE"));pump([&]{return std::find(events.begin(),events.end(),"REAPED")!=events.end()&&sources.Receiver().ExitObserved();});assert(sources.Physical()==1&&!host.Exited());++controls;}
  }
  assert(host.Send("EXIT_HOST"));pump([&]{return host.Exited().has_value();});assert(!host.Exited()->signaled&&host.Exited()->code==0&&sources.Physical()==1);++controls;
  assert(descendants.Arrive(batch,1,host,true,DedicatedRealMilliseconds(),0));sources.DomainArrived(descendants,batch);assert(!sources.Retire({owner,batch,true,true,true},DedicatedRealMilliseconds()));
  pump([&]{return !descendants.Pending(batch)&&sources.Receiver().ChannelDrained()&&(cancel||sources.Receiver().ExitObserved());});const auto errors=descendants.TakeErrors();if(orphan){assert(errors.size()==1&&errors[0].find("PID "+std::to_string(child)+" signal 9")!=std::string::npos);}else assert(errors.empty());++controls;
  assert(sources.Retire({owner,batch,true,true,true},DedicatedRealMilliseconds())&&sources.Physical()==0&&policy.Outstanding()==0&&!sources.Receiver().BrowserReuseSupported());assert(host.Stderr().empty()&&host.Stdout().empty());if(!cancel)assert(kill(child,0)==-1&&errno==ESRCH);++controls;
 }
 assert(controls==33);std::cout<<"Retained Source native host adapter: "<<controls<<" actual fd4/callback-origin/credential/startup/shutdown-label/pidfd/parent-waitpid/sole-subreaper batch controls passed; CEF eligibility disabled\n";

 return 0;
}
