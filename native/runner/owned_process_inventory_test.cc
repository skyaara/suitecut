// SPDX-License-Identifier: MIT
// Actual kernel lineage observation, including adopted live and zombie children.
#include "owned_process_inventory.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <iostream>
void Same(const std::vector<pid_t>& expected){
 auto actual=suitecut::runner::ReadOwnedChildren(),want=expected;
 std::sort(actual.begin(),actual.end());std::sort(want.begin(),want.end());assert(actual==want);
}
int main(){
 int prior=0;assert(!prctl(PR_GET_CHILD_SUBREAPER,&prior));assert(!prctl(PR_SET_CHILD_SUBREAPER,1));Same({});
 int ready[2],finish[2],release[2];assert(!pipe2(ready,O_CLOEXEC)&&!pipe2(finish,O_CLOEXEC)&&!pipe2(release,O_CLOEXEC));
 const auto worker=fork();assert(worker>=0);
 if(!worker){
  assert(!prctl(PR_SET_NAME,"own ) worker"));close(ready[0]);close(finish[1]);close(release[1]);
  const auto child=fork();assert(child>=0);
  if(!child){assert(!prctl(PR_SET_NAME,"adopt ) child"));close(ready[1]);close(finish[0]);char byte;assert(read(release[0],&byte,1)==1);_exit(23);}
  close(release[0]);assert(write(ready[1],&child,sizeof(child))==sizeof(child));char byte;assert(read(finish[0],&byte,1)==1);_exit(17);
 }
 close(ready[1]);close(finish[0]);close(release[0]);pid_t child=0;assert(read(ready[0],&child,sizeof(child))==sizeof(child));assert(child>0);Same({worker});
 assert(write(finish[1],"X",1)==1);int worker_status=0;assert(waitpid(worker,&worker_status,0)==worker);assert(WIFEXITED(worker_status)&&WEXITSTATUS(worker_status)==17);Same({child});
 assert(write(release[1],"X",1)==1);siginfo_t observed{};assert(waitid(P_PID,static_cast<id_t>(child),&observed,WEXITED|WNOWAIT)==0);assert(observed.si_pid==child&&observed.si_code==CLD_EXITED&&observed.si_status==23);Same({child});
 std::ifstream stat("/proc/"+std::to_string(child)+"/stat");std::string record;std::getline(stat,record);const auto closing=record.rfind(')');assert(closing!=std::string::npos&&record.substr(closing+1).starts_with(" Z "));
 int child_status=0;assert(waitpid(child,&child_status,0)==child);assert(WIFEXITED(child_status)&&WEXITSTATUS(child_status)==23);Same({});
 assert(kill(worker,0)==-1&&errno==ESRCH);assert(kill(child,0)==-1&&errno==ESRCH);
 for(const auto fd:{ready[0],finish[1],release[1]})close(fd);
 assert(!prctl(PR_SET_CHILD_SUBREAPER,prior));
 std::cout<<"{\"passed\":true,\"observedOwnershipStages\":5,\"workerPID\":"<<worker<<",\"workerActualExit\":17,\"adoptedChildPID\":"<<child<<",\"adoptedActualZombieObservedBeforeReap\":true,\"adoptedActualExit\":23,\"actualOwnedPIDsAbsent\":true,\"weirdCommParenthesesAndSpaces\":true}\n";
}
