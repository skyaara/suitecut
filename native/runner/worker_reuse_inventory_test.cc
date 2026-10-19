// SPDX-License-Identifier: MIT
#include "worker_reuse_inventory.h"
#include <cassert>
#include <csignal>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
using P=suitecut::runner::WorkerReusePool;
static void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
static double Now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Wire {char command=0;uint64_t lease=0,borrow=0,request=0;};
struct Reply {Wire wire;int64_t pid=0,grandchild=0;size_t fixtures=0,runs=0;int child_status=0;};
static bool Write(int fd,const void* bytes,size_t size){
  const auto* p=static_cast<const char*>(bytes);
  while(size){const auto n=::write(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=static_cast<size_t>(n);}return true;
}
static bool Read(int fd,void* bytes,size_t size){
  auto* p=static_cast<char*>(bytes);
  while(size){const auto n=::read(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=static_cast<size_t>(n);}return true;
}
// A controlled native child, not the SuiteCut SDK. It retains a real in-process
// fixture between pipe commands and can own a real grandchild. It supplies OS
// inventory/liveness controls; actual SDK idle/reuse remains an adapter proof.
class Child {
 public:
  explicit Child(double deadline):deadline_(deadline){
    int input[2],output[2];Check(pipe2(input,O_CLOEXEC)==0,"kernel input pipe failed");
    if(pipe2(output,O_CLOEXEC)){close(input[0]);close(input[1]);throw std::runtime_error("kernel output pipe failed");}
    pid_=fork();
    if(!pid_){
      close(input[1]);close(output[0]);size_t runs=0;int64_t child=0;int held=-1;Wire wire;
      while(Read(input[0],&wire,sizeof(wire))){Reply reply;reply.wire=wire;reply.pid=getpid();reply.fixtures=1;
        if(wire.command=='R')++runs;
        else if(wire.command=='F'){
          int gate[2];if(child||pipe2(gate,O_CLOEXEC))_exit(21);child=fork();
          if(!child){close(gate[1]);char byte;if(!Read(gate[0],&byte,1))_exit(22);_exit(byte=='G'?0:23);}
          if(child<0)_exit(24);
          close(gate[0]);held=gate[1];
        }else if(wire.command=='K'){
          const char go='G';if(child<=0||!Write(held,&go,1))_exit(25);close(held);held=-1;
          int status=0;pid_t found;do{found=waitpid(static_cast<pid_t>(child),&status,0);}while(found<0&&errno==EINTR);
          if(found!=child)_exit(26);
          reply.grandchild=child;reply.child_status=status;child=0;
        }else if(wire.command!='I'&&wire.command!='Q')_exit(27);
        if(child)reply.grandchild=child;
        reply.runs=runs;
        if(!Write(output[1],&reply,sizeof(reply)))_exit(28);
        if(wire.command=='Q'){if(child)_exit(29);_exit(0);}
      }_exit(30);
    }
    close(input[0]);close(output[1]);
    if(pid_<0){close(input[1]);close(output[0]);throw std::runtime_error("kernel child fork failed");}
    input_=input[1];output_=output[0];
  }
  ~Child(){
    if(grandchild_>0&&kill(grandchild_,SIGKILL)&&errno!=ESRCH)std::cerr<<"Grandchild cleanup signal failed\n";
    if(pid_>0){
      if(kill(pid_,SIGKILL)&&errno!=ESRCH)std::cerr<<"Child cleanup signal failed\n";
      int status;pid_t result;do{result=waitpid(pid_,&status,0);}while(result<0&&errno==EINTR);
      if(result!=pid_)std::cerr<<"Child cleanup waitpid failed\n";
    }
    if(grandchild_>0){int status;pid_t result;do{result=waitpid(grandchild_,&status,0);}while(result<0&&errno==EINTR);if(result!=grandchild_&&errno!=ECHILD)std::cerr<<"Adopted grandchild cleanup waitpid failed\n";}
    if(input_>=0)close(input_);
    if(output_>=0)close(output_);
  }
  int64_t Pid()const{return pid_;}
  Reply Command(Wire wire){
    Check(Write(input_,&wire,sizeof(wire)),"actual native child command failed");
    Reply reply;auto* cursor=reinterpret_cast<char*>(&reply);size_t remaining=sizeof(reply);
    while(remaining){
      pollfd fd{output_,POLLIN,0};const auto left=deadline_-Now();Check(left>0,"original kernel control deadline expired");
      const auto ready=poll(&fd,1,static_cast<int>(left)+1);if(ready<0&&errno==EINTR)continue;
      Check(ready>0&&(fd.revents&POLLIN),"actual native child pipe not ready");
      const auto got=::read(output_,cursor,remaining);if(got<0&&errno==EINTR)continue;
      Check(got>0,"actual child reply missing");cursor+=got;remaining-=static_cast<size_t>(got);
    }
    Check(reply.pid==pid_&&reply.wire.command==wire.command&&reply.wire.lease==wire.lease&&reply.wire.borrow==wire.borrow&&reply.wire.request==wire.request,"actual child idle identity mismatch");
    if(wire.command=='F')grandchild_=static_cast<pid_t>(reply.grandchild);
    if(wire.command=='K'){Check(reply.grandchild==grandchild_&&WIFEXITED(reply.child_status)&&WEXITSTATUS(reply.child_status)==0,"actual known grandchild waitpid/status missing");grandchild_=0;}
    return reply;
  }
  void Living(){int status;Check(waitpid(pid_,&status,WNOHANG)==0,"actual retained child unexpectedly exited");}
  int Exit(){Command({'Q'});const auto original=pid_;int status=0;pid_t result;do{result=waitpid(original,&status,0);}while(result<0&&errno==EINTR);if(result==original)pid_=0;Check(result==original&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"actual known primary waitpid/status missing");return status;}
 private:
  double deadline_;pid_t pid_=0,grandchild_=0;int input_=-1,output_=-1;
};
static P::Key Key(){return {{1,2,3},1};}
static P::Group Group(size_t i){return {Key().file,i,i+1};}
static P::GroupFence Fence(){P::GroupFence f;f.retained.count=1;f.retained.values[0]={1,1,1};return f;}
static void Idle(P& pool,const P::Borrow& borrow,Child& child){
  Check(pool.Complete(borrow,P::Outcome::Passed),"native controlled group completion failed");const auto now=Now();
  const auto request=pool.RequestIdle(borrow,Fence(),now,now+1000);Check(request.has_value(),"native idle request rejected");
  const auto actual=child.Command({'I',borrow.host.lease,borrow.generation,request->request});
  Check(actual.fixtures==1&&pool.Acknowledge({borrow,actual.wire.request,actual.fixtures},Now()),"actual controlled pipe idle ACK rejected");
}
static bool Confirm(P& pool,const P::Borrow& borrow){
  const auto probe=pool.InventoryProbe();Check(probe.has_value(),"native inventory probe missing");
  const auto actual=suitecut::runner::ObserveWorkerReuseInventory(*probe);
  const std::array<P::CohortFence,1> fences{{{borrow,Fence()}}};return pool.ConfirmInventory(actual,fences,Now());
}
int main(){
  signal(SIGPIPE,SIG_IGN);int previous=0;Check(prctl(PR_GET_CHILD_SUBREAPER,&previous)==0&&prctl(PR_SET_CHILD_SUBREAPER,1)==0,"controlled native subreaper setup failed");
  try{
    {P pool(Key(),1,4);Child host(Now()+5000);const auto borrow=pool.Started(Key(),Group(0),1,host.Pid());Check(borrow.has_value(),"actual child worker admission failed");
      const auto first=host.Command({'R'});Check(first.runs==1&&first.fixtures==1,"actual first retained fixture use missing");Idle(pool,*borrow,host);host.Living();
      Check(Confirm(pool,*borrow)&&pool.Transferred(*borrow)&&pool.Physical()==1&&pool.RetainedFixtures()==1,"idle transfer dropped a living PID/fixture lease");
      const auto next=pool.Reuse(borrow->host,Key(),Group(1));Check(next.has_value(),"native compatible live-host reuse rejected");const auto second=host.Command({'R'});
      Check(second.pid==first.pid&&second.fixtures==1&&second.runs==2,"actual child host/fixture did not persist across native borrows");Idle(pool,*next,host);Check(Confirm(pool,*next)&&pool.Physical()==1,"second idle transfer lost physical ownership");
      host.Living();const auto status=host.Exit();Check(pool.PrimaryExited(next->host,{WEXITSTATUS(status),false},Now())&&pool.Physical()==1,"primary exit prematurely released batch ownership");
      P::RetirementFence fence;fence.descendants_retired=true;Check(pool.Retired(next->host,fence,Now())&&pool.Physical()==0&&pool.Transferred(*borrow)&&pool.Transferred(*next),"actual final retirement changed original idle-transfer history");
    }
    {P pool(Key(),1,4);Child host(Now()+5000);const auto borrow=*pool.Started(Key(),Group(0),1,host.Pid());Idle(pool,borrow,host);
      const auto child=host.Command({'F'});Check(child.grandchild>0,"actual worker-owned child was not created");
      Check(!Confirm(pool,borrow)&&!pool.Transferred(borrow)&&pool.Physical()==1,"live worker child incorrectly qualified for reuse");
      host.Command({'K'});Check(Confirm(pool,borrow)&&pool.Transferred(borrow)&&pool.Physical()==1,"genuine child waitpid did not clear observed inventory");
      Child unknown(Now()+5000);unknown.Command({'R'});const auto next=*pool.Reuse(borrow.host,Key(),Group(1));Idle(pool,next,host);
      Check(!Confirm(pool,next)&&pool.Physical()==1,"unknown native direct child incorrectly qualified for reuse");unknown.Exit();Check(Confirm(pool,next),"known unknown-child exit did not clear inventory");
      pool.Stop();host.Living();Check(pool.Physical()==1&&pool.RetainedFixtures()==1,"stop fabricated live worker/fixture retirement");const auto status=host.Exit();Check(pool.PrimaryExited(next.host,{WEXITSTATUS(status),false},Now()),"actual stopped primary exit rejected");
      P::RetirementFence fence;fence.descendants_retired=true;Check(pool.Retired(next.host,fence,Now())&&pool.Physical()==0,"actual stopped physical retirement rejected");
    }
    Check(prctl(PR_SET_CHILD_SUBREAPER,previous)==0,"controlled subreaper restoration failed");
    std::cout<<"Native worker reuse inventory: 2 actual known-PID/pipe/proc ownership controls passed; SDK reuse remains separate\n";return 0;
  }catch(const std::exception& error){if(prctl(PR_SET_CHILD_SUBREAPER,previous))std::cerr<<"Controlled subreaper restoration failed\n";std::cerr<<error.what()<<'\n';return 1;}
}
