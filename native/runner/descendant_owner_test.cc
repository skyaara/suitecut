// SPDX-License-Identifier: MIT
#include "descendant_owner.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>
namespace sr = suitecut::runner;
template<class F> void Reject(F fn) { bool rejected=false; try { fn(); } catch(const std::runtime_error&) { rejected=true; } assert(rejected); }
void Pause() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
int Child(const std::string& mode) {
  int pipefd[2]; assert(pipe2(pipefd,O_CLOEXEC)==0);
  const auto child=fork(); assert(child>=0);
  if (!child) {
    ::close(3); ::close(pipefd[1]);
    if(mode=="graceful") { char byte; while(read(pipefd[0],&byte,1)<0&&errno==EINTR){} _exit(7); }
    assert(setsid()>0);
    signal(SIGTERM,SIG_IGN);
    if(mode=="tree") { const auto grandchild=fork(); assert(grandchild>=0); if(!grandchild) for(;;) pause(); }
    for(;;) pause();
  }
  ::close(pipefd[0]);
  dprintf(3,"READY %d\n",child);
  char byte; assert(read(3,&byte,1)==1);
  return 0;
}
std::string Ready(sr::DedicatedHostProcess& worker) {
  const auto deadline=sr::DedicatedRealMilliseconds()+3000;
  while(sr::DedicatedRealMilliseconds()<deadline) {
    for(const auto& line:worker.Poll(10)) if(line.starts_with("READY ")) return line;
  }
  throw std::runtime_error("Real worker readiness expired");
}
void Retire(sr::DescendantOwner& owner,sr::DedicatedHostProcess& worker,double grace) {
  const auto deadline=sr::DedicatedRealMilliseconds()+3000;
  while(!worker.Exited()&&sr::DedicatedRealMilliseconds()<deadline)worker.Poll(5);
  assert(worker.Exited());
  Reject([&]{owner.BeginRetirement(worker,sr::DedicatedRealMilliseconds(),std::numeric_limits<double>::quiet_NaN());});
  owner.BeginRetirement(worker,sr::DedicatedRealMilliseconds(),grace);
  while(sr::DedicatedRealMilliseconds()<deadline) { if(owner.Tick(sr::DedicatedRealMilliseconds())) return; Pause(); }
  throw std::runtime_error("Actual descendant retirement fence expired");
}
int main(int argc,char** argv) {
  if(argc==3&&std::string(argv[1])=="--child")return Child(argv[2]);
  if(argc==4&&std::string(argv[1])=="--node") {
    sr::DescendantOwner owner;
    sr::DedicatedHostProcess worker(argv[2],{argv[3]});owner.Begin(worker);
    const auto ready=Ready(worker);const auto child=static_cast<pid_t>(std::stol(ready.substr(6)));
    assert(worker.Send("EXIT"));Retire(owner,worker,30);
    assert(owner.Exits().size()>=2);bool found=false;
    for(const auto& event:owner.Exits()){found|=event.pid==child;assert(event.signaled&&event.code==SIGKILL&&event.kill_requested);assert(kill(event.pid,0)==-1&&errno==ESRCH);}
    assert(found);std::cout<<"PASS real Node host detached shell and child retired after actual host exit\n";return 0;
  }
  int prior=0;assert(!prctl(PR_GET_CHILD_SUBREAPER,&prior));
  for(const auto& mode:{"graceful","detached","tree"}) {
    sr::DescendantOwner owner;
    Reject([]{sr::DescendantOwner duplicate;});
    sr::DedicatedHostProcess worker(argv[0],{"--child",mode});
    owner.Begin(worker);Ready(worker);
    Reject([&]{owner.BeginRetirement(worker,sr::DedicatedRealMilliseconds(),0);});
    assert(worker.Send("EXIT"));Retire(owner,worker,std::string(mode)=="graceful"?500:30);
    assert(!owner.Active());assert(owner.Exits().size()==(std::string(mode)=="tree"?2:1));
    for(const auto& event:owner.Exits()) {
      if(std::string(mode)=="graceful")assert(!event.signaled&&event.code==7&&!event.kill_requested);
      else assert(event.signaled&&event.code==SIGKILL&&event.kill_requested);
      assert(kill(event.pid,0)==-1&&errno==ESRCH);
    }
  }
  int restored=-1;assert(!prctl(PR_GET_CHILD_SUBREAPER,&restored)&&restored==prior);
  // A preexisting child cannot be silently adopted as this worker's resource.
  const auto unrelated=fork();assert(unrelated>=0);if(!unrelated){for(;;)pause();}
  Reject([]{sr::DescendantOwner owner;});assert(kill(unrelated,0)==0);
  assert(!kill(unrelated,SIGKILL));int status=0;assert(waitpid(unrelated,&status,0)==unrelated);
  // A second actual worker may only start after the first physical subtree drained.
  { sr::DescendantOwner owner; sr::DedicatedHostProcess worker("/bin/true",{}); owner.Begin(worker); Retire(owner,worker,0); assert(owner.Exits().empty()); }
  std::cout<<"PASS five real process ownership branches\n";
}
