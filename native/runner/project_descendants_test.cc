// SPDX-License-Identifier: MIT
#include "project_descendants.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace suitecut::runner;
int Worker(bool detached=true){
  char byte=0;if(read(3,&byte,1)!=1||byte!='X')return 4;
  if(!detached)return 0;
  int hold[2];if(pipe2(hold,O_CLOEXEC))return 5;
  const auto child=fork();if(child<0)return 6;
  if(!child){if(setsid()<0)_exit(7);close(3);close(STDOUT_FILENO);close(STDERR_FILENO);char data=0;while(read(hold[0],&data,1)<0&&errno==EINTR){} _exit(8);}
  return 0;
}
void Exited(DedicatedHostProcess& host){const auto end=DedicatedRealMilliseconds()+3000;while(!host.Exited()){assert(DedicatedRealMilliseconds()<end);assert(host.Poll(10).empty());}assert(!host.Exited()->signaled&&host.Exited()->code==0);}
int main(int argc,char** argv){
  if(argc==2&&std::strcmp(argv[1],"--worker")==0)return Worker();
  if(argc==2&&std::strcmp(argv[1],"--file")==0)return Worker(false);
  ProjectDescendants domain;DedicatedHostProcess first("/proc/self/exe",{"--worker"}),second("/proc/self/exe",{"--worker"}),file("/proc/self/exe",{"--file"});
  const auto generation=domain.Enroll(1,first);assert(domain.Enroll(2,second)==generation);
  assert(!domain.Arrive(generation,1,first,true,DedicatedRealMilliseconds()));
  assert(first.Send("X"));Exited(first);assert(!domain.Arrive(generation,1,first,false,DedicatedRealMilliseconds()));
  assert(domain.Arrive(generation,1,first,true,DedicatedRealMilliseconds(),0));assert(domain.Arrived(generation,1,first));assert(!domain.Arrived(generation+1,1,first));assert(!domain.Arrived(generation,2,first));assert(!domain.Arrived(generation,1,file));assert(!domain.Arrive(generation,1,first,true,DedicatedRealMilliseconds()));domain.Tick(DedicatedRealMilliseconds());assert(domain.Pending(generation)&&!second.Exited());
  assert(!domain.Arrive(generation+1,2,second,true,DedicatedRealMilliseconds()));assert(second.Send("X"));Exited(second);
  assert(!domain.Arrive(generation,2,second,false,DedicatedRealMilliseconds(),0));domain.Tick(DedicatedRealMilliseconds());assert(!file.Exited());assert(domain.Pending(generation));assert(file.Send("X"));Exited(file);
  assert(domain.Arrive(generation,2,second,true,DedicatedRealMilliseconds(),0));assert(!domain.CanEnroll());assert(domain.Arrived(generation,1,first)&&domain.Arrived(generation,2,second));const auto end=DedicatedRealMilliseconds()+3000;
  while(domain.Pending(generation)){assert(DedicatedRealMilliseconds()<end);domain.Tick(DedicatedRealMilliseconds());}
  assert(!domain.Active()&&domain.CanEnroll());assert(!domain.Arrived(generation,1,first)&&!domain.Arrived(generation,2,second));const auto errors=domain.TakeErrors();assert(errors.size()==2);for(const auto& error:errors)assert(error.find("signal 9 after native cleanup SIGKILL")!=std::string::npos);
  std::cout<<"Native actual shared descendant batch ownership controls passed\n";
}
