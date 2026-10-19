// SPDX-License-Identifier: MIT
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
int main(int argc,char** argv){assert(argc>=2);const std::string mode=argv[1];
  if(mode=="stdio"){assert(argc==4&&std::string(argv[2])=="literal argument"&&std::string(argv[3])=="--child-flag");char input[32]{};assert(read(0,input,sizeof(input))==9&&std::string(input)=="realinput");assert(std::string(getenv("PROOF_ENV"))=="unchanged-value");assert(write(1,"actual-stdout\n",14)==14&&write(2,"actual-stderr\n",14)==14);return 0;}
  if(mode=="exit7")return 7;
  if(mode=="signal"){raise(SIGUSR1);return 99;}
  if(mode=="timeout"){signal(SIGTERM,SIG_IGN);for(;;)pause();}
  if(mode=="interrupt"){dprintf(1,"ready\n");for(;;)pause();}
  int gate[2];assert(pipe(gate)==0);const pid_t child=fork();assert(child>=0);
  if(!child){close(gate[0]);
    if(mode=="doublefork"){assert(setsid()>0);const pid_t grandchild=fork();assert(grandchild>=0);if(!grandchild){close(gate[1]);usleep(30000);_exit(0);}assert(write(gate[1],"G",1)==1);close(gate[1]);_exit(0);}
    if(mode=="held"){assert(setsid()>0);signal(SIGTERM,SIG_IGN);assert(write(gate[1],"G",1)==1);close(gate[1]);for(;;)pause();}
    assert(mode=="fork"||mode=="abnormal");assert(write(gate[1],"G",1)==1);close(gate[1]);_exit(mode=="abnormal"?7:0);
  }
  close(gate[1]);char go=0;assert(read(gate[0],&go,1)==1&&go=='G');close(gate[0]);return 0;
}
