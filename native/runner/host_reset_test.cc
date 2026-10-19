// SPDX-License-Identifier: MIT
#include "host_process.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
int main(int argc,char** argv){
  assert(argc==2);
  DedicatedHostProcess host(argv[1],{"-e","process.stdout.write('actual-ready');setInterval(()=>{},1000)"});
  const auto end=DedicatedRealMilliseconds()+3000;
  while(host.Stdout()!="actual-ready"){host.Poll(5);assert(DedicatedRealMilliseconds()<end);}
  assert(host.Send("actual-unread-control-data"));
  host.Kill();
  // The child never opens/reads fd3: kill produces a genuine Unix socket reset.
  while(!host.Exited()){host.Poll(5);assert(DedicatedRealMilliseconds()<end);}
  host.Poll(0);
  assert(host.ControlClosed()&&host.Exited()->signaled&&host.Exited()->code==9);
  assert(host.Stdout()=="actual-ready"&&host.Stderr().empty());
  std::cout<<"Actual unread control socket reset + confirmed SIGKILL waitpid passed\n";
}
