// SPDX-License-Identifier: MIT
#include "descendant_owner.h"
#include "retained_source_process.h"
#include <cassert>
#include <thread>
namespace sr=suitecut::runner;
struct Host {
 pid_t pid;bool exited=false;
 pid_t Pid()const{return pid;}
 bool Exited(){if(exited)return true;int status=0;const auto result=waitpid(pid,&status,WNOHANG);assert(result==0||result==pid);if(result==pid){assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);exited=true;}return exited;}
};
int main(){sr::DescendantOwner owner;int gate[2];assert(pipe(gate)==0);const pid_t pid=fork();assert(pid>=0);
 if(!pid){close(gate[0]);const pid_t child=fork();assert(child>=0);if(!child){close(gate[1]);usleep(40000);_exit(0);}assert(write(gate[1],&child,sizeof(child))==sizeof(child));close(gate[1]);_exit(0);}
 close(gate[1]);Host host{pid};owner.Begin(host);pid_t adopted=-1;assert(read(gate[0],&adopted,sizeof(adopted))==sizeof(adopted));close(gate[0]);while(!host.Exited())std::this_thread::sleep_for(std::chrono::milliseconds(1));owner.BeginRetirement(host,sr::DedicatedRealMilliseconds(),1000);
 bool threw=false;try{owner.Tick(sr::DedicatedRealMilliseconds(),[](pid_t){throw std::runtime_error("actual observer failure");});}catch(const std::runtime_error&){threw=true;}assert(threw&&owner.Active()&&owner.Exits().empty());const auto before=sr::RetainedSourceProcess::ReadStat(adopted);assert(before.parent==getpid());
 const double end=sr::DedicatedRealMilliseconds()+2000;bool done=false;while(sr::DedicatedRealMilliseconds()<end){done=owner.Tick(sr::DedicatedRealMilliseconds(),[&](pid_t actual){const auto now=sr::RetainedSourceProcess::ReadStat(actual);assert(actual==adopted&&now.parent==getpid()&&now.start==before.start);});if(done)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(done&&!owner.Active()&&owner.Exits().size()==1&&owner.Exits()[0].pid==adopted&&!owner.Exits()[0].kill_requested&&owner.Exits()[0].code==0);int status=0;assert(waitpid(adopted,&status,WNOHANG)==-1&&errno==ECHILD);return 0;
}
