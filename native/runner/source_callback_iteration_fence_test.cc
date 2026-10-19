// SPDX-License-Identifier: MIT
// Actual owned-host/native-source transport controls. This component exercises
// adapter receipt/lifetime authority; its explicit Source producer is not CEF.
#include "runner_retained_sources.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <sys/wait.h>
using namespace suitecut::runner;
using Sources=RunnerRetainedSources;using Native=RetainedSourceProcess;using Endpoint=RetainedSourceEndpoint;using Wire=RetainedSourceWorkWire;
static std::string Executable(){std::array<char,4096>b{};const auto n=readlink("/proc/self/exe",b.data(),b.size());assert(n>0&&static_cast<size_t>(n)<b.size());return {b.data(),static_cast<size_t>(n)};}
static void Write(int fd,std::string_view s){size_t at=0;while(at<s.size()){const auto n=write(fd,s.data()+at,s.size()-at);if(n<0&&errno==EINTR)continue;assert(n>0);at+=static_cast<size_t>(n);}}
static std::string ReadLine(int fd=3){std::string s;char c;while(s.size()<65536){const auto n=read(fd,&c,1);if(n<0&&errno==EINTR)continue;assert(n==1);if(c=='\n')return s;s+=c;}assert(false);return {};}
static std::vector<std::string> Fields(const std::string&s){std::istringstream in(s);std::vector<std::string> f;std::string v;while(in>>v)f.push_back(v);return f;}
static std::string Hex(std::string_view s){std::string h;for(unsigned char c:s){h+="0123456789abcdef"[c>>4];h+="0123456789abcdef"[c&15];}return h;}
static void Reject(auto f){bool threw=false;try{f();}catch(const std::runtime_error&){threw=true;}assert(threw);}
static int Source(int argc,char**argv,bool refusal){
 Endpoint endpoint(Endpoint::Prepare(argc,argv));assert(endpoint.WriteStartup()&&endpoint.PrepareAcceptedWork());
 suitecut::AcceptedReaderWork reader;suitecut::AcceptedWriterWork writer;std::optional<Wire::Cuts> cuts;
 for(;;){
  std::array<pollfd,2> fds{{{endpoint.WorkDescriptor(),POLLIN,0},{8,POLLIN,0}}};assert(poll(fds.data(),fds.size(),1000)>=0);
  if(fds[1].revents&POLLIN){assert(ReadLine(8)=="EXIT");assert(endpoint.WriteShutdownReached(0));return 0;}
  if(auto m=endpoint.ReceiveAcceptedWork()){
   if(m->kind==Wire::Kind::Install){assert(endpoint.WriteAcceptedCapability());continue;}
   if(m->kind==Wire::Kind::Begin){assert(!cuts);if(refusal){assert(endpoint.WriteAcceptedUnsupported(m->challenge.nonce,7));Write(8,Wire::Unsupported(m->challenge.ticket,m->challenge.nonce,7));continue;}cuts=Wire::Cuts{m->challenge.ticket,m->challenge.nonce,reader.Cut(),writer.Cut()};assert(endpoint.WriteAcceptedCuts(Wire::Kind::Cut,*cuts));Write(8,Wire::Encode(Wire::Kind::Cut,*cuts));continue;}
   if(m->kind==Wire::Kind::Observed){assert(cuts&&*cuts==m->cuts&&reader.Through(cuts->reader)&&writer.Through(cuts->writer));assert(endpoint.WriteAcceptedCuts(Wire::Kind::Acknowledged,*cuts));continue;}
   assert(m->kind==Wire::Kind::Resume&&cuts&&m->resume.nonce==cuts->nonce);assert(reader.ReleaseCut(cuts->reader)&&writer.ReleaseCut(cuts->writer));assert(endpoint.WriteAcceptedResume(m->resume));cuts.reset();
  }
 }
}
static int Host(bool refusal,bool reuse){
 const auto setup=Fields(ReadLine());assert(setup.size()==9&&setup[0]=="SOURCE_SETUP"&&setup.back()=="test");
 Write(3,"SOURCE_REGISTER 1 3 11 7 5 5 13 nativeFixture11 "+Hex(Executable())+"\n");const auto registered=Fields(ReadLine());assert(registered.size()==9&&registered[5]=="1");
 int pipe[2];assert(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pipe)==0);const auto child=fork();assert(child>=0);
 if(!child){assert(dup2(4,7)==7&&dup2(pipe[1],8)==8);close(3);close(4);const auto ticket="--suitecut-runner-source-ticket="+registered[6],cookie="--suitecut-runner-source-cookie="+registered[8];execl(Executable().c_str(),Executable().c_str(),refusal?"--source-refuse":"--source","--suitecut-runner-source-fd=7",ticket.c_str(),cookie.c_str(),nullptr);_exit(127);}
 close(pipe[1]);Write(3,"SOURCE_WAIT 2 "+registered[6]+" 7 5 13\n");const auto started=Fields(ReadLine());assert(started.size()==9&&started[6]=="1");Write(3,"ATTESTED "+std::to_string(child)+"\n");std::vector<std::string> begin,resume;std::optional<Wire::Cuts> observed;
 for(;;){
  const auto line=ReadLine();const auto f=Fields(line);assert(!f.empty());
  if(f[0]=="SOURCE_WORK_BEGIN"){
   assert(reuse?f.size()==6:(f.size()==7&&f[6]=="callbackIteration"));begin=f;const auto actual=Wire::Decode(ReadLine(pipe[0])+"\n");if(refusal){assert(actual.kind==Wire::Kind::Unsupported&&actual.ticket==std::stoull(f[1])&&actual.nonce==std::stoull(f[2]));Write(3,"NATIVE_REFUSAL_SEEN\n");continue;}observed=actual.cuts;assert(observed->ticket==std::stoull(f[1])&&observed->nonce==std::stoull(f[2]));Write(3,"BEGIN_SEEN\n");
  }else if(f[0]=="SOURCE_WORK_EXPECT"){
   assert(observed&&f.size()==9);assert(f[1]==begin[1]&&f[2]==begin[2]);std::string packet="SOURCE_WORK_OBSERVED";for(size_t i=1;i<f.size();++i)packet+=" "+f[i];Write(3,packet+"\n");
  }else if(f[0]=="SOURCE_WORK_ACK"){assert(f.size()==9);Write(3,"SDK_RAW_AND_ACK_SEEN\n");
  }else if(line=="READY"){assert(!begin.empty());Write(3,"SOURCE_WORK_READY "+begin[1]+" "+begin[2]+"\n");
  }else if(f[0]=="SOURCE_WORK_RESUME"){assert(f.size()==4);resume=f;Write(3,"SDK_RESUME_PENDING\n");
  }else if(line=="HOST_ACK"){assert(!resume.empty());Write(3,"SOURCE_WORK_HOST_RESUMED "+resume[1]+" "+resume[2]+" "+resume[3]+"\n");
  }else if(f[0]=="SOURCE_WORK_REFUSE"){Write(3,"REFUSED\n");
  }else{assert(line=="EXIT_HOST");Write(pipe[0],"EXIT\n");int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);Write(3,"SOURCE_REAPED "+std::to_string(child)+" "+std::to_string(status)+"\n");close(pipe[0]);return 0;}
 }
}
int main(int argc,char**argv){
 if(argc>1&&(std::string_view(argv[1])=="--source"||std::string_view(argv[1])=="--source-refuse"))return Source(argc,argv,std::string_view(argv[1])=="--source-refuse");
 if(argc==2&&(std::string_view(argv[1])=="--host"||std::string_view(argv[1])=="--host-refuse"||std::string_view(argv[1])=="--host-reuse"))return Host(std::string_view(argv[1])=="--host-refuse",std::string_view(argv[1])=="--host-reuse");
 assert(argc==1);size_t controls=0;
 for(unsigned scenario=0;scenario<7;++scenario){
  ProjectDescendants descendants;RetainedSourceLeases policy;DedicatedHostProcess* actual=nullptr;bool throws=false,partial=false,valid=true;
  Sources::CallbackIterationOwner owner{{5,17,0,5,17,1,0,"body"},21,0,32,41,1};
  Sources sources(policy,{[&](const std::string& line){if(throws&&line.starts_with("SOURCE_WORK_BEGIN")){if(partial)assert(actual->Send(line));throw std::runtime_error("actual adapter host send failure");}assert(actual&&actual->Send(line));},[&](const auto& candidate){return valid&&candidate==owner;}});
  DedicatedHostProcess host(Executable(),{scenario==5?"--host-refuse":scenario==6?"--host-reuse":"--host"},sources.PeerDescriptor());actual=&host;owner.callback.pid=host.Pid();const auto stat=Native::ReadStat(host.Pid());const RetainedSourceLeases::Host identity{1,1,host.Pid(),stat.start};const auto batch=descendants.Enroll(1,host);sources.HostLaunched(identity,host);sources.Enable(2);
  const auto setup=sources.Setup({3,11,7,5,5,13,"nativeFixture11",Scope::Worker,FixtureOptionCatalog::Origin::Callback,DedicatedRealMilliseconds()+15000},DedicatedRealMilliseconds());assert(setup&&host.Send(*setup));std::vector<std::string> events;pid_t child=0;
  const auto pump=[&](auto done){const auto end=DedicatedRealMilliseconds()+15000;while(!done()){
   for(const auto&line:host.Poll(0)){const auto f=Fields(line);if(sources.Packet(f,DedicatedRealMilliseconds(),false))continue;if(f[0]=="ATTESTED")child=std::stoll(f[1]);else events.push_back(line);}
   sources.Tick(DedicatedRealMilliseconds());descendants.Tick(DedicatedRealMilliseconds());if(DedicatedRealMilliseconds()>=end){std::cerr<<"scenario "<<scenario<<" owner invocation "<<owner.invocation<<" events ";for(const auto&e:events)std::cerr<<e<<";";if(sources.Receiver().Work()){const auto&w=*sources.Receiver().Work();std::cerr<<" work observed="<<w.observed<<" ack="<<w.acknowledged<<" resumed="<<w.resumed<<" canceled="<<w.canceled;}std::cerr<<"\n";assert(false);}std::array<pollfd,8>fds{};size_t n=0;for(auto fd:host.PollDescriptors())fds[n++]=fd;sources.ForEachPollDescriptor([&](auto fd){assert(n<fds.size());fds[n++]=fd;});const auto r=poll(fds.data(),n,2);assert(r>=0||errno==EINTR);
  }};
  pump([&]{return child>0;});const auto source_identity=Native::ReadStat(child);
  const auto emit=[&](std::string_view phase){if(const auto path=std::getenv("SOURCE_FENCE_EVIDENCE")){std::ofstream file(path,std::ios::app);file<<"{\"phase\":\""<<phase<<"\",\"scenario\":"<<scenario<<",\"nativeOwner\":"<<getpid()<<",\"hostPid\":"<<host.Pid()<<",\"hostStart\":"<<identity.start<<",\"sourcePid\":"<<child<<",\"sourceStart\":"<<source_identity.start<<"}\n";assert(file.good());}};emit("actual-admission");assert(sources.Physical()==1&&!sources.Receiver().BrowserReuseSupported());++controls;
  auto foreign=owner;++foreign.callback.pid;assert(!sources.BeginCallbackIterationFence(foreign,DedicatedRealMilliseconds()+1000,DedicatedRealMilliseconds()));++controls;
  valid=false;assert(!sources.BeginCallbackIterationFence(owner,DedicatedRealMilliseconds()+1000,DedicatedRealMilliseconds()));valid=true;++controls;
  const size_t rounds=scenario==0?64:1;uint64_t last_nonce=0,last_generation=0;
  for(size_t round=0;round<rounds;++round){
   ++owner.invocation;++owner.iteration_step;events.clear();const auto deadline=DedicatedRealMilliseconds()+(scenario==1?100:5000);
   throws=scenario==3||scenario==4;partial=scenario==4;
   if(throws){
    assert(sources.BeginCallbackIterationFence(owner,deadline,DedicatedRealMilliseconds()));
    Reject([&]{pump([&]{return false;});});throws=false;assert(sources.Receiver().Work()&&sources.Physical()==1&&!sources.CallbackIterationFenceReady(owner,DedicatedRealMilliseconds()));++controls;break;
   }
   if(scenario==6){assert(sources.BeginAcceptedIdle({identity.lease,1,identity.worker,identity.pid,identity.start},deadline,DedicatedRealMilliseconds()));pump([&]{return sources.AcceptedIdleReady(DedicatedRealMilliseconds());});assert(sources.Physical()==1&&!sources.Receiver().BrowserReuseSupported());++controls;break;}
   assert(sources.BeginCallbackIterationFence(owner,deadline,DedicatedRealMilliseconds()));
   if(scenario==5){pump([&]{return sources.CallbackIterationFenceRefused(owner);});assert(!sources.CallbackIterationFenceReady(owner,DedicatedRealMilliseconds())&&sources.Receiver().Work()->refusal==7&&sources.Physical()==1);++controls;break;}
   pump([&]{return std::find(events.begin(),events.end(),"SDK_RAW_AND_ACK_SEEN")!=events.end();});
   const auto work=*sources.Receiver().Work();assert(work.challenge.nonce>last_nonce&&work.challenge.borrow_generation>last_generation);last_nonce=work.challenge.nonce;last_generation=work.challenge.borrow_generation;
   assert(!sources.CallbackIterationFenceReceipt(owner,DedicatedRealMilliseconds())&&!sources.CallbackIterationFenceReady(owner,DedicatedRealMilliseconds())&&!work.resume_generation);++controls;
   Reject([&]{sources.Packet(Fields("SOURCE_WORK_HOST_RESUMED "+std::to_string(work.challenge.ticket)+" "+std::to_string(work.challenge.nonce)+" 2"),DedicatedRealMilliseconds(),false);});++controls;
   Reject([&]{sources.Packet(Fields("SOURCE_WORK_READY "+std::to_string(work.challenge.ticket)+" "+std::to_string(work.challenge.nonce+1)),DedicatedRealMilliseconds(),false);});++controls;
   if(scenario==1){pump([&]{return DedicatedRealMilliseconds()>=deadline;});sources.Tick(DedicatedRealMilliseconds());assert(sources.CallbackIterationFenceRefused(owner)&&!sources.FinishCallbackIterationFence(owner,DedicatedRealMilliseconds()));assert(host.Send("READY"));pump([&]{return sources.Receiver().Work()->observed;});++controls;break;}
   assert(host.Send("READY"));pump([&]{return std::find(events.begin(),events.end(),"SDK_RESUME_PENDING")!=events.end();});assert(sources.Receiver().Work()->resumed&&!sources.CallbackIterationFenceReady(owner,DedicatedRealMilliseconds()));++controls;
   if(scenario==2){sources.CancelCallbackIterationFence(owner);assert(host.Send("HOST_ACK"));pump([&]{return std::find(events.begin(),events.end(),"SDK_RESUME_PENDING")!=events.end();});assert(!sources.FinishCallbackIterationFence(owner,DedicatedRealMilliseconds())&&sources.Physical()==1);++controls;break;}
   assert(host.Send("HOST_ACK"));pump([&]{return sources.CallbackIterationFenceReady(owner,DedicatedRealMilliseconds());});
   const auto receipt=sources.CallbackIterationFenceReceipt(owner,DedicatedRealMilliseconds());assert(receipt&&receipt->challenge==work.challenge&&receipt->cuts==*work.cuts);
   assert(receipt->resume_generation>receipt->challenge.borrow_generation);assert(receipt->observed&&receipt->acknowledged&&receipt->resumed&&receipt->host_ready&&receipt->host_resumed);
   assert(receipt->observed_at>0&&receipt->observed_at<=receipt->acknowledged_at&&receipt->acknowledged_at<=receipt->host_ready_at&&receipt->host_ready_at<=receipt->resumed_at&&receipt->resumed_at<=receipt->host_resumed_at);
   assert(!sources.CallbackIterationFenceReceipt(foreign,DedicatedRealMilliseconds()));assert(!sources.FinishCallbackIterationFence(foreign,DedicatedRealMilliseconds()));assert(sources.FinishCallbackIterationFence(owner,DedicatedRealMilliseconds()));assert(!sources.CallbackIterationFenceReceipt(owner,DedicatedRealMilliseconds()));assert(!sources.Receiver().Work()&&sources.Physical()==1&&!sources.Receiver().BrowserReuseSupported());++controls;
  }
  if(scenario==0){assert(!sources.BeginCallbackIterationFence(owner,DedicatedRealMilliseconds()+1000,DedicatedRealMilliseconds()));assert(!sources.BeginAcceptedIdle({1,1,1,host.Pid(),identity.start},DedicatedRealMilliseconds()+1000,DedicatedRealMilliseconds()));++controls;}
  sources.Cancel(DedicatedRealMilliseconds());assert(host.Send("EXIT_HOST"));pump([&]{return host.Exited().has_value();});assert(!host.Exited()->signaled&&host.Exited()->code==0&&sources.Physical()==1);assert(descendants.Arrive(batch,1,host,true,DedicatedRealMilliseconds(),0));sources.DomainArrived(descendants,batch);pump([&]{return !descendants.Pending(batch)&&sources.ChannelDrained()&&sources.Receiver().ExitObserved();});assert(descendants.TakeErrors().empty());assert(sources.Retire({identity,batch,true,true,true},DedicatedRealMilliseconds())&&sources.Physical()==0&&policy.Outstanding()==0);assert(host.Stderr().empty()&&host.Stdout().empty());assert(kill(child,0)==-1&&errno==ESRCH);assert(std::find(events.begin(),events.end(),"SOURCE_REAPED "+std::to_string(child)+" 0")!=events.end());emit("actual-waitpid-and-domain-retirement");++controls;
 }
 std::cout<<"Actual Source callback-iteration adapter: "<<controls<<" ordered-ready/resume/foreign/expiry/cancel/64-history/send-failure and actual host-child retirement controls passed; no CEF/reuse claim\n";
}
